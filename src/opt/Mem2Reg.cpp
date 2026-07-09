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

// ================================================================
// 检查 alloca 是否可提升
// 条件：alloca 仅被 LOAD 和 STORE 使用，地址未被取走（无 GEP、无传参）
// ================================================================
bool isPromotableAlloca(IR::Instruction* alloca) {
    if (alloca->getOpcode() != IR::Instruction::Opcode::ALLOCA)
        return false;

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

    // 2. 预计算分析结果
    auto dom = computeDominators(func);
    auto idom = computeImmediateDominators(func, dom);
    auto df = computeDominanceFrontier(func, dom, idom);
    auto children = buildDomTreeChildren(func, idom);
    auto succs = buildSuccessors(func);  // 预计算，避免在重命名循环中重复计算
    auto preds = buildPredecessors(func);
    auto* entry = func->getEntryBlock();

    bool changed = false;

    // 3. 对每个 alloca 执行 SSA 构造
    for (auto* alloca : promotableAllocas) {
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

        // 3b. 计算 PHI 放置位置（迭代支配边界）
        auto phiBlocks = computeIteratedDominanceFrontier(defBlocks, df);

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
        // 如果 entry 块也在 defBlocks 中，STORE 会在重命名时处理
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
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (mem2regOnFunction(func.get())) {
            changed = true;
        }
    }
    return changed;
}

} // namespace Opt