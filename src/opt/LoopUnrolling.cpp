// ================================================================
// O3: 循环展开（Loop Unrolling）
// 策略：
//   对迭代次数 ≤ 32 的简单 while 循环做 2 倍展开
//   将循环体的非控制指令拷贝一份到同一个 BB 中，减少分支开销
//   对新产生的常量表达式由后续 constantFolding 进行折叠
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

using BBSet = std::unordered_set<IR::BasicBlock*>;

// ---- 构建后继 ----
std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>>
buildSuccs(IR::Function* func) {
    std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>> succ;
    for (auto& bb : func->getBlocks()) {
        succ[bb.get()];
        auto* term = bb->getTerminator();
        if (!term) continue;
        if (term->getOpcode() == IR::Instruction::Opcode::BR) {
            if (auto* t = dynamic_cast<IR::BasicBlock*>(term->getOperand(0)))
                succ[bb.get()].push_back(t);
        } else if (term->getOpcode() == IR::Instruction::Opcode::COND_BR) {
            if (auto* t = dynamic_cast<IR::BasicBlock*>(term->getOperand(1)))
                succ[bb.get()].push_back(t);
            if (auto* e = dynamic_cast<IR::BasicBlock*>(term->getOperand(2)))
                succ[bb.get()].push_back(e);
        }
    }
    return succ;
}

// ---- 构建前驱 ----
std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>>
buildPreds(IR::Function* func) {
    std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>> pred;
    for (auto& bb : func->getBlocks()) {
        pred[bb.get()];
        auto* term = bb->getTerminator();
        if (!term) continue;
        if (term->getOpcode() == IR::Instruction::Opcode::BR) {
            if (auto* t = dynamic_cast<IR::BasicBlock*>(term->getOperand(0)))
                pred[t].push_back(bb.get());
        } else if (term->getOpcode() == IR::Instruction::Opcode::COND_BR) {
            if (auto* t = dynamic_cast<IR::BasicBlock*>(term->getOperand(1)))
                pred[t].push_back(bb.get());
            if (auto* e = dynamic_cast<IR::BasicBlock*>(term->getOperand(2)))
                pred[e].push_back(bb.get());
        }
    }
    return pred;
}

// ---- 支配者计算 ----
std::unordered_map<IR::BasicBlock*, BBSet>
computeDom(IR::Function* func) {
    auto preds = buildPreds(func);
    auto* entry = func->getEntryBlock();
    if (!entry) return {};

    std::vector<IR::BasicBlock*> allBBs;
    for (auto& bb : func->getBlocks()) allBBs.push_back(bb.get());
    BBSet allSet(allBBs.begin(), allBBs.end());

    std::unordered_map<IR::BasicBlock*, BBSet> dom;
    for (auto* bb : allBBs) dom[bb] = allSet;
    dom[entry] = {entry};

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* bb : allBBs) {
            if (bb == entry) continue;
            BBSet inter = allSet;
            for (auto* p : preds[bb]) {
                BBSet temp;
                for (auto* d : inter)
                    if (dom[p].count(d)) temp.insert(d);
                inter = std::move(temp);
            }
            inter.insert(bb);
            if (inter != dom[bb]) {
                dom[bb] = std::move(inter);
                changed = true;
            }
        }
    }
    return dom;
}

// ---- 回边检测 → 循环 ----
struct LoopInfo {
    IR::BasicBlock* header;
    IR::BasicBlock* latch;       // 回边的源 BB
    BBSet body;
    int tripCount;               // -1 表示未知
};

std::vector<LoopInfo> detectLoops(IR::Function* func) {
    auto dom = computeDom(func);
    auto succs = buildSuccs(func);
    auto preds = buildPreds(func);
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
        if (inst->getName() == "slt" && val > 0 && val <= 32) {
            return static_cast<int>(val);
        }
        // sle（有符号小于等于）：i <= N → tripCount = N+1
        if (inst->getName() == "sle" && val >= 0 && val < 32) {
            return static_cast<int>(val) + 1;
        }
    }
    return -1;
}

// ---- 判断 BB 是否为简单循环体（无 break/continue/嵌套循环） ----
bool isSimpleBody(IR::BasicBlock* bodyBB) {
    for (auto& inst : bodyBB->getInstructions()) {
        auto op = inst->getOpcode();
        if (op == IR::Instruction::Opcode::CALL) return false;
        if (op == IR::Instruction::Opcode::PHI) return false;
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
        auto* ptr = src->getOperand(1);
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

// ---- 对单个循环做 2 倍展开 ----
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
    if (tc < 2 || tc > 32) return false;
    unsigned factor = 2;
    if (tc % factor != 0) return false;

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

void loopUnrolling(IR::Module* mod) {
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;
        auto loops = detectLoops(func.get());
        for (auto& loop : loops) {
            unrollLoop(loop, func.get());
        }
    }
}

} // namespace Opt