// ================================================================
// 循环完全展开（Loop Full Unroll）
// 借鉴 Cpl3 的 LoopFullUnroll 设计
// 基于 SCEV 分析确定精确迭代次数，完全展开小循环
// 约束：迭代次数 ≤ 64，展开后指令数 ≤ 500
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// ================================================================
// 克隆指令到新 BB，映射旧值到新值
// ================================================================
IR::Instruction* cloneInstruction(IR::Instruction* inst,
    std::unordered_map<IR::Value*, IR::Value*>& valueMap) {
    using Opc = IR::Instruction::Opcode;
    auto op = inst->getOpcode();

    // 收集克隆后的操作数
    std::vector<IR::Value*> newOperands;
    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
        auto* oldOp = inst->getOperand(i);
        if (!oldOp) {
            newOperands.push_back(nullptr);
            continue;
        }
        // 常量、全局变量、函数不需要映射
        if (dynamic_cast<IR::ConstantInt*>(oldOp) ||
            dynamic_cast<IR::ConstantFloat*>(oldOp) ||
            dynamic_cast<IR::GlobalVariable*>(oldOp) ||
            dynamic_cast<IR::Function*>(oldOp)) {
            newOperands.push_back(oldOp);
        } else {
            auto it = valueMap.find(oldOp);
            newOperands.push_back(it != valueMap.end() ? it->second : oldOp);
        }
    }

    std::string newName = inst->getName();

    // 根据 opcode 创建新指令
    switch (op) {
        case Opc::ADD: case Opc::SUB: case Opc::MUL: case Opc::SDIV: case Opc::SREM:
        case Opc::AND: case Opc::OR: case Opc::XOR: case Opc::SHL: case Opc::ASHR:
        case Opc::FADD: case Opc::FSUB: case Opc::FMUL: case Opc::FDIV:
            return IR::Instruction::createBinOp(op, inst->getType(), newName,
                newOperands[0], newOperands[1]);

        case Opc::ICMP: case Opc::FCMP:
            return IR::Instruction::createCmp(op, newOperands[0], newOperands[1], newName);

        case Opc::LOAD:
            return IR::Instruction::createLoad(inst->getType(), newOperands[0], newName);

        case Opc::STORE:
            return IR::Instruction::createStore(newOperands[0], newOperands[1]);

        case Opc::GETELEMENTPTR:
            if (auto* gep = dynamic_cast<IR::Instruction*>(inst->getOperand(0))) {
                // GEP: ptr, offset
                return IR::Instruction::createGetElementPtr(
                    inst->getType(), newOperands[0], {newOperands[1]}, newName);
            }
            return nullptr;

        case Opc::ZEXT: case Opc::SEXT: case Opc::TRUNC: case Opc::SITOFP: case Opc::FPTOSI:
            return IR::Instruction::createCast(op, inst->getType(), newOperands[0], newName);

        case Opc::SELECT:
            return IR::Instruction::createSelect(newOperands[0], newOperands[1], newOperands[2], newName);

        default:
            return nullptr;
    }
}

// ================================================================
// 完全展开循环体
// ================================================================
bool fullUnrollLoop(const NaturalLoop& loop, IR::Function* func) {
    auto info = analyzeLoopInduction(loop, func);
    int64_t tc = info.tripCount;
    if (tc < 2 || tc > 64) return false;
    if (!info.var || !info.start || !info.step) return false;

    auto* startCI = dynamic_cast<IR::ConstantInt*>(info.start);
    auto* stepCI = dynamic_cast<IR::ConstantInt*>(info.step);
    if (!startCI || !stepCI) return false;

    // 计算展开后的指令数：body 指令数 * tc
    size_t bodyInstCount = 0;
    for (auto* bb : loop.body) {
        bodyInstCount += bb->getInstructions().size();
    }
    // 减去 terminator（每个 BB 一个）
    size_t loopBBs = loop.body.size();
    size_t expandedSize = (bodyInstCount - loopBBs) * tc;
    if (expandedSize > 500) return false;  // 防止代码膨胀

    // 收集循环体中的非 header 块
    std::vector<IR::BasicBlock*> bodyBBs;
    std::vector<IR::BasicBlock*> bodyBBsExceptHeader;
    for (auto* bb : loop.body) {
        bodyBBs.push_back(bb);
        if (bb != loop.header) bodyBBsExceptHeader.push_back(bb);
    }
    if (bodyBBs.size() > 2) return false;  // 仅支持 header + 1 body 的简单循环

    // 收集循环体的所有指令（非 terminator）
    std::vector<IR::Instruction*> bodyInsts;
    for (auto* bb : bodyBBsExceptHeader) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            if (op != IR::Instruction::Opcode::BR &&
                op != IR::Instruction::Opcode::COND_BR) {
                bodyInsts.push_back(inst.get());
            }
        }
    }

    // 收集 header 中的指令（除 ICMP, COND_BR, ALLOCA）
    std::vector<IR::Instruction*> headerInsts;
    for (auto& inst : loop.header->getInstructions()) {
        auto op = inst->getOpcode();
        if (op != IR::Instruction::Opcode::ICMP &&
            op != IR::Instruction::Opcode::COND_BR &&
            op != IR::Instruction::Opcode::ALLOCA &&
            op != IR::Instruction::Opcode::PHI) {
            headerInsts.push_back(inst.get());
        }
    }

    // 找到循环外的前驱
    auto preds = buildPredecessors(func);
    std::vector<IR::BasicBlock*> outsidePreds;
    for (auto* p : preds[loop.header]) {
        if (!loop.body.count(p)) outsidePreds.push_back(p);
    }
    if (outsidePreds.empty()) return false;

    // 找到循环的出口块
    IR::BasicBlock* exitBlock = nullptr;
    auto* term = loop.header->getTerminator();
    if (term && term->getOpcode() == IR::Instruction::Opcode::COND_BR) {
        auto* thenBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
        auto* elseBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
        if (thenBB && !loop.body.count(thenBB)) exitBlock = thenBB;
        else if (elseBB && !loop.body.count(elseBB)) exitBlock = elseBB;
    }
    if (!exitBlock) return false;

    // 展开：将循环体复制 tc 次到 header 之前
    // 先将 header 中的 ICMP 和 COND_BR 移除
    auto* headerBB = loop.header;

    int64_t ivVal = startCI->getValue();
    int64_t step = stepCI->getValue();

    // 在 header 之前插入展开的指令
    // 实际上我们直接在 header 中插入展开的指令（在 ICMP 之前）
    for (int64_t iter = 0; iter < tc; ++iter) {
        std::unordered_map<IR::Value*, IR::Value*> valueMap;

        // 克隆 header 指令（除了 terminator 相关）
        for (auto* inst : headerInsts) {
            auto* cloned = cloneInstruction(inst, valueMap);
            if (cloned) {
                valueMap[inst] = cloned;
                // 插入到 header 中 ICMP 之前
                auto it = headerBB->begin();
                while (it != headerBB->end() &&
                       (*it)->getOpcode() != IR::Instruction::Opcode::ICMP) {
                    ++it;
                }
                headerBB->insert(it, cloned);
            }
        }

        // 克隆 body 指令
        for (auto* inst : bodyInsts) {
            auto* cloned = cloneInstruction(inst, valueMap);
            if (cloned) {
                valueMap[inst] = cloned;
                auto it = headerBB->begin();
                while (it != headerBB->end() &&
                       (*it)->getOpcode() != IR::Instruction::Opcode::ICMP) {
                    ++it;
                }
                headerBB->insert(it, cloned);
            }
        }

        ivVal += step;
    }

    // 将 COND_BR 替换为无条件 BR 到 exitBlock
    for (auto it = headerBB->begin(); it != headerBB->end(); ++it) {
        if ((*it)->getOpcode() == IR::Instruction::Opcode::COND_BR) {
            (*it)->dropAllUses();
            auto* br = IR::Instruction::createBr(exitBlock);
            *it = std::unique_ptr<IR::Instruction>(br);
            break;
        }
    }

    // 移除 ICMP 指令
    for (auto it = headerBB->begin(); it != headerBB->end(); ) {
        if ((*it)->getOpcode() == IR::Instruction::Opcode::ICMP) {
            (*it)->dropAllUses();
            it = headerBB->erase(it);
        } else {
            ++it;
        }
    }

    return true;
}

} // namespace

bool loopFullUnroll(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        // 从最内层循环开始展开
        auto loops = getLoopsInnermostFirst(func.get());
        for (auto& loop : loops) {
            if (fullUnrollLoop(loop, func.get())) changed = true;
        }
    }
    return changed;
}

} // namespace Opt