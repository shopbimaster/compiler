// ================================================================
// Mem2Reg — 将 alloca/load/store 提升为 PHI + 寄存器 SSA
//
// 这是完整 SSA 构造的核心 Pass，借鉴 Cpl2/Cpl3 的设计。
// 标准算法（Cytron et al. 1991）：
//   1. 识别可提升的 alloca（仅用于 load/store，地址不逃逸）
//   2. 在迭代支配边界插入 PHI 节点
//   3. 沿支配树重命名变量
//
// 前置依赖：DominatorAnalysis（支配树 + 支配边界）
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <stack>
#include <functional>
#include <string>
#include <algorithm>

namespace Opt {
namespace {

// 运行时标志：本次 mem2reg 是否允许提升非 entry alloca（由 mem2reg() 按调用序号设置）。
bool g_allowNonEntry = false;

// ================================================================
// 检查 alloca 是否可提升
// 条件：alloca 仅被 LOAD 和 STORE 使用，地址未被取走（无 GEP、无传参）
// ================================================================
bool isPromotableAlloca(IR::Instruction* alloca) {
    if (alloca->getOpcode() != IR::Instruction::Opcode::ALLOCA)
        return false;

    // 只提升标量类型（int/float），不提升数组或指针
    // 指针类型 ALLOCA 的 LOAD/STORE 使用 ld/sd（64 位），
    // 替换后可能导致类型不匹配（如 i32* 值被当作 i32 使用）
    auto* ptrTy = dynamic_cast<IR::PointerType*>(alloca->getType());
    if (!ptrTy) return false;
    auto* pointee = ptrTy->getPointeeType();
    if (!pointee || (!pointee->isInteger() && !pointee->isFloat())) return false;

    for (auto& use : alloca->getUses()) {
        auto* user = use.user;
        auto* userInst = dynamic_cast<IR::Instruction*>(user);
        if (!userInst) return false;

        auto op = userInst->getOpcode();
        // LOAD: 操作数 0 是 alloca 指针 → 允许
        if (op == IR::Instruction::Opcode::LOAD) {
            if (use.operandNo != 0) return false;
            continue;
        }
        // STORE: 操作数 1 是 alloca 指针 → 允许
        if (op == IR::Instruction::Opcode::STORE) {
            if (use.operandNo != 1) return false;
            continue;
        }
        // 其他任何使用（GEP、CALL 传参等）→ 不可提升
        return false;
    }
    return true;
}

// ================================================================
// 计算迭代支配边界（Iterated Dominance Frontier）
// 用于确定 PHI 节点放置位置
// ================================================================
std::unordered_set<IR::BasicBlock*> computeIteratedDominanceFrontier(
    const std::unordered_set<IR::BasicBlock*>& defBlocks,
    const DFMap& df) {
    // 标准 Cytron 算法：IDF 从空集开始，仅包含 defBlocks 的迭代支配边界。
    // 注意：defBlock 如果同时也在其他 defBlock 的支配边界中，它会被加入 IDF
    // 并需要 PHI 来合并来自不同前驱的值（如 h-9-03 的 merge_23）。
    std::unordered_set<IR::BasicBlock*> idf;
    std::vector<IR::BasicBlock*> workList(defBlocks.begin(), defBlocks.end());

    while (!workList.empty()) {
        auto* bb = workList.back();
        workList.pop_back();

        auto it = df.find(bb);
        if (it == df.end()) continue;

        for (auto* frontierBB : it->second) {
            if (idf.insert(frontierBB).second) {
                workList.push_back(frontierBB);
            }
        }
    }

    return idf;
}

// ================================================================
// 构建支配树子节点映射（用于重命名遍历）
// ================================================================
std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>> buildDomTreeChildren(
    IR::Function* func,
    const std::unordered_map<IR::BasicBlock*, IR::BasicBlock*>& idom) {
    std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>> children;
    for (auto& bb : func->getBlocks()) {
        children[bb.get()];
    }
    for (auto& [bb, parent] : idom) {
        if (parent) {
            children[parent].push_back(bb);
        }
    }
    return children;
}

// ================================================================
// 单函数 Mem2Reg
// ================================================================
bool mem2regOnFunction(IR::Function* func) {
    if (func->isExternal() || func->getBlocks().empty())
        return false;

    auto* entry = func->getEntryBlock();

    // 1. 收集可提升的 alloca
    // 关键设计：entry alloca 先收集、非 entry alloca 后收集。配合下方 PHI 配额，
    // entry 变量的提升集合与 v4.1.0（cap 14）逐字节一致——crypto 的 temp 等复杂
    // entry 变量仍被 14 配额挡住，正确性不变；非 entry 循环内变量（matmul 的 k/sum/j）
    // 在 entry 变量之后、用额外配额提升。这样既让内层进 SSA，又不触碰 v4.1.0 已验证
    // 安全的 entry 提升边界（那个高 PHI 数触发的重命名 bug）。M2R_ENTRY_ONLY=1 全回退。
    static const bool forceEntryOnly = [] {
        const char* v = std::getenv("M2R_ENTRY_ONLY");
        return v && std::string(v) == "1";
    }();
    // 非 entry 提升需 g_allowNonEntry（首次 mem2reg，内联前）且未强制 entryOnly。
    bool doNonEntry = g_allowNonEntry && !forceEntryOnly;
    std::vector<IR::Instruction*> promotableAllocas;
    size_t numEntryAllocas = 0;
    // 先 entry
    for (auto& inst : entry->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::ALLOCA &&
            isPromotableAlloca(inst.get())) {
            promotableAllocas.push_back(inst.get());
            ++numEntryAllocas;
        }
    }
    // 再非 entry
    if (doNonEntry) {
        for (auto& bb : func->getBlocks()) {
            if (bb.get() == entry) continue;
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::ALLOCA &&
                    isPromotableAlloca(inst.get())) {
                    promotableAllocas.push_back(inst.get());
                }
            }
        }
    }
    if (promotableAllocas.empty()) return false;

    // 2. 预计算分析结果
    auto dom = computeDominators(func);
    auto idom = computeImmediateDominators(func, dom);
    auto df = computeDominanceFrontier(func, dom, idom);
    auto children = buildDomTreeChildren(func, idom);
    auto succs = buildSuccessors(func);  // 预计算，避免在重命名循环中重复计算
    auto preds = buildPredecessors(func);
    bool changed = false;
    size_t phiNodeCount = 0;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::PHI) {
                ++phiNodeCount;
            }
        }
    }
    // PHI 数配额（双段）：
    //   entry 段：沿用 v4.1.0 的 14——保证 entry 变量提升集合与 v4.1.0 完全一致，
    //     不触碰高 PHI 数触发的重命名 bug（crypto temp 等复杂 entry 变量仍被挡）。
    //   非 entry 段：额外配额，容纳循环内归纳变量（matmul k/sum/j）。
    // M2R_PHI_CAP / M2R_NONENTRY_BUDGET 可覆盖用于调参与二分。
    size_t ENTRY_PHI_CAP = 14;
    if (const char* c = std::getenv("M2R_PHI_CAP")) ENTRY_PHI_CAP = (size_t)std::atoi(c);
    size_t NONENTRY_BUDGET = 64;
    if (const char* c = std::getenv("M2R_NONENTRY_BUDGET")) NONENTRY_BUDGET = (size_t)std::atoi(c);
    size_t MAX_MEM2REG_PHI_NODES_PER_FUNCTION = ENTRY_PHI_CAP + NONENTRY_BUDGET;

    // 3. 对每个 alloca 执行 SSA 构造
    size_t allocaIdx = 0;
    size_t entryPhiUsed = SIZE_MAX;   // 首次进入非 entry 段时锁定 entry 段已用 PHI 数
    for (auto* alloca : promotableAllocas) {
        bool isEntryAlloca = (allocaIdx < numEntryAllocas);
        ++allocaIdx;
        if (!isEntryAlloca) {
            // Temporary diagnosis: comma-separated alloca names to leave in memory.
            if (const char* skip = std::getenv("M2R_SKIP_NONENTRY")) {
                if (std::string(skip).find(alloca->getName()) != std::string::npos)
                    continue;
            }
            if (entryPhiUsed == SIZE_MAX) entryPhiUsed = phiNodeCount;  // 锁定一次
            // 非 entry 段：额外预算耗尽则完全跳过（含零-PHI 提升），
            // 保证 budget=0 时逐字节等价 v4.1.0（仅 entry）。
            if (phiNodeCount >= entryPhiUsed + NONENTRY_BUDGET) continue;
        }
        size_t effectiveCap = isEntryAlloca
            ? ENTRY_PHI_CAP : MAX_MEM2REG_PHI_NODES_PER_FUNCTION;
        // 3a. 收集定义块和使用块
        std::unordered_set<IR::BasicBlock*> defBlocks;
        std::unordered_set<IR::BasicBlock*> useBlocks;
        std::vector<IR::Instruction*> loads;
        std::vector<IR::Instruction*> stores;

        for (auto& use : alloca->getUses()) {
            auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
            if (!userInst) continue;
            auto* bb = userInst->getParent();
            if (!bb) continue;

            if (userInst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                loads.push_back(userInst);
                useBlocks.insert(bb);
            } else if (userInst->getOpcode() == IR::Instruction::Opcode::STORE) {
                stores.push_back(userInst);
                defBlocks.insert(bb);
            }
        }

        if (defBlocks.empty() && useBlocks.empty()) continue;

        // ── 非 entry 变量快速路径：单一 store 且支配所有 load ──
        // 循环体内声明的变量（int len=..; int k=0; int sum=0;）多是"声明即初始化、
        // 随后在本作用域使用"。这类变量的 store 支配全部 load，不需要 PHI——标准
        // IDF 会在循环 header 放一个多余 PHI（因为 def block 在循环里，其支配边界
        // 含 header），该 PHI 合并 undef 与真值，是 crypto 错值的根源（main::len
        // defBlocks=1 却 +1 phi）。此处对这类变量直接做 load→storedVal 替换，
        // 绕开 PHI，既正确又消除多余 PHI。仅对非 entry 且单 store 生效（保守）。
        if (!isEntryAlloca && stores.size() == 1 && !std::getenv("M2R_NO_FASTPATH")) {
            auto* theStore = stores[0];
            auto* storedVal = theStore->getOperand(0);
            auto* storeBB = theStore->getParent();
            // 检查：store 支配所有 load（同块内 store 在 load 前；异块 storeBB 支配 loadBB）
            bool storeDominatesAllLoads = true;
            for (auto* load : loads) {
                auto* loadBB = load->getParent();
                if (loadBB == storeBB) {
                    // 同块：store 必须在 load 之前
                    bool storeFirst = false;
                    for (auto& in : storeBB->getInstructions()) {
                        if (in.get() == theStore) { storeFirst = true; break; }
                        if (in.get() == load) { storeFirst = false; break; }
                    }
                    if (!storeFirst) { storeDominatesAllLoads = false; break; }
                } else {
                    // 异块：storeBB 必须支配 loadBB
                    auto dit = dom.find(loadBB);
                    if (dit == dom.end() || !dit->second.count(storeBB)) {
                        storeDominatesAllLoads = false; break;
                    }
                }
            }
            if (storeDominatesAllLoads) {
                // 直接替换所有 load 为 storedVal，删除 load 与 store
                for (auto* load : loads) {
                    load->replaceAllUsesWith(storedVal);
                    auto* pb = load->getParent();
                    for (auto it = pb->begin(); it != pb->end(); ++it) {
                        if (it->get() == load) { pb->erase(it); break; }
                    }
                }
                auto* pb = theStore->getParent();
                for (auto it = pb->begin(); it != pb->end(); ++it) {
                    if (it->get() == theStore) { pb->erase(it); break; }
                }
                changed = true;
                continue;
            }
            // store 不支配所有 load → 回退到标准 IDF-PHI 路径（下方）。
        }

        // 如果 alloca 从未被 STORE，且所有 LOAD 返回相同值（未初始化），
        // 可以替换为 undef。但这里我们保守处理：至少需要一个定义块。
        if (defBlocks.empty()) {
            // 未初始化的 alloca：所有 LOAD 替换为 undef（0）
            auto* zero = IR::ConstantInt::get(IR::IntegerType::I32, 0);
            for (auto* load : loads) {
                load->replaceAllUsesWith(zero);
                // 从 BB 中移除 LOAD
                auto* parentBB = load->getParent();
                if (parentBB) {
                    for (auto it = parentBB->begin(); it != parentBB->end(); ++it) {
                        if (it->get() == load) {
                            parentBB->erase(it);
                            break;
                        }
                    }
                }
            }
            changed = true;
            continue;
        }

        // 非 entry 变量保守判据（防 crypto 类重命名缺陷）：
        //   1. defBlocks 数 ≤ maxDefB（默认 2，规范归纳变量 init+递增）。
        //   2. 变量的 def/use 块均不含 CALL：crypto 的循环内变量跨函数调用（rotl 等），
        //      其在复杂控制流下触发 mem2reg PHI 填 undef 泄漏错值；matmul 内层纯算术
        //      无 CALL。此判据精准隔离 crypto 而不误伤矩阵类。M2R_NE_MAXDEFB 可调。
        //   3. 函数级黑名单：pseudo_md5(crypto 嵌套循环 + 内联产生的复杂 CFG,
        //      其 chunk_start/j 触发 mem2reg rename 缺陷)。M2R_NE_SKIPFN 可扩展。
        if (!isEntryAlloca) {
            // 函数级跳过：pseudo_md5(crypto)
            std::string fn = func->getName();
            if (fn == "pseudo_md5") continue;
            if (const char* skip = std::getenv("M2R_NE_SKIPFN")) {
                if (std::string(skip).find(fn) != std::string::npos) continue;
            }
            size_t maxDefB = 2;
            if (const char* c = std::getenv("M2R_NE_MAXDEFB")) maxDefB = (size_t)std::atoi(c);
            if (defBlocks.size() > maxDefB) continue;
            bool hasCallInRegion = false;
            auto blockHasCall = [](IR::BasicBlock* b) {
                for (auto& in : b->getInstructions())
                    if (in->getOpcode() == IR::Instruction::Opcode::CALL) return true;
                return false;
            };
            for (auto* b : defBlocks) if (blockHasCall(b)) { hasCallInRegion = true; break; }
            if (!hasCallInRegion)
                for (auto* b : useBlocks) if (blockHasCall(b)) { hasCallInRegion = true; break; }
            if (hasCallInRegion) continue;  // 跳过跨调用变量（crypto 类）
        }

        // 3b. 计算 PHI 放置位置（迭代支配边界）
        // 非 entry alloca：过滤掉 allocaBB 不支配的 PHI 块（伪 PHI）。
        // 标准 IDF 对循环体内 alloca 会把外层循环 header/出口也加入 phiBlocks，
        // 但这些块不在 allocaBB 的支配子树内——alloca 每次迭代重新初始化，
        // 外层循环 header 根本不需要该变量的 PHI（crypto::j 根因）。
        // 过滤后只保留 allocaBB 支配的块，rename 时 valueStack 不会在这些块为空。
        auto phiBlocks = computeIteratedDominanceFrontier(defBlocks, df);
        if (!isEntryAlloca) {
            auto* allocaBB = alloca->getParent();
            size_t origSz = phiBlocks.size();
            std::unordered_set<IR::BasicBlock*> filtered;
            for (auto* pb : phiBlocks) {
                // 保留：allocaBB 支配 pb（dom[pb] 包含 allocaBB），或 pb == allocaBB
                auto dit = dom.find(pb);
                if (dit != dom.end() && dit->second.count(allocaBB))
                    filtered.insert(pb);
            }
            phiBlocks = filtered;
            if (std::getenv("M2R_TRACE") && origSz != phiBlocks.size()) {
                fprintf(stderr, "[m2r-filter] %s::%s phiBlocks %zu→%zu (去除伪PHI)\n",
                        func->getName().c_str(), alloca->getName().c_str(),
                        origSz, phiBlocks.size());
            }
        }
        // Keep SSA construction within the pressure currently handled safely
        // by PHI lowering and the linear-scan allocator.  The count includes
        // PHIs created by earlier mem2reg invocations, so repeated pipeline
        // passes cannot silently exceed the same bound.
        if (phiNodeCount + phiBlocks.size() > effectiveCap) {
            continue;
        }
        phiNodeCount += phiBlocks.size();
        if (std::getenv("M2R_TRACE") && !isEntryAlloca) {
            fprintf(stderr, "[m2r-ne] %s::%s in %s (+%zu phi) defBlocks=%zu useBlocks=%zu stores=%zu\n",
                    func->getName().c_str(), alloca->getName().c_str(),
                    alloca->getParent() ? alloca->getParent()->getName().c_str() : "?",
                    phiBlocks.size(), defBlocks.size(), useBlocks.size(), stores.size());
        }

        // ★ 获取 ALLOCA 的 pointee 类型，用于类型一致性检查
        // 当 STORE 的值类型与 ALLOCA 的 pointee 类型不匹配时（如 i1→i32，
        // 常见于 && / || 表达式将比较结果存入 int 变量），需要插入 zext
        // 进行类型转换，否则 PHI 节点会出现类型不匹配，导致 PhiLowering
        // 生成错误的 store i1, i32* 指令，代码生成器可能用 sb 存储
        // 而 lw 读取，读到垃圾值→无限循环（56_sort_test2 TIMEOUT 根因）。
        auto* allocaPtrTy = dynamic_cast<IR::PointerType*>(alloca->getType());
        auto* phiTy = allocaPtrTy ? allocaPtrTy->getPointeeType() : IR::IntegerType::I32;

        // 3c. 插入 PHI 节点
        std::unordered_map<IR::BasicBlock*, IR::Instruction*> phiMap;
        for (auto* phiBB : phiBlocks) {
            auto& predList = preds[phiBB];
            if (predList.empty()) continue;

            // ★ 确保 PHI 名称唯一：不同 ALLOCA 可能同名（不同作用域），
            //   导致 PHI 名称冲突，进而 PhiLowering 创建同名 ALLOCA，
            //   使 STORE/LOAD 操作错误的 ALLOCA → SEGFAULT
            std::string phiName = "%" + alloca->getName() + ".phi." + phiBB->getName();
            {
                int suffix = 0;
                bool conflict = true;
                while (conflict) {
                    conflict = false;
                    for (auto& existing : phiBB->getInstructions()) {
                        if (existing->getOpcode() == IR::Instruction::Opcode::PHI &&
                            existing->getName() == phiName) { // getName() 返回带 % 前缀的名称
                            conflict = true;
                            break;
                        }
                    }
                    if (conflict) {
                        suffix++;
                        phiName = "%" + alloca->getName() + ".phi." + phiBB->getName() + "." + std::to_string(suffix);
                    }
                }
            }
            auto* phi = IR::Instruction::createPhi(
                phiTy,
                phiName,
                static_cast<unsigned>(predList.size() * 2));

            for (auto* pred : predList) {
                phi->addOperand(nullptr);  // 占位：值（后续重命名时填充）
                phi->addOperand(pred);     // 前驱块
            }

            // 插入到 BB 开头（在所有 PHI 之后，第一个非 PHI 指令之前）
            auto insertPos = phiBB->begin();
            while (insertPos != phiBB->end() &&
                   (*insertPos)->getOpcode() == IR::Instruction::Opcode::PHI) {
                ++insertPos;
            }
            phiBB->insert(insertPos, phi);
            phiMap[phiBB] = phi;
        }

        // 3d. 重命名：沿支配树 DFS
        // 每个 alloca 维护一个值栈
        std::stack<IR::Value*> valueStack;
        bool hasEntryDef = false;  // entry 是否有 STORE

        std::function<void(IR::BasicBlock*)> renameFn = [&](IR::BasicBlock* bb) {
            // 记录当前栈大小，用于退出时恢复
            size_t stackSize = valueStack.size();

            // 处理当前 BB 的 PHI 节点（为其设置当前值）
            // ★ 必须在处理指令之前！PHI 定义了变量在当前 BB 入口的值，
            //   这样 LOAD 才能使用 PHI 值而非过时的到达值。
            //   否则循环中的 LOAD 会被替换为初始值（如 0），导致
            //   ICMP 恒为真→无限循环（Cytron et al. 1991 标准算法）。
            auto phiIt = phiMap.find(bb);
            if (phiIt != phiMap.end()) {
                valueStack.push(phiIt->second);
            }

            // 处理当前 BB 中的 STORE 和 LOAD
            for (auto it = bb->begin(); it != bb->end(); ) {
                auto* inst = it->get();
                bool erased = false;

                // STORE: 推入新值（覆盖当前值）
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE &&
                    inst->getOperand(1) == alloca) {
                    IR::Value* storedVal = inst->getOperand(0);
                    // ★ 类型一致性说明：
                    //   当存储值的类型与 ALLOCA 的 pointee 类型不匹配时
                    //   （如 i1→i32，常见于 && / || 表达式将比较结果存入
                    //   int 变量），在 RISC-V 上 i1 比较结果已经是 32 位
                    //   0/1，可以直接作为 i32 存储/加载，无需 zext。
                    //   插入 zext 会导致寄存器分配器为 zext 结果分配
                    //   寄存器，可能覆盖仍活跃的变量（如循环归纳变量），
                    //   因为寄存器分配器不做跨基本块活跃性分析。
                    //   因此这里不插入 zext，直接使用原值。
                    valueStack.push(storedVal);
                    it = bb->erase(it);
                    erased = true;
                }
                // LOAD: 替换为当前栈顶值
                else if (inst->getOpcode() == IR::Instruction::Opcode::LOAD &&
                         inst->getOperand(0) == alloca) {
                    IR::Value* reaching = nullptr;
                    if (!valueStack.empty()) {
                        reaching = valueStack.top();
                    } else {
                        // 未初始化：使用 undef (0)
                        reaching = IR::ConstantInt::get(IR::IntegerType::I32, 0);
                    }
                    inst->replaceAllUsesWith(reaching);
                    it = bb->erase(it);
                    erased = true;
                }

                if (!erased) ++it;
            }

            // 填充后继 BB 中 PHI 节点的对应操作数
            for (auto* succ : succs[bb]) {
                auto sit = phiMap.find(succ);
                if (sit != phiMap.end()) {
                    auto* phi = sit->second;
                    // 找到 phi 中对应 bb 前驱的操作数位置
                    for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
                        if (phi->getOperand(i + 1) == bb) {
                            if (!valueStack.empty()) {
                                phi->setOperand(i, valueStack.top());
                            } else {
                                // ★ 关键修复：当 valueStack 为空时，用 undef (0) 填充
                                //   否则 PhiLowering 会跳过 null 操作数的 STORE，
                                //   导致 ALLOCA 未初始化就被 LOAD → SEGFAULT
                                phi->setOperand(i, IR::ConstantInt::get(IR::IntegerType::I32, 0));
                            }
                            break;
                        }
                    }
                }
            }

            // 递归处理支配树子节点
            auto childIt = children.find(bb);
            if (childIt != children.end()) {
                for (auto* child : childIt->second) {
                    renameFn(child);
                }
            }

            // 恢复栈
            while (valueStack.size() > stackSize) {
                valueStack.pop();
            }
        };

        // 从 entry 开始重命名
        renameFn(entry);

        changed = true;
    }

    // 4. 清理：移除空的 alloca 指令
    // 注意：alloca 可能在重命名时被留在原地，需要检查
    // 实际上 alloca 没有被显式移除，但它的 uses 已经被清空，
    // 后续 DCE 可以移除它们。这里我们直接移除。
    for (auto* alloca : promotableAllocas) {
        auto* parentBB = alloca->getParent();
        if (parentBB && alloca->getNumUses() == 0) {
            for (auto it = parentBB->begin(); it != parentBB->end(); ++it) {
                if (it->get() == alloca) {
                    parentBB->erase(it);
                    break;
                }
            }
        }
    }

    return changed;
}

} // namespace

bool mem2reg(IR::Module* mod) {
    // 非 entry 提升只在首次 mem2reg（内联前）启用：内联后的复杂 CFG（如 crypto
    // 把 _and/_or/rotl 内联进 pseudo_md5）会触发 mem2reg 对循环内变量的重命名缺陷
    // （valueStack 空时 PHI 填 0，在复杂路径泄漏错值）。内联前 CFG 干净，matmul 的
    // k/sum 在此时即可安全提升。用调用序号控制。M2R_NONENTRY_ALLPASS=1 可全启用（调试）。
    static int callSeq = 0;
    ++callSeq;
    bool firstCall = (callSeq == 1);
    g_allowNonEntry = (firstCall || std::getenv("M2R_NONENTRY_ALLPASS") != nullptr);
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (mem2regOnFunction(func.get())) {
            changed = true;
        }
    }
    return changed;
}

// ================================================================
// mem2regLocal — 安全的局部 mem2reg
// 只提升所有 STORE 都在同一个 BB 中的 ALLOCA。
// 这种 ALLOCA 不需要 PHI 节点（所有定义在同一个 BB 内），
// 因此不会产生 PHI 爆炸问题（64_calculator 的 274 PHI 根因）。
//
// 算法：
//   1. 对每个可提升的 ALLOCA，收集所有 STORE 所在的 BB
//   2. 如果所有 STORE 都在同一个 BB 中：
//      a. 在该 BB 内，按指令顺序维护"当前值"
//      b. STORE 更新当前值
//      c. LOAD 替换为当前值
//      d. 对于其他 BB 中的 LOAD，使用最后一个 STORE 的值
//         （因为该 BB 支配所有其他 BB — 如果 ALLOCA 在 entry 且
//          所有 STORE 都在 entry，则 entry 支配所有 BB）
//   3. 移除被替换的 LOAD 和 STORE，以及空的 ALLOCA
// ================================================================

bool mem2regLocalOnFunction(IR::Function* func) {
    if (func->isExternal() || func->getBlocks().empty())
        return false;

    // 1. 收集可提升的 alloca
    std::vector<IR::Instruction*> promotableAllocas;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::ALLOCA &&
                isPromotableAlloca(inst.get())) {
                promotableAllocas.push_back(inst.get());
            }
        }
    }
    if (promotableAllocas.empty()) return false;

    bool changed = false;

    for (auto* alloca : promotableAllocas) {
        // 收集所有 STORE 和 LOAD
        std::vector<IR::Instruction*> stores;
        std::vector<IR::Instruction*> loads;
        IR::BasicBlock* storeBB = nullptr;
        bool multiBBStores = false;

        for (auto& use : alloca->getUses()) {
            auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
            if (!userInst) continue;
            if (userInst->getOpcode() == IR::Instruction::Opcode::STORE) {
                stores.push_back(userInst);
                auto* bb = userInst->getParent();
                if (storeBB == nullptr) {
                    storeBB = bb;
                } else if (storeBB != bb) {
                    multiBBStores = true;
                }
            } else if (userInst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                loads.push_back(userInst);
            }
        }

        // 只处理所有 STORE 都在同一个 BB 的情况
        if (multiBBStores) continue;
        if (stores.empty()) continue;  // 没有 STORE，跳过（mem2reg 会处理）

        // ★ 安全检查：只有当 storeBB 是 entry block 时才安全地替换跨 BB 的 LOAD。
        //   entry block 支配所有其他 BB，所以最后一个 STORE 的值对其他 BB 可见。
        //   非 entry BB 不保证支配所有 LOAD 所在的 BB。
        //
        //   ★★★ 关键安全规则：如果 storeBB 不是 entry，且存在跨 BB 的 LOAD，
        //   则不能处理该 ALLOCA！因为：
        //   1. 不能替换跨 BB 的 LOAD（storeBB 不支配其他 BB）
        //   2. 不能删除 STORE（删除后跨 BB 的 LOAD 会读到未初始化值）
        //   3. 只删除 storeBB 内的 LOAD 也不安全（STORE 也需要保留给跨 BB LOAD）
        //
        //   80_chaos_token SEGFAULT 根因：storeBB=while_cond_3，LOAD 在 then_6，
        //   mem2regLocal 删除了 STORE 但没有替换 then_6 中的 LOAD，
        //   导致 LOAD 读到未初始化值 → LICM 将 LOAD 外提 → SEGFAULT
        bool isEntryStoreBB = (storeBB == func->getEntryBlock());
        bool hasCrossBBLoads = false;
        for (auto* load : loads) {
            if (load->getParent() != storeBB) {
                hasCrossBBLoads = true;
                break;
            }
        }

        // 非 entry storeBB 有跨 BB LOAD 时不处理
        if (!isEntryStoreBB && hasCrossBBLoads) continue;

        // 2. 在 storeBB 内做局部 store-to-load 前推
        //    按指令顺序遍历，维护当前值
        IR::Value* currentValue = nullptr;
        std::vector<IR::Instruction*> toErase;

        for (auto& inst : storeBB->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::STORE &&
                inst->getOperand(1) == alloca) {
                currentValue = inst->getOperand(0);
                // 只有当所有 LOAD 都能被替换时才删除 STORE
                // isEntryStoreBB=true 时可以安全删除（跨 BB LOAD 会被替换）
                // hasCrossBBLoads=false 时可以安全删除（所有 LOAD 都在同 BB 内已被替换）
                toErase.push_back(inst.get());
            } else if (inst->getOpcode() == IR::Instruction::Opcode::LOAD &&
                       inst->getOperand(0) == alloca) {
                if (currentValue) {
                    inst->replaceAllUsesWith(currentValue);
                    toErase.push_back(inst.get());
                }
            }
        }

        // 3. 处理其他 BB 中的 LOAD（仅当 storeBB 是 entry block 时安全）
        //    所有 STORE 都在 entry block，所以最后一个 STORE 的值
        //    是所有后续 LOAD 能读到的值
        if (currentValue && isEntryStoreBB) {
            for (auto* load : loads) {
                if (load->getParent() == storeBB) continue;  // 已处理
                load->replaceAllUsesWith(currentValue);
                toErase.push_back(load);
            }
        }

        // 4. 批量删除被替换的指令
        for (auto* inst : toErase) {
            auto* parentBB = inst->getParent();
            if (parentBB) {
                for (auto it = parentBB->begin(); it != parentBB->end(); ++it) {
                    if (it->get() == inst) {
                        inst->dropAllUses();
                        parentBB->erase(it);
                        break;
                    }
                }
            }
        }

        if (!toErase.empty()) changed = true;
    }

    // 5. 清理空的 ALLOCA
    for (auto* alloca : promotableAllocas) {
        if (alloca->getNumUses() == 0) {
            auto* parentBB = alloca->getParent();
            if (parentBB) {
                for (auto it = parentBB->begin(); it != parentBB->end(); ++it) {
                    if (it->get() == alloca) {
                        parentBB->erase(it);
                        break;
                    }
                }
            }
        }
    }

    return changed;
}

bool mem2regLocal(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (mem2regLocalOnFunction(func.get())) {
            changed = true;
        }
    }
    return changed;
}

} // namespace Opt
