// ================================================================
// O3: 循环展开（Loop Unrolling）
// 策略：
//   对迭代次数 ≤ 64 的简单 while 循环做展开（最大 8×）
//   将循环体的非控制指令拷贝一份到同一个 BB 中，减少分支开销
//   对新产生的常量表达式由后续 constantFolding 进行折叠
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

// ---- 回边检测 → 循环（使用共享的 computeDominators / buildSuccessors / buildPredecessors） ----
struct LoopInfo {
    IR::BasicBlock* header;
    IR::BasicBlock* latch;       // 回边的源 BB
    BBSet body;
    int tripCount;               // -1 表示未知
};

std::vector<LoopInfo> detectLoops(IR::Function* func) {
    auto dom = computeDominators(func);
    auto succs = buildSuccessors(func);
    auto preds = buildPredecessors(func);
    std::vector<LoopInfo> loops;

    for (auto& bb : func->getBlocks()) {
        for (auto* succ : succs[bb.get()]) {
            auto it = dom.find(bb.get());
            if (it != dom.end() && it->second.count(succ) && succ != bb.get()) {
                // succ 支配 bb → 回边 bb→succ
                LoopInfo loop;
                loop.header = succ;
                loop.latch = bb.get();
                loop.body.insert(succ);

                std::vector<IR::BasicBlock*> wl;
                std::unordered_set<IR::BasicBlock*> visited;
                wl.push_back(bb.get());
                visited.insert(bb.get());

                while (!wl.empty()) {
                    auto* cur = wl.back(); wl.pop_back();
                    loop.body.insert(cur);
                    for (auto* p : preds[cur]) {
                        if (!visited.count(p) && !loop.body.count(p)) {
                            visited.insert(p);
                            wl.push_back(p);
                        }
                    }
                }
                loop.tripCount = -1;
                loops.push_back(std::move(loop));
            }
        }
    }
    return loops;
}

// ---- 从 header 的 ICMP 推导迭代次数 ----
int inferTripCount(IR::BasicBlock* header) {
    for (auto& inst : header->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::ICMP) continue;
        if (inst->getNumOperands() < 2) continue;

        // 查找右侧常量（如 icmp slt %i, 8）
        auto* rc = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
        if (!rc) rc = dynamic_cast<IR::ConstantInt*>(inst->getOperand(0));
        if (!rc) continue;

        int64_t val = rc->getValue();
        // 仅处理 slt（有符号小于）：i < N  → tripCount = N
        if (inst->getName() == "slt" && val > 0 && val <= 64) {
            return static_cast<int>(val);
        }
        // sle（有符号小于等于）：i <= N → tripCount = N+1
        if (inst->getName() == "sle" && val >= 0 && val < 64) {
            return static_cast<int>(val) + 1;
        }
    }
    return -1;
}

// ---- 判断 BB 是否为简单循环体（无 break/continue/if/嵌套循环） ----
// 必须确保循环体中除了末尾的 BR 终止指令外，不存在其他 BR 或 COND_BR，
// 否则 continue/break/if 语句会破坏循环展开的语义正确性。
bool isSimpleBody(IR::BasicBlock* bodyBB) {
    auto& insts = bodyBB->getInstructions();
    for (auto it = insts.begin(); it != insts.end(); ++it) {
        auto op = (*it)->getOpcode();
        if (op == IR::Instruction::Opcode::CALL) return false;
        if (op == IR::Instruction::Opcode::PHI) return false;
        // 检查是否为非终止指令的 BR/COND_BR（continue/break/if）
        auto next = it; ++next;
        if (next != insts.end()) {
            if (op == IR::Instruction::Opcode::BR) return false;
            if (op == IR::Instruction::Opcode::COND_BR) return false;
        }
    }
    return true;
}

// ---- 克隆一条非终止指令，给新名字避免冲突，支持操作数重映射 ----
IR::Instruction* cloneNonTermInst(IR::Instruction* src, int copyId,
                                   std::unordered_map<IR::Value*, IR::Value*>& valueMap) {
    auto op = src->getOpcode();
    using Opc = IR::Instruction::Opcode;

    if (op == Opc::BR || op == Opc::COND_BR || op == Opc::RET) return nullptr;
    if (op == Opc::PHI || op == Opc::CALL || op == Opc::ALLOCA) return nullptr;

    std::string newName = src->getName() + ".u" + std::to_string(copyId);

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
        return IR::Instruction::createCmp(op, lhs, rhs, src->getName());
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

// ---- 对单个循环做展开（最大 8×，按因子 8/6/4/3/2 优先级） ----
bool unrollLoop(LoopInfo& loop, IR::Function* func) {
    // 仅处理单 BB 循环体
    if (loop.body.size() > 2) return false; // header + body

    // 找到 body BB（非 header 的那个）
    IR::BasicBlock* bodyBB = nullptr;
    for (auto* bb : loop.body) {
        if (bb != loop.header) {
            if (bodyBB) return false; // 超过 1 个非 header BB
            bodyBB = bb;
        }
    }
    if (!bodyBB) return false;
    if (!isSimpleBody(bodyBB)) return false;

    // 推导或使用预设迭代次数
    int tc = loop.tripCount;
    if (tc < 0) tc = inferTripCount(loop.header);
    loop.tripCount = tc;
    if (tc < 2 || tc > 64) return false;

    // 按从大到小尝试因子，最大 8×
    unsigned factor = 0;
    static const unsigned candidates[] = {8, 6, 4, 3, 2};
    for (unsigned f : candidates) {
        if (tc % f == 0 && tc >= f) {
            factor = f;
            break;
        }
    }
    if (factor == 0) return false;

    // 收集可克隆的非终止指令
    std::vector<IR::Instruction*> toClone;
    for (auto& inst : bodyBB->getInstructions()) {
        if (inst->getOpcode() == IR::Instruction::Opcode::BR ||
            inst->getOpcode() == IR::Instruction::Opcode::COND_BR ||
            inst->getOpcode() == IR::Instruction::Opcode::RET)
            continue;
        toClone.push_back(inst.get());
    }
    if (toClone.empty()) return false;

    std::unordered_map<IR::Value*, IR::Value*> valueMap;
    std::vector<IR::Instruction*> clonedInsts;
    for (unsigned u = 1; u < factor; ++u) {
        for (auto* src : toClone) {
            auto* cloned = cloneNonTermInst(src, u, valueMap);
            if (cloned) {
                valueMap[src] = cloned;
                clonedInsts.push_back(cloned);
            }
        }
    }
    if (clonedInsts.empty()) return false;

    for (auto* cloned : clonedInsts) {
        auto termIt = bodyBB->end();
        --termIt;
        bodyBB->insert(termIt, cloned);
    }

    return true;
}

} // namespace

bool loopUnrolling(IR::Module* mod) {
    bool anyChanged = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        auto loops = detectLoops(func.get());
        for (auto& loop : loops) {
            if (unrollLoop(loop, func.get())) anyChanged = true;
        }
    }
    return anyChanged;
}

} // namespace Opt