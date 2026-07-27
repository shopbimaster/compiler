// ================================================================
// 循环不变量外提（Loop Invariant Code Motion）
// 策略：使用循环森林（NaturalLoop）检测循环 → 从最内层到最外层处理
//       → 标记不变量 → 外提到前置块
// 增强：使用全局只读分析，在循环含 CALL 时仍可提升只读全局变量的 LOAD
// 增强：PureFuncDect 纯函数分析，允许提升纯函数 CALL（借鉴 Cpl5）
// 安全：内联后 CFG 可能更复杂，添加多项安全检查防止段错误
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <string>

namespace Opt {
namespace {

// ================================================================
// 收集函数中所有被 STORE 的全局变量（仅当前函数内）
// ================================================================
std::unordered_set<IR::GlobalVariable*> collectStoredGlobals(IR::Function* func) {
    std::unordered_set<IR::GlobalVariable*> storedGlobals;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                auto* ptr = inst->getOperand(1);
                if (auto* gv = dynamic_cast<IR::GlobalVariable*>(ptr)) {
                    storedGlobals.insert(gv);
                }
            }
        }
    }
    return storedGlobals;
}

// ================================================================
// 检查循环体中是否包含非纯 CALL 指令
// 纯函数 CALL 不影响 LOAD 安全性（不修改任何外部可见内存）
// ================================================================
bool loopHasImpureCalls(const NaturalLoop& loop,
                        const std::unordered_set<IR::Function*>& pureFuncs) {
    for (auto* bb : loop.body) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::CALL) {
                auto* callee = dynamic_cast<IR::Function*>(inst->getOperand(0));
                if (!callee || !pureFuncs.count(callee)) {
                    return true;  // 非纯 CALL
                }
            }
        }
    }
    return false;
}

// ================================================================
// 不变量判定：所有操作数是常量 || 在循环外定义 || 已标不变量
// 增强：使用模块级只读全局变量分析，允许在循环含 CALL 时仍提升
//       只读全局变量的 LOAD（因为 CALL 不可能修改只读全局变量）
// 增强：纯函数 CALL 在参数均为循环不变量时可外提（借鉴 Cpl5）
// ================================================================
bool isLoopInvariant(
    IR::Instruction* inst,
    const NaturalLoop& loop,
    const std::unordered_set<IR::Instruction*>& invariants,
    const std::unordered_set<IR::GlobalVariable*>& storedGlobals,
    const std::unordered_set<IR::GlobalVariable*>& moduleReadOnlyGlobals,
    bool hasImpureCalls,
    const std::unordered_set<IR::Function*>& pureFuncs) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    // 不可外提的副作用指令
    if (op == Opc::PHI || op == Opc::STORE ||
        op == Opc::BR || op == Opc::COND_BR || op == Opc::RET ||
        op == Opc::ALLOCA)
        return false;

    // ★ 纯函数 CALL：若被调用函数是纯函数且所有参数（操作数 1..N）均为不变量，可外提
    // 借鉴 Cpl5 PureFuncDect：纯函数不修改外部可见内存，外提到循环外安全
    if (op == Opc::CALL) {
        auto* callee = dynamic_cast<IR::Function*>(inst->getOperand(0));
        if (!callee || !pureFuncs.count(callee)) return false;  // 非纯 CALL 不可外提
        // 检查所有参数（操作数 1 开始）是否均为循环不变量
        for (unsigned i = 1; i < inst->getNumOperands(); ++i) {
            auto* argVal = inst->getOperand(i);
            if (!argVal) continue;
            if (dynamic_cast<IR::ConstantInt*>(argVal)) continue;
            if (dynamic_cast<IR::ConstantFloat*>(argVal)) continue;
            if (dynamic_cast<IR::GlobalVariable*>(argVal)) continue;
            if (dynamic_cast<IR::Function*>(argVal)) continue;
            if (auto* defInst = dynamic_cast<IR::Instruction*>(argVal)) {
                auto* defBB = defInst->getParent();
                if (defBB && loop.body.count(defBB)) {
                    if (!invariants.count(defInst)) {
                        return false;  // 参数在循环内定义且非不变量
                    }
                }
            }
        }
        return true;  // 纯函数 + 参数不变 → 可外提
    }

    // LOAD 指令：加载自全局变量或 ALLOCA 时，检查是否可外提
    if (op == Opc::LOAD) {
        auto* ptr = inst->getOperand(0);
        auto* gv = dynamic_cast<IR::GlobalVariable*>(ptr);
        if (gv) {
            // 如果全局变量在函数内被 STORE 过，不能外提
            if (storedGlobals.count(gv)) return false;
            // 增强：如果全局变量在整个模块中都是只读的，即使循环有 CALL 也可以外提
            if (moduleReadOnlyGlobals.count(gv)) return true;
            // 否则，如果循环有非纯 CALL，保守不提升
            if (hasImpureCalls) return false;
            return true;
        }
        // ALLOCA：如果 ALLOCA 在循环体中从未被 STORE，则 LOAD 是不变的
        // 这覆盖了外层循环变量（如 %i）和函数参数指针（如 %A）的情况
        auto* alloca = dynamic_cast<IR::Instruction*>(ptr);
        if (alloca && alloca->getOpcode() == Opc::ALLOCA) {
            // 检查该 ALLOCA 在循环体中是否有 STORE
            bool hasStoreInLoop = false;
            for (auto& use : alloca->getUses()) {
                auto* user = dynamic_cast<IR::Instruction*>(use.user);
                if (!user) continue;
                if (user->getOpcode() == Opc::STORE && use.operandNo == 1) {
                    // STORE 的第 2 操作数是目标地址
                    auto* storeBB = user->getParent();
                    if (storeBB && loop.body.count(storeBB)) {
                        hasStoreInLoop = true;
                        break;
                    }
                }
            }
            if (!hasStoreInLoop) return true;
        }
        return false;
    }

    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
        auto* opVal = inst->getOperand(i);
        if (!opVal) continue;
        if (dynamic_cast<IR::ConstantInt*>(opVal)) continue;
        if (dynamic_cast<IR::ConstantFloat*>(opVal)) continue;
        if (dynamic_cast<IR::GlobalVariable*>(opVal)) continue;
        if (dynamic_cast<IR::Function*>(opVal)) continue;
        if (auto* defInst = dynamic_cast<IR::Instruction*>(opVal)) {
            auto* defBB = defInst->getParent();
            if (defBB && loop.body.count(defBB)) {
                if (!invariants.count(defInst)) {
                    return false;
                }
            }
        }
    }
    return true;
}

// ================================================================
// 安全验证：检查循环的所有块是否仍在函数中
// ================================================================
bool isLoopValid(const NaturalLoop& loop, IR::Function* func) {
    // 构建函数中所有块的快速查找集合
    std::unordered_set<IR::BasicBlock*> funcBlocks;
    for (auto& bb : func->getBlocks()) {
        funcBlocks.insert(bb.get());
    }
    // 检查 header
    if (!funcBlocks.count(loop.header)) return false;
    // 检查 body 中的所有块
    for (auto* bb : loop.body) {
        if (!funcBlocks.count(bb)) return false;
    }
    return true;
}

// ================================================================
// 外提循环不变量到前置块
// 安全检查：
//   1. 循环块必须仍在函数中（内联后可能重组）
//   2. insertBlock 必须成功
//   3. 前驱的 terminator 必须有效且确实引用 header
// ================================================================
bool hoistLoopInvariants(NaturalLoop& loop, IR::Function* func,
                         const std::unordered_set<IR::GlobalVariable*>& moduleReadOnlyGlobals,
                         const std::unordered_set<IR::Function*>& pureFuncs) {
    static int preheaderCounter = 0;
    // 头块是入口块 → 跳过
    if (loop.header == func->getEntryBlock()) return false;

    // 安全检查：循环块是否仍在函数中
    if (!isLoopValid(loop, func)) return false;

    // 1. 收集循环体内所有指令
    std::vector<IR::Instruction*> loopInsts;
    for (auto* bb : loop.body) {
        for (auto& inst : bb->getInstructions()) {
            loopInsts.push_back(inst.get());
        }
    }

    // 2. 迭代标记不变量直到收敛
    auto storedGlobals = collectStoredGlobals(func);
    bool hasImpureCalls = loopHasImpureCalls(loop, pureFuncs);
    std::unordered_set<IR::Instruction*> invariants;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* inst : loopInsts) {
            if (!invariants.count(inst) &&
                isLoopInvariant(inst, loop, invariants, storedGlobals, moduleReadOnlyGlobals, hasImpureCalls, pureFuncs)) {
                invariants.insert(inst);
                changed = true;
            }
        }
    }
    if (invariants.empty()) return false;

    // 收敛性修复：检查是否有非 header 块的不变量可外提
    // 如果所有不变量都在 header 中，无需创建 preheader（避免外层循环反复迭代）
    {
        bool hasNonHeaderInvariants = false;
        for (auto* bb : loop.body) {
            if (bb == loop.header) continue;
            for (auto& inst : bb->getInstructions()) {
                if (invariants.count(inst.get())) {
                    hasNonHeaderInvariants = true;
                    break;
                }
            }
            if (hasNonHeaderInvariants) break;
        }
        if (!hasNonHeaderInvariants) return false;
    }

    // 3. 区分循环外前驱
    auto preds = buildPredecessors(func);
    std::vector<IR::BasicBlock*> outsidePreds;
    for (auto* p : preds[loop.header]) {
        if (!loop.body.count(p)) outsidePreds.push_back(p);
    }
    if (outsidePreds.empty()) return false;

    // 安全检查：验证所有外部前驱的 terminator 确实引用 header
    for (auto* p : outsidePreds) {
        auto* term = p->getTerminator();
        if (!term) return false;  // 安全：前驱必须有 terminator
        bool foundHeader = false;
        for (unsigned i = 0; i < term->getNumOperands(); ++i) {
            if (term->getOperand(i) == static_cast<IR::Value*>(loop.header)) {
                foundHeader = true;
                break;
            }
        }
        if (!foundHeader) return false;  // 安全：前驱必须引用 header
    }

    // 4. 创建前置块
    std::string preName = loop.header->getName() + ".preheader." + std::to_string(++preheaderCounter);
    auto* preheader = func->insertBlock(preName, loop.header);
    if (!preheader) return false;  // 安全：insertBlock 必须成功

    // 5. 按原始顺序将不变指令移到前置块（仅从非头块外提）
    for (auto* bb : loop.body) {
        if (bb == loop.header) continue;
        for (auto it = bb->begin(); it != bb->end(); ) {
            if (invariants.count(it->get())) {
                auto released = std::move(*it);
                it = bb->erase(it);
                preheader->pushBack(released.release());
            } else {
                ++it;
            }
        }
    }

    // 6. 前置块末尾添加无条件跳转到 header
    preheader->pushBack(IR::Instruction::createBr(loop.header));

    // 7. 重定向循环外前驱
    for (auto* p : outsidePreds) {
        auto* term = p->getTerminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->getNumOperands(); ++i) {
            if (term->getOperand(i) == static_cast<IR::Value*>(loop.header)) {
                term->setOperand(i, preheader);
            }
        }
    }

    // 8. 更新 header 中 PHI 节点的前驱指针：
    //    原来从 outsidePred 进入的值现在从 preheader 进入。
    //    如果不更新，PHI elimination 会基于过时的前驱边发射 moves，
    //    导致 moves 被发送到不存在的边上 → SEGFAULT。
    //    使用 continue 而非 break：PHI 节点可能因前序 Pass 被交错
    //    在非 PHI 指令之间。
    for (auto& inst : loop.header->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::PHI) continue;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
            if (!predBB) continue;
            if (std::find(outsidePreds.begin(), outsidePreds.end(), predBB) != outsidePreds.end()) {
                inst->setOperand(i + 1, preheader);
            }
        }
    }

    return true;
}

// ================================================================
// 单函数 LICM（从最内层循环开始处理，使用 NaturalLoop 森林）
// ================================================================
bool licmOnFunction(IR::Function* func,
                    const std::unordered_set<IR::GlobalVariable*>& moduleReadOnlyGlobals,
                    const std::unordered_set<IR::Function*>& pureFuncs) {
    if (func->isExternal()) return false;
    if (func->getBlocks().empty()) return false;

    // 使用循环森林，从最内层到最外层处理（getLoopsInnermostFirst 按 depth 降序排列）
    auto loops = getLoopsInnermostFirst(func);
    if (loops.empty()) return false;

    bool changed = false;
    for (auto& loop : loops) {
        if (loop.body.size() <= 1) continue;
        // 安全检查：处理内层循环后，外层循环可能已被修改
        if (!isLoopValid(loop, func)) continue;
        if (hoistLoopInvariants(loop, func, moduleReadOnlyGlobals, pureFuncs)) {
            changed = true;
        }
    }
    return changed;
}

} // namespace

bool loopInvariantCodeMotion(IR::Module* mod) {
    // 计算模块级只读全局变量（一次分析，所有函数复用）
    auto moduleReadOnlyGlobals = readOnlyGlobalAnalysis(mod);
    // ★ PureFuncDect：计算纯函数集合（一次分析，所有函数复用）
    // 逃生开关 OPT_DISABLE_PUREFUNC=1：返回空集，LICM 不提升任何 CALL
    std::unordered_set<IR::Function*> pureFuncs;
    static const bool disablePure = [] {
        const char* v = std::getenv("OPT_DISABLE_PUREFUNC");
        return v && std::string(v) == "1";
    }();
    if (!disablePure) {
        pureFuncs = computePureFunctions(mod);
    }

    bool changed = true;
    bool anyChanged = false;
    int iter = 0;
    while (changed) {
        if (++iter > 10) {
            break;
        }
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (licmOnFunction(func.get(), moduleReadOnlyGlobals, pureFuncs)) {
                changed = true;
                anyChanged = true;
            }
        }
    }
    return anyChanged;
}

} // namespace Opt