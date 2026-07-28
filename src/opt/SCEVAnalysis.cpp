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
        // LOAD 模式：循环变量存储在 ALLOCA 中（mem2reg 未提升的情况）
        info.var = ivInst->getOperand(0);  // ALLOCA 指针

        // 辅助函数：判断值是否是从同一 ALLOCA 的 LOAD
        auto isLoadFromVar = [&](IR::Value* v) -> bool {
            auto* inst = dynamic_cast<IR::Instruction*>(v);
            return inst && inst->getOpcode() == IR::Instruction::Opcode::LOAD &&
                   inst->getNumOperands() > 0 && inst->getOperand(0) == info.var;
        };

        // 在循环体中找 STORE 到同一 ALLOCA 的 ADD/SUB 指令
        for (auto* bb : loop.body) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                    auto* storePtr = inst->getOperand(1);
                    if (storePtr == info.var) {
                        auto* storeVal = inst->getOperand(0);
                        if (auto* arithInst = dynamic_cast<IR::Instruction*>(storeVal)) {
                            if (arithInst->getOpcode() == IR::Instruction::Opcode::ADD) {
                                auto* op0 = arithInst->getOperand(0);
                                auto* op1 = arithInst->getOperand(1);
                                // ★ 修复：循环体内的 LOAD 可能与 header 中的 LOAD
                                //   是不同指令（但来自同一 ALLOCA），用 isLoadFromVar 判断
                                if (isLoadFromVar(op0)) info.step = op1;
                                else if (isLoadFromVar(op1)) info.step = op0;
                            } else if (arithInst->getOpcode() == IR::Instruction::Opcode::SUB) {
                                auto* op0 = arithInst->getOperand(0);
                                auto* op1 = arithInst->getOperand(1);
                                if (isLoadFromVar(op0)) {
                                    if (auto* ci = dynamic_cast<IR::ConstantInt*>(op1)) {
                                        info.step = IR::ConstantInt::get(
                                            IR::IntegerType::I32, -ci->getValue());
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ★ 修复：在循环前置块（preheader）中找初始 STORE，而非仅 entry block
        //   循环变量如 `j = 0` 通常在循环外但非 entry 的 BB 中初始化
        auto allPreds = buildPredecessors(func);
        for (auto* pred : allPreds[header]) {
            if (loop.body.count(pred)) continue;  // 跳过 back-edge 前驱
            // 这是 preheader，查找最后一个 STORE 到 info.var
            IR::Value* lastStore = nullptr;
            for (auto& inst : pred->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE &&
                    inst->getNumOperands() > 1 &&
                    inst->getOperand(1) == info.var) {
                    lastStore = inst->getOperand(0);
                }
            }
            if (lastStore) info.start = lastStore;
        }
        // 回退：也检查 entry block（以防 preheader 未找到）
        if (!info.start) {
            for (auto& inst : func->getEntryBlock()->getInstructions()) {
                if (inst->getOpcode() == IR::Instruction::Opcode::STORE &&
                    inst->getNumOperands() > 1 &&
                    inst->getOperand(1) == info.var) {
                    info.start = inst->getOperand(0);
                }
            }
        }
    } else if (ivInst->getOpcode() == IR::Instruction::Opcode::ADD
               || ivInst->getOpcode() == IR::Instruction::Opcode::SUB) {
        // ADD/SUB 模式：循环变量是 ADD/SUB 的结果
        // 追踪到 LOAD
        bool isSub = (ivInst->getOpcode() == IR::Instruction::Opcode::SUB);
        auto* op0 = ivInst->getOperand(0);
        auto* op1 = ivInst->getOperand(1);
        IR::Value* loadSrc = nullptr;
        if (auto* op0Inst = dynamic_cast<IR::Instruction*>(op0)) {
            if (op0Inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                info.var = op0Inst->getOperand(0);
                loadSrc = op0;
                if (isSub) {
                    // sub(load, C) → step = -C
                    if (auto* ci = dynamic_cast<IR::ConstantInt*>(op1)) {
                        info.step = IR::ConstantInt::get(
                            IR::IntegerType::I32, -ci->getValue());
                    }
                } else {
                    info.step = op1;
                }
            }
        }
        if (!loadSrc && !isSub && dynamic_cast<IR::Instruction*>(op1)) {
            // ADD 模式：add(C, load) → step = C
            // SUB 模式：sub(C, load) 不合法（不是归纳变量）
            auto* op1Inst = dynamic_cast<IR::Instruction*>(op1);
            if (op1Inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                info.var = op1Inst->getOperand(0);
                loadSrc = op1;
                info.step = op0;
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
    } else if (ivInst->getOpcode() == IR::Instruction::Opcode::PHI) {
        // PHI 模式：循环变量是 PHI 节点（Mem2Reg 后的形式）
        auto* phi = ivInst;
        if (phi->getParent() != header) return info;  // PHI 必须在 header 中

        info.var = phi;

        // 遍历 PHI 的 incoming values
        for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
            auto* val = phi->getOperand(i);
            auto* predBB = dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
            if (!predBB) continue;

            bool isOutsidePred = !loop.body.count(predBB);

            if (isOutsidePred) {
                // 初始值
                info.start = val;
            } else {
                // back-edge value：应该是 ADD(PHI, step) 或 SUB(PHI, step) 形式
                if (auto* arithInst = dynamic_cast<IR::Instruction*>(val)) {
                    if (arithInst->getOpcode() == IR::Instruction::Opcode::ADD) {
                        auto* op0 = arithInst->getOperand(0);
                        auto* op1 = arithInst->getOperand(1);
                        if (op0 == phi) info.step = op1;
                        else if (op1 == phi) info.step = op0;
                    } else if (arithInst->getOpcode() == IR::Instruction::Opcode::SUB) {
                        // SUB 模式：i = i - C → 有效步长 = -C
                        // 仅支持 sub(phi, C)，不支持 sub(C, phi)
                        auto* op0 = arithInst->getOperand(0);
                        auto* op1 = arithInst->getOperand(1);
                        if (op0 == phi) {
                            if (auto* ci = dynamic_cast<IR::ConstantInt*>(op1)) {
                                info.step = IR::ConstantInt::get(
                                    IR::IntegerType::I32, -ci->getValue());
                            }
                        }
                    }
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
            } else if (info.cmpKind == "ne") {
                // while (x != end) { x += step; } 模式
                // 保守识别:仅支持递减到 0 (end==0, step==-1)
                // 用于 CRC _and/_xor 的 32 次循环
                if (e == 0 && d == -1 && s > 0 && s <= 64) {
                    info.tripCount = s;
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