// ================================================================
// P0: 全局变量提升 —— 将频繁访问的全局变量提升为函数内局部变量
// 让寄存器分配器将其放入寄存器，消除 la + lw/sw 开销
//
// 策略：
// 1. 扫描函数中所有全局变量访问（LOAD/STORE/GETELEMENTPTR）
// 2. 为每个全局变量创建局部 ALLOCA
// 3. 在函数入口处加载全局变量到 ALLOCA
// 4. 在函数出口处将 ALLOCA 存回全局变量
// 5. 在 CALL 指令前将 ALLOCA 存回全局变量（同步）
// 6. 在 CALL 指令后将全局变量加载回 ALLOCA（重新加载）
// 7. 替换所有 LOAD/STORE 全局变量为 LOAD/STORE ALLOCA
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// SysY 运行时库函数集合 —— 这些函数为 external，且不访问任何用户全局变量。
// 在 GVP 中遇到这些函数的 CALL 时，可完全跳过同步（极大降低 CALL 同步开销）。
// 来源：SysYlib/sylib.h 中所有声明函数 + 编译器可能生成的 runtime helper。
const std::unordered_set<std::string>& getSylibFunctionNames() {
    static const std::unordered_set<std::string> sylibNames = {
        "getint", "getch", "getarray", "getfloat", "getfarray",
        "putint", "putch", "putarray", "putfloat", "putfarray", "putf",
        "_sysy_starttime", "_sysy_stoptime", "starttime", "stoptime",
        "memset", "memcpy", "memmove", "memcmp",
        // 标准库辅助函数（保守包含）
        "abort", "exit",
    };
    return sylibNames;
}

// 判断函数名是否属于 sylib（不访问用户全局变量的运行时函数）
bool isSylibFunction(const std::string& name) {
    return getSylibFunctionNames().count(name) > 0;
}

// 收集函数中"直接"访问的全局变量集合
// 区分读访问（LOAD/GEP-read）和写访问（STORE/GEP-write）
// 注意：GEP 本身不读写，但其结果被用于 LOAD 或 STORE。这里把 GEP 视为读访问
// （因为 GEP 计算地址，通常用于后续 LOAD/STORE）。STORE 的指针视为写访问。
struct GlobalAccessInfo {
    std::unordered_set<IR::GlobalVariable*> reads;   // 可能被读取的全局
    std::unordered_set<IR::GlobalVariable*> writes;  // 可能被写入的全局
};

GlobalAccessInfo collectDirectGlobalAccess(IR::Function* func) {
    GlobalAccessInfo info;
    if (!func || func->isExternal()) return info;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            if (op == IR::Instruction::Opcode::LOAD) {
                if (inst->getNumOperands() >= 1) {
                    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(inst->getOperand(0))) {
                        info.reads.insert(gv);
                    }
                }
            } else if (op == IR::Instruction::Opcode::STORE) {
                if (inst->getNumOperands() >= 2) {
                    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(inst->getOperand(1))) {
                        info.writes.insert(gv);
                    }
                }
            } else if (op == IR::Instruction::Opcode::GETELEMENTPTR) {
                if (inst->getNumOperands() >= 1) {
                    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(inst->getOperand(0))) {
                        // GEP 计算地址，保守视为读
                        info.reads.insert(gv);
                    }
                }
            }
        }
    }
    return info;
}

// 检查函数中是否有全局变量访问（保留旧接口，用于兼容）
bool hasGlobalAccess(IR::Function* func) {
    if (!func || func->isExternal()) return false;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            if (op == IR::Instruction::Opcode::LOAD ||
                op == IR::Instruction::Opcode::STORE ||
                op == IR::Instruction::Opcode::GETELEMENTPTR) {
                unsigned ptrIdx = (op == IR::Instruction::Opcode::STORE) ? 1 : 0;
                if (ptrIdx < inst->getNumOperands()) {
                    if (dynamic_cast<IR::GlobalVariable*>(inst->getOperand(ptrIdx))) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

// 收集函数中访问的所有全局变量（保留旧接口）
std::unordered_set<IR::GlobalVariable*> collectAccessedGlobals(IR::Function* func) {
    std::unordered_set<IR::GlobalVariable*> globals;
    if (!func || func->isExternal()) return globals;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            if (op == IR::Instruction::Opcode::LOAD ||
                op == IR::Instruction::Opcode::STORE ||
                op == IR::Instruction::Opcode::GETELEMENTPTR) {
                unsigned ptrIdx = (op == IR::Instruction::Opcode::STORE) ? 1 : 0;
                if (ptrIdx < inst->getNumOperands()) {
                    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(inst->getOperand(ptrIdx))) {
                        globals.insert(gv);
                    }
                }
            }
        }
    }
    return globals;
}

// ================================================================
// 模块级传递性全局访问分析
// 对每个函数 F，计算 F（含其调用的所有函数）可能读/写的全局变量集合。
// 用途：在 GVP 的 CALL 同步中，只同步 callee 实际可能访问的全局变量，
//       而非所有提升的全局变量。可大幅降低 CALL 同步开销。
// ================================================================
struct TransitiveAccessInfo {
    std::unordered_set<IR::GlobalVariable*> reads;
    std::unordered_set<IR::GlobalVariable*> writes;
};

// 计算模块中所有函数的传递性全局访问集合（fixpoint 迭代）
// 返回：函数指针 -> TransitiveAccessInfo
// 注意：external 函数（包括 sylib）返回空集合（视为不访问用户全局变量）
std::unordered_map<IR::Function*, TransitiveAccessInfo>
computeTransitiveGlobalAccess(IR::Module* mod) {
    std::unordered_map<IR::Function*, TransitiveAccessInfo> result;
    if (!mod) return result;

    // 初始化：每个用户函数的直接访问
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        auto info = collectDirectGlobalAccess(func.get());
        auto& ti = result[func.get()];
        ti.reads = info.reads;
        ti.writes = info.writes;
    }

    // Fixpoint 迭代：通过 CALL 边传递访问集合
    // 每个 CALL 指令把 callee 的访问集合并入 caller 的访问集合
    bool changed = true;
    int maxIter = 16;  // 防止病态情况死循环
    while (changed && maxIter-- > 0) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            auto& callerInfo = result[func.get()];
            for (auto& bb : func->getBlocks()) {
                for (auto& inst : bb->getInstructions()) {
                    if (inst->getOpcode() != IR::Instruction::Opcode::CALL) continue;
                    if (inst->getNumOperands() < 1) continue;
                    auto* calleeVal = inst->getOperand(0);
                    auto* calleeFunc = dynamic_cast<IR::Function*>(calleeVal);
                    if (!calleeFunc) continue;
                    // external 函数（包括 sylib）视为不访问用户全局变量
                    if (calleeFunc->isExternal()) continue;
                    auto it = result.find(calleeFunc);
                    if (it == result.end()) continue;
                    // 合并 callee 的访问集合到 caller
                    size_t oldReads = callerInfo.reads.size();
                    size_t oldWrites = callerInfo.writes.size();
                    for (auto* gv : it->second.reads) callerInfo.reads.insert(gv);
                    for (auto* gv : it->second.writes) callerInfo.writes.insert(gv);
                    if (callerInfo.reads.size() != oldReads ||
                        callerInfo.writes.size() != oldWrites) {
                        changed = true;
                    }
                }
            }
        }
    }
    return result;
}

// 检查值是否为全局变量
bool isGlobal(IR::Value* v) {
    return dynamic_cast<IR::GlobalVariable*>(v) != nullptr;
}

// 在 entry block 开头插入指令（在所有 ALLOCA 之后、第一个非 ALLOCA 指令之前）
// 确保 ALLOCA 及其初始化在使用之前被定义
void insertAtEntryBeginning(IR::BasicBlock* bb, IR::Instruction* inst) {
    inst->setParent(bb);
    auto& insts = const_cast<std::vector<std::unique_ptr<IR::Instruction>>&>(bb->getInstructions());
    // 找到第一个非 ALLOCA 指令的位置
    for (size_t i = 0; i < insts.size(); i++) {
        if (insts[i]->getOpcode() != IR::Instruction::Opcode::ALLOCA) {
            insts.insert(insts.begin() + i, std::unique_ptr<IR::Instruction>(inst));
            return;
        }
    }
    // 所有指令都是 ALLOCA，或 BB 为空 — 插入到末尾
    insts.push_back(std::unique_ptr<IR::Instruction>(inst));
}

// 在指定指令之前插入新指令
void insertBefore(IR::BasicBlock* bb, IR::Instruction* beforeInst, IR::Instruction* newInst) {
    newInst->setParent(bb);
    auto& insts = const_cast<std::vector<std::unique_ptr<IR::Instruction>>&>(bb->getInstructions());
    for (auto it = insts.begin(); it != insts.end(); ++it) {
        if (it->get() == beforeInst) {
            insts.insert(it, std::unique_ptr<IR::Instruction>(newInst));
            return;
        }
    }
}

// 在指定指令之后插入新指令
void insertAfter(IR::BasicBlock* bb, IR::Instruction* afterInst, IR::Instruction* newInst) {
    newInst->setParent(bb);
    auto& insts = const_cast<std::vector<std::unique_ptr<IR::Instruction>>&>(bb->getInstructions());
    for (auto it = insts.begin(); it != insts.end(); ++it) {
        if (it->get() == afterInst) {
            ++it;
            insts.insert(it, std::unique_ptr<IR::Instruction>(newInst));
            return;
        }
    }
}

// 为指定的全局变量集合创建同步存储（存回内存）
// 用途：CALL 之前，把 callee 可能读取的全局变量的本地副本写回内存
// syncSet: 需要同步的全局变量集合（callee 可能读取的）
std::vector<IR::Instruction*> createSyncStoresForSet(
    const std::unordered_map<IR::GlobalVariable*, IR::Instruction*>& globalToAlloca,
    const std::unordered_set<IR::GlobalVariable*>& syncSet) {
    std::vector<IR::Instruction*> stores;
    for (auto* gv : syncSet) {
        auto it = globalToAlloca.find(gv);
        if (it == globalToAlloca.end()) continue;
        auto* alloca = it->second;
        auto* ptrTy = dynamic_cast<IR::PointerType*>(gv->getType());
        auto* pointee = ptrTy ? ptrTy->getPointeeType() : nullptr;
        if (!pointee) continue;
        auto* load = IR::Instruction::createLoad(pointee, alloca, gv->getName() + ".sync");
        auto* store = IR::Instruction::createStore(load, gv);
        stores.push_back(load);
        stores.push_back(store);
    }
    return stores;
}

// 为指定的全局变量集合创建同步加载（从内存重新加载）
// 用途：CALL 之后，把 callee 可能修改的全局变量重新加载到本地副本
// syncSet: 需要同步的全局变量集合（callee 可能修改的）
std::vector<IR::Instruction*> createSyncLoadsForSet(
    const std::unordered_map<IR::GlobalVariable*, IR::Instruction*>& globalToAlloca,
    const std::unordered_set<IR::GlobalVariable*>& syncSet) {
    std::vector<IR::Instruction*> loads;
    for (auto* gv : syncSet) {
        auto it = globalToAlloca.find(gv);
        if (it == globalToAlloca.end()) continue;
        auto* alloca = it->second;
        auto* ptrTy = dynamic_cast<IR::PointerType*>(gv->getType());
        auto* pointee = ptrTy ? ptrTy->getPointeeType() : nullptr;
        if (!pointee) continue;
        auto* load = IR::Instruction::createLoad(pointee, gv, gv->getName() + ".reload");
        auto* store = IR::Instruction::createStore(load, alloca);
        loads.push_back(load);
        loads.push_back(store);
    }
    return loads;
}

// 旧接口保留（同步所有提升的全局变量）—— 用于 RET 出口同步
std::vector<IR::Instruction*> createSyncStores(
    const std::unordered_map<IR::GlobalVariable*, IR::Instruction*>& globalToAlloca) {
    std::vector<IR::Instruction*> stores;
    for (auto& [gv, alloca] : globalToAlloca) {
        auto* ptrTy = dynamic_cast<IR::PointerType*>(gv->getType());
        auto* pointee = ptrTy ? ptrTy->getPointeeType() : nullptr;
        if (!pointee) continue;
        auto* load = IR::Instruction::createLoad(pointee, alloca, gv->getName() + ".sync");
        auto* store = IR::Instruction::createStore(load, gv);
        stores.push_back(load);
        stores.push_back(store);
    }
    return stores;
}

std::vector<IR::Instruction*> createSyncLoads(
    const std::unordered_map<IR::GlobalVariable*, IR::Instruction*>& globalToAlloca) {
    std::vector<IR::Instruction*> loads;
    for (auto& [gv, alloca] : globalToAlloca) {
        auto* ptrTy = dynamic_cast<IR::PointerType*>(gv->getType());
        auto* pointee = ptrTy ? ptrTy->getPointeeType() : nullptr;
        if (!pointee) continue;
        auto* load = IR::Instruction::createLoad(pointee, gv, gv->getName() + ".reload");
        auto* store = IR::Instruction::createStore(load, alloca);
        loads.push_back(load);
        loads.push_back(store);
    }
    return loads;
}

bool promoteGlobalsInFunction(
    IR::Function* func,
    const std::unordered_map<IR::Function*, TransitiveAccessInfo>& transitiveAccess) {
    if (func->isExternal()) return false;
    if (!hasGlobalAccess(func)) return false;

    auto accessedGlobals = collectAccessedGlobals(func);
    if (accessedGlobals.empty()) return false;

    // 收集读/写访问信息，用于判断哪些全局变量在函数中被修改
    auto accessInfo = collectDirectGlobalAccess(func);

    // 只提升标量全局变量（int/float），不提升数组
    // 数组提升需要缓存基地址，收益较小且更复杂
    // 也不提升 const 全局变量，因为它们放在 .rodata 只读段，无法写回
    std::unordered_set<IR::GlobalVariable*> scalarGlobals;
    for (auto* gv : accessedGlobals) {
        if (gv->isConstant()) continue;  // const 全局变量在 .rodata，跳过
        auto* ptrTy = dynamic_cast<IR::PointerType*>(gv->getType());
        if (ptrTy) {
            auto* pointee = ptrTy->getPointeeType();
            if (pointee && (pointee->isInteger() || pointee->isFloat())) {
                scalarGlobals.insert(gv);
            }
        }
    }
    if (scalarGlobals.empty()) return false;

    // ================================================================
    // 区分"只读"和"可写"全局变量
    // 只读：函数本身不写入，且所有 callee（传递性）也不写入，且没有调用
    //       未知 external 函数（可能写入任何全局变量）。
    // 只读全局变量无需 ALLOCA + 同步点，直接在 entry 处 LOAD 一次，
    // 将所有引用替换为该 LOAD 结果。这消除了循环中的冗余 LOAD，且
    // 不需要 Mem2Reg 提升（没有 ALLOCA）。
    // ================================================================

    // 检查函数是否调用了未知的 external 函数（非 sylib）
    // 如果是，所有全局变量都视为可写（保守处理）
    bool callsUnknownExternal = false;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::CALL) continue;
            if (inst->getNumOperands() < 1) continue;
            auto* calleeFunc = dynamic_cast<IR::Function*>(inst->getOperand(0));
            if (calleeFunc && calleeFunc->isExternal() && !isSylibFunction(calleeFunc->getName())) {
                callsUnknownExternal = true;
                break;
            }
        }
        if (callsUnknownExternal) break;
    }

    // 计算传递性写入集合（函数自身 + 所有 callee）
    std::unordered_set<IR::GlobalVariable*> allWrites = accessInfo.writes;
    auto tiIt = transitiveAccess.find(func);
    if (tiIt != transitiveAccess.end()) {
        for (auto* gv : tiIt->second.writes) allWrites.insert(gv);
    }

    // 分类：只读 vs 可写
    std::unordered_set<IR::GlobalVariable*> readOnlyGlobals;
    std::unordered_set<IR::GlobalVariable*> readWriteGlobals;
    for (auto* gv : scalarGlobals) {
        if (!callsUnknownExternal && allWrites.find(gv) == allWrites.end()) {
            // 只读：函数和所有 callee 都不写入此全局变量
            readOnlyGlobals.insert(gv);
        } else {
            readWriteGlobals.insert(gv);
        }
    }

    // Step 0: 先收集所有需要替换的 LOAD/STORE 指令（在创建初始化指令之前）
    // 避免初始化指令（从全局变量加载）被错误替换
    struct Replacement {
        IR::Instruction* inst;
        unsigned operandIdx;
    };
    std::vector<Replacement> replacements;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            if (op == IR::Instruction::Opcode::LOAD) {
                if (inst->getNumOperands() >= 1) {
                    auto* ptr = inst->getOperand(0);
                    if (dynamic_cast<IR::GlobalVariable*>(ptr)) {
                        replacements.push_back({inst.get(), 0});
                    }
                }
            } else if (op == IR::Instruction::Opcode::STORE) {
                if (inst->getNumOperands() >= 2) {
                    auto* ptr = inst->getOperand(1);
                    if (dynamic_cast<IR::GlobalVariable*>(ptr)) {
                        replacements.push_back({inst.get(), 1});
                    }
                }
            }
        }
    }

    // Step 1: 在 entry block 中为每个标量全局变量创建 ALLOCA 和初始化指令
    // 分两批插入：先插入所有 ALLOCA（在现有 ALLOCA 之后），再插入所有 LOAD/STORE（在所有 ALLOCA 之后）
    //
    // ★ 优化：只读全局变量不创建 ALLOCA，直接用 entry 处的 LOAD 替换所有引用。
    // 这消除了循环中的冗余 LOAD，且不需要 Mem2Reg 提升。
    // 可写全局变量仍使用 ALLOCA + 同步点。
    auto* entryBB = func->getEntryBlock();
    std::unordered_map<IR::GlobalVariable*, IR::Instruction*> globalToAlloca;
    // globalToLoad: 只读全局变量 → entry 处的 LOAD 指令（用于替换所有 LOAD 引用）
    std::unordered_map<IR::GlobalVariable*, IR::Instruction*> globalToReadOnlyLoad;

    struct InitGroup {
        IR::Instruction* alloca;
        IR::Instruction* load;
        IR::Instruction* store;
    };
    std::vector<InitGroup> initGroups;

    int counter = 0;
    for (auto* gv : scalarGlobals) {
        auto* ptrTy = dynamic_cast<IR::PointerType*>(gv->getType());
        auto* pointee = ptrTy ? ptrTy->getPointeeType() : nullptr;
        if (!pointee) continue;

        if (readOnlyGlobals.count(gv)) {
            // ★ 只读全局变量：只创建一个 LOAD，不创建 ALLOCA/STORE
            // 所有 load @gv 将被替换为引用此 LOAD 的结果
            auto* load = IR::Instruction::createLoad(pointee, gv,
                gv->getName() + ".readonly" + std::to_string(counter));
            globalToReadOnlyLoad[gv] = load;
            // 插入到 entry block 开头（在 ALLOCA 之后）
            insertAtEntryBeginning(entryBB, load);
        } else {
            auto* alloca = IR::Instruction::createAlloca(pointee, gv->getName() + ".local" + std::to_string(counter));
            auto* load = IR::Instruction::createLoad(pointee, gv, gv->getName() + ".init" + std::to_string(counter));
            auto* store = IR::Instruction::createStore(load, alloca);

            initGroups.push_back({alloca, load, store});
            globalToAlloca[gv] = alloca;
        }
        counter++;
    }

    // 第一批：插入所有 ALLOCA（在现有 ALLOCA 之后、第一个非 ALLOCA 指令之前）
    for (auto& g : initGroups) {
        insertAtEntryBeginning(entryBB, g.alloca);
    }
    // 第二批：插入所有初始化 LOAD/STORE（在所有 ALLOCA 之后、第一个原始指令之前）
    // 逆序插入以保持正确顺序（每次 insertAtEntryBeginning 都插入到同一位置，后插入的会排前面）
    std::vector<IR::Instruction*> initInsts;
    for (auto& g : initGroups) {
        initInsts.push_back(g.load);
        initInsts.push_back(g.store);
    }
    for (auto it = initInsts.rbegin(); it != initInsts.rend(); ++it) {
        insertAtEntryBeginning(entryBB, *it);
    }

    // Step 2: 替换之前收集的 LOAD/STORE 中的全局变量指针
    // - 可写全局变量：替换为 ALLOCA
    // - 只读全局变量：LOAD 替换为引用 entry 处的 LOAD 结果
    //   （只读全局变量不应有 STORE，但保守起见如果有则跳过）
    for (auto& [inst, idx] : replacements) {
        auto* ptr = inst->getOperand(idx);
        auto* gv = dynamic_cast<IR::GlobalVariable*>(ptr);
        if (!gv) continue;

        auto allocaIt = globalToAlloca.find(gv);
        if (allocaIt != globalToAlloca.end()) {
            inst->setOperand(idx, allocaIt->second);
            continue;
        }

        auto roIt = globalToReadOnlyLoad.find(gv);
        if (roIt != globalToReadOnlyLoad.end()) {
            // 只读全局变量：如果是 LOAD，直接替换整个 LOAD 指令的 uses
            if (inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                inst->replaceAllUsesWith(roIt->second);
                // 标记此 LOAD 为死代码（DCE 会清除）
                for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                    inst->setOperand(i, nullptr);
                }
            }
            // 如果是 STORE 到只读全局变量，说明分析有误（不应出现），
            // 保守跳过，保留原指令
        }
    }

    // Step 3: 在函数出口处存回全局变量
    // 先收集所有 RET 指令，避免在遍历时修改指令列表导致迭代器失效
    struct RetInfo {
        IR::BasicBlock* bb;
        IR::Instruction* inst;
    };
    std::vector<RetInfo> retInsts;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::RET) {
                retInsts.push_back({bb.get(), inst.get()});
            }
        }
    }

    for (auto& [bb, retInst] : retInsts) {
        for (auto& [gv, alloca] : globalToAlloca) {
            // 如果函数中从未写入此全局变量，则跳过出口同步
            // （local 的值始终等于 global 的原始值，写回是死存储）
            if (accessInfo.writes.find(gv) == accessInfo.writes.end()) continue;
            auto* ptrTy = dynamic_cast<IR::PointerType*>(gv->getType());
            auto* pointee = ptrTy ? ptrTy->getPointeeType() : nullptr;
            if (!pointee) continue;
            auto* load = IR::Instruction::createLoad(pointee, alloca, gv->getName() + ".exit");
            auto* store = IR::Instruction::createStore(load, gv);
            // 先插入 LOAD，再插入 STORE，确保 LOAD 在 STORE 之前（STORE 引用了 LOAD 的结果）
            insertBefore(bb, retInst, load);
            insertBefore(bb, retInst, store);
        }
    }

    // Step 4: 在 CALL 指令前后同步全局变量（精细化同步）
    // 优化策略：
    //   1. sylib 函数（getint/putint/putarray 等）完全不访问用户全局变量 → 跳过同步
    //   2. 用户定义函数：基于传递性分析，只同步 callee 实际可能访问的全局变量
    //      - callee 可能读取的全局 → CALL 之前 STORE local→global
    //      - callee 可能修改的全局 → CALL 之后 LOAD global→local
    //   3. 其他 external 函数（非 sylib）：保守同步所有提升的全局变量
    for (auto& bb : func->getBlocks()) {
        // 收集当前 BB 中的 CALL 指令（因为我们要在迭代中修改指令列表）
        std::vector<IR::Instruction*> callInsts;
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                callInsts.push_back(inst.get());
            }
        }

        for (auto* callInst : callInsts) {
            if (callInst->getNumOperands() == 0) continue;
            auto* calleeVal = callInst->getOperand(0);
            auto* calleeFunc = dynamic_cast<IR::Function*>(calleeVal);

            // 确定需要同步的全局变量集合
            // readsToSync:  callee 可能读取的全局 → CALL 前需 STORE local→global
            // writesToSync: callee 可能修改的全局 → CALL 后需 LOAD global→local
            std::unordered_set<IR::GlobalVariable*> readsToSync;
            std::unordered_set<IR::GlobalVariable*> writesToSync;
            bool needSync = true;

            if (calleeFunc) {
                if (calleeFunc->isExternal()) {
                    // 外部函数：检查是否为 sylib
                    if (isSylibFunction(calleeFunc->getName())) {
                        // sylib 函数不访问用户全局变量，完全跳过同步
                        needSync = false;
                    } else {
                        // 未知 external 函数：保守同步所有提升的全局变量
                        for (auto& [gv, alloca] : globalToAlloca) {
                            readsToSync.insert(gv);
                            writesToSync.insert(gv);
                        }
                    }
                } else {
                    // 用户定义函数：使用传递性分析
                    auto it = transitiveAccess.find(calleeFunc);
                    if (it != transitiveAccess.end()) {
                        readsToSync = it->second.reads;
                        writesToSync = it->second.writes;
                        // 如果 callee 既不读也不写任何全局变量，跳过同步
                        if (readsToSync.empty() && writesToSync.empty()) {
                            needSync = false;
                        }
                    } else {
                        // 分析信息缺失：保守同步所有
                        for (auto& [gv, alloca] : globalToAlloca) {
                            readsToSync.insert(gv);
                            writesToSync.insert(gv);
                        }
                    }
                }
            } else {
                // 间接调用（函数指针）：保守同步所有
                for (auto& [gv, alloca] : globalToAlloca) {
                    readsToSync.insert(gv);
                    writesToSync.insert(gv);
                }
            }

            if (!needSync) continue;

            // 在 CALL 之前：STORE local→global，仅对 callee 可能读取且本函数修改过的全局
            // 如果本函数从未写入此全局变量，local 的值始终等于 global 的原始值，无需同步
            std::unordered_set<IR::GlobalVariable*> effectiveReadsToSync;
            for (auto* gv : readsToSync) {
                if (accessInfo.writes.find(gv) != accessInfo.writes.end()) {
                    effectiveReadsToSync.insert(gv);
                }
            }
            auto syncStores = createSyncStoresForSet(globalToAlloca, effectiveReadsToSync);
            for (auto* s : syncStores) {
                insertBefore(bb.get(), callInst, s);
            }

            // 在 CALL 之后：LOAD global→local，仅对 callee 可能修改的全局
            auto syncLoads = createSyncLoadsForSet(globalToAlloca, writesToSync);
            // 逆序插入，使它们在 CALL 之后按正确顺序排列
            for (auto it = syncLoads.rbegin(); it != syncLoads.rend(); ++it) {
                insertAfter(bb.get(), callInst, *it);
            }
        }
    }

    return true;
}

} // anonymous namespace

bool globalVariablePromotion(IR::Module* mod) {
    // 先计算模块级传递性全局访问分析
    // 这使得每个 CALL 的同步可以精确到 callee 实际访问的全局变量
    auto transitiveAccess = computeTransitiveGlobalAccess(mod);

    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (promoteGlobalsInFunction(func.get(), transitiveAccess)) {
            changed = true;
        }
    }
    return changed;
}

} // namespace Opt