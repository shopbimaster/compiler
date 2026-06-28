// ================================================================
// 标量演化分析（Scalar Evolution Analysis）
// 借鉴 Cpl3 的 SCEVAnalysis 设计
// 为循环归纳变量构建 CR（Chain of Recurrence）递推链
// 支持：{start, +, step} 形式的加法递推
// 用于：LoopStrengthReduce、LoopFullUnroll
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cassert>

namespace Opt {
namespace {

// ================================================================
// 查找循环的归纳变量
// 模式：循环头处的 LOAD → ADD → ICMP
// 返回：(induction_var, start_value, step, end_value, trip_count)
// ================================================================
InductionInfo analyzeInduction(const NaturalLoop& loop, IR::Function* func) {
    InductionInfo info;
    auto* header = loop.header;

    // 在 header 中查找 ICMP 指令（循环退出条件）
    IR::Instruction* cmpInst = nullptr;
    for (auto& inst : header->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::ICMP) {
            cmpInst = inst.get();
            break;
        }
    }
    if (!cmpInst) return info;

    info.cmpKind = cmpInst->getName();
    auto* lhs = cmpInst->getOperand(0);
    auto* rhs = cmpInst->getOperand(1);

    // 确定哪个操作数是循环变量，哪个是上界
    IR::Value* ivVal = nullptr;
    IR::Value* boundVal = nullptr;

    // 检查 lhs 是否在循环中被修改
    if (auto* lhsInst = dynamic_cast<IR::Instruction*>(lhs)) {
        auto* lhsBB = lhsInst->getParent();
        if (lhsBB && loop.body.count(lhsBB)) {
            ivVal = lhs;
            boundVal = rhs;
        }
    }
    if (!ivVal && dynamic_cast<IR::Instruction*>(rhs)) {
        auto* rhsInst = dynamic_cast<IR::Instruction*>(rhs);
        auto* rhsBB = rhsInst->getParent();
        if (rhsBB && loop.body.count(rhsBB)) {
            ivVal = rhs;
            boundVal = lhs;
            // 翻转比较类型
            if (info.cmpKind == "slt") info.cmpKind = "sgt";
            else if (info.cmpKind == "sle") info.cmpKind = "sge";
            else if (info.cmpKind == "sgt") info.cmpKind = "slt";
            else if (info.cmpKind == "sge") info.cmpKind = "sle";
        }
    }
    if (!ivVal) return info;

    info.end = boundVal;

    // 追踪 ivVal 的定义链：找到 ADD 指令
    IR::Instruction* ivInst = dynamic_cast<IR::Instruction*>(ivVal);
    if (!ivInst) return info;

    // ivVal 应该是 LOAD 或 ADD 的结果
    if (ivInst->getOpcode() == IR::Instruction::Opcode::LOAD) {
        // LOAD 模式：循环变量存储在 ALLOCA 中
        info.var = ivInst->getOperand(0);  // ALLOCA 指针

        // 在循环体中找 STORE 到同一 ALLOCA 的 ADD 指令
        for (auto* bb : loop.body) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                    auto* storePtr = inst->getOperand(1);
                    if (storePtr == info.var) {
                        auto* storeVal = inst->getOperand(0);
                        if (auto* addInst = dynamic_cast<IR::Instruction*>(storeVal)) {
                            if (addInst->getOpcode() == IR::Instruction::Opcode::ADD) {
                                // 找到步长：ADD 的另一个操作数
                                auto* op0 = addInst->getOperand(0);
                                auto* op1 = addInst->getOperand(1);
                                if (op0 == ivVal) info.step = op1;
                                else if (op1 == ivVal) info.step = op0;
                            }
                        }
                    }
                }
                // 也在 entry block 中找初始 STORE
                if (bb == func->getEntryBlock()) {
                    if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                        auto* storePtr = inst->getOperand(1);
                        if (storePtr == info.var) {
                            info.start = inst->getOperand(0);
                        }
                    }
                }
            }
        }
    } else if (ivInst->getOpcode() == IR::Instruction::Opcode::ADD) {
        // ADD 模式：循环变量是 ADD 的结果
        // 追踪到 LOAD
        auto* addOp0 = ivInst->getOperand(0);
        auto* addOp1 = ivInst->getOperand(1);
        IR::Value* loadSrc = nullptr;
        if (auto* op0Inst = dynamic_cast<IR::Instruction*>(addOp0)) {
            if (op0Inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                info.var = op0Inst->getOperand(0);
                loadSrc = addOp0;
                info.step = addOp1;
            }
        }
        if (!loadSrc && dynamic_cast<IR::Instruction*>(addOp1)) {
            auto* op1Inst = dynamic_cast<IR::Instruction*>(addOp1);
            if (op1Inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                info.var = op1Inst->getOperand(0);
                loadSrc = addOp1;
                info.step = addOp0;
            }
        }
        if (!info.var) return info;

        // 找初始值
        for (auto& inst : func->getEntryBlock()->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                if (inst->getOperand(1) == info.var) {
                    info.start = inst->getOperand(0);
                }
            }
        }
    }

    // 计算迭代次数
    if (info.start && info.end && info.step) {
        auto* startCI = dynamic_cast<IR::ConstantInt*>(info.start);
        auto* endCI = dynamic_cast<IR::ConstantInt*>(info.end);
        auto* stepCI = dynamic_cast<IR::ConstantInt*>(info.step);

        if (startCI && endCI && stepCI) {
            int64_t s = startCI->getValue();
            int64_t e = endCI->getValue();
            int64_t d = stepCI->getValue();
            if (d == 0) return info;

            if (info.cmpKind == "slt" || info.cmpKind == "sgt") {
                // for (i = s; i < e; i += d) → tripCount = max(0, ceil((e-s)/d))
                int64_t diff = e - s;
                if (d > 0 && diff > 0) {
                    info.tripCount = (diff + d - 1) / d;
                } else if (d < 0 && diff < 0) {
                    info.tripCount = ((-diff) + (-d) - 1) / (-d);
                }
            } else if (info.cmpKind == "sle" || info.cmpKind == "sge") {
                // for (i = s; i <= e; i += d) → tripCount = max(0, floor((e-s)/d) + 1)
                int64_t diff = e - s;
                if (d > 0 && diff >= 0) {
                    info.tripCount = diff / d + 1;
                } else if (d < 0 && diff <= 0) {
                    info.tripCount = (-diff) / (-d) + 1;
                }
            }
        }
    }

    return info;
}

} // namespace

// ================================================================
// 导出：分析循环的归纳变量
// ================================================================
InductionInfo analyzeLoopInduction(const NaturalLoop& loop, IR::Function* func) {
    return analyzeInduction(loop, func);
}

} // namespace Opt