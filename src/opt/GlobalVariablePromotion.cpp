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

// 检查函数中是否有全局变量访问
bool hasGlobalAccess(IR::Function* func) {
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            if (op == IR::Instruction::Opcode::LOAD ||
                op == IR::Instruction::Opcode::STORE ||
                op == IR::Instruction::Opcode::GETELEMENTPTR) {
                // LOAD: operand(0) is pointer
                // STORE: operand(1) is pointer
                // GETELEMENTPTR: operand(0) is pointer
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

// 收集函数中访问的所有全局变量
std::unordered_set<IR::GlobalVariable*> collectAccessedGlobals(IR::Function* func) {
    std::unordered_set<IR::GlobalVariable*> globals;
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

// 检查值是否为全局变量
bool isGlobal(IR::Value* v) {
    return dynamic_cast<IR::GlobalVariable*>(v) != nullptr;
}

// 在 entry block 开头插入指令（在所有 ALLOCA 之后、第一个非 ALLOCA 指令之前）
// 确保 ALLOCA 及其初始化在使用之前被定义
void insertAtEntryBeginning(IR::BasicBlock* bb, IR::Instruction* inst) {
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
    auto& insts = const_cast<std::vector<std::unique_ptr<IR::Instruction>>&>(bb->getInstructions());
    for (auto it = insts.begin(); it != insts.end(); ++it) {
        if (it->get() == afterInst) {
            ++it;
            insts.insert(it, std::unique_ptr<IR::Instruction>(newInst));
            return;
        }
    }
}

// 为函数中的所有全局变量访问创建同步存储（存回内存）
// 返回新创建的指令列表
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

// 为函数中的所有全局变量创建同步加载（从内存重新加载）
// 返回新创建的指令列表
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

bool promoteGlobalsInFunction(IR::Function* func) {
    if (func->isExternal()) return false;
    if (!hasGlobalAccess(func)) return false;

    auto accessedGlobals = collectAccessedGlobals(func);
    if (accessedGlobals.empty()) return false;

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
    auto* entryBB = func->getEntryBlock();
    std::unordered_map<IR::GlobalVariable*, IR::Instruction*> globalToAlloca;

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

        auto* alloca = IR::Instruction::createAlloca(pointee, gv->getName() + ".local" + std::to_string(counter));
        auto* load = IR::Instruction::createLoad(pointee, gv, gv->getName() + ".init" + std::to_string(counter));
        auto* store = IR::Instruction::createStore(load, alloca);

        initGroups.push_back({alloca, load, store});
        globalToAlloca[gv] = alloca;
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

    // Step 2: 替换之前收集的 LOAD/STORE 中的全局变量指针为 ALLOCA
    // 初始化指令是新创建的，不在 replacements 列表中，不会被错误修改
    for (auto& [inst, idx] : replacements) {
        auto* ptr = inst->getOperand(idx);
        auto it = globalToAlloca.find(dynamic_cast<IR::GlobalVariable*>(ptr));
        if (it != globalToAlloca.end()) {
            inst->setOperand(idx, it->second);
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

    // Step 4: 在 CALL 指令前后同步全局变量
    for (auto& bb : func->getBlocks()) {
        // 收集当前 BB 中的 CALL 指令（因为我们要在迭代中修改指令列表）
        std::vector<IR::Instruction*> callInsts;
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                callInsts.push_back(inst.get());
            }
        }

        for (auto* callInst : callInsts) {
            // 在 CALL 之前：存储所有提升的全局变量
            auto syncStores = createSyncStores(globalToAlloca);
            for (auto* s : syncStores) {
                insertBefore(bb.get(), callInst, s);
            }

            // 在 CALL 之后：重新加载所有提升的全局变量
            auto syncLoads = createSyncLoads(globalToAlloca);
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
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (promoteGlobalsInFunction(func.get())) {
            changed = true;
        }
    }
    return changed;
}

} // namespace Opt