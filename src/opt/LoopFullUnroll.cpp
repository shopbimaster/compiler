// ================================================================
// 循环完全展开（Loop Full Unroll）
// 借鉴 Cpl3 的 LoopFullUnroll 设计
// 基于 SCEV 分析确定精确迭代次数，完全展开小循环
// 约束：迭代次数 ≤ 64，展开后指令数 ≤ 500
// 支持单 BB 和多 BB 循环体展开
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

    // ★ 给克隆指令添加唯一后缀，避免 SSA 名称冲突
    //   LoopFullUnroll 克隆指令时保留原始名称会导致多个指令同名（如 25 个 %t107），
    //   违反 SSA 不变量。虽然寄存器分配器使用指针相等性，但某些 pass 可能依赖
    //   名称唯一性。添加 .u 后缀确保唯一性。
    std::string newName = inst->getName();
    // 添加基于 valueMap 大小的后缀（在同一迭代内唯一）
    if (!newName.empty() && newName[0] == '%') {
        newName += ".c" + std::to_string(valueMap.size());
    }

    // 根据 opcode 创建新指令
    switch (op) {
        case Opc::ADD: case Opc::SUB: case Opc::MUL: case Opc::SDIV: case Opc::SREM:
        case Opc::AND: case Opc::OR: case Opc::XOR: case Opc::SHL: case Opc::ASHR:
        case Opc::SMULH:
        case Opc::FADD: case Opc::FSUB: case Opc::FMUL: case Opc::FDIV:
            return IR::Instruction::createBinOp(op, inst->getType(), newName,
                newOperands[0], newOperands[1]);

        case Opc::WIDE_SMOD_MUL:
            return IR::Instruction::createTernaryOp(
                op, inst->getType(), newName,
                newOperands[0], newOperands[1], newOperands[2]);

        case Opc::ICMP: case Opc::FCMP:
            return IR::Instruction::createCmp(op, newOperands[0], newOperands[1], newName);

        case Opc::LOAD:
            return IR::Instruction::createLoad(inst->getType(), newOperands[0], newName);

        case Opc::STORE:
            return IR::Instruction::createStore(newOperands[0], newOperands[1]);

        case Opc::GETELEMENTPTR: {
            // GEP: ptr, idx0, idx1, ...
            // inst->getType() = PointerType::get(resultTy)
            // 提取 resultTy 作为 pointee（重新计算会得到相同结果类型）
            auto* ptrTy = dynamic_cast<IR::PointerType*>(inst->getType());
            if (!ptrTy) return nullptr;
            IR::Type* pointee = ptrTy->getPointeeType();
            // 收集所有索引（从 newOperands[1] 开始）
            std::vector<IR::Value*> indices(newOperands.begin() + 1, newOperands.end());
            return IR::Instruction::createGetElementPtr(pointee, newOperands[0], indices, newName);
        }

        case Opc::ZEXT: case Opc::SEXT: case Opc::TRUNC: case Opc::SITOFP: case Opc::FPTOSI:
            return IR::Instruction::createCast(op, inst->getType(), newOperands[0], newName);

        case Opc::SELECT:
            return IR::Instruction::createSelect(newOperands[0], newOperands[1], newOperands[2], newName);

        case Opc::CALL: {
            // CALL: callee, arg0, arg1, ...
            // 从 callee (Function) 获取 FunctionType
            auto* callee = newOperands[0];
            if (auto* fn = dynamic_cast<IR::Function*>(callee)) {
                std::vector<IR::Value*> args(newOperands.begin() + 1, newOperands.end());
                return IR::Instruction::createCall(fn->getFunctionType(), fn, args, newName);
            }
            // 间接调用不支持克隆
            return nullptr;
        }

        case Opc::ALLOCA: {
            // ALLOCA: type is PointerType, need to extract pointee type
            auto* ptrTy = dynamic_cast<IR::PointerType*>(inst->getType());
            if (ptrTy) {
                return IR::Instruction::createAlloca(ptrTy->getPointeeType(), newName);
            }
            return nullptr;
        }

        case Opc::RET: {
            // RET: 0 operands (void) or 1 operand (value)
            if (newOperands.empty() || !newOperands[0]) {
                return IR::Instruction::createRet(nullptr);
            }
            return IR::Instruction::createRet(newOperands[0]);
        }

        default:
            return nullptr;
    }
}

// ================================================================
// 完全展开单 BB 循环体（header + 1 body BB）
// 将 body 指令扁平化到 header 中
// 支持 PHI 模式（Mem2Reg 后）：用每次迭代的值替换 PHI 使用
// ================================================================
bool fullUnrollSingleBB(const NaturalLoop& loop, IR::Function* func,
                        const std::vector<IR::BasicBlock*>& bodyBBsExceptHeader) {
    using Opc = IR::Instruction::Opcode;

    auto info = analyzeLoopInduction(loop, func);
    int64_t tc = info.tripCount;
    auto* startCI = dynamic_cast<IR::ConstantInt*>(info.start);
    auto* stepCI = dynamic_cast<IR::ConstantInt*>(info.step);
    (void)startCI; (void)stepCI; // 约束已在 fullUnrollLoop 中检查

    // 收集循环体的所有指令（非 terminator）
    std::vector<IR::Instruction*> bodyInsts;
    for (auto* bb : bodyBBsExceptHeader) {
        for (auto& inst : bb->getInstructions()) {
            auto op = inst->getOpcode();
            if (op != Opc::BR && op != Opc::COND_BR) {
                bodyInsts.push_back(inst.get());
            }
        }
    }

    // 收集 header 中的指令（除 ICMP, COND_BR, ALLOCA, PHI）
    std::vector<IR::Instruction*> headerInsts;
    for (auto& inst : loop.header->getInstructions()) {
        auto op = inst->getOpcode();
        if (op != Opc::ICMP && op != Opc::COND_BR &&
            op != Opc::ALLOCA && op != Opc::PHI) {
            headerInsts.push_back(inst.get());
        }
    }

    // 收集 header 中的 PHI 指令及其初始值和回边值
    std::vector<IR::Instruction*> headerPhis;
    std::unordered_map<IR::Instruction*, IR::Value*> phiInitValues;
    std::unordered_map<IR::Instruction*, IR::Value*> phiBackEdgeValues;
    bool hasPhi = false;
    for (auto& inst : loop.header->getInstructions()) {
        if (inst->getOpcode() == Opc::PHI) {
            hasPhi = true;
            headerPhis.push_back(inst.get());
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                if (predBB && !loop.body.count(predBB)) {
                    phiInitValues[inst.get()] = inst->getOperand(i);
                } else if (predBB && loop.body.count(predBB)) {
                    phiBackEdgeValues[inst.get()] = inst->getOperand(i);
                }
            }
        }
    }

    // 如果有 PHI，检查所有 PHI 都有初始值和回边值
    if (hasPhi) {
        for (auto* phi : headerPhis) {
            if (!phiInitValues.count(phi) || !phiBackEdgeValues.count(phi)) {
                return false; // PHI 不完整，放弃
            }
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
    if (term && term->getOpcode() == Opc::COND_BR) {
        auto* thenBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
        auto* elseBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
        if (thenBB && !loop.body.count(thenBB)) exitBlock = thenBB;
        else if (elseBB && !loop.body.count(elseBB)) exitBlock = elseBB;
    }
    if (!exitBlock) return false;

    auto* headerBB = loop.header;

    // 查找 ICMP 的位置（所有克隆指令插入此处之前）
    auto findIcmpIter = [&]() {
        auto it = headerBB->begin();
        while (it != headerBB->end() && (*it)->getOpcode() != Opc::ICMP) {
            ++it;
        }
        return it;
    };

    // 展开：将循环体复制 tc 次到 header 中（在 ICMP 之前）
    // 跨迭代维护 valueMap，使前一次迭代的回边值映射到本次迭代的 PHI 值
    std::unordered_map<IR::Value*, IR::Value*> crossIterMap;
    // 迭代 0：PHI 的值 = 初始值
    if (hasPhi) {
        for (auto* phi : headerPhis) {
            crossIterMap[phi] = phiInitValues[phi];
        }
    }

    // 记录最后一次迭代的回边值（用于更新 exit block 中对 PHI 的引用）
    std::unordered_map<IR::Value*, IR::Value*> finalPhiValues;

    for (int64_t iter = 0; iter < tc; ++iter) {
        std::unordered_map<IR::Value*, IR::Value*> valueMap = crossIterMap;

        // 克隆 header 指令（除了 terminator 相关）
        for (auto* inst : headerInsts) {
            auto* cloned = cloneInstruction(inst, valueMap);
            if (cloned) {
                valueMap[inst] = cloned;
                headerBB->insert(findIcmpIter(), cloned);
            }
        }

        // 克隆 body 指令
        for (auto* inst : bodyInsts) {
            auto* cloned = cloneInstruction(inst, valueMap);
            if (cloned) {
                valueMap[inst] = cloned;
                headerBB->insert(findIcmpIter(), cloned);
            }
        }

        // 为下一次迭代准备 crossIterMap
        if (hasPhi) {
            crossIterMap.clear();
            for (auto* phi : headerPhis) {
                auto* bev = phiBackEdgeValues[phi];
                auto it = valueMap.find(bev);
                if (it != valueMap.end()) {
                    crossIterMap[phi] = it->second;
                } else {
                    // 回边值是常量或外部值，直接使用
                    crossIterMap[phi] = bev;
                }
            }
            // 最后一次迭代：记录 finalPhiValues
            if (iter + 1 == tc) {
                finalPhiValues = crossIterMap;
            }
        }
    }

    // 如果有 PHI，替换 header PHI 的所有外部使用为最后一次迭代的值
    if (hasPhi) {
        for (auto* phi : headerPhis) {
            auto it = finalPhiValues.find(phi);
            if (it != finalPhiValues.end()) {
                phi->replaceAllUsesWith(it->second);
            }
        }
    }

    // 将 COND_BR 替换为无条件 BR 到 exitBlock
    for (auto it = headerBB->begin(); it != headerBB->end(); ++it) {
        if ((*it)->getOpcode() == Opc::COND_BR) {
            (*it)->dropAllUses();
            auto* br = IR::Instruction::createBr(exitBlock);
            *it = std::unique_ptr<IR::Instruction>(br);
            break;
        }
    }

    // 移除 ICMP 指令
    for (auto it = headerBB->begin(); it != headerBB->end(); ) {
        if ((*it)->getOpcode() == Opc::ICMP) {
            (*it)->dropAllUses();
            it = headerBB->erase(it);
        } else {
            ++it;
        }
    }

    // 移除 header PHI 指令（如果有）
    if (hasPhi) {
        for (auto it = headerBB->begin(); it != headerBB->end(); ) {
            if ((*it)->getOpcode() == Opc::PHI) {
                (*it)->dropAllUses();
                it = headerBB->erase(it);
            } else {
                ++it;
            }
        }
    }

    // 删除原始 body BB（指令已被克隆到 header）
    // 先清理 body BB 中指令的 uses（将外部引用设为 null）
    for (auto* bb : bodyBBsExceptHeader) {
        for (auto& inst : bb->getInstructions()) {
            inst->replaceAllUsesWith(nullptr);
        }
    }
    // 清理 PHI 中对 body BB 的引用（防止悬空指针）
    for (auto* bb : bodyBBsExceptHeader) {
        for (auto& fn : func->getBlocks()) {
            for (auto& inst : fn->getInstructions()) {
                if (inst->getOpcode() != Opc::PHI) continue;
                for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                    if (inst->getOperand(i + 1) == bb) {
                        inst->setOperand(i, nullptr);
                        inst->setOperand(i + 1, nullptr);
                    }
                }
            }
        }
    }
    // 从 function 中删除 body BB
    for (auto* bb : bodyBBsExceptHeader) {
        auto& blocks = func->getBlocks();
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == bb) {
                blocks.erase(it);
                break;
            }
        }
    }

    return true;
}

// ================================================================
// 完全展开多 BB 循环体
// 为每个迭代克隆所有 body BB，正确连接分支和 PHI
// ================================================================
bool fullUnrollMultiBB(const NaturalLoop& loop, IR::Function* func,
                       const std::vector<IR::BasicBlock*>& bodyBBs) {
    using Opc = IR::Instruction::Opcode;

    auto info = analyzeLoopInduction(loop, func);
    int64_t tc = info.tripCount;
    (void)info.start; (void)info.step; // 约束已在 fullUnrollLoop 中检查

    // 找到 body 入口和 exit block
    auto* headerTerm = loop.header->getTerminator();
    if (!headerTerm || headerTerm->getOpcode() != Opc::COND_BR) return false;

    auto* bodyEntry = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(1));
    auto* exitBlock = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(2));
    if (!bodyEntry || !exitBlock) return false;
    // bodyEntry 可能是 true 或 false target
    if (!loop.body.count(bodyEntry)) {
        bodyEntry = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(2));
        exitBlock = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(1));
        if (!bodyEntry || !exitBlock) return false;
        if (!loop.body.count(bodyEntry)) return false;
    }
    if (loop.body.count(exitBlock)) return false;

    // 找到 latch（回边到 header 的 BB），不支持多个 latch
    IR::BasicBlock* latch = nullptr;
    for (auto* bb : bodyBBs) {
        auto* bbTerm = bb->getTerminator();
        if (bbTerm && bbTerm->getOpcode() == Opc::BR) {
            if (bbTerm->getOperand(0) == loop.header) {
                if (latch) return false; // 多个 latch，不支持
                latch = bb;
            }
        }
    }
    if (!latch) return false;

    // header 只能有 PHI、ICMP、COND_BR（不能有其他计算指令）
    for (auto& inst : loop.header->getInstructions()) {
        auto op = inst->getOpcode();
        if (op != Opc::PHI && op != Opc::ICMP && op != Opc::COND_BR &&
            op != Opc::ALLOCA) {
            return false;
        }
    }

    // 收集 header 中的 PHI 指令
    std::vector<IR::Instruction*> headerPhis;
    for (auto& inst : loop.header->getInstructions()) {
        if (inst->getOpcode() == Opc::PHI) {
            headerPhis.push_back(inst.get());
        }
    }

    // 为每个 header PHI 找到初始值（来自 outsidePred）和回边值（来自 body）
    std::unordered_map<IR::Instruction*, IR::Value*> initValues;
    std::unordered_map<IR::Instruction*, IR::Value*> backEdgeValues;
    for (auto* phi : headerPhis) {
        for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
            if (predBB && !loop.body.count(predBB)) {
                initValues[phi] = phi->getOperand(i);
            } else if (predBB && loop.body.count(predBB)) {
                backEdgeValues[phi] = phi->getOperand(i);
            }
        }
    }

    // 检查所有 header PHI 都有 init 和 back-edge 值
    for (auto* phi : headerPhis) {
        if (!initValues.count(phi) || !backEdgeValues.count(phi)) return false;
    }

    // ================================================================
    // 前期验证：确保删除 body BB 后不会产生悬空引用
    // ================================================================
    std::unordered_set<IR::BasicBlock*> bodyBBSet(bodyBBs.begin(), bodyBBs.end());
    // header 不在 bodyBBSet 中（不会被删除，外部 BB 可以引用它）

    // 检查 1：body BB 的 terminator 只能跳转到 bodyBBs、header 或 exitBlock
    for (auto* bb : bodyBBs) {
        auto* term = bb->getTerminator();
        if (!term) return false;
        if (term->getOpcode() == Opc::BR) {
            auto* target = dynamic_cast<IR::BasicBlock*>(term->getOperand(0));
            if (!target) return false;
            if (target != loop.header && target != exitBlock && !bodyBBSet.count(target)) {
                return false; // 跳转到未知 BB
            }
        } else if (term->getOpcode() == Opc::COND_BR) {
            for (unsigned i = 1; i <= 2; ++i) {
                auto* target = dynamic_cast<IR::BasicBlock*>(term->getOperand(i));
                if (!target) return false;
                if (target != loop.header && target != exitBlock && !bodyBBSet.count(target)) {
                    return false;
                }
            }
        } else if (term->getOpcode() == Opc::RET) {
            // RET 在循环体内是允许的
        } else {
            return false; // 未知 terminator
        }
    }

    // 检查 2：没有外部 BB 引用任何 body BB（header 除外，其 COND_BR 会在 Phase 4 替换）
    for (auto& fn : func->getBlocks()) {
        if (fn.get() == loop.header) continue; // 跳过 header
        if (bodyBBSet.count(fn.get())) continue; // 跳过 body
        auto* term = fn->getTerminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->getNumOperands(); ++i) {
            auto* op = term->getOperand(i);
            if (!op) continue;
            // 检查是否引用了 body BB（非 header）
            for (auto* bb : bodyBBs) {
                if (op == bb) {
                    return false; // 外部 BB 引用 body BB
                }
            }
        }
        // 检查 PHI 中的前驱引用
        for (auto& inst : fn->getInstructions()) {
            if (inst->getOpcode() != Opc::PHI) continue;
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                auto* predBB = inst->getOperand(i + 1);
                if (!predBB) continue;
                for (auto* bb : bodyBBs) {
                    if (predBB == bb) {
                        return false; // 外部 PHI 引用 body BB
                    }
                }
            }
        }
    }

    // ================================================================
    // 阶段 1：为所有迭代创建所有 BB（空），建立 bbMap
    // ================================================================
    std::vector<std::unordered_map<IR::BasicBlock*, IR::BasicBlock*>> bbMaps(tc);
    for (int64_t iter = 0; iter < tc; ++iter) {
        for (auto* bb : bodyBBs) {
            std::string newName = bb->getName() + ".u" + std::to_string(iter);
            auto* newBB = func->createBlock(newName);
            bbMaps[iter][bb] = newBB;
        }
    }

    // ================================================================
    // 阶段 2：克隆指令和 terminator
    // ================================================================
    std::vector<std::unordered_map<IR::Value*, IR::Value*>> valueMaps(tc);

    // 初始化迭代 0 的 header PHI 值映射
    for (auto* phi : headerPhis) {
        valueMaps[0][phi] = initValues[phi];
    }

    for (int64_t iter = 0; iter < tc; ++iter) {
        auto& valueMap = valueMaps[iter];
        auto& bbMap = bbMaps[iter];

        for (auto* bb : bodyBBs) {
            auto* newBB = bbMap[bb];

            // 克隆指令（非 terminator）
            for (auto& inst : bb->getInstructions()) {
                auto op = inst->getOpcode();
                if (op == Opc::BR || op == Opc::COND_BR || op == Opc::RET) continue;

                if (op == Opc::PHI) {
                    // body 内的 PHI：更新前驱 BB 引用和值引用
                    auto* newPhi = IR::Instruction::createPhi(inst->getType(),
                        inst->getName() + ".u" + std::to_string(iter), inst->getNumOperands());
                    for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                        auto* val = inst->getOperand(i);
                        auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));

                        // 更新值引用
                        auto valIt = valueMap.find(val);
                        if (valIt != valueMap.end()) val = valIt->second;

                        // 更新前驱 BB 引用（同迭代内）
                        if (predBB) {
                            auto bbIt = bbMap.find(predBB);
                            if (bbIt != bbMap.end()) {
                                predBB = bbIt->second;
                            }
                        }

                        newPhi->addOperand(val);
                        newPhi->addOperand(predBB);
                    }
                    newBB->pushBack(newPhi);
                    valueMap[inst.get()] = newPhi;
                } else {
                    auto* cloned = cloneInstruction(inst.get(), valueMap);
                    if (!cloned) return false; // 克隆失败，放弃展开
                    newBB->pushBack(cloned);
                    valueMap[inst.get()] = cloned;
                }
            }

            // 克隆 terminator
            auto* bbTerm = bb->getTerminator();
            if (!bbTerm) continue;

            if (bbTerm->getOpcode() == Opc::BR) {
                auto* target = dynamic_cast<IR::BasicBlock*>(bbTerm->getOperand(0));
                if (target == loop.header) {
                    // back edge → 下一个迭代的 body 入口或 exit
                    if (iter + 1 < tc) {
                        newBB->pushBack(IR::Instruction::createBr(bbMaps[iter + 1][bodyEntry]));
                    } else {
                        newBB->pushBack(IR::Instruction::createBr(exitBlock));
                    }
                } else {
                    // 内部跳转
                    auto it = bbMap.find(target);
                    if (it != bbMap.end()) {
                        newBB->pushBack(IR::Instruction::createBr(it->second));
                    } else {
                        newBB->pushBack(IR::Instruction::createBr(target));
                    }
                }
            } else if (bbTerm->getOpcode() == Opc::COND_BR) {
                auto* cond = bbTerm->getOperand(0);
                auto* trueBB = dynamic_cast<IR::BasicBlock*>(bbTerm->getOperand(1));
                auto* falseBB = dynamic_cast<IR::BasicBlock*>(bbTerm->getOperand(2));

                // 更新条件
                auto condIt = valueMap.find(cond);
                if (condIt != valueMap.end()) cond = condIt->second;

                // 更新目标 BB
                if (trueBB) {
                    auto it = bbMap.find(trueBB);
                    if (it != bbMap.end()) trueBB = it->second;
                }
                if (falseBB) {
                    auto it = bbMap.find(falseBB);
                    if (it != bbMap.end()) falseBB = it->second;
                }

                if (cond && trueBB && falseBB) {
                    newBB->pushBack(IR::Instruction::createCondBr(cond, trueBB, falseBB));
                }
            } else if (bbTerm->getOpcode() == Opc::RET) {
                // RET: 循环体内的 return 语句，直接克隆
                auto* retVal = bbTerm->getNumOperands() > 0 ? bbTerm->getOperand(0) : nullptr;
                if (retVal) {
                    auto it = valueMap.find(retVal);
                    if (it != valueMap.end()) retVal = it->second;
                    newBB->pushBack(IR::Instruction::createRet(retVal));
                } else {
                    newBB->pushBack(IR::Instruction::createRet(nullptr));
                }
            }
        }

        // 为下一个迭代设置 header PHI 的值
        if (iter + 1 < tc) {
            for (auto* phi : headerPhis) {
                auto* bev = backEdgeValues[phi];
                auto it = valueMap.find(bev);
                if (it != valueMap.end()) {
                    valueMaps[iter + 1][phi] = it->second;
                } else {
                    // back-edge 值未在 valueMap 中（可能是常量或外部值）
                    valueMaps[iter + 1][phi] = bev;
                }
            }
        }
    }

    // ================================================================
    // 阶段 2b：修复前向引用
    // body PHI 可能引用后续 BB 中的值，这些值在克隆 PHI 时尚未在 valueMap 中
    // 现在所有指令都已克隆，修复这些前向引用
    // ================================================================
    for (int64_t iter = 0; iter < tc; ++iter) {
        auto& valueMap = valueMaps[iter];
        auto& bbMap = bbMaps[iter];
        for (auto* bb : bodyBBs) {
            auto* newBB = bbMap[bb];
            for (auto& inst : newBB->getInstructions()) {
                for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                    auto* op = inst->getOperand(i);
                    if (!op) continue;
                    // 跳过常量、全局变量、函数、BB
                    if (dynamic_cast<IR::ConstantInt*>(op) ||
                        dynamic_cast<IR::ConstantFloat*>(op) ||
                        dynamic_cast<IR::GlobalVariable*>(op) ||
                        dynamic_cast<IR::Function*>(op) ||
                        dynamic_cast<IR::BasicBlock*>(op) ||
                        dynamic_cast<IR::Argument*>(op)) continue;
                    // 检查是否是 body 指令（应该已被映射但可能遗漏）
                    auto it = valueMap.find(op);
                    if (it != valueMap.end() && it->second != op) {
                        inst->setOperand(i, it->second);
                    }
                }
            }
        }
    }

    // ================================================================
    // 阶段 3：更新 exit block 中对 header PHI 的引用
    // 循环结束后，header PHI 的值 = 最后迭代的 back-edge value
    // ================================================================
    for (auto* phi : headerPhis) {
        auto* finalVal = backEdgeValues[phi];
        auto it = valueMaps[tc - 1].find(finalVal);
        if (it != valueMaps[tc - 1].end()) {
            finalVal = it->second;
        }
        // 替换 header PHI 的所有使用为 finalVal
        phi->replaceAllUsesWith(finalVal);
    }

    // ================================================================
    // 阶段 4：清理 header，替换 COND_BR 为 BR 到迭代 0 的 body 入口
    // ================================================================
    for (auto it = loop.header->begin(); it != loop.header->end(); ) {
        auto op = (*it)->getOpcode();
        if (op == Opc::PHI) {
            // PHI 的 uses 已在 Phase 3 中被 replaceAllUsesWith 清除
            (*it)->dropAllUses();
            it = loop.header->erase(it);
        } else if (op == Opc::ICMP || op == Opc::COND_BR) {
            // 先清除其他指令对本指令的引用（如 COND_BR 引用 ICMP）
            // 否则 erase 后引用变为悬空指针，后续 dropAllUses 访问已释放内存 → UB
            (*it)->replaceAllUsesWith(nullptr);
            (*it)->dropAllUses();
            it = loop.header->erase(it);
        } else {
            ++it;
        }
    }
    loop.header->pushBack(IR::Instruction::createBr(bbMaps[0][bodyEntry]));

    // ================================================================
    // 阶段 5：删除原始 body BB
    // ================================================================
    // 清理 body BB 中指令：1) 清除别人对本指令的引用  2) 清除本指令对别人的引用
    for (auto* bb : bodyBBs) {
        for (auto& inst : bb->getInstructions()) {
            inst->replaceAllUsesWith(nullptr); // 清除 use-list（别人对我的引用 → null）
            inst->dropAllUses();                // 清除 operands（我对别人的引用 → 从别人的 use-list 移除）
        }
    }
    // 清理 body BB 本身的 uses（terminator 等可能引用 body BB 作为目标）
    for (auto* bb : bodyBBs) {
        bb->replaceAllUsesWith(nullptr);
    }
    // 修复 terminator 中被设为 nullptr 的 BB 操作数：替换为 exitBlock
    for (auto& fn : func->getBlocks()) {
        // 跳过 body BB（它们即将被删除）
        bool isBodyBB = false;
        for (auto* bb : bodyBBs) {
            if (fn.get() == bb) { isBodyBB = true; break; }
        }
        if (isBodyBB) continue;

        auto* term = fn->getTerminator();
        if (!term) continue;
        if (term->getOpcode() == Opc::BR) {
            if (!term->getOperand(0)) {
                term->setOperand(0, exitBlock);
            }
        } else if (term->getOpcode() == Opc::COND_BR) {
            if (!term->getOperand(1)) term->setOperand(1, exitBlock);
            if (!term->getOperand(2)) term->setOperand(2, exitBlock);
        }
    }
    // 从 function 中删除 body BB
    for (auto* bb : bodyBBs) {
        auto& blocks = func->getBlocks();
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == bb) {
                blocks.erase(it);
                break;
            }
        }
    }

    return true;
}

// ================================================================
// 完全展开循环体（入口）
// ================================================================
bool fullUnrollLoop(const NaturalLoop& loop, IR::Function* func) {
    using Opc = IR::Instruction::Opcode;

    auto info = analyzeLoopInduction(loop, func);
    int64_t tc = info.tripCount;
    if (tc < 2 || tc > 64) return false;
    if (!info.var || !info.start || !info.step) return false;

    auto* startCI = dynamic_cast<IR::ConstantInt*>(info.start);
    auto* stepCI = dynamic_cast<IR::ConstantInt*>(info.step);
    if (!startCI || !stepCI) return false;

    // 嵌套展开保护：检查函数当前总指令数
    // 防止嵌套展开导致代码指数膨胀（如 34_multi_loop 的 16 层嵌套）
    size_t funcInstCount = 0;
    for (auto& bb : func->getBlocks()) {
        funcInstCount += bb->getInstructions().size();
    }
    if (funcInstCount > 1500) return false;

    // 单层代码膨胀保护：展开后指令数 = (body指令数 - terminator数) * tc
    // 限制为 500：允许小循环展开，阻止固定尺寸的嵌套内核导致寄存器溢出。
    // 嵌套展开会产生大量同时活跃的 GEP 值，
    // 加上函数参数（arg0/arg1/arg2）和循环变量，总计 30+ 个同时活跃值，
    // 远超 16 个可用寄存器，导致寄存器分配器在溢出时产生错误代码 → SEGFAULT
    size_t bodyInstCount = 0;
    for (auto* bb : loop.body) {
        bodyInstCount += bb->getInstructions().size();
    }
    size_t loopBBs = loop.body.size();
    size_t expandedSize = (bodyInstCount - loopBBs) * tc;
    if (expandedSize > 500) return false;

    // ★ 寄存器压力检查：统计循环体内使用的、定义在循环外的不同值数量
    //   这些值在展开后需要同时保持活跃，如果超过可用寄存器数（16），
    //   寄存器分配器需要大量溢出，在当前实现下可能导致错误代码 → SEGFAULT
    {
        std::unordered_set<IR::Value*> externalVals;
        for (auto* bb : loop.body) {
            for (auto& inst : bb->getInstructions()) {
                for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
                    auto* op = inst->getOperand(i);
                    if (!op) continue;
                    if (dynamic_cast<IR::Constant*>(op)) continue;
                    if (dynamic_cast<IR::BasicBlock*>(op)) continue;
                    if (dynamic_cast<IR::Function*>(op)) continue;
                    if (dynamic_cast<IR::GlobalVariable*>(op)) continue;
                    // 检查操作数是否定义在循环外
                    auto* opInst = dynamic_cast<IR::Instruction*>(op);
                    if (opInst) {
                        auto* opBB = opInst->getParent();
                        if (!loop.body.count(opBB)) {
                            externalVals.insert(op);
                        }
                    } else {
                        // 函数参数等非指令值，视为外部值
                        if (dynamic_cast<IR::Argument*>(op)) {
                            externalVals.insert(op);
                        }
                    }
                }
            }
        }
        // 外部值 × 展开次数 = 展开后同时活跃的外部值数量（近似）
        // 加上循环内的 PHI 值（每次迭代产生一个新值）
        if (externalVals.size() * tc > 14 || externalVals.size() > 10) {
            return false;
        }
    }

    // 嵌套循环保护：跳过含内层循环的外层循环的完全展开
    // 原因：完全展开外层循环 tc 次会创建 tc 份内层循环的副本，每份都需要
    // 独立的 header/body/latch 和寄存器分配。这会导致代码指数膨胀和寄存器
    // 压力激增。例如一个 trip count 为 20 的外层循环若包含内层循环，
    // 完全展开会复制 20 份内层 CFG，容易导致代码膨胀和寄存器溢出。
    {
        auto allLoops = getLoopsInnermostFirst(func);
        for (auto& otherLoop : allLoops) {
            if (otherLoop.header == loop.header) continue;
            if (loop.body.count(otherLoop.header)) return false;
        }
    }

    // 收集循环体中的非 header 块
    // ★ 必须按 func->getBlocks() 的 vector 顺序遍历（确定性），
    //   而非 loop.body（unordered_set，迭代顺序不确定）。
    //   非确定性会导致新 BB 创建顺序不同 → 指令 ID 不同 →
    //   活跃区间不同 → 寄存器分配不同 → 非确定性错误代码。
    std::vector<IR::BasicBlock*> bodyBBs;
    std::vector<IR::BasicBlock*> bodyBBsExceptHeader;
    for (auto& bb : func->getBlocks()) {
        if (loop.body.count(bb.get())) {
            bodyBBs.push_back(bb.get());
            if (bb.get() != loop.header) bodyBBsExceptHeader.push_back(bb.get());
        }
    }

    // 单 BB 循环（header + 1 body BB）：使用 fullUnrollSingleBB
    // 现已支持 PHI 模式（Mem2Reg 后），用每次迭代的值替换 PHI 使用
    if (bodyBBsExceptHeader.size() == 1) {
        return fullUnrollSingleBB(loop, func, bodyBBsExceptHeader);
    }

    // 多 BB 循环：克隆所有 body BB 并正确连接分支和 PHI
    // 注意：只传非 header 的 body BB，header 保留不删除（外部 BB 可能引用它）
    return fullUnrollMultiBB(loop, func, bodyBBsExceptHeader);
}

} // namespace

bool loopFullUnroll(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;

        // 每次展开后重新分析循环，因为展开会改变循环结构
        // （内层循环展开后，外层循环的 body 集合会失效）
        bool funcChanged = true;
        while (funcChanged) {
            funcChanged = false;
            auto loops = getLoopsInnermostFirst(func.get());
            for (auto& loop : loops) {
                if (fullUnrollLoop(loop, func.get())) {
                    changed = true;
                    funcChanged = true;
                    break;  // 重新分析循环
                }
            }
        }
    }
    return changed;
}

} // namespace Opt
