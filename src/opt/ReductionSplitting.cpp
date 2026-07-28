// ================================================================
// P1: 多累加器归约分裂（Reduction Splitting）
// 对应"长依赖链消除"：将串行归约依赖链 sum = sum + expr 拆分为 N 路独立
// 累加器，让 BOOM 双发射 OoO 硬件并行执行独立的 ADD 链。
//
// 算法（v2 正确版，参考 LoopUnrolling 的 IV 链式传递）：
//   检测 %sum = phi [init, preheader], [sum.next, body]
//        %sum.next = ADD %sum, %expr
//   创建 N 个独立累加器，将 body 克隆 N-1 份：
//     原始体:  acc0 += expr(i);       i.next = i + 1
//     克隆1:   acc1 += expr(i+1);     i.next1 = i.next + 1
//     克隆2:   acc2 += expr(i+2);     i.next2 = i.next1 + 1
//     克隆3:   acc3 += expr(i+3);     i.next3 = i.next2 + 1
//   IV PHI back-edge → i.next3（总步长 = N，但通过链式 +1 实现，非 ×N）
//   出口插入合并：final = acc0 + acc1 + ... + acc(N-1)
//
// ★ 正确性关键（v1 bug 修复）：
//   v1 将所有克隆用同一个 i 值 + 步长 ×N，导致只计算 1/N 的项且结果 ×N。
//   v2 通过 valueMap 链式传递 IV：每个克隆用前一个克隆的 i.next 作为当前 i，
//   确保各克隆访问 a[i], a[i+1], a[i+2], ... 不同元素。
//
// ★ 安全性（溢出保护）：
//   有符号整数加法重排在溢出时是 UB（C/C++ 语义），故对 i32 安全。
//   浮点归约不分裂（重排改变舍入结果）。
//   仅对 ADD 归约生效；MUL 归约（乘积）不分裂（依赖链更长但乘法在 BOOM 单周期）。
// ================================================================

#include "opt/Optimizer.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {

namespace {

using Opc = IR::Instruction::Opcode;

// ---- 归约循环信息 ----
struct ReductionInfo {
    IR::Instruction* phi;         // %sum = phi [init, preheader], [sum.next, body]
    IR::Instruction* update;      // %sum.next = ADD %sum, %expr
    IR::Value* initVal;           // 初始值（来自 preheader）
    IR::Value* addedExpr;         // 被加的表达式 %expr
    IR::BasicBlock* preheader;    // 非 back-edge 前驱
    IR::BasicBlock* body;         // 循环体 BB
    IR::BasicBlock* header;       // 循环头 BB
};

// ---- 检测单 BB 加法归约循环 ----
bool detectReduction(const NaturalLoop& loop, ReductionInfo& info) {
    if (loop.body.size() != 2) return false;

    IR::BasicBlock* body = nullptr;
    for (auto* bb : loop.body) {
        if (bb != loop.header) {
            if (body) return false;
            body = bb;
        }
    }
    if (!body) return false;

    // body 不能有 CALL/PHI/ALLOCA（cloneInst 无法克隆这些，会导致 IR 畸形）
    for (auto& inst : body->getInstructions()) {
        Opc oc = inst->getOpcode();
        if (oc == Opc::CALL || oc == Opc::PHI || oc == Opc::ALLOCA) return false;
    }

    for (auto& inst : loop.header->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) continue;

        IR::Value* backEdgeVal = nullptr;
        IR::Value* initVal = nullptr;
        IR::BasicBlock* preheader = nullptr;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
            if (predBB == body) {
                backEdgeVal = inst->getOperand(i);
            } else {
                initVal = inst->getOperand(i);
                preheader = predBB;
            }
        }
        if (!backEdgeVal || !initVal || !preheader) continue;

        auto* updateInst = dynamic_cast<IR::Instruction*>(backEdgeVal);
        if (!updateInst || updateInst->getOpcode() != Opc::ADD) continue;
        if (updateInst->getParent() != body) continue;

        // 跳过浮点 ADD（重排改变舍入）
        if (updateInst->getType() && updateInst->getType()->isFloat()) continue;

        IR::Value* lhs = updateInst->getOperand(0);
        IR::Value* rhs = updateInst->getOperand(1);
        IR::Value* addedExpr = nullptr;
        if (lhs == inst.get()) addedExpr = rhs;
        else if (rhs == inst.get()) addedExpr = lhs;
        else continue;

        // 归约变量 PHI 的使用限制：
        //   允许在循环体外有其他使用（如出口 store c[i][j]=sum），
        //   replaceAllUsesWith(merged) 会将它们替换为合并后的值。
        //   但循环体内除 update 外不能有其他使用（避免克隆语义错误）。
        bool hasBodyUseOtherThanUpdate = false;
        for (auto& u : inst->getUses()) {
            auto* useInst = dynamic_cast<IR::Instruction*>(u.user);
            if (!useInst) continue;
            if (useInst->getParent() == body && useInst != updateInst) {
                hasBodyUseOtherThanUpdate = true;
                break;
            }
        }
        if (hasBodyUseOtherThanUpdate) continue;

        // ★ 区分 IV 和归约：PHI 被 header 中的 ICMP 使用 → 是 IV（循环变量），
        //   不是归约变量。IV 分裂会产生多个无意义的累加器并破坏循环边界。
        bool isIV = false;
        for (auto& u : inst->getUses()) {
            auto* useInst = dynamic_cast<IR::Instruction*>(u.user);
            if (!useInst) continue;
            if (useInst->getParent() == loop.header &&
                useInst->getOpcode() == Opc::ICMP) {
                isIV = true;
                break;
            }
        }
        if (isIV) continue;

        info.phi = inst.get();
        info.update = updateInst;
        info.initVal = initVal;
        info.addedExpr = addedExpr;
        info.preheader = preheader;
        info.body = body;
        info.header = loop.header;
        return true;
    }
    return false;
}

// ---- 溢出安全性检查 ----
// i32 有符号加法溢出是 UB，故对整数安全。
// 仅排除浮点（已在 detectReduction 中过滤）和过大 tripCount。
bool isSafeToSplit(const ReductionInfo& info, int64_t tripCount, int factor) {
    if (tripCount < factor * 2) return false;
    int64_t perAcc = tripCount / factor;
    // 限制每路累加器处理 ≤ 2^20 项（防止极端情况）
    return perAcc < (1LL << 20);
}

// ---- 统计循环体的外部值数量（寄存器压力） ----
size_t countExternalVals(const NaturalLoop& loop) {
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
                auto* opInst = dynamic_cast<IR::Instruction*>(op);
                if (opInst) {
                    auto* opBB = opInst->getParent();
                    if (!loop.body.count(opBB)) {
                        externalVals.insert(op);
                    }
                } else if (dynamic_cast<IR::Argument*>(op)) {
                    externalVals.insert(op);
                }
            }
        }
    }
    return externalVals.size();
}

// ---- 克隆非终止指令（参考 LoopUnrolling.cpp 的 cloneNonTermInst） ----
IR::Instruction* cloneInst(IR::Instruction* src, int copyId,
                            std::unordered_map<IR::Value*, IR::Value*>& valueMap) {
    auto op = src->getOpcode();
    if (op == Opc::BR || op == Opc::COND_BR || op == Opc::RET) return nullptr;
    if (op == Opc::PHI || op == Opc::CALL || op == Opc::ALLOCA) return nullptr;

    std::string newName = src->getName() + ".rs" + std::to_string(copyId);

    auto lookup = [&](IR::Value* v) -> IR::Value* {
        if (!v) return nullptr;
        auto it = valueMap.find(v);
        return (it != valueMap.end()) ? it->second : v;
    };

    if (op == Opc::LOAD) {
        auto* ptr = lookup(src->getOperand(0));
        return IR::Instruction::createLoad(src->getType(), ptr, newName);
    }
    if (op == Opc::STORE) {
        auto* val = lookup(src->getOperand(0));
        auto* ptr = lookup(src->getOperand(1));
        return IR::Instruction::createStore(val, ptr);
    }
    if (op == Opc::GETELEMENTPTR) {
        auto* ptr = lookup(src->getOperand(0));
        auto* ptrType = dynamic_cast<IR::PointerType*>(src->getOperand(0)->getType());
        IR::Type* pointee = ptrType ? ptrType->getPointeeType() : src->getType();
        std::vector<IR::Value*> indices;
        for (unsigned i = 1; i < src->getNumOperands(); ++i)
            indices.push_back(lookup(src->getOperand(i)));
        return IR::Instruction::createGetElementPtr(pointee, ptr, indices, newName);
    }
    if (op == Opc::ICMP || op == Opc::FCMP) {
        auto* lhs = lookup(src->getOperand(0));
        auto* rhs = lookup(src->getOperand(1));
        return IR::Instruction::createCmp(op, lhs, rhs, newName);
    }
    if (op == Opc::SELECT) {
        return IR::Instruction::createSelect(
            lookup(src->getOperand(0)), lookup(src->getOperand(1)),
            lookup(src->getOperand(2)), newName);
    }
    if (op == Opc::WIDE_SMOD_MUL) {
        return IR::Instruction::createTernaryOp(
            op, src->getType(), newName,
            lookup(src->getOperand(0)), lookup(src->getOperand(1)),
            lookup(src->getOperand(2)));
    }
    if (src->getNumOperands() >= 2) {
        auto* lhs = lookup(src->getOperand(0));
        auto* rhs = lookup(src->getOperand(1));
        return IR::Instruction::createBinOp(op, src->getType(), newName, lhs, rhs);
    }
    if (src->getNumOperands() >= 1) {
        auto* op0 = lookup(src->getOperand(0));
        return IR::Instruction::createCast(op, src->getType(), op0, newName);
    }
    return nullptr;
}

// ---- 执行归约分裂（v2：IV 链式传递，无步长 ×N） ----
// 将 sum = sum + expr（tc 次串行 ADD）拆分为 N 路独立累加器。
// 每个克隆用链式 IV 访问不同元素：a[i], a[i+1], ..., a[i+N-1]
bool splitReduction(ReductionInfo& info, int factor, IR::Function* func) {
    const int N = factor;

    // 1. 创建 N-1 个新累加器 PHI（sum1..sumN-1，初值 0）
    auto* i32Ty = IR::IntegerType::I32;
    auto* zero = IR::ConstantInt::get(i32Ty, 0);

    std::vector<IR::Instruction*> accPhis(N);
    accPhis[0] = info.phi;
    for (int k = 1; k < N; ++k) {
        auto* newPhi = IR::Instruction::createPhi(
            info.phi->getType(), info.phi->getName() + ".acc" + std::to_string(k), 2);
        newPhi->addOperand(zero);
        newPhi->addOperand(info.preheader);
        newPhi->addOperand(zero);  // 占位，稍后替换为克隆 update
        newPhi->addOperand(info.body);
        auto it = std::find_if(info.header->begin(), info.header->end(),
            [&](std::unique_ptr<IR::Instruction>& p) { return p.get() == info.phi; });
        if (it == info.header->end()) return false;
        info.header->insert(++it, newPhi);
        accPhis[k] = newPhi;
    }

    // 2. 收集 body 中的非终止指令（按顺序）
    std::vector<IR::Instruction*> bodyInsts;
    for (auto& inst : info.body->getInstructions()) {
        Opc oc = inst->getOpcode();
        if (oc == Opc::BR || oc == Opc::COND_BR || oc == Opc::RET) continue;
        bodyInsts.push_back(inst.get());
    }
    if (bodyInsts.empty()) return false;

    // 3. 收集 header 中所有 PHI 的 back-edge 值（用于 IV 链式传递）
    //    phiToBackEdge[phi] = original back-edge value（永不在循环中更新）
    //    valueMap[original_back_edge] 始终映射到最新克隆值
    std::unordered_map<IR::Value*, IR::Value*> phiToBackEdge;
    for (auto& inst : info.header->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) continue;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
            if (predBB == info.body) {
                IR::Value* backEdgeVal = inst->getOperand(i);
                phiToBackEdge[inst.get()] = backEdgeVal;
                // 安全检查：back-edge 值若在 body 中定义，必须在 bodyInsts 中
                if (auto* beInst = dynamic_cast<IR::Instruction*>(backEdgeVal)) {
                    if (beInst->getParent() == info.body) {
                        bool found = false;
                        for (auto* bi : bodyInsts) {
                            if (bi == beInst) { found = true; break; }
                        }
                        if (!found) return false;
                    }
                }
                break;
            }
        }
    }

    // 3b. ★ 安全预检：验证所有体指令（除 update 外）都能被 cloneInst 克隆
    //     cloneInst 对不支持的 opcode 返回 nullptr，若体中有此类指令且被后续
    //     指令引用，会导致 valueMap 缺失 → IR 畸形 → 后续 pass 崩溃。
    {
        for (auto* src : bodyInsts) {
            if (src == info.update) continue;
            Opc oc = src->getOpcode();
            // cloneInst 支持的 opcode：BinOp(2+op), Cast(1op), LOAD, STORE,
            // GEP, ICMP, FCMP, SELECT, WIDE_SMOD_MUL
            // 不支持：PHI, CALL, ALLOCA, BR, COND_BR, RET（已在前面过滤）
            switch (oc) {
                case Opc::LOAD: case Opc::STORE: case Opc::GETELEMENTPTR:
                case Opc::ICMP: case Opc::FCMP: case Opc::SELECT:
                case Opc::WIDE_SMOD_MUL:
                case Opc::ADD: case Opc::SUB: case Opc::MUL: case Opc::SDIV: case Opc::SREM:
                case Opc::FADD: case Opc::FSUB: case Opc::FMUL: case Opc::FDIV:
                case Opc::AND: case Opc::OR: case Opc::XOR: case Opc::SHL: case Opc::ASHR:
                case Opc::SMULH:
                case Opc::ZEXT: case Opc::SEXT: case Opc::TRUNC: case Opc::SITOFP: case Opc::FPTOSI:
                    break;  // 可克隆
                default:
                    return false;  // 不可克隆的 opcode
            }
        }
    }

    // 4. 克隆 body N-1 份，每份更新不同累加器 + 链式 IV
    std::vector<IR::Instruction*> newUpdates(N - 1);
    // IV PHI 的最终 back-edge 值（最后一个克隆的 i.next）
    IR::Value* finalIVBackEdge = nullptr;

    std::unordered_map<IR::Value*, IR::Value*> valueMap;

    for (int k = 1; k < N; ++k) {
        // ★ IV 链式传递：在克隆前，将每个 PHI 映射为"当前归纳变量值"
        //   通过 lookup(original back-edge) 获取：
        //   k=1: valueMap 为空，lookup 返回 original（第 1 次迭代后的值）
        //   k=2: valueMap[original] = k1 克隆（第 2 次迭代后的值）
        for (auto& [phi, origBackEdge] : phiToBackEdge) {
            if (phi == info.phi) {
                // 归约 PHI 映射为第 k 路累加器
                valueMap[phi] = accPhis[k];
            } else {
                // IV PHI 映射为链式值
                auto it = valueMap.find(origBackEdge);
                valueMap[phi] = (it != valueMap.end()) ? it->second : origBackEdge;
            }
        }

        for (auto* src : bodyInsts) {
            if (src == info.update) {
                // 特殊处理：update 的 ADD 操作数中，sum 替换为 accPhis[k]
                auto* lhs = src->getOperand(0);
                auto* rhs = src->getOperand(1);
                IR::Value* exprOp = (lhs == info.phi) ? rhs : lhs;
                auto it = valueMap.find(exprOp);
                IR::Value* mappedExpr = (it != valueMap.end()) ? it->second : exprOp;

                std::string newName = src->getName() + ".rs" + std::to_string(k);
                auto* newUpd = IR::Instruction::createBinOp(
                    Opc::ADD, src->getType(), newName, accPhis[k], mappedExpr);

                auto termIt = info.body->end(); --termIt;
                info.body->insert(termIt, newUpd);
                newUpdates[k - 1] = newUpd;
                valueMap[src] = newUpd;
            } else {
                auto* cloned = cloneInst(src, k, valueMap);
                if (cloned) {
                    auto termIt = info.body->end(); --termIt;
                    info.body->insert(termIt, cloned);
                    valueMap[src] = cloned;
                }
            }
        }
    }

    // 5. 更新各累加器 PHI 的 back-edge 值为对应的克隆 update
    for (int k = 1; k < N; ++k) {
        auto* phi = accPhis[k];
        for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
            if (predBB == info.body) {
                phi->setOperand(i, newUpdates[k - 1]);
                break;
            }
        }
    }

    // 6. ★ 更新 IV PHI 的 back-edge 为最后一次克隆的 i.next（非步长 ×N）
    //    查找 IV PHI（非归约 PHI）的 back-edge 值
    for (auto& inst : info.header->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) continue;
        if (inst.get() == info.phi) continue;  // 跳过归约 PHI

        auto it = phiToBackEdge.find(inst.get());
        if (it == phiToBackEdge.end()) continue;
        IR::Value* origBackEdge = it->second;
        auto vIt = valueMap.find(origBackEdge);
        if (vIt == valueMap.end()) continue;  // back-edge 未被克隆（常量等）
        IR::Value* finalBackEdge = vIt->second;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
            if (predBB == info.body) {
                inst->setOperand(i, finalBackEdge);
                break;
            }
        }
    }

    // 7. 在循环出口插入合并代码：final = sum0 + sum1 + ... + sum(N-1)
    IR::BasicBlock* exitBB = nullptr;
    auto* headerTerm = info.header->getTerminator();
    if (!headerTerm) return false;
    if (headerTerm->getOpcode() == Opc::COND_BR) {
        auto* thenBB = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(1));
        auto* elseBB = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(2));
        exitBB = (thenBB == info.body) ? elseBB : thenBB;
    } else if (headerTerm->getOpcode() == Opc::BR) {
        auto* target = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(0));
        exitBB = (target == info.body) ? nullptr : target;
    }
    if (!exitBB) return false;

    IR::Value* merged = accPhis[0];
    auto insertIt = exitBB->begin();
    for (int k = 1; k < N; ++k) {
        std::string mName = info.phi->getName() + ".merge" + std::to_string(k);
        auto* mAdd = IR::Instruction::createBinOp(
            Opc::ADD, info.phi->getType(), mName, merged, accPhis[k]);
        exitBB->insert(insertIt, mAdd);
        merged = mAdd;
        ++insertIt;
    }

    // 8. 将原始归约 PHI (sum0) 在 exitBB 中的所有使用替换为 merged
    info.phi->replaceAllUsesWith(merged);
    // 恢复 body 中 update 的 sum 操作数（replaceAllUsesWith 会改它）
    {
        auto* lhs = info.update->getOperand(0);
        auto* rhs = info.update->getOperand(1);
        if (lhs == merged) info.update->setOperand(0, accPhis[0]);
        else if (rhs == merged) info.update->setOperand(1, accPhis[0]);
    }
    // 恢复 header 中归约 PHI 的 back-edge 值
    for (unsigned i = 0; i + 1 < info.phi->getNumOperands(); i += 2) {
        auto* predBB = dynamic_cast<IR::BasicBlock*>(info.phi->getOperand(i + 1));
        if (predBB == info.body) {
            if (info.phi->getOperand(i) != info.update) {
                info.phi->setOperand(i, info.update);
            }
            break;
        }
    }

    return true;
}

} // namespace

bool reductionSplitting(IR::Module* mod) {
    bool anyChanged = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;

        auto loops = getLoopsInnermostFirst(func.get());
        for (auto& loop : loops) {
            ReductionInfo info;
            if (!detectReduction(loop, info)) continue;

            auto indInfo = analyzeLoopInduction(loop, func.get());
            if (indInfo.tripCount < 8) continue;

            size_t extVals = countExternalVals(loop);

            // 选择分裂因子：默认 4，降级到 2
            int factor = 4;
            if (indInfo.tripCount % factor != 0) {
                factor = 2;
                if (indInfo.tripCount % 2 != 0) continue;
            }
            if (extVals + factor > 12) {
                factor = 2;
                if (extVals + 2 > 12) continue;
            }

            if (!isSafeToSplit(info, indInfo.tripCount, factor)) continue;

            if (splitReduction(info, factor, func.get())) {
                anyChanged = true;
            }
        }
    }
    return anyChanged;
}

} // namespace Opt
