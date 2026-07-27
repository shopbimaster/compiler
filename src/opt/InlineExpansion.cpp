// ================================================================
// O2: 函数内联 —— 将小函数体复制到调用点，消除 call 开销
// 策略：
//   - 仅内联单 BB 叶子函数（指令数 < 20）
//   - 多 BB 函数不内联（已知导致运行时死循环）
// 注意：仅内联叶子函数（不再调用其他用户函数）
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <iostream>

namespace Opt {
namespace {

const unsigned MAX_INLINE_INSTS_SINGLE = 20;
const unsigned MAX_INLINE_INSTS_MULTI = 120;
const unsigned MAX_INLINE_BBS_MULTI = 15;
// 多BB函数在同一caller中被调用超过此次数则不内联，避免代码膨胀导致性能下降
// 但小函数（≤30指令）豁免此限制，因为它们的内联收益远大于代码膨胀代价
const unsigned MAX_MULTI_BB_CALL_SITES = 2;
const unsigned SMALL_FUNCTION_INST_THRESHOLD = 30;
// 总内联指令预算：单个 caller 中所有被内联 callee 的总指令数不超过此值
const unsigned MAX_TOTAL_INLINE_INSTS = 800;

bool isLeafCall(IR::Function* func) {
    if (func->isExternal()) return false;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                auto* callee = inst->getOperand(0);
                // 递归调用自身视为非叶子函数，避免无限内联
                if (callee && callee->getName() == func->getName()) return false;
                return false; // calls another function → not a leaf
            }
        }
    }
    return true;
}

unsigned countInstructions(IR::Function* func) {
    if (!func) {
        return 0;
    }
    unsigned n = 0;
    for (auto& bb : func->getBlocks()) {
        if (!bb) {
            continue;
        }
        n += static_cast<unsigned>(bb->getInstructions().size());
    }
    return n;
}

bool isInlineCandidate(IR::Function* func) {
    if (func->isExternal()) return false;
    if (!isLeafCall(func)) return false;
    auto numBBs = func->getBlocks().size();
    auto numInsts = countInstructions(func);
    if (numBBs == 1) {
        return numInsts <= MAX_INLINE_INSTS_SINGLE;
    }
    // 多 BB 函数：限制 BB 数和指令数，避免控制流过于复杂
    return numBBs <= MAX_INLINE_BBS_MULTI && numInsts <= MAX_INLINE_INSTS_MULTI;
}

// 从 RETURN 指令提取返回值
IR::Value* getReturnValue(IR::Instruction* retInst) {
    if (retInst->getNumOperands() > 0) return retInst->getOperand(0);
    return nullptr;
}

// 构建参数 ALLOCA → 实参 的映射（仅针对只被存储一次的参数）
// 这种参数的 ALLOCA/STORE/LOAD 可以安全消除，直接用实参替代
std::unordered_map<IR::Value*, IR::Value*> buildParamAllocaMap(
    IR::Function* callee,
    std::unordered_map<IR::Value*, IR::Value*>& valueMap) {

    std::unordered_map<IR::Value*, IR::Value*> paramAllocaToArg;

    for (auto& calleeBB : callee->getBlocks()) {
        for (auto& inst : calleeBB->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::STORE) continue;
            if (inst->getNumOperands() < 2) continue;

            auto* src = inst->getOperand(0);
            auto* dst = inst->getOperand(1);

            // ★ 必须验证 dst 是 ALLOCA 指令！
            // 此 pass 的目的是消除参数 ALLOCA（stack slot）模式：
            //   %param_alloca = alloca i32
            //   store i32 %arg, i32* %param_alloca
            //   %val = load i32, i32* %param_alloca
            // 如果不检查 dst 是 ALLOCA，在 mem2reg 后会错误匹配对全局数组的
            // 单次存储（如 to[cnt] = To 中 dst 是 GEP，只被存一次），导致
            // 关键 STORE 被错误消除 → 程序语义错误 → SEGFAULT（13_LCA 根因）。
            auto* dstInst = dynamic_cast<IR::Instruction*>(dst);
            if (!dstInst || dstInst->getOpcode() != IR::Instruction::Opcode::ALLOCA) {
                continue;
            }

            // 检查 src 是否为参数
            bool isParam = false;
            for (unsigned i = 0; i < callee->getNumArgs(); ++i) {
                if (src == callee->getArg(i)) {
                    isParam = true;
                    break;
                }
            }
            if (!isParam) continue;

            // 检查此 ALLOCA 是否仅被存储一次（仅初始参数存储）
            int storeCount = 0;
            for (auto& bb2 : callee->getBlocks()) {
                for (auto& inst2 : bb2->getInstructions()) {
                    if (inst2->getOpcode() == IR::Instruction::Opcode::STORE &&
                        inst2->getNumOperands() >= 2 &&
                        inst2->getOperand(1) == dst) {
                        storeCount++;
                    }
                }
            }
            if (storeCount == 1) {
                auto it = valueMap.find(src);
                if (it != valueMap.end()) {
                    paramAllocaToArg[dst] = it->second;
                }
            }
        }
    }

    return paramAllocaToArg;
}

// 克隆单条指令，将 callee 的操作数映射到 caller 上下文中
IR::Instruction* cloneInstruction(
    IR::Instruction* src,
    std::unordered_map<IR::Value*, IR::Value*>& valueMap,
    IR::Function* caller,
    const std::unordered_map<IR::Value*, IR::Value*>& paramAllocaToArg) {
    auto op = src->getOpcode();
    using Opc = IR::Instruction::Opcode;

    auto lookup = [&](IR::Value* v) -> IR::Value* {
        if (!v) return nullptr;
        auto it = valueMap.find(v);
        if (it != valueMap.end()) return it->second;
        return v; // constant/global stays the same
    };

    IR::Instruction* cloned = nullptr;

    // 消除参数 ALLOCA：如果此 ALLOCA 仅用于参数传递，则完全跳过
    if (op == Opc::ALLOCA && paramAllocaToArg.count(src)) {
        return nullptr;
    }
    // 消除参数 STORE：跳过对参数 ALLOCA 的初始存储
    if (op == Opc::STORE && src->getNumOperands() >= 2) {
        if (paramAllocaToArg.count(src->getOperand(1))) {
            return nullptr;
        }
    }
    // 消除参数 LOAD：跳过从参数 ALLOCA 的加载（valueMap 已预映射）
    if (op == Opc::LOAD) {
        if (paramAllocaToArg.count(src->getOperand(0))) {
            return nullptr;
        }
    }

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
    } else if (op == Opc::BR) {
        // BR target will be remapped after cloning all BBs
        auto* targetBB = dynamic_cast<IR::BasicBlock*>(src->getOperand(0));
        cloned = IR::Instruction::createBr(targetBB); // temporarily use original target
    } else if (op == Opc::COND_BR) {
        auto* cond = lookup(src->getOperand(0));
        auto* thenBB = dynamic_cast<IR::BasicBlock*>(src->getOperand(1));
        auto* elseBB = dynamic_cast<IR::BasicBlock*>(src->getOperand(2));
        cloned = IR::Instruction::createCondBr(cond, thenBB, elseBB);
    } else if (op == Opc::PHI) {
        // 克隆 PHI 节点：暂用原始操作数引用，后续在 step 4b 中修正
        // 延迟映射的原因：PHI 的值可能来自后向边（如循环回边），此时 valueMap
        // 尚未包含该值（因为定义该值的 BB 可能还未被克隆）。
        // 块操作数也需延迟映射，因为 bbMap 在所有 BB 克隆后才完整。
        cloned = IR::Instruction::createPhi(
            src->getType(), src->getName() + ".i", src->getNumOperands());
        for (unsigned i = 0; i < src->getNumOperands(); ++i) {
            cloned->addOperand(src->getOperand(i));
        }
        // ★ 必须更新 valueMap！否则使用此 PHI 的其他指令无法映射到克隆值
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
    } else if (op == Opc::SELECT) {
        cloned = IR::Instruction::createSelect(
            lookup(src->getOperand(0)), lookup(src->getOperand(1)),
            lookup(src->getOperand(2)), src->getName() + ".i");
    } else if (op == Opc::WIDE_SMOD_MUL) {
        cloned = IR::Instruction::createTernaryOp(
            op, src->getType(), src->getName() + ".i",
            lookup(src->getOperand(0)), lookup(src->getOperand(1)),
            lookup(src->getOperand(2)));
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

// 对单个调用尝试内联（单 BB）
bool tryInlineSingleBBCall(IR::Instruction* callInst, IR::Function* callee,
                           std::unordered_map<IR::Value*, IR::Value*>& valueMap) {
    auto* func = callInst->getParent()->getParent();

    // 构建参数 ALLOCA → 实参 映射，消除冗余的 ALLOCA/STORE/LOAD
    auto paramAllocaToArg = buildParamAllocaMap(callee, valueMap);
    // 预填充 valueMap：LOAD from 参数 ALLOCA → 实参
    for (auto& calleeBB : callee->getBlocks()) {
        for (auto& inst : calleeBB->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                auto* srcPtr = inst->getOperand(0);
                auto it = paramAllocaToArg.find(srcPtr);
                if (it != paramAllocaToArg.end()) {
                    valueMap[inst.get()] = it->second;
                }
            }
        }
    }

    // 克隆 callee 的所有非终止指令
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
            auto* cloned = cloneInstruction(inst.get(), valueMap, func, paramAllocaToArg);
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

// 对单个调用尝试内联（多 BB）—— 克隆 BB 结构并重定向控制流
bool tryInlineMultiBBCall(IR::Instruction* callInst, IR::Function* callee,
                          std::unordered_map<IR::Value*, IR::Value*>& valueMap) {
    auto* caller = callInst->getParent()->getParent();
    auto* callBB = callInst->getParent();

    using Opc = IR::Instruction::Opcode;

    // 每次内联使用唯一 ID，避免同函数多次内联时标签名冲突
    static int inlineId = 0;
    int curId = ++inlineId;

    // ================================================================
    // 0. 创建共享 ALLOCA 用于统一存储所有 return 路径的返回值
    // ================================================================
    IR::Instruction* retAlloca = nullptr;
    bool hasReturnValue = false;
    // 检查 callee 是否有返回值
    for (auto& calleeBB : callee->getBlocks()) {
        for (auto& inst : calleeBB->getInstructions()) {
            if (inst->getOpcode() == Opc::RET && inst->getNumOperands() > 0) {
                hasReturnValue = true;
                break;
            }
        }
        if (hasReturnValue) break;
    }
    if (hasReturnValue) {
        auto* retTy = callInst->getType();
        retAlloca = IR::Instruction::createAlloca(retTy, "inline_ret_" + std::to_string(curId));
        // 插入到 callBB 中 call 之前
        for (auto it = callBB->begin(); it != callBB->end(); ++it) {
            if (it->get() == callInst) {
                callBB->insert(it, retAlloca);
                break;
            }
        }
    }

    // ================================================================
    // 1. 拆分 call BB：将 call 之后的指令移到新的 continuation BB
    // ================================================================
    auto* contBB = caller->createBlock("inline_cont_" + callee->getName() + "_" + std::to_string(curId));

    // 收集 call 之后的指令，从 callBB 中移出（释放 unique_ptr 所有权）
    std::vector<std::unique_ptr<IR::Instruction>> afterCallInsts;
    bool foundCall = false;
    for (auto it = callBB->begin(); it != callBB->end(); ) {
        auto* inst = it->get();
        if (inst == callInst) {
            foundCall = true;
            ++it;
            continue;
        }
        if (foundCall) {
            afterCallInsts.push_back(std::move(*it)); // 转移所有权
            it = callBB->erase(it);                   // 删除已空的 unique_ptr 槽位
        } else {
            ++it;
        }
    }

    // 如果函数有返回值，在 contBB 开头插入 LOAD 从共享 ALLOCA 读取返回值
    if (retAlloca) {
        auto* loadRet = IR::Instruction::createLoad(
            callInst->getType(), retAlloca, "inline_ret_val_" + std::to_string(curId));
        contBB->pushBack(loadRet);
        // 将 call 的所有使用替换为 LOAD 的结果
        callInst->replaceAllUsesWith(loadRet);
    }

    // 将 afterCallInsts 移到 contBB（释放所有权，传递裸指针）
    for (auto& inst : afterCallInsts) {
        contBB->pushBack(inst.release());
    }

    // ★ 修复 PHI 前驱引用：callBB 的原始 terminator 已移到 contBB，
    //   所有引用 callBB 作为前驱的 PHI 节点必须更新为 contBB。
    //   否则 PHI 的 block 操作数指向错误的前驱 → 寄存器分配器
    //   计算 live interval 时遗漏回边 → 循环变量永不更新 → 无限循环
    //   （59_sort_test5 根因：while_cond_14 的 PHI 仍引用 while_body_15
    //   而非 inline_cont_heap_ajust_2，导致 i=i-1 的结果进入错误 PHI 条目）
    for (auto& bb : caller->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != Opc::PHI) continue;
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                if (inst->getOperand(i + 1) == callBB) {
                    inst->setOperand(i + 1, contBB);
                }
            }
        }
    }

    // ================================================================
    // 2. 构建参数 ALLOCA → 实参 映射，消除冗余的 ALLOCA/STORE/LOAD
    // ================================================================
    auto paramAllocaToArg = buildParamAllocaMap(callee, valueMap);
    // 预填充 valueMap：LOAD from 参数 ALLOCA → 实参
    for (auto& calleeBB : callee->getBlocks()) {
        for (auto& inst : calleeBB->getInstructions()) {
            if (inst->getOpcode() == Opc::LOAD) {
                auto* srcPtr = inst->getOperand(0);
                auto it = paramAllocaToArg.find(srcPtr);
                if (it != paramAllocaToArg.end()) {
                    valueMap[inst.get()] = it->second;
                }
            }
        }
    }

    // ================================================================
    // 3. 克隆 callee 的所有 BB
    // ================================================================
    std::unordered_map<IR::BasicBlock*, IR::BasicBlock*> bbMap;
    std::vector<IR::BasicBlock*> clonedBBs; // 按 callee BB 顺序排列

    // 使用 insertBlock 将克隆块插入到 contBB 之前，确保基本块列表顺序
    // 与控制流一致。否则寄存器分配器的指令 ID 顺序会出错，导致
    // 错误地复用 caller 中仍活跃的寄存器（如 s6 同时用于 p[i][j] 指针和 b.i 临时值）。
    for (auto& calleeBB : callee->getBlocks()) {
        auto* clonedBB = caller->insertBlock("inline_" + callee->getName() + "_" + calleeBB->getName() + "_" + std::to_string(curId), contBB);
        bbMap[calleeBB.get()] = clonedBB;
        clonedBBs.push_back(clonedBB);
    }

    // 3a. 第一遍：先克隆所有 PHI 节点
    //   ★ 关键：PHI 必须先于其他指令克隆！
    //   原因：callee 的 BB 列表顺序可能使得使用 PHI 的指令（如循环体中的
    //   算术运算）在 PHI 之前被克隆。如果此时 valueMap 中还没有 PHI 映射，
    //   lookup() 会返回原始 PHI 指针，导致克隆的指令引用错误的（原始）PHI。
    //   后续 LICM 会将这些指令误判为循环不变量（因为引用的是另一个函数的
    //   PHI），将其外提到 preheader，使循环体变空 → 无限循环。
    //   解决方案：两遍克隆——先克隆所有 PHI（填充 valueMap），再克隆其他指令。
    for (auto& calleeBB : callee->getBlocks()) {
        auto* clonedBB = bbMap[calleeBB.get()];
        for (auto& inst : calleeBB->getInstructions()) {
            if (inst->getOpcode() != Opc::PHI) continue;
            auto* cloned = cloneInstruction(inst.get(), valueMap, caller, paramAllocaToArg);
            if (cloned) {
                clonedBB->pushBack(cloned);
            }
        }
    }

    // 3b. 第二遍：克隆所有非 PHI、非 RET 指令
    for (auto& calleeBB : callee->getBlocks()) {
        auto* clonedBB = bbMap[calleeBB.get()];
        for (auto& inst : calleeBB->getInstructions()) {
            auto op = inst->getOpcode();
            if (op == Opc::PHI || op == Opc::RET) continue;
            auto* cloned = cloneInstruction(inst.get(), valueMap, caller, paramAllocaToArg);
            if (cloned) {
                clonedBB->pushBack(cloned);
            }
        }
    }

    // ================================================================
    // 4. 重定向 BR/COND_BR 目标到克隆的 BB
    // ================================================================
    for (auto& [calleeBB, clonedBB] : bbMap) {
        auto* term = clonedBB->getTerminator();
        if (!term) continue;
        auto op = term->getOpcode();
        if (op == Opc::BR) {
            auto* origTarget = dynamic_cast<IR::BasicBlock*>(term->getOperand(0));
            if (origTarget && bbMap.count(origTarget)) {
                term->setOperand(0, bbMap[origTarget]);
            }
        } else if (op == Opc::COND_BR) {
            auto* origThen = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
            auto* origElse = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
            if (origThen && bbMap.count(origThen)) {
                term->setOperand(1, bbMap[origThen]);
            }
            if (origElse && bbMap.count(origElse)) {
                term->setOperand(2, bbMap[origElse]);
            }
        }
    }

    // ================================================================
    // 4b. 修正 PHI 节点的值和块操作数（延迟映射）
    //   cloneInstruction 中 PHI 暂用原始引用，此处统一修正：
    //     - 值操作数（偶数索引）：通过 valueMap 映射到克隆值
    //     - 块操作数（奇数索引）：通过 bbMap 映射到克隆块
    //   延迟映射确保后向边（循环回边）的值已被克隆并进入 valueMap
    // ================================================================
    for (auto* clonedBB : clonedBBs) {
        for (auto& inst : clonedBB->getInstructions()) {
            if (inst->getOpcode() != Opc::PHI) continue;
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                // 修正值操作数
                IR::Value* val = inst->getOperand(i);
                if (val) {
                    auto it = valueMap.find(val);
                    if (it != valueMap.end()) {
                        inst->setOperand(i, it->second);
                    }
                }
                // 修正块操作数
                auto* origBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                if (origBB) {
                    auto it = bbMap.find(origBB);
                    if (it != bbMap.end()) {
                        inst->setOperand(i + 1, it->second);
                    }
                }
            }
        }
    }

    // ================================================================
    // 5. 处理返回值：将每个 RET 替换为 STORE(返回值, retAlloca) + BR 到 contBB
    // ================================================================
    for (auto& calleeBB : callee->getBlocks()) {
        for (auto& inst : calleeBB->getInstructions()) {
            if (inst->getOpcode() == Opc::RET) {
                auto* clonedBB = bbMap[calleeBB.get()];
                // 如果有返回值，先 STORE 到共享 ALLOCA
                if (retAlloca && inst->getNumOperands() > 0) {
                    IR::Value* retVal = inst->getOperand(0);
                    IR::Value* mapped = valueMap.count(retVal) ? valueMap[retVal] : retVal;
                    auto* storeRet = IR::Instruction::createStore(mapped, retAlloca);
                    clonedBB->pushBack(storeRet);
                }
                // 添加 BR 到 contBB
                auto* brToCont = IR::Instruction::createBr(contBB);
                clonedBB->pushBack(brToCont);
                break;
            }
        }
    }

    // ================================================================
    // 6. 替换 CALL 为 BR 到克隆的 entry BB
    // ================================================================
    auto* entryBB = bbMap[callee->getEntryBlock()];
    auto* brToEntry = IR::Instruction::createBr(entryBB);

    // 在 callBB 中插入 BR 到 entry，然后删除 CALL
    for (auto it = callBB->begin(); it != callBB->end(); ++it) {
        if (it->get() == callInst) {
            callInst->dropAllUses();
            it = callBB->erase(it);
            callBB->insert(it, brToEntry);
            break;
        }
    }

    // ================================================================
    // 7. 防御性清理：删除克隆 BB 中第一条 terminator 之后的所有指令
    //   SimplifyCFG 合并空块时可能遗留额外的 BR，导致同一 BB
    //   出现多条 terminator。getTerminator() 返回 insts.back()
    //   会选择错误的 terminator → 无限循环（59_sort_test5 根因）
    // ================================================================
    for (auto* clonedBB : clonedBBs) {
        bool seenTerm = false;
        for (auto it = clonedBB->begin(); it != clonedBB->end(); ) {
            auto op = (*it)->getOpcode();
            if (seenTerm) {
                (*it)->replaceAllUsesWith(nullptr);
                for (unsigned i = 0; i < (*it)->getNumOperands(); ++i) {
                    (*it)->setOperand(i, nullptr);
                }
                it = clonedBB->erase(it);
            } else if (op == Opc::BR || op == Opc::COND_BR || op == Opc::RET) {
                seenTerm = true;
                ++it;
            } else {
                ++it;
            }
        }
    }

    return true;
}

// 对单个调用尝试内联
bool tryInlineCall(IR::Instruction* callInst, IR::Function* callee) {
    // 建立参数 -> 实参的映射
    std::unordered_map<IR::Value*, IR::Value*> valueMap;
    for (unsigned i = 0; i < callee->getNumArgs(); ++i) {
        valueMap[callee->getArg(i)] = callInst->getOperand(i + 1);
    }

    if (callee->getBlocks().size() == 1) {
        return tryInlineSingleBBCall(callInst, callee, valueMap);
    } else {
        return tryInlineMultiBBCall(callInst, callee, valueMap);
    }
}

} // namespace

bool inlineExpansion(IR::Module* mod) {
    if (!mod) return false;

    // 识别可内联的候选函数
    std::unordered_set<IR::Function*> candidates;

    auto& funcs = mod->getFunctions();

    for (auto& func : funcs) {
        if (!func) continue;
        if (isInlineCandidate(func.get())) {
            candidates.insert(func.get());
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;

        // 复制函数列表避免迭代器失效
        std::vector<IR::Function*> funcList;
        for (auto& func : mod->getFunctions()) {
            funcList.push_back(func.get());
        }

        for (auto* func : funcList) {
            if (func->isExternal()) continue;
            // 跳过候选函数自身（避免内联到自身）
            if (candidates.count(func)) continue;

            // 统计当前 caller 中各候选 callee 的调用次数
            std::unordered_map<IR::Function*, unsigned> callCount;

            for (auto& bb : func->getBlocks()) {
                for (auto& inst : bb->getInstructions()) {
                    if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                        if (inst->getNumOperands() >= 1) {
                            auto* calleeVal = inst->getOperand(0);
                            if (calleeVal) {
                                auto* calleeFunc = dynamic_cast<IR::Function*>(calleeVal);
                                if (calleeFunc && candidates.count(calleeFunc)) {
                                    callCount[calleeFunc]++;
                                }
                            }
                        }
                    }
                }
            }

            // 标记需要跳过的多BB函数
            std::unordered_set<IR::Function*> skipMultiBB;
            unsigned totalInlineInsts = 0;

            // 复制callCount到vector避免迭代器问题
            std::vector<std::pair<IR::Function*, unsigned>> callCountVec(callCount.begin(), callCount.end());

            for (auto& [callee, cnt] : callCountVec) {
                if (callee->getBlocks().size() > 1) {
                    unsigned calleeInsts = countInstructions(callee);
                    unsigned projectedInsts = calleeInsts * cnt;
                    bool tooManyCalls = (cnt > MAX_MULTI_BB_CALL_SITES) &&
                                        (calleeInsts > SMALL_FUNCTION_INST_THRESHOLD);
                    bool budgetExceeded = (totalInlineInsts + projectedInsts) > MAX_TOTAL_INLINE_INSTS;
                    if (tooManyCalls || budgetExceeded) {
                        skipMultiBB.insert(callee);
                    } else {
                        totalInlineInsts += projectedInsts;
                    }
                }
            }


            for (auto& bb : func->getBlocks()) {
                for (auto it = bb->begin(); it != bb->end(); ) {
                    auto* inst = it->get();
                    if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                        auto* calleeVal = inst->getOperand(0);
                        auto* calleeFunc = dynamic_cast<IR::Function*>(calleeVal);
                        if (calleeFunc && candidates.count(calleeFunc) && !skipMultiBB.count(calleeFunc)) {
                            if (std::getenv("DEBUG_INLINE")) {
                                std::cerr << "[InlineExpansion] Inlining " << calleeFunc->getName()
                                          << " into " << func->getName() << "\n";
                            }
                            if (tryInlineCall(inst, calleeFunc)) {
                                changed = true;
                                it = bb->begin(); // restart iteration
                                break;
                            }
                        }
                    }
                    ++it;
                }
                if (changed) break;
            }
            if (changed) break;
        }
        // 内联后重新识别候选函数（之前不是叶子的函数可能变成叶子）
        if (changed) {
            candidates.clear();
            for (auto& func : mod->getFunctions()) {
                if (isInlineCandidate(func.get())) {
                    candidates.insert(func.get());
                }
            }
        }
    }
    return changed;
}

} // namespace Opt
