// ================================================================
// E4: 循环旋转 (Loop Rotation)
// ----------------------------------------------------------------
// 将 while(cond){body} 重构为 guard(cond, 一次) + do{body}while(cond)，
// 消除回边无条件 `j header`，让循环体成为 fall-through 流。
//
// 微架构动机（BOOM 双发射）：
//   - 原形态每迭代 1 条 cond_br（header）+ 1 条无条件 j（回边）
//   - 旋转后每迭代仅 1 条 cond_br（body 末尾），消除 j
//   - j 每迭代占用 1 个取指槽位（BOOM 16B/周期），N=1024 循环省 1024 次取指重定向
//   - 回边"热"是可靠静态启发（循环迭代 N 次，回边执行 N-1 次），后向分支预测
//     taken 在 BOOM 2-bit 预测器下几乎总是正确，不会重蹈 E2 的 if-then 回归
//
// 保守约束（本轮）：
//   - 仅单 BB 体循环（header + body，body 是 latch）
//   - IV 可以是 SSA PHI 或 alloca LOAD（mem2reg 未提升时）
//   - header 仅含 PHI + ICMP + COND_BR + LOAD(from alloca)（无其他指令）
//   - body 无 CALL、无 COND_BR（纯顺序体）
//   - body 指令数 ≤ 60
//   - exit 的 PHI 若引用 header PHI，需正确分裂为 guard/body 两路 incoming
//   - alloca IV 路径：header 的 LOAD 结果仅用于 ICMP，exit 不引用 header 定义值
//
// 开关：LOOP_ROTATE_OFF=1 或 OPT_DISABLE=loopRotation 可关闭
// ================================================================

#include "opt/Optimizer.h"
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using Opc = IR::Instruction::Opcode;

bool loopRotateDisabled() {
    if (const char* v = std::getenv("LOOP_ROTATE_OFF"))
        return std::string(v) == "1";
    return false;
}

// Debug logging controlled by DBG_ROT=1
static const bool dbgRot = [] {
    const char* v = std::getenv("DBG_ROT");
    return v && std::string(v) == "1";
}();

// 查找 header 的唯一循环外前驱（preheader）。多个外前驱则返回 nullptr。
IR::BasicBlock* findPreheader(IR::Function* func, const NaturalLoop& loop) {
    auto preds = buildPredecessors(func);
    auto it = preds.find(loop.header);
    if (it == preds.end()) return nullptr;
    IR::BasicBlock* preheader = nullptr;
    int count = 0;
    for (auto* p : it->second) {
        if (!loop.body.count(p)) {
            preheader = p;
            ++count;
        }
    }
    return (count == 1) ? preheader : nullptr;
}

// header PHI 信息：init 来自 preheader，back 来自 body（回边）
struct PhiInfo {
    IR::Instruction* phi;
    IR::Value* initVal;  // 来自 preheader 的值
    IR::Value* backVal;  // 来自 body 的回边值
};

// 收集 header 中的所有 PHI 及其 init/back 值
// 返回 false 若任一 PHI 的 incoming 不完整
bool collectHeaderPhis(IR::BasicBlock* header, IR::BasicBlock* preheader,
                       IR::BasicBlock* body, std::vector<PhiInfo>& out) {
    for (auto& inst : header->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) continue;
        auto* phi = inst.get();
        IR::Value* initVal = nullptr;
        IR::Value* backVal = nullptr;
        for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
            if (predBB == preheader) initVal = phi->getOperand(i);
            else if (predBB == body) backVal = phi->getOperand(i);
        }
        if (!initVal || !backVal) return false;
        out.push_back({phi, initVal, backVal});
    }
    return true;
}

// 在 exitBB 的 PHI 中，将 [val, header] incoming 替换为 [initVal, guard] + [backVal, body]
// val 必须是 header PHI（在 phis 中）。返回 false 若遇到非 header PHI 的值。
bool fixExitPhis(IR::BasicBlock* exitBB, IR::BasicBlock* header,
                 IR::BasicBlock* guard, IR::BasicBlock* body,
                 const std::vector<PhiInfo>& phis) {
    if (!exitBB) return true;
    for (auto& inst : exitBB->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) continue;
        auto* phi = inst.get();
        // 查找 incoming [val, header]
        for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
            if (predBB != header) continue;
            IR::Value* val = phi->getOperand(i);
            // val 必须是 header PHI
            const PhiInfo* info = nullptr;
            for (const auto& pi : phis) {
                if (pi.phi == val) { info = &pi; break; }
            }
            if (!info) return false;  // 非 header PHI 值，保守放弃
            // 替换 [val, header] → [initVal, guard]
            phi->setOperand(i, info->initVal);
            phi->setOperand(i + 1, guard);
            // 追加 [backVal, body]
            phi->addOperand(info->backVal);
            phi->addOperand(body);
            break;  // 每个 PHI 至多一个 [*, header] incoming
        }
    }
    return true;
}

// 预检查：exitBB 的 PHI 中，所有 [val, header] incoming 的 val 必须是 header PHI。
// 若存在非 header PHI 的值（如 header 的 ICMP 结果），fixExitPhis 会在变换中途
// 返回 false，但此时 guard 块已创建 → IR 半修改 → 损坏。故先做只读检查。
bool exitPhisRotatable(IR::BasicBlock* exitBB, IR::BasicBlock* header,
                       const std::vector<PhiInfo>& phis) {
    if (!exitBB) return true;
    for (auto& inst : exitBB->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) continue;
        auto* phi = inst.get();
        for (unsigned i = 0; i + 1 < phi->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(phi->getOperand(i + 1));
            if (predBB != header) continue;
            IR::Value* val = phi->getOperand(i);
            bool isHeaderPhi = false;
            for (const auto& pi : phis) {
                if (pi.phi == val) { isHeaderPhi = true; break; }
            }
            if (!isHeaderPhi) return false;
        }
    }
    return true;
}

// 检查所有 header PHI 的 use 是否仅在安全位置：
//   - header 内（ICMP，随 header 删除）
//   - body 内（被 replaceAllUsesWith 替换为新 PHI，新 PHI 在 body 中有效）
//   - exitBB 内且 use 是 PHI（由 fixExitPhis 修复为 [init,guard]+[back,body]）
// 其余 use 位置（如 exitBB 的非 PHI 指令、或其他块）一律放弃旋转：
//   replaceAllUsesWith 会将其改指 body 新 PHI，而 body 新 PHI 在 guard→exitBB
//   路径下未定义，导致读到错值。
bool phiUsesSafe(const std::vector<PhiInfo>& phis, IR::BasicBlock* header,
                 IR::BasicBlock* body, IR::BasicBlock* exitBB) {
    for (const auto& pi : phis) {
        for (auto& use : pi.phi->getUses()) {
            auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
            if (!userInst) return false;
            auto* useBB = userInst->getParent();
            if (useBB == header) continue;        // header ICMP，随 header 删除
            if (useBB == body) continue;          // body 内 use，RAUW 替换为新 PHI
            if (useBB == exitBB &&
                userInst->getOpcode() == Opc::PHI) continue;  // fixExitPhis 修复
            return false;
        }
    }
    return true;
}

// 尝试旋转单个循环。返回 true 表示已旋转。
bool rotateLoop(IR::Function* func, const NaturalLoop& loop) {
    // === 结构检查 ===
    // 1. 单 BB 体：loop.body = {header, body}
    if (loop.body.size() != 2) return false;

    auto* header = loop.header;
    IR::BasicBlock* body = nullptr;
    for (auto* bb : loop.body) {
        if (bb != header) { body = bb; break; }
    }
    if (!body) return false;

    // 2. latch 必须是 body（body → header 回边）
    if (loop.latch != body) return false;

    // 3. body 的 terminator 是 BR → header（回边）
    auto* bodyTerm = body->getTerminator();
    if (!bodyTerm || bodyTerm->getOpcode() != Opc::BR) return false;
    if (bodyTerm->getOperand(0) != header) return false;

    // 4. body 指令数限制（避免代码膨胀）
    if (body->size() > 60) return false;

    // 5. body 无 CALL、无 COND_BR、无 PHI（纯顺序体）
    //    body 现有 PHI 的 incoming 必为 [val, header]（body 仅前驱 header），
    //    旋转后 header 删除会使其悬空，故直接放弃。
    for (auto& inst : body->getInstructions()) {
        auto op = inst->getOpcode();
        if (op == Opc::CALL || op == Opc::COND_BR || op == Opc::PHI) return false;
    }

    // 6. header terminator 是 COND_BR
    auto* headerTerm = header->getTerminator();
    if (!headerTerm || headerTerm->getOpcode() != Opc::COND_BR) return false;

    // 7. COND_BR 目标：一个 body（继续），一个 exit（循环外）
    auto* condBrThen = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(1));
    auto* condBrElse = dynamic_cast<IR::BasicBlock*>(headerTerm->getOperand(2));
    if (!condBrThen || !condBrElse) return false;
    IR::BasicBlock* exitBB = nullptr;
    if (condBrThen == body && !loop.body.count(condBrElse)) exitBB = condBrElse;
    else if (condBrElse == body && !loop.body.count(condBrThen)) exitBB = condBrThen;
    else return false;

    // 8. header 仅含 PHI + ICMP + COND_BR + LOAD(from alloca)
    //    alloca LOAD 路径：当 mem2reg 未提升 IV（useBlocks > 3）时，header 包含
    //    load alloca 读取 IV。允许此模式以支持 alloca-based IV 的旋转。
    IR::Instruction* allocaLoad = nullptr;
    for (auto& inst : header->getInstructions()) {
        auto op = inst->getOpcode();
        if (op == Opc::LOAD) {
            auto* ptr = inst->getOperand(0);
            auto* ptrInst = dynamic_cast<IR::Instruction*>(ptr);
            if (!ptrInst || ptrInst->getOpcode() != Opc::ALLOCA) return false;
            if (allocaLoad) return false;  // 仅允许 1 个 alloca LOAD（IV）
            allocaLoad = inst.get();
        } else if (op != Opc::PHI && op != Opc::ICMP && op != Opc::COND_BR) {
            return false;
        }
    }
    bool isAllocaIV = (allocaLoad != nullptr);
    // alloca IV 模式下不允许 header 同时有 PHI（复杂交互，保守跳过）
    if (isAllocaIV) {
        for (auto& inst : header->getInstructions()) {
            if (inst->getOpcode() == Opc::PHI) return false;
        }
    }

    // 9. COND_BR 的条件是 header 中的 ICMP
    auto* condVal = headerTerm->getOperand(0);
    auto* icmp = dynamic_cast<IR::Instruction*>(condVal);
    if (!icmp || icmp->getOpcode() != Opc::ICMP) return false;
    if (icmp->getParent() != header) return false;

    // === 查找 preheader ===
    auto* preheader = findPreheader(func, loop);
    if (!preheader) return false;

    // === 识别 IV 及 bound ===
    auto* icmpOp0 = icmp->getOperand(0);
    auto* icmpOp1 = icmp->getOperand(1);
    const std::string& pred = icmp->getName();  // "slt"/"sge"/"eq"/"ne"/"sgt"/"sle"
    if (pred.empty()) return false;

    int ivOperandIdx = -1;   // IV 在 ICMP 中的操作数位置（0 或 1）
    IR::Value* boundVal = nullptr;

    if (isAllocaIV) {
        // === alloca IV 路径 ===
        // header 无 PHI，仅有 LOAD(alloca) + ICMP + COND_BR
        // 可用 ROT_ALLOCA_OFF=1 禁用此路径（诊断用）
        if (const char* v = std::getenv("ROT_ALLOCA_OFF"); v && std::string(v) == "1") return false;
        if (dbgRot) fprintf(stderr, "[rot] alloca-IV candidate: %s header=%s body=%s\n",
                            func->getName().c_str(), header->getName().c_str(), body->getName().c_str());
        // 安全检查 1：header 中所有指令的 use 仅在 header 内
        //   （body 有自己的 alloca LOAD，不引用 header 的 LOAD 结果；
        //    若 body 引用 header 值，删除 header 后会悬空）
        for (auto& inst : header->getInstructions()) {
            for (auto& use : inst->getUses()) {
                auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
                if (!userInst) continue;
                if (userInst->getParent() == header) continue;
                if (dbgRot) fprintf(stderr, "[rot] reject %s: header value used outside\n",
                                    func->getName().c_str());
                return false;  // header 值被外部引用，保守放弃
            }
        }
        // 安全检查 2：没有其他块的 PHI 引用 header 作为 incoming BB
        //   （删除 header 后 PHI 的 BB 引用会悬空）
        for (auto& bb : func->getBlocks()) {
            if (bb.get() == header) continue;
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() != Opc::PHI) continue;
                for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                    auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                    if (predBB == header) {
                        if (dbgRot) fprintf(stderr, "[rot] reject %s: exit PHI refs header\n",
                                            func->getName().c_str());
                        return false;
                    }
                }
            }
        }

        // ICMP 必须使用 allocaLoad
        if (icmpOp0 == static_cast<IR::Value*>(allocaLoad)) {
            ivOperandIdx = 0; boundVal = icmpOp1;
        } else if (icmpOp1 == static_cast<IR::Value*>(allocaLoad)) {
            ivOperandIdx = 1; boundVal = icmpOp0;
        } else {
            if (dbgRot) fprintf(stderr, "[rot] reject %s: ICMP not using allocaLoad\n",
                                func->getName().c_str());
            return false;
        }
        // boundVal 循环不变
        if (auto* boundInst = dynamic_cast<IR::Instruction*>(boundVal)) {
            auto* defBB = boundInst->getParent();
            if (defBB && loop.body.count(defBB)) {
                if (dbgRot) fprintf(stderr, "[rot] reject %s: bound not loop-invariant\n",
                                    func->getName().c_str());
                return false;
            }
        }
        if (dbgRot) fprintf(stderr, "[rot] ACCEPT %s: rotating %s\n",
                            func->getName().c_str(), header->getName().c_str());

        // === 旋转变换（alloca IV）===
        IR::BasicBlock* guard = func->insertBlock(header->getName() + ".guard", header);

        // 1. guard：克隆 LOAD（读 init 值 from alloca）+ ICMP + COND_BR
        auto* guardLoad = IR::Instruction::createLoad(
            allocaLoad->getType(), allocaLoad->getOperand(0),
            allocaLoad->getName() + ".grd");
        guard->pushBack(guardLoad);
        IR::Instruction* guardIcmp;
        if (ivOperandIdx == 0)
            guardIcmp = IR::Instruction::createCmp(Opc::ICMP, guardLoad, boundVal, pred);
        else
            guardIcmp = IR::Instruction::createCmp(Opc::ICMP, boundVal, guardLoad, pred);
        guard->pushBack(guardIcmp);
        guard->pushBack(IR::Instruction::createCondBr(guardIcmp, condBrThen, condBrElse));

        // 2. body 末尾追加 latch：克隆 LOAD（读更新值 from alloca）+ ICMP + COND_BR
        //    body 的 STORE 已更新 alloca，克隆的 LOAD 读取最新值（iv_next）
        auto bodyTermIt = body->end();
        --bodyTermIt;
        (*bodyTermIt)->dropAllUses();
        body->erase(bodyTermIt);

        auto* latchLoad = IR::Instruction::createLoad(
            allocaLoad->getType(), allocaLoad->getOperand(0),
            allocaLoad->getName() + ".lth");
        body->pushBack(latchLoad);
        IR::Instruction* latchIcmp;
        if (ivOperandIdx == 0)
            latchIcmp = IR::Instruction::createCmp(Opc::ICMP, latchLoad, boundVal, pred);
        else
            latchIcmp = IR::Instruction::createCmp(Opc::ICMP, boundVal, latchLoad, pred);
        body->pushBack(latchIcmp);
        body->pushBack(IR::Instruction::createCondBr(latchIcmp, condBrThen, condBrElse));

        // 3. 重定向 preheader → guard
        auto* preTerm = preheader->getTerminator();
        if (preTerm && preTerm->getOpcode() == Opc::BR) {
            preTerm->setOperand(0, guard);
        } else if (preTerm && preTerm->getOpcode() == Opc::COND_BR) {
            for (unsigned i = 1; i <= 2; ++i) {
                if (preTerm->getOperand(i) == header) preTerm->setOperand(i, guard);
            }
        }

        // 4. 删除 header
        for (auto& inst : header->getInstructions()) {
            inst->dropAllUses();
        }
        auto& blocks = func->getBlocks();
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == header) { blocks.erase(it); break; }
        }
        if (dbgRot) {
            auto* bt = body->getTerminator();
            bool selfLoop = false;
            if (bt && bt->getOpcode() == Opc::COND_BR) {
                auto* t1 = dynamic_cast<IR::BasicBlock*>(bt->getOperand(1));
                auto* t2 = dynamic_cast<IR::BasicBlock*>(bt->getOperand(2));
                selfLoop = (t1 == body || t2 == body);
            }
            fprintf(stderr, "[rot] post-rotate %s: body=%s selfLoop=%d\n",
                    func->getName().c_str(), body->getName().c_str(), selfLoop);
        }
        return true;
    }

    // === SSA PHI 路径（现有逻辑）===
    // === 收集 header PHI ===
    std::vector<PhiInfo> phis;
    if (!collectHeaderPhis(header, preheader, body, phis)) return false;

    // === PHI use 安全检查 ===
    // 确保所有 header PHI 的 use 仅在 {header, body, exitBB-PHI} 中，
    // 避免 replaceAllUsesWith 将外部 use 改指 body 新 PHI（guard→exit 路径下未定义）。
    if (!phiUsesSafe(phis, header, body, exitBB)) return false;

    // === exitBB PHI 可旋转性预检查 ===
    // 确保变换不会在 fixExitPhis 中途失败而留下半修改 IR。
    if (!exitPhisRotatable(exitBB, header, phis)) return false;

    // === 识别 IV：ICMP 的某个操作数是 header PHI ===
    const PhiInfo* ivPhi = nullptr;
    for (const auto& pi : phis) {
        if (icmpOp0 == static_cast<IR::Value*>(pi.phi)) {
            ivPhi = &pi; ivOperandIdx = 0; boundVal = icmpOp1; break;
        }
        if (icmpOp1 == static_cast<IR::Value*>(pi.phi)) {
            ivPhi = &pi; ivOperandIdx = 1; boundVal = icmpOp0; break;
        }
    }
    if (!ivPhi) return false;  // ICMP 不使用 header PHI → 非计数循环，放弃

    // boundVal 必须循环不变（常量或定义在循环外）
    if (auto* boundInst = dynamic_cast<IR::Instruction*>(boundVal)) {
        auto* defBB = boundInst->getParent();
        if (defBB && loop.body.count(defBB)) return false;
    }

    // === 旋转变换 ===
    // 创建 guard 块（插入到 header 之前）
    IR::BasicBlock* guard = func->insertBlock(header->getName() + ".guard", header);

    // 1. guard：ICMP（用 initVal）+ COND_BR（与 header 同方向）
    IR::Value* guardIvVal = ivPhi->initVal;
    IR::Instruction* guardIcmp;
    if (ivOperandIdx == 0)
        guardIcmp = IR::Instruction::createCmp(Opc::ICMP, guardIvVal, boundVal, pred);
    else
        guardIcmp = IR::Instruction::createCmp(Opc::ICMP, boundVal, guardIvVal, pred);
    guard->pushBack(guardIcmp);
    guard->pushBack(IR::Instruction::createCondBr(guardIcmp, condBrThen, condBrElse));

    // 2. 修复 exitBB 的 PHI（在 replaceAllUsesWith 之前！）
    //    [ivPhi, header] → [initVal, guard] + [backVal, body]
    if (!fixExitPhis(exitBB, header, guard, body, phis)) return false;

    // 3. 为每个 header PHI 在 body 开头创建新 PHI（incoming: [init, guard], [back, body]）
    //    body 现在是自循环（body → body 回边）
    std::unordered_map<IR::Instruction*, IR::Instruction*> oldToNewPhi;
    auto bodyInsertPos = body->begin();
    for (const auto& pi : phis) {
        auto* newPhi = IR::Instruction::createPhi(pi.phi->getType(),
            pi.phi->getName() + ".rot", 4);
        newPhi->addOperand(pi.initVal);   // [init, guard]
        newPhi->addOperand(guard);
        newPhi->addOperand(pi.backVal);   // [back, body] (self-loop)
        newPhi->addOperand(body);
        body->insert(bodyInsertPos, newPhi);
        oldToNewPhi[pi.phi] = newPhi;
    }

    // 4. 替换 header PHI 的所有 use 为新 PHI
    //    （exitBB 的 PHI 已在步骤 2 修复，不再引用 header PHI）
    //    body 内的 use（如 IV 更新）→ 新 PHI
    //    header 内的 use（ICMP）→ 新 PHI（但 header 即将删除，无影响）
    for (const auto& pi : phis) {
        auto* newPhi = oldToNewPhi[pi.phi];
        pi.phi->replaceAllUsesWith(newPhi);
    }

    // 5. body 末尾追加 latch ICMP（用 backVal）+ COND_BR（与 header 同方向）
    //    替换 body 原有的 br header
    IR::Value* latchIvVal = ivPhi->backVal;
    IR::Instruction* latchIcmp;
    if (ivOperandIdx == 0)
        latchIcmp = IR::Instruction::createCmp(Opc::ICMP, latchIvVal, boundVal, pred);
    else
        latchIcmp = IR::Instruction::createCmp(Opc::ICMP, boundVal, latchIvVal, pred);

    // 移除 body 原 terminator（br header）
    auto bodyTermIt = body->end();
    --bodyTermIt;
    (*bodyTermIt)->dropAllUses();
    body->erase(bodyTermIt);
    // 追加 latch ICMP + COND_BR
    body->pushBack(latchIcmp);
    body->pushBack(IR::Instruction::createCondBr(latchIcmp, condBrThen, condBrElse));

    // 6. 重定向 preheader 的 terminator：br header → br guard
    auto* preTerm = preheader->getTerminator();
    if (preTerm && preTerm->getOpcode() == Opc::BR) {
        preTerm->setOperand(0, guard);
    } else if (preTerm && preTerm->getOpcode() == Opc::COND_BR) {
        // preheader 以 cond_br 结尾时，将 header 引用改为 guard
        for (unsigned i = 1; i <= 2; ++i) {
            if (preTerm->getOperand(i) == header) preTerm->setOperand(i, guard);
        }
    }

    // 7. 清理 header 的 PHI/ICMP 引用并删除 header 块
    //    header 的指令已迁移（PHI → body 新 PHI；ICMP → guard/body 新 ICMP）
    for (auto& inst : header->getInstructions()) {
        inst->dropAllUses();
    }
    // 从函数块列表中删除 header
    auto& blocks = func->getBlocks();
    for (auto it = blocks.begin(); it != blocks.end(); ++it) {
        if (it->get() == header) {
            blocks.erase(it);
            break;
        }
    }

    return true;
}

} // namespace

bool loopRotation(IR::Module* mod) {
    if (loopRotateDisabled()) return false;

    // ROT_MAX=N：限制本轮最多旋转 N 个循环（诊断用）
    int rotMax = -1;
    if (const char* v = std::getenv("ROT_MAX")) rotMax = std::atoi(v);
    int rotCount = 0;

    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;

        // 内层优先；旋转可能改变 CFG，一次只处理一轮（外层循环下次调用再处理）
        auto loops = getLoopsInnermostFirst(func.get());
        for (auto& loop : loops) {
            if (rotMax >= 0 && rotCount >= rotMax) goto done;
            // 旋转后循环结构变化，跳过已被破坏的循环（body 已不含原 header）
            // 通过检查 header 是否仍在函数中来判断
            bool headerExists = false;
            for (auto& bb : func->getBlocks()) {
                if (bb.get() == loop.header) { headerExists = true; break; }
            }
            if (!headerExists) continue;

            if (rotateLoop(func.get(), loop)) {
                changed = true;
                ++rotCount;
            }
        }
    }
done:
    return changed;
}

} // namespace Opt
