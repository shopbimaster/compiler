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

            bool hasIntermediate = false;
            for (size_t mi = 0; mi < loops.size(); ++mi) {
                if (mi == oi || mi == ii) continue;
                auto& mid = loops[mi];
                if (outer.body.count(mid.header) && mid.body.count(inner.header)) {
                    hasIntermediate = true;
                    break;
                }
            }
            if (hasIntermediate) continue;

            auto* outerVar = extractIndVar(outer.header);
            auto* innerVar = extractIndVar(inner.header);
            if (!outerVar || !innerVar || outerVar == innerVar) continue;

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

            // 4. 交换 ICMP 中的 LOAD 引用（已在步骤1中通过 swapLoadsInBB 处理）
            //    但 header 中的 ICMP 未在步骤1中处理，需单独处理
            replaceCmpLoadInBB(outer.header, outerVar, innerVar);
            replaceCmpLoadInBB(inner.header, innerVar, outerVar);

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