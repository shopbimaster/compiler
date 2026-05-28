// ================================================================
// O2: 函数内联 —— 将小函数体复制到调用点，消除 call 开销
// 策略：内联 ≤2 基本块、非递归、指令数 < 20 的叶子函数
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>

namespace Opt {
namespace {

const unsigned MAX_INLINE_INSTS = 20;

bool isLeafCall(IR::Function* func) {
    if (func->isExternal()) return false;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                auto* callee = inst->getOperand(0);
                if (callee && callee->getName() == func->getName()) continue;
                return false; // calls another function → not a leaf
            }
        }
    }
    return true;
}

unsigned countInstructions(IR::Function* func) {
    unsigned n = 0;
    for (auto& bb : func->getBlocks()) {
        n += static_cast<unsigned>(bb->getInstructions().size());
    }
    return n;
}

bool isInlineCandidate(IR::Function* func) {
    if (func->isExternal()) return false;
    if (func->getBlocks().size() > 2) return false;   // 不超过 2 个 BB（entry + body）
    if (countInstructions(func) > MAX_INLINE_INSTS) return false;
    if (!isLeafCall(func)) return false;
    return true;
}

// 从 RETURN 指令提取返回值
IR::Value* getReturnValue(IR::Instruction* retInst) {
    if (retInst->getNumOperands() > 0) return retInst->getOperand(0);
    return nullptr;
}

// 克隆单条指令，将 callee 的操作数映射到 caller 上下文中
IR::Instruction* cloneInstruction(
    IR::Instruction* src,
    std::unordered_map<IR::Value*, IR::Value*>& valueMap,
    IR::Function* caller) {
    auto op = src->getOpcode();
    using Opc = IR::Instruction::Opcode;

    auto lookup = [&](IR::Value* v) -> IR::Value* {
        if (!v) return nullptr;
        auto it = valueMap.find(v);
        if (it != valueMap.end()) return it->second;
        return v; // constant/global stays the same
    };

    IR::Instruction* cloned = nullptr;

    if (op == Opc::ALLOCA) {
        IR::Type* ty = src->getType();
        auto* ptrTy = dynamic_cast<IR::PointerType*>(ty);
        IR::Type* elemTy = ptrTy ? ptrTy->getPointeeType() : ty;
        cloned = IR::Instruction::createAlloca(elemTy, src->getName() + ".i");
    } else if (op == Opc::STORE && src->getNumOperands() >= 2) {
        cloned = IR::Instruction::createStore(
            lookup(src->getOperand(0)), lookup(src->getOperand(1)));
    } else if (op == Opc::LOAD) {
        cloned = IR::Instruction::createLoad(
            src->getType(), lookup(src->getOperand(0)), src->getName() + ".i");
    } else if (op == Opc::CALL) {
        auto* callee = src->getOperand(0);
        std::vector<IR::Value*> args;
        for (unsigned i = 1; i < src->getNumOperands(); ++i) {
            args.push_back(lookup(src->getOperand(i)));
        }
        auto* ft = dynamic_cast<IR::FunctionType*>(callee->getType());
        cloned = IR::Instruction::createCall(ft, callee, args, src->getName() + ".i");
    } else if (op == Opc::RET) {
        return nullptr; // handled separately
    } else if (op == Opc::BR || op == Opc::COND_BR) {
        return nullptr; // should not appear in single-BB function mid-body
    } else if (op == Opc::PHI) {
        return nullptr;
    } else if (op == Opc::GETELEMENTPTR) {
        std::vector<IR::Value*> indices;
        for (unsigned i = 1; i < src->getNumOperands(); ++i)
            indices.push_back(lookup(src->getOperand(i)));
        auto* ptrTy = dynamic_cast<IR::PointerType*>(src->getOperand(0)->getType());
        IR::Type* pointee = ptrTy ? ptrTy->getPointeeType() : IR::IntegerType::I32;
        cloned = IR::Instruction::createGetElementPtr(
            pointee, lookup(src->getOperand(0)), indices, src->getName() + ".i");
    } else if (op == Opc::ICMP || op == Opc::FCMP) {
        cloned = IR::Instruction::createCmp(
            op, lookup(src->getOperand(0)), lookup(src->getOperand(1)),
            src->getName());
    } else if (op == Opc::ZEXT || op == Opc::SEXT || op == Opc::TRUNC ||
               op == Opc::SITOFP || op == Opc::FPTOSI) {
        cloned = IR::Instruction::createCast(
            op, src->getType(), lookup(src->getOperand(0)), src->getName() + ".i");
    } else {
        // 通用二元运算
        cloned = IR::Instruction::createBinOp(
            op, src->getType(), src->getName() + ".i",
            lookup(src->getOperand(0)),
            src->getNumOperands() >= 2 ? lookup(src->getOperand(1)) : nullptr);
    }

    if (cloned) {
        valueMap[src] = cloned;
    }
    return cloned;
}

// 对单个调用尝试内联
bool tryInlineCall(IR::Instruction* callInst, IR::Function* callee) {
    auto* func = callInst->getParent()->getParent();

    // 建立参数 -> 实参的映射
    std::unordered_map<IR::Value*, IR::Value*> valueMap;
    for (unsigned i = 0; i < callee->getNumArgs(); ++i) {
        valueMap[callee->getArg(i)] = callInst->getOperand(i + 1);
    }

    // 克隆 callee 的所有非终止指令（遍历所有基本块）
    std::vector<IR::Instruction*> clonedInsts;
    IR::Instruction* retInst = nullptr;
    for (auto& calleeBB : callee->getBlocks()) {
        for (auto& inst : calleeBB->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::RET) {
                retInst = inst.get();
                continue;
            }
            if (inst->getOpcode() == IR::Instruction::Opcode::BR ||
                inst->getOpcode() == IR::Instruction::Opcode::COND_BR) {
                continue;
            }
            auto* cloned = cloneInstruction(inst.get(), valueMap, func);
            if (cloned) clonedInsts.push_back(cloned);
        }
    }

    // 处理返回值
    if (retInst) {
        IR::Value* retVal = getReturnValue(retInst);
        if (retVal) {
            IR::Value* mapped = valueMap.count(retVal) ? valueMap[retVal] : retVal;
            callInst->replaceAllUsesWith(mapped);
        }
    }

    // 将克隆指令插入到 call 之前
    auto* bb = callInst->getParent();
    auto callIt = bb->begin();
    for (; callIt != bb->end(); ++callIt) {
        if (callIt->get() == callInst) break;
    }
    for (auto* cloned : clonedInsts) {
        callIt = bb->insert(callIt, cloned);
        ++callIt;
    }

    // 删除 call
    callInst->dropAllUses();
    for (auto it = bb->begin(); it != bb->end(); ++it) {
        if (it->get() == callInst) {
            bb->erase(it);
            break;
        }
    }

    return true;
}

} // namespace

void inlineExpansion(IR::Module* mod) {
    // 识别可内联的候选函数
    std::unordered_set<IR::Function*> candidates;
    for (auto& func : mod->getFunctions()) {
        if (isInlineCandidate(func.get())) {
            candidates.insert(func.get());
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (func->isExternal()) continue;
            for (auto& bb : func->getBlocks()) {
                for (auto it = bb->begin(); it != bb->end(); ) {
                    auto* inst = it->get();
                    if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                        auto* calleeVal = inst->getOperand(0);
                        auto* calleeFunc = dynamic_cast<IR::Function*>(calleeVal);
                        if (calleeFunc && candidates.count(calleeFunc)) {
                            tryInlineCall(inst, calleeFunc);
                            changed = true;
                            it = bb->begin(); // restart iteration
                            break;
                        }
                    }
                    ++it;
                }
                if (changed) break;
            }
            if (changed) break;
        }
    }
}

} // namespace Opt