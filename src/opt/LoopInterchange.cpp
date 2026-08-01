// ================================================================
// P1: 循环交换（Loop Interchange）
// 策略：
//   对完全嵌套的二重 while 循环，交换内外循环顺序以改善缓存局部性
//   仅处理简单情况：无 break/continue/call，相同的循环边界
//   通过交换 ALLOCA 引用和分支目标实现
// ================================================================

#include "opt/Optimizer.h"
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// ---- 循环信息（使用共享的 computeDominators / buildSuccessors / buildPredecessors） ----
struct LoopInfo {
    IR::BasicBlock* header;
    IR::BasicBlock* latch;
    BBSet body;
};

std::vector<LoopInfo> detectLoops(IR::Function* func) {
    auto dom = computeDominators(func);
    auto succs = buildSuccessors(func);
    auto preds = buildPredecessors(func);
    std::vector<LoopInfo> loops;

    for (auto& bb : func->getBlocks()) {
        for (auto* succ : succs[bb.get()]) {
            auto it = dom.find(bb.get());
            if (it != dom.end() && it->second.count(succ) && succ != bb.get()) {
                LoopInfo loop;
                loop.header = succ;
                loop.latch = bb.get();
                loop.body.insert(succ);

                std::vector<IR::BasicBlock*> wl;
                std::unordered_set<IR::BasicBlock*> visited;
                wl.push_back(bb.get());
                visited.insert(bb.get());

                while (!wl.empty()) {
                    auto* cur = wl.back(); wl.pop_back();
                    loop.body.insert(cur);
                    if (cur == succ) continue;
                    for (auto* p : preds[cur]) {
                        if (!visited.count(p) && !loop.body.count(p)) {
                            visited.insert(p);
                            wl.push_back(p);
                        }
                    }
                }
                loops.push_back(std::move(loop));
            }
        }
    }
    return loops;
}

// ---- 从 header 的 ICMP 中提取 ALLOCA 引用 ----
IR::Value* extractIndVar(IR::BasicBlock* header) {
    for (auto& inst : header->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::ICMP) continue;
        if (inst->getNumOperands() < 2) continue;
        for (unsigned i = 0; i < 2; ++i) {
            auto* op = inst->getOperand(i);
            auto* load = dynamic_cast<IR::Instruction*>(op);
            if (load && load->getOpcode() == IR::Instruction::Opcode::LOAD) {
                return load->getOperand(0);
            }
        }
    }
    return nullptr;
}

// ---- 检查 BB 是否为简单循环体（无 call/phi/嵌套循环） ----
bool isSimpleBB(IR::BasicBlock* bb) {
    for (auto& inst : bb->getInstructions()) {
        auto op = inst->getOpcode();
        if (op == IR::Instruction::Opcode::CALL) return false;
        if (op == IR::Instruction::Opcode::PHI) return false;
    }
    return true;
}

// ---- 从 BB 中查找 STORE(x+1, x) 的 ALLOCA 引用 ----
IR::Value* findIncrementVar(IR::BasicBlock* bb) {
    for (auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::STORE) continue;
        auto* val = inst->getOperand(0);
        auto* add = dynamic_cast<IR::Instruction*>(val);
        if (add && add->getOpcode() == IR::Instruction::Opcode::ADD) {
            if (add->getNumOperands() >= 2) {
                for (unsigned i = 0; i < 2; ++i) {
                    auto* op = add->getOperand(i);
                    if (auto* ci = dynamic_cast<IR::ConstantInt*>(op)) {
                        if (ci->getValue() == 1) {
                            return inst->getOperand(1); // STORE 的指针
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

// ---- 替换 STORE 0, from → STORE 0, to（初始化交换） ----
void replaceStore0InBB(IR::BasicBlock* bb, IR::Value* from, IR::Value* to) {
    for (auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::STORE) continue;
        auto* ci = dynamic_cast<IR::ConstantInt*>(inst->getOperand(0));
        if (ci && ci->getValue() == 0 && inst->getOperand(1) == from) {
            inst->setOperand(1, to);
        }
    }
}

// ---- 替换自增模式：load from; add 1; store from → load to; add 1; store to ----
// 仅替换自增指令链（LOAD from → ADD 1 → STORE from），不影响计算体中的其他引用
void replaceIncrementInBB(IR::BasicBlock* bb, IR::Value* from, IR::Value* to) {
    for (auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::STORE) continue;
        if (inst->getOperand(1) != from) continue;
        auto* add = dynamic_cast<IR::Instruction*>(inst->getOperand(0));
        if (!add || add->getOpcode() != IR::Instruction::Opcode::ADD) continue;
        IR::Instruction* loadFrom = nullptr;
        bool hasConst1 = false;
        for (unsigned i = 0; i < add->getNumOperands(); ++i) {
            auto* op = add->getOperand(i);
            auto* ci = dynamic_cast<IR::ConstantInt*>(op);
            if (ci && ci->getValue() == 1) { hasConst1 = true; continue; }
            auto* ld = dynamic_cast<IR::Instruction*>(op);
            if (ld && ld->getOpcode() == IR::Instruction::Opcode::LOAD
                && ld->getOperand(0) == from) {
                loadFrom = ld;
            }
        }
        if (!hasConst1 || !loadFrom) continue;
        loadFrom->setOperand(0, to);
        inst->setOperand(1, to);
    }
}

// ---- 替换 ICMP 中 LOAD from 的指针操作数，同时交换常量 bound ----
void replaceCmpLoadInBB(IR::BasicBlock* bb, IR::Value* from, IR::Value* to) {
    for (auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::ICMP) continue;
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            auto* load = dynamic_cast<IR::Instruction*>(inst->getOperand(i));
            if (load && load->getOpcode() == IR::Instruction::Opcode::LOAD
                && load->getOperand(0) == from) {
                load->setOperand(0, to);
                return;
            }
        }
    }
}

// ---- 替换 BB 中所有 LOAD from 的指针操作数 ----
// 循环交换时，ALLOCA 值已交换，循环体内的 LOAD 指令也需要交换
// 以保持数组索引计算的一致性。例如 A[i][k] 在交换后应仍为 A[i][k]，
// 而不是因为 ALLOCA 值交换而变成 A[k][i]。
void replaceLoadInBB(IR::BasicBlock* bb, IR::Value* from, IR::Value* to) {
    for (auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::LOAD) continue;
        if (inst->getOperand(0) == from) {
            inst->setOperand(0, to);
        }
    }
}

// ---- 在 BB 中双向交换 LOAD 引用（from↔to），单次遍历避免重复交换 ----
void swapLoadsInBB(IR::BasicBlock* bb, IR::Value* a, IR::Value* b) {
    for (auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::LOAD) continue;
        if (inst->getOperand(0) == a) {
            inst->setOperand(0, b);
        } else if (inst->getOperand(0) == b) {
            inst->setOperand(0, a);
        }
    }
}

// ---- 在 BB 中替换 STORE 的指针操作数（from→to） ----
// 用于交换自增链中的 STORE 指针，仅在 LOAD 已交换后调用
void replaceStorePtrInBB(IR::BasicBlock* bb, IR::Value* from, IR::Value* to) {
    for (auto& inst : bb->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::STORE) continue;
        if (inst->getOperand(1) == from) {
            inst->setOperand(1, to);
        }
    }
}

// ---- 交换 outer/inner header 中 ICMP 的常量 bound ----
// 循环交换后，outer loop bound 应从 M 变为 N，inner loop bound 应从 N 变为 M。
// 当 ICMP 的一个操作数是 LOAD from 全局变量时（如 @M、@N），交换这些 LOAD 的全局引用。
// 当 ICMP 的一个操作数是 ConstantInt 时，交换这些常量。
void swapICmpConstants(IR::BasicBlock* outerHeader, IR::BasicBlock* innerHeader) {
    IR::Instruction* outerICmp = nullptr;
    IR::Instruction* innerICmp = nullptr;
    for (auto& inst : outerHeader->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::ICMP) {
            outerICmp = inst.get(); break;
        }
    }
    for (auto& inst : innerHeader->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::ICMP) {
            innerICmp = inst.get(); break;
        }
    }
    if (!outerICmp || !innerICmp) return;

    // 尝试交换 ConstantInt（如 i < 64 这种字面量 bound）
    IR::ConstantInt* outerConst = nullptr;
    unsigned outerConstIdx = 0;
    IR::ConstantInt* innerConst = nullptr;
    unsigned innerConstIdx = 0;

    for (unsigned i = 0; i < outerICmp->getNumOperands(); ++i) {
        auto* ci = dynamic_cast<IR::ConstantInt*>(outerICmp->getOperand(i));
        if (ci) { outerConst = ci; outerConstIdx = i; break; }
    }
    for (unsigned i = 0; i < innerICmp->getNumOperands(); ++i) {
        auto* ci = dynamic_cast<IR::ConstantInt*>(innerICmp->getOperand(i));
        if (ci) { innerConst = ci; innerConstIdx = i; break; }
    }

    if (outerConst && innerConst) {
        outerICmp->setOperand(outerConstIdx, innerConst);
        innerICmp->setOperand(innerConstIdx, outerConst);
        return;
    }

    // 尝试交换 LOAD from 全局变量（如 LOAD @M、LOAD @N）
    // ICMP 有两个操作数：一个是 LOAD from 局部 ALLOCA（循环变量），
    // 另一个是 LOAD from 全局变量（循环边界）。交换后者。
    for (unsigned i = 0; i < outerICmp->getNumOperands(); ++i) {
        auto* outerLoad = dynamic_cast<IR::Instruction*>(outerICmp->getOperand(i));
        if (!outerLoad || outerLoad->getOpcode() != IR::Instruction::Opcode::LOAD) continue;
        auto* outerPtr = outerLoad->getOperand(0);
        if (!outerPtr || !dynamic_cast<IR::GlobalVariable*>(outerPtr)) continue;

        for (unsigned j = 0; j < innerICmp->getNumOperands(); ++j) {
            auto* innerLoad = dynamic_cast<IR::Instruction*>(innerICmp->getOperand(j));
            if (!innerLoad || innerLoad->getOpcode() != IR::Instruction::Opcode::LOAD) continue;
            auto* innerPtr = innerLoad->getOperand(0);
            if (!innerPtr || !dynamic_cast<IR::GlobalVariable*>(innerPtr)) continue;

            outerLoad->setOperand(0, innerPtr);
            innerLoad->setOperand(0, outerPtr);
            return;
        }
    }
}

// ---- 检查 header 的 ICMP 中是否引用了指定的变量（作为 LOAD 的操作数） ----
// 用于防止循环交换时内层循环边界依赖外层循环变量
bool icmpUsesVar(IR::BasicBlock* header, IR::Value* var) {
    for (auto& inst : header->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::ICMP) continue;
        for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
            auto* load = dynamic_cast<IR::Instruction*>(inst->getOperand(i));
            if (load && load->getOpcode() == IR::Instruction::Opcode::LOAD
                && load->getOperand(0) == var) {
                return true;
            }
        }
    }
    return false;
}

// ---- 检查两个循环 header 的边界是否相同 ----
// 循环交换要求 A[i][j] → A[j][i] 的访问模式在数组维度上安全，
// 仅当两个循环边界相同时才安全（否则非方阵会越界）。
// 例如 array[20][100] 中 i<20, j<100，交换后 A[j][i] 写 j 可达 99 越界。
// 边界可以是：ConstantInt、全局变量、或局部变量（同一 ALLOCA）。
bool sameLoopBounds(IR::BasicBlock* outerHeader, IR::BasicBlock* innerHeader,
                    IR::Value* outerVar, IR::Value* innerVar) {
    IR::Instruction* outerICmp = nullptr;
    IR::Instruction* innerICmp = nullptr;
    for (auto& inst : outerHeader->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::ICMP) {
            outerICmp = inst.get(); break;
        }
    }
    for (auto& inst : innerHeader->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::ICMP) {
            innerICmp = inst.get(); break;
        }
    }
    if (!outerICmp || !innerICmp) return false;

    // 提取非循环变量的操作数作为边界
    auto getBound = [](IR::Instruction* icmp, IR::Value* indVar) -> IR::Value* {
        for (unsigned i = 0; i < icmp->getNumOperands(); ++i) {
            auto* op = icmp->getOperand(i);
            auto* load = dynamic_cast<IR::Instruction*>(op);
            if (load && load->getOpcode() == IR::Instruction::Opcode::LOAD
                && load->getOperand(0) == indVar) {
                continue; // 这是循环变量，跳过
            }
            return op; // 这是边界
        }
        return nullptr;
    };

    IR::Value* outerBound = getBound(outerICmp, outerVar);
    IR::Value* innerBound = getBound(innerICmp, innerVar);
    if (!outerBound || !innerBound) return false;

    // 同一 Value → 相同
    if (outerBound == innerBound) return true;

    // 两个 ConstantInt → 比较值
    auto* oc = dynamic_cast<IR::ConstantInt*>(outerBound);
    auto* ic = dynamic_cast<IR::ConstantInt*>(innerBound);
    if (oc && ic) return oc->getValue() == ic->getValue();

    // 两个 LOAD → 比较指针操作数（ALLOCA 或 GlobalVariable）
    auto* ol = dynamic_cast<IR::Instruction*>(outerBound);
    auto* il = dynamic_cast<IR::Instruction*>(innerBound);
    if (ol && ol->getOpcode() == IR::Instruction::Opcode::LOAD
        && il && il->getOpcode() == IR::Instruction::Opcode::LOAD) {
        return ol->getOperand(0) == il->getOperand(0);
    }

    // 无法确定 → 保守地拒绝
    return false;
}

// ---- 检查变量是否在指定的 BB 集合之外被使用 ----
// 如果循环变量在循环体外被使用（如后续的循环嵌套），则交换会破坏后续代码的正确性
bool isUsedOutsideBBSet(IR::Value* var, const BBSet& allowed) {
    for (auto& use : var->getUses()) {
        auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
        if (!userInst) continue;
        auto* userBB = userInst->getParent();
        if (!userBB) continue;
        if (!allowed.count(userBB)) return true;
    }
    return false;
}

// ---- 将 ALLOCA 指令移动到 entry block ----
// 当内层循环变量的 ALLOCA 不在 entry block 时，需要先移到 entry block，
// 否则交换后 entry block 的 STORE 会引用未定义的 ALLOCA
void moveAllocaToEntry(IR::Function* func, IR::Value* allocaVal) {
    auto* allocaInst = dynamic_cast<IR::Instruction*>(allocaVal);
    if (!allocaInst || allocaInst->getOpcode() != IR::Instruction::Opcode::ALLOCA) return;
    auto* parentBB = allocaInst->getParent();
    if (!parentBB) return;
    auto* entry = func->getEntryBlock();
    if (!entry || parentBB == entry) return;

    // 从原 BB 中移除 ALLOCA，插入到 entry block 末尾（terminator 之前）
    for (auto it = parentBB->begin(); it != parentBB->end(); ++it) {
        if (it->get() == allocaInst) {
            auto node = std::move(*it);
            parentBB->erase(it);

            auto termIt = entry->end();
            --termIt; // terminator 之前
            entry->insert(termIt, node.release());
            break;
        }
    }
}

// ---- 尝试对单个函数做循环交换 ----
bool tryInterchange(IR::Function* func) {
    auto loops = detectLoops(func);
    bool changed = false;

    for (size_t oi = 0; oi < loops.size(); ++oi) {
        for (size_t ii = 0; ii < loops.size(); ++ii) {
            if (oi == ii) continue;
            auto& outer = loops[oi];
            auto& inner = loops[ii];

            if (outer.body.count(inner.header) == 0) continue;
            if (inner.body.count(outer.header) != 0) continue;

            // 安全检查：外层循环体中不能有"平行"的其他循环（除了当前内层循环）
            // 例如 row_reduce: r 循环体中有两个 c 循环，交换后第二个 c 循环仍使用
            // 原来的循环变量，导致语义错误
            // 例如 trsm_optimized: i 循环中有 k 和 j 两个内层循环，交换后 j 循环
            // 依赖的 i 变量变成内层变量，导致 use-before-def
            //
            // 改进：允许完全嵌套的三重循环（i-j-k）
            // 如果其他循环（k）在当前内层循环（j）的循环体内 → OK（完全嵌套）
            // 如果其他循环（k）在外层循环（i）体内但不在内层循环（j）内 → SKIP（平行循环）
            bool hasParallelLoop = false;
            for (size_t mi = 0; mi < loops.size(); ++mi) {
                if (mi == oi || mi == ii) continue;
                auto& mid = loops[mi];
                // 检查 mid 是否在 outer 的循环体内
                if (outer.body.count(mid.header)) {
                    // 如果 mid 也在 inner 的循环体内，说明是完全嵌套（i包含j，j包含k）→ OK
                    // 否则是平行循环（i包含j和k，但j和k是兄弟）→ SKIP
                    if (!inner.body.count(mid.header)) {
                        hasParallelLoop = true;
                        break;
                    }
                }
            }
            if (hasParallelLoop) continue;

            auto* outerVar = extractIndVar(outer.header);
            auto* innerVar = extractIndVar(inner.header);
            if (!outerVar || !innerVar || outerVar == innerVar) continue;

            // 安全检查1：内层循环边界不能依赖外层循环变量
            // 例如 while (j < i) 中内层边界依赖外层变量，交换后语义错误。
            if (icmpUsesVar(inner.header, outerVar)) continue;
            // 安全检查1b：外层循环边界也不能依赖内层循环变量
            if (icmpUsesVar(outer.header, innerVar)) continue;

            IR::BasicBlock* outerBody = nullptr;
            for (auto* bb : outer.body) {
                if (bb == outer.header) continue;
                if (bb == inner.header) continue;
                if (inner.body.count(bb)) continue;
                auto* term = bb->getTerminator();
                if (term && term->getOpcode() == IR::Instruction::Opcode::BR) {
                    if (term->getOperand(0) == inner.header) {
                        if (outerBody) { outerBody = nullptr; break; }
                        outerBody = bb;
                    }
                }
            }
            if (!outerBody) continue;

            IR::BasicBlock* outerLatch = nullptr;
            for (auto* bb : outer.body) {
                if (bb == outer.header) continue;
                if (bb == inner.header) continue;
                if (inner.body.count(bb)) continue;
                if (bb == outerBody) continue;
                auto* term = bb->getTerminator();
                if (term && term->getOpcode() == IR::Instruction::Opcode::BR) {
                    if (term->getOperand(0) == outer.header) {
                        if (outerLatch) { outerLatch = nullptr; break; }
                        outerLatch = bb;
                    }
                }
            }
            if (!outerLatch) continue;

            IR::BasicBlock* innerBody = nullptr;
            for (auto* bb : inner.body) {
                if (bb == inner.header) continue;
                if (innerBody) { innerBody = nullptr; break; }
                innerBody = bb;
            }
            if (!innerBody) continue;

            if (!isSimpleBB(outerBody) || !isSimpleBB(innerBody) || !isSimpleBB(outerLatch)) continue;

            bool outerInitOk = false;
            for (auto& inst : outerBody->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                    auto* ci = dynamic_cast<IR::ConstantInt*>(inst->getOperand(0));
                    if (ci && ci->getValue() == 0 && inst->getOperand(1) == innerVar) {
                        outerInitOk = true;
                        break;
                    }
                }
            }
            if (!outerInitOk) continue;

            auto* innerIncVar = findIncrementVar(innerBody);
            if (innerIncVar != innerVar) continue;

            auto* outerIncVar = findIncrementVar(outerLatch);
            if (outerIncVar != outerVar) continue;

            auto* obTerm = outerBody->getTerminator();
            if (!obTerm || obTerm->getOpcode() != IR::Instruction::Opcode::BR) continue;
            if (obTerm->getOperand(0) != inner.header) continue;

            auto* ibTerm = innerBody->getTerminator();
            if (!ibTerm || ibTerm->getOpcode() != IR::Instruction::Opcode::BR) continue;
            if (ibTerm->getOperand(0) != inner.header) continue;

            auto* olTerm = outerLatch->getTerminator();
            if (!olTerm || olTerm->getOpcode() != IR::Instruction::Opcode::BR) continue;
            if (olTerm->getOperand(0) != outer.header) continue;

            auto* ohTerm = outer.header->getTerminator();
            if (!ohTerm || ohTerm->getOpcode() != IR::Instruction::Opcode::COND_BR) continue;
            auto* outerExit = dynamic_cast<IR::BasicBlock*>(ohTerm->getOperand(2));
            if (!outerExit) continue;
            if (outer.body.count(outerExit)) continue;

            auto* entry = func->getEntryBlock();

            // 安全检查2：循环变量不能在循环体外被使用
            // 循环变量若在后续循环中复用，交换会破坏后续代码的正确性。
            // 允许的 BB 集合：循环体 + entry block（初始化）
            BBSet allowedBBs = outer.body;
            allowedBBs.insert(innerBody);
            allowedBBs.insert(outerLatch);
            if (entry) allowedBBs.insert(entry);
            if (isUsedOutsideBBSet(outerVar, allowedBBs)) continue;
            if (isUsedOutsideBBSet(innerVar, allowedBBs)) continue;

            // 安全检查3：循环边界必须相同（非方阵交换 A[i][j]→A[j][i] 会越界）
            // 例如 array[20][100] 中 i<20, j<100，交换后 A[j][i] 的 j 可达 99 越界
            if (!sameLoopBounds(outer.header, inner.header, outerVar, innerVar)) continue;

            // 将 innerVar 的 ALLOCA 移到 entry block，避免 entry block 的 STORE 引用未定义值
            moveAllocaToEntry(func, innerVar);

            if (entry)
                replaceStore0InBB(entry, outerVar, innerVar);

            // 交换步骤：
            // 1. 先交换循环体内所有 LOAD 引用（保持数组索引计算一致性）
            //    例如 A[i][k] 的 LOAD(i)↔LOAD(k) 同步交换
            swapLoadsInBB(innerBody, innerVar, outerVar);
            swapLoadsInBB(outerBody, innerVar, outerVar);
            swapLoadsInBB(outerLatch, innerVar, outerVar);

            // 2. 交换 LOAD 后，自增链变为：
            //    innerBody: LOAD(outerVar) → ADD 1 → STORE(innerVar)
            //    outerLatch: LOAD(innerVar) → ADD 1 → STORE(outerVar)
            //    需要修正 STORE 指针以匹配交换后的 LOAD
            replaceStorePtrInBB(innerBody, innerVar, outerVar);
            replaceStorePtrInBB(outerLatch, outerVar, innerVar);

            // 3. 交换 outerBody 中的初始化 STORE（STORE 0, innerVar → STORE 0, outerVar）
            //    注意：outerBody 中的 LOAD 已在步骤1中交换，STORE 指针需同步
            replaceStorePtrInBB(outerBody, innerVar, outerVar);

            // 4. 交换 header 中 ICMP 的 LOAD 引用
            //    使用 swapLoadsInBB 双向交换，因为 inner header 的 ICMP 有两个 LOAD 操作数
            //   （循环变量 LOAD 和 bound LOAD），需要同时交换
            swapLoadsInBB(outer.header, outerVar, innerVar);
            swapLoadsInBB(inner.header, innerVar, outerVar);

            // 交换 ICMP 常量 bound：outer loop 的 bound 从 M→N，inner loop 的 bound 从 N→M
            // 修复非方阵 array[M][N] 交换后越界的问题（如 31_many_indirections）
            swapICmpConstants(outer.header, inner.header);

            changed = true;
            break;
        }
        if (changed) break;
    }
    return changed;
}

} // namespace

bool loopInterchange(IR::Module* mod) {
    bool anyChanged = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        if (tryInterchange(func.get())) anyChanged = true;
    }
    return anyChanged;
}

} // namespace Opt
