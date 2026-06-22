// ================================================================
// O2: 循环不变量外提（Loop Invariant Code Motion）
// 策略：构建支配树 → 检测自然循环（回边）→ 标记不变量 → 外提到前置块
// 保守处理：跳过头块含 PHI 的循环，避免破坏 PHI 节点
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// ================================================================
// 检测自然循环：回边 B→H 且 H strictly dominates B
// （使用共享的 computeDominators / buildPredecessors / buildSuccessors）
// ================================================================
struct Loop {
    IR::BasicBlock* header;
    BBSet body;
};

std::vector<Loop> findLoops(IR::Function* func, const DomMap& dom) {
    auto preds = buildPredecessors(func);
    auto succs = buildSuccessors(func);
    std::vector<Loop> loops;

    for (auto& bb : func->getBlocks()) {
        for (auto* succ : succs[bb.get()]) {
            if (strictlyDominates(succ, bb.get(), dom)) {
                Loop loop;
                loop.header = succ;
                loop.body.insert(succ);

                std::vector<IR::BasicBlock*> worklist;
                std::unordered_set<IR::BasicBlock*> visited;
                worklist.push_back(bb.get());
                visited.insert(bb.get());

                while (!worklist.empty()) {
                    auto* cur = worklist.back();
                    worklist.pop_back();
                    loop.body.insert(cur);
                    for (auto* p : preds[cur]) {
                        if (!visited.count(p) && !loop.body.count(p)) {
                            visited.insert(p);
                            worklist.push_back(p);
                        }
                    }
                }
                loops.push_back(std::move(loop));
            }
        }
    }
    return loops;
}

// ================================================================
// 检查头块是否含 PHI 节点（安全起见跳过）
// ================================================================
bool headerHasPhi(IR::BasicBlock* header) {
    for (auto& inst : header->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::PHI)
            return true;
    }
    return false;
}

// ================================================================
// 收集函数中所有被 STORE 的全局变量
// ================================================================
std::unordered_set<IR::GlobalVariable*> collectStoredGlobals(IR::Function* func) {
    std::unordered_set<IR::GlobalVariable*> storedGlobals;
    for (auto& bb : func->getBlocks()) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == IR::Instruction::Opcode::STORE) {
                // STORE 的指针操作数是 operand(1)
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
// 不变量判定：所有操作数是常量 || 在循环外定义 || 已标不变量
// ================================================================
bool isLoopInvariant(
    IR::Instruction* inst,
    const Loop& loop,
    const std::unordered_set<IR::Instruction*>& invariants,
    const std::unordered_set<IR::GlobalVariable*>& storedGlobals) {
    auto op = inst->getOpcode();
    using Opc = IR::Instruction::Opcode;
    // 不可外提的副作用指令
    if (op == Opc::PHI || op == Opc::STORE || op == Opc::CALL ||
        op == Opc::BR || op == Opc::COND_BR || op == Opc::RET ||
        op == Opc::ALLOCA)
        return false;

    // LOAD 指令：仅当加载自全局变量且该全局变量在函数中从未被 STORE 时，允许外提
    // 这样可以安全地外提如 N_eff、N 等循环边界常量的 LOAD
    if (op == Opc::LOAD) {
        auto* ptr = inst->getOperand(0);
        auto* gv = dynamic_cast<IR::GlobalVariable*>(ptr);
        if (!gv || storedGlobals.count(gv)) return false;
        // 全局变量未被存储 → LOAD 是循环不变量
        return true;
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
                if (!invariants.count(defInst)) return false;
            }
        }
    }
    return true;
}

// ================================================================
// 外提循环不变量到前置块
// ================================================================
bool hoistLoopInvariants(Loop& loop, IR::Function* func) {
    static int preheaderCounter = 0;
    // 头块是入口块 → 跳过（入口块支配所有块，创建preheader会形成新的回边，导致无限循环）
    if (loop.header == func->getEntryBlock()) return false;
    // 头块含 PHI → 保守跳过
    if (headerHasPhi(loop.header)) return false;

    // 1. 收集循环体内所有指令
    std::vector<IR::Instruction*> loopInsts;
    for (auto* bb : loop.body) {
        for (auto& inst : bb->getInstructions()) {
            loopInsts.push_back(inst.get());
        }
    }

    // 2. 迭代标记不变量直到收敛
    auto storedGlobals = collectStoredGlobals(func);
    std::unordered_set<IR::Instruction*> invariants;
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* inst : loopInsts) {
            if (!invariants.count(inst) && isLoopInvariant(inst, loop, invariants, storedGlobals)) {
                invariants.insert(inst);
                changed = true;
            }
        }
    }
    if (invariants.empty()) return false;

    // 3. 区分循环外前驱
    auto preds = buildPredecessors(func);
    std::vector<IR::BasicBlock*> outsidePreds;
    for (auto* p : preds[loop.header]) {
        if (!loop.body.count(p)) outsidePreds.push_back(p);
    }
    if (outsidePreds.empty()) return false;

    // 4. 创建前置块（使用唯一名称防止重复符号，插入到header之前确保指令ID顺序正确）
    std::string preName = loop.header->getName() + ".preheader." + std::to_string(++preheaderCounter);
    auto* preheader = func->insertBlock(preName, loop.header);

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

    // 7. 重定向循环外前驱：header 引用 → preheader 引用
    for (auto* p : outsidePreds) {
        auto* term = p->getTerminator();
        if (!term) continue;
        for (unsigned i = 0; i < term->getNumOperands(); ++i) {
            if (term->getOperand(i) == static_cast<IR::Value*>(loop.header)) {
                term->setOperand(i, preheader);
            }
        }
    }

    return true;
}

// ================================================================
// 单函数 LICM
// ================================================================
bool licmOnFunction(IR::Function* func) {
    if (func->isExternal()) return false;
    if (func->getBlocks().empty()) return false;

    auto dom = computeDominators(func);
    auto loops = findLoops(func, dom);
    if (loops.empty()) return false;

    bool changed = false;
    for (auto& loop : loops) {
        if (loop.body.size() <= 1) continue;
        if (hoistLoopInvariants(loop, func)) changed = true;
    }
    return changed;
}

} // namespace

bool loopInvariantCodeMotion(IR::Module* mod) {
    bool changed = true;
    bool anyChanged = false;
    int iter = 0;
    while (changed) {
        if (++iter > 10) {
            // Safety limit: prevent infinite loops in LICM
            // This can happen when hoisting creates new back edges
            break;
        }
        changed = false;
        for (auto& func : mod->getFunctions()) {
            if (licmOnFunction(func.get())) { changed = true; anyChanged = true; }
        }
    }
    return anyChanged;
}

} // namespace Opt