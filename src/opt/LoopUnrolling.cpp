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
// ★ 必须考虑 PHI 的初始值！
//   while (i = 1; i < 16; i++) 的 tripCount = 16 - 1 = 15，不是 16。
//   如果忽略初始值，会导致 factor 选择错误（16%8==0 但 15%8!=0），
//   进而导致循环展开后越界访问或无限循环。
int inferTripCount(IR::BasicBlock* header) {
    for (auto& inst : header->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::ICMP) continue;
        if (inst->getNumOperands() < 2) continue;

        // 查找右侧常量（如 icmp slt %i, 8）
        auto* rc = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
        if (!rc) rc = dynamic_cast<IR::ConstantInt*>(inst->getOperand(0));
        if (!rc) continue;

        int64_t bound = rc->getValue();

        // 查找左侧非常量操作数（循环变量）
        auto* varOp = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1)) ? inst->getOperand(0) : inst->getOperand(1);

        // 尝试从 PHI 获取初始值
        int64_t initVal = 0;
        bool initKnown = false;
        if (auto* phiInst = dynamic_cast<IR::Instruction*>(varOp)) {
            if (phiInst->getOpcode() == IR::Instruction::Opcode::PHI) {
                // 遍历 PHI 的操作数，找来自非 body BB 的初始值
                for (unsigned i = 0; i + 1 < phiInst->getNumOperands(); i += 2) {
                    auto* predBB = dynamic_cast<IR::BasicBlock*>(phiInst->getOperand(i + 1));
                    if (predBB != header) {
                        // 这是初始值
                        if (auto* ci = dynamic_cast<IR::ConstantInt*>(phiInst->getOperand(i))) {
                            initVal = ci->getValue();
                            initKnown = true;
                        }
                        break;
                    }
                }
            }
        }

        // 如果初始值未知，迭代次数依赖于外层循环变量等动态值
        // 实际 tripCount 不是常数，展开会导致越界访问（如 h-5-01 回代循环）
        if (!initKnown) return -1;

        // 仅处理 slt（有符号小于）：i < N  → tripCount = N - init
        if (inst->getName() == "slt" && bound > 0 && bound <= 64) {
            int tc = static_cast<int>(bound - initVal);
            if (tc > 0 && tc <= 64) return tc;
            return -1;
        }
        // sle（有符号小于等于）：i <= N → tripCount = N+1 - init
        if (inst->getName() == "sle" && bound >= 0 && bound < 64) {
            int tc = static_cast<int>(bound + 1 - initVal);
            if (tc > 0 && tc <= 64) return tc;
            return -1;
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
        return IR::Instruction::createCmp(op, lhs, rhs, newName);
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

// ---- 对单个循环做展开（最大 8×，按因子 8/6/4/3/2 优先级） ----
// ★ 支持 Mem2Reg 引入的 PHI 归纳变量：
//   1. 识别 header 中每个 PHI 从 latch（bodyBB）来的 back-edge 值
//   2. 第 u 轮克隆时，将 PHI 映射为"当前归纳变量值"（即上一轮的 back-edge 值）
//   3. 每轮克隆后，更新映射为该轮克隆产生的新 back-edge 值
//   4. 全部克隆完成后，更新 PHI 的 back-edge operand 为最后一次克隆的值
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
    // 包含质数因子 5 和 7，支持 tc 为质数的循环完全展开
    // （如 conv2d 的 KSIZE=5 循环，如果循环体是单 BB）
    unsigned factor = 0;
    static const unsigned candidates[] = {8, 6, 4, 3, 2};
    for (unsigned f : candidates) {
        if (tc % f == 0 && tc >= f) {
            factor = f;
            break;
        }
    }
    // 如果没有整除因子且 tc ≤ 8，完全展开（支持质数 tc：5, 7）
    if (factor == 0 && tc <= 8) {
        factor = static_cast<unsigned>(tc);
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

    // ★ 收集 header PHI → back-edge 值（从 bodyBB 来的值）的映射
    // 例如：%k = phi [0, entry], [%t4, body] → phiToBackEdge[%k] = %t4
    // ★★ 关键：phiToBackEdge 保存的是 ORIGINAL back-edge 值，永不在克隆循环中更新。
    //   valueMap[original] 始终映射到最新克隆值，所以每轮开始时通过 lookup(original)
    //   即可获取当前归纳变量值。之前的 bug 是在每轮后更新 phiToBackEdge 为克隆值，
    //   导致下一轮 lookup(克隆值) 在 valueMap 中找不到（valueMap 只映射 original→clone）。
    std::unordered_map<IR::Value*, IR::Value*> phiToBackEdge;
    for (auto& inst : loop.header->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::PHI) continue;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
            if (predBB == bodyBB) {
                IR::Value* backEdgeVal = inst->getOperand(i);
                phiToBackEdge[inst.get()] = backEdgeVal;
                // 安全检查：若 back-edge 值是 body 中的指令但不在 toClone 中（如 CALL），
                // 跳过展开（无法正确克隆 back-edge 计算指令）
                if (auto* beInst = dynamic_cast<IR::Instruction*>(backEdgeVal)) {
                    if (beInst->getParent() == bodyBB) {
                        bool found = false;
                        for (auto* tc_inst : toClone) {
                            if (tc_inst == beInst) { found = true; break; }
                        }
                        if (!found) return false;
                    }
                }
                break;
            }
        }
    }

    std::unordered_map<IR::Value*, IR::Value*> valueMap;
    std::vector<IR::Instruction*> clonedInsts;
    for (unsigned u = 1; u < factor; ++u) {
        // ★ 在本轮克隆前，将 PHI 映射为"当前归纳变量值"
        // 通过 lookup(original back-edge) 获取：
        //   u=1 轮：valueMap 为空，lookup 返回 original（即第 1 次迭代后的值）
        //   u=2 轮：valueMap[original] = u1 克隆（即第 2 次迭代后的值）
        //   u=3 轮：valueMap[original] = u2 克隆（即第 3 次迭代后的值）
        for (auto& [phi, origBackEdge] : phiToBackEdge) {
            auto it = valueMap.find(origBackEdge);
            valueMap[phi] = (it != valueMap.end()) ? it->second : origBackEdge;
        }

        for (auto* src : toClone) {
            auto* cloned = cloneNonTermInst(src, u, valueMap);
            if (cloned) {
                valueMap[src] = cloned;
                clonedInsts.push_back(cloned);
            }
        }
        // ★ 不更新 phiToBackEdge！保持原始 back-edge 值，
        //   valueMap[original] 已被更新为最新克隆值。
    }
    if (clonedInsts.empty()) return false;

    for (auto* cloned : clonedInsts) {
        auto termIt = bodyBB->end();
        --termIt;
        bodyBB->insert(termIt, cloned);
    }

    // ★ 全部克隆完成后，更新 PHI 的 back-edge operand 为最后一次克隆的值
    // 通过 lookup(original back-edge) 获取最新克隆值
    for (auto& inst : loop.header->getInstructions()) {
        if (inst->getOpcode() != IR::Instruction::Opcode::PHI) continue;
        auto it = phiToBackEdge.find(inst.get());
        if (it == phiToBackEdge.end()) continue;
        IR::Value* origBackEdge = it->second;
        auto vIt = valueMap.find(origBackEdge);
        if (vIt == valueMap.end()) continue; // back-edge 未被克隆（常量等）
        IR::Value* finalBackEdge = vIt->second;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
            if (predBB == bodyBB) {
                inst->setOperand(i, finalBackEdge);
                break;
            }
        }
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
