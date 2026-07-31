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

// ---- 追溯 add 链：从 val 回溯到 target，累加常量增量 ----
// 每一环必须是 `add const, X`（或 `add X, const`），X 是下一环。
// 返回净步长；若链不能干净到达 target 则返回 -1（保守跳过）。
// 用于 PHI-form IV：back-edge 值经 add 链回到 IV PHI，净步长 = 链上常量和。
//   正常 step=1：backVal = add 1, phi → step 1
//   redsplit N 路：backVal = add 1, (add 1, (... (add 1, phi))) → step N
int64_t traceAddChain(IR::Value* val, IR::Value* target) {
    int64_t step = 0;
    std::unordered_set<IR::Value*> visited;
    IR::Value* cur = val;
    while (cur != target) {
        if (visited.count(cur)) return -1;  // 环
        visited.insert(cur);
        auto* inst = dynamic_cast<IR::Instruction*>(cur);
        if (!inst || inst->getOpcode() != IR::Instruction::Opcode::ADD) return -1;
        if (inst->getNumOperands() < 2) return -1;
        auto* c0 = dynamic_cast<IR::ConstantInt*>(inst->getOperand(0));
        auto* c1 = dynamic_cast<IR::ConstantInt*>(inst->getOperand(1));
        if (!c0 && !c1) return -1;  // 无常量 → 步长未知
        if (c0 && c1) return -1;    // 双常量（应被折叠）→ 可疑
        step += c0 ? c0->getValue() : c1->getValue();
        cur = c0 ? inst->getOperand(1) : inst->getOperand(0);
    }
    return step;
}

// ---- 计算 alloca-IV 的净步长：body 内对 allocaPtr 的所有 store 的常量增量之和 ----
// 正常 step=1：1 个 store `add 1, load %j` → step 1
// redsplit N 路：N 个 store `add 1, X` → step N
// step!=1 的普通循环：1 个 store `add k, load %j` → step k
// 返回净步长；任何 store 非干净 add-const 则返回 -1（保守跳过）。
int64_t computeAllocaStep(IR::Value* allocaPtr, const BBSet& body) {
    int64_t step = 0;
    int storeCount = 0;
    for (auto* bb : body) {
        if (!bb) continue;
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::STORE) continue;
            if (inst->getNumOperands() < 2) continue;
            if (inst->getOperand(1) != allocaPtr) continue;
            ++storeCount;
            auto* storedInst = dynamic_cast<IR::Instruction*>(inst->getOperand(0));
            if (!storedInst || storedInst->getOpcode() != IR::Instruction::Opcode::ADD) return -1;
            if (storedInst->getNumOperands() < 2) return -1;
            auto* c0 = dynamic_cast<IR::ConstantInt*>(storedInst->getOperand(0));
            auto* c1 = dynamic_cast<IR::ConstantInt*>(storedInst->getOperand(1));
            if (!c0 && !c1) return -1;
            if (c0 && c1) return -1;
            step += c0 ? c0->getValue() : c1->getValue();
        }
    }
    if (storeCount == 0) return -1;
    return step;
}

// ---- 从 header 的 ICMP 推导迭代次数 ----
// ★ 必须考虑 PHI 的初始值！
//   while (i = 1; i < 16; i++) 的 tripCount = 16 - 1 = 15，不是 16。
//   如果忽略初始值，会导致 factor 选择错误（16%8==0 但 15%8!=0），
//   进而导致循环展开后越界访问或无限循环。
// ★ P2-fix: 支持 LOAD/STORE 形式的循环变量（mem2reg 未提升时）
//   如 matmul1 的 j 变量：header 中 %t = load %j; icmp %t, 200
//   需要在 preheader 中找 store 初始值到 %j 的指令
// ★ step-fix: 感知 IV 步长。redsplit 会把步长从 1 变 N，若仍按 step=1
//   算 tc 会高估 N 倍，导致选了不整除的 factor → 展开后末次迭代越界。
//   修复：tc = (bound - init) / step，要求 (bound-init) % step == 0。
int inferTripCount(IR::BasicBlock* header, const BBSet& body, IR::Function* func) {
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
        // ★ IV 步长（默认 1）。redsplit/step!=1 循环需正确感知，否则 tc 高估。
        int64_t step = 1;
        bool stepKnown = false;
        if (auto* varInst = dynamic_cast<IR::Instruction*>(varOp)) {
            if (varInst->getOpcode() == IR::Instruction::Opcode::PHI) {
                // 遍历 PHI 的操作数，找来自非 body BB 的初始值 + back-edge 值
                IR::Value* backVal = nullptr;
                for (unsigned i = 0; i + 1 < varInst->getNumOperands(); i += 2) {
                    auto* predBB = dynamic_cast<IR::BasicBlock*>(varInst->getOperand(i + 1));
                    if (predBB != header && !body.count(predBB)) {
                        // 这是初始值
                        if (auto* ci = dynamic_cast<IR::ConstantInt*>(varInst->getOperand(i))) {
                            initVal = ci->getValue();
                            initKnown = true;
                        }
                    } else {
                        // back-edge 值（来自 body）
                        backVal = varInst->getOperand(i);
                    }
                }
                // ★ 追溯 back-edge 的 add 链到 IV PHI，得到净步长
                //   正常 step=1：backVal=add(1,phi)；redsplit N 路：链 N 个 add(1,...)
                if (backVal) {
                    int64_t s = traceAddChain(backVal, varInst);
                    if (s > 0) { step = s; stepKnown = true; }
                    else return -1;  // 链不干净或退化（s<=0）→ 步长未知，保守跳过
                }
            } else if (varInst->getOpcode() == IR::Instruction::Opcode::LOAD) {
                // ★ LOAD 模式：循环变量在 ALLOCA 中（mem2reg 未提升）
                //   在 preheader（非 back-edge 前驱）中找最后一个 STORE 到同一 ALLOCA
                //   若 preheader 是合成空块（LICM 插入），沿前驱链向上查找（≤3 层）
                auto* allocaPtr = varInst->getOperand(0);
                if (std::getenv("DBG_UNROLL")) fprintf(stderr, "[unroll] %s LOAD varOp alloca=%s\n",
                    header->getName().c_str(),
                    allocaPtr ? allocaPtr->getName().c_str() : "?");
                // ★ 计算 alloca-IV 净步长（body 内对 allocaPtr 的 store 常量增量之和）
                //   redsplit N 路 → step N；正常 step=1；step!=1 普通循环 → step k
                {
                    int64_t s = computeAllocaStep(allocaPtr, body);
                    if (s > 0) { step = s; stepKnown = true; }
                    else if (s < 0) return -1;  // store 非干净 add-const → 步长未知，跳过
                }
                auto preds = buildPredecessors(func);

                // 辅助函数：在 BB 中查找最后一个 STORE 常量到 allocaPtr
                auto findConstStore = [&](IR::BasicBlock* bb) -> IR::ConstantInt* {
                    IR::Value* lastStore = nullptr;
                    for (auto& pInst : bb->getInstructions()) {
                        if (pInst->getOpcode() == IR::Instruction::Opcode::STORE &&
                            pInst->getNumOperands() > 1 &&
                            pInst->getOperand(1) == allocaPtr) {
                            lastStore = pInst->getOperand(0);
                        }
                    }
                    return dynamic_cast<IR::ConstantInt*>(lastStore);
                };

                for (auto* pred : preds[header]) {
                    if (body.count(pred)) continue;  // 跳过 back-edge 前驱
                    // 先在 preheader 本身查找
                    if (auto* ci = findConstStore(pred)) {
                        initVal = ci->getValue();
                        initKnown = true;
                        break;
                    }
                    // preheader 可能是合成空块，沿前驱链向上查找（≤3 层）
                    auto predPreds = buildPredecessors(func);
                    std::vector<IR::BasicBlock*> wl = {pred};
                    std::unordered_set<IR::BasicBlock*> visited = {pred, header};
                    int depth = 0;
                    while (!wl.empty() && depth < 3 && !initKnown) {
                        auto* cur = wl.back(); wl.pop_back();
                        ++depth;
                        for (auto* pp : predPreds[cur]) {
                            if (visited.count(pp) || body.count(pp)) continue;
                            visited.insert(pp);
                            if (auto* ci = findConstStore(pp)) {
                                initVal = ci->getValue();
                                initKnown = true;
                                break;
                            }
                            wl.push_back(pp);
                        }
                    }
                    if (initKnown) break;
                }
                // 回退：也检查 entry block
                if (!initKnown) {
                    for (auto& eInst : func->getEntryBlock()->getInstructions()) {
                        if (eInst->getOpcode() == IR::Instruction::Opcode::STORE &&
                            eInst->getNumOperands() > 1 &&
                            eInst->getOperand(1) == allocaPtr) {
                            if (auto* ci = dynamic_cast<IR::ConstantInt*>(eInst->getOperand(0))) {
                                initVal = ci->getValue();
                                initKnown = true;
                            }
                        }
                    }
                }
            }
        }

        // 如果初始值未知，迭代次数依赖于外层循环变量等动态值
        // 实际 tripCount 不是常数，展开会导致越界访问（如 h-5-01 回代循环）
        if (!initKnown) return -1;
        // step 已知时必须 > 0；未知则按 step=1（兼容旧行为）
        if (stepKnown && step <= 0) return -1;

        // 仅处理 slt（有符号小于）：i < N  → tripCount = (N - init) / step
        // P2: 上界从 64 放宽到 256，允许大循环部分展开（如 matmul1 tc=200）
        // ★ step-fix: 除以步长并要求整除。redsplit 后 step=N，若不整除则展开
        //   会导致末次迭代越界（matmul3: range=250, step=2 → tc=125, 125%2≠0 → 跳过）
        if (inst->getName() == "slt" && bound > 0 && bound <= 256) {
            int64_t range = bound - initVal;
            if (range <= 0) return -1;
            if (range % step != 0) return -1;  // 非整除 → 展开会越界，保守跳过
            int tc = static_cast<int>(range / step);
            if (tc > 0 && tc <= 256) return tc;
            return -1;
        }
        // sle（有符号小于等于）：i <= N → tripCount = (N+1 - init) / step
        if (inst->getName() == "sle" && bound >= 0 && bound < 256) {
            int64_t range = bound + 1 - initVal;
            if (range <= 0) return -1;
            if (range % step != 0) return -1;
            int tc = static_cast<int>(range / step);
            if (tc > 0 && tc <= 256) return tc;
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

// ---- 对单个循环做展开（最大 8×，按因子 8/6/4/3/2 优先级） ----
// ★ 支持 Mem2Reg 引入的 PHI 归纳变量：
//   1. 识别 header 中每个 PHI 从 latch（bodyBB）来的 back-edge 值
//   2. 第 u 轮克隆时，将 PHI 映射为"当前归纳变量值"（即上一轮的 back-edge 值）
//   3. 每轮克隆后，更新映射为该轮克隆产生的新 back-edge 值
//   4. 全部克隆完成后，更新 PHI 的 back-edge operand 为最后一次克隆的值
bool unrollLoop(LoopInfo& loop, IR::Function* func) {
    // 仅处理单 BB 循环体
    if (loop.body.size() > 2) {
        if (std::getenv("DBG_UNROLL")) fprintf(stderr, "[unroll] %s body.size=%zu >2 skip\n",
            loop.header->getName().c_str(), loop.body.size());
        return false; // header + body
    }

    // 找到 body BB（非 header 的那个）
    IR::BasicBlock* bodyBB = nullptr;
    for (auto* bb : loop.body) {
        if (bb != loop.header) {
            if (bodyBB) return false; // 超过 1 个非 header BB
            bodyBB = bb;
        }
    }
    if (!bodyBB) return false;
    if (!isSimpleBody(bodyBB)) {
        if (std::getenv("DBG_UNROLL")) fprintf(stderr, "[unroll] %s not simple body\n",
            loop.header->getName().c_str());
        return false;
    }

    // 推导或使用预设迭代次数
    int tc = loop.tripCount;
    if (tc < 0) tc = inferTripCount(loop.header, loop.body, func);
    loop.tripCount = tc;
    if (std::getenv("DBG_UNROLL")) fprintf(stderr, "[unroll] %s tc=%d\n",
        loop.header->getName().c_str(), tc);
    // P2: 上界从 64 放宽到 256，允许大循环部分展开
    if (tc < 2 || tc > 256) return false;

    // 按从大到小尝试因子，最大 16×（P2: 原 8×）
    // 包含质数因子 5 和 7，支持 tc 为质数的循环完全展开
    // （如 conv2d 的 KSIZE=5 循环，如果循环体是单 BB）
    unsigned factor = 0;
    static const unsigned candidates[] = {16, 12, 8, 6, 4, 3, 2};
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

    // ★ P2: 寄存器压力检查（改进版：区分不变量与循环携带值）
    //   原版 ev*factor 公式假设每个外部值都需要 factor 份拷贝，过于保守。
    //   实际上：
    //     - 不变量（定义在循环外）：只需 1 份寄存器，与 factor 无关
    //     - 循环携带 PHI（header 中的 PHI）：展开后每迭代需 1 份寄存器
    //   新公式：live ≈ invariants + factor * loopCarriedPhis ≤ 18
    //   （BOOM 有 20 个可用寄存器，留 2 个临时寄存器）
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
        size_t invariants = externalVals.size();

        // 统计 header 中的循环携带 PHI 数量（有 back-edge 来自 bodyBB 的 PHI）
        size_t loopCarriedPhis = 0;
        for (auto& inst : loop.header->getInstructions()) {
            if (inst->getOpcode() != IR::Instruction::Opcode::PHI) continue;
            for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
                auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
                if (predBB == bodyBB) {
                    ++loopCarriedPhis;
                    break;
                }
            }
        }

        // 改进后的寄存器压力公式：
        //   live = invariants + factor * loopCarriedPhis
        //   上限 18（BOOM 20 个可用寄存器，留 2 个临时）
        //   同时保留硬上限：invariants ≤ 12（避免不变量本身就溢出）
        const size_t REG_BUDGET = 18;
        const size_t INVARIANT_LIMIT = 12;

        auto liveRegs = [&](unsigned f) -> size_t {
            return invariants + f * loopCarriedPhis;
        };

        // factor=16 需要更严格：invariants + 16*lc ≤ 18 → 仅 lc=0/1 且 invariants 小时可行
        if (factor >= 16 && liveRegs(factor) > REG_BUDGET) {
            factor = 12;
        }
        // 逐步降级，找最大的满足 liveRegs(f) ≤ REG_BUDGET 的因子
        if (liveRegs(factor) > REG_BUDGET || invariants > INVARIANT_LIMIT) {
            unsigned downgraded = 0;
            for (unsigned f : {12, 8, 6, 4, 3, 2}) {
                if (tc % f == 0 && tc >= f && liveRegs(f) <= REG_BUDGET &&
                    invariants <= INVARIANT_LIMIT) {
                    downgraded = f;
                    break;
                }
            }
            if (downgraded == 0) return false;
            factor = downgraded;
        }
    }

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

    // ★ 更新 latch COND_BR 的条件为最后一次克隆的 ICMP（alloca-IV 自循环路径）
    //   旋转后的自循环 body 末尾是 COND_BR(icmp, body, exit)，展开后 COND_BR 仍用
    //   原始 ICMP（检查第一份 IV 值），导致边界检查错误（多执行迭代→越界）。
    //   修复：将 COND_BR 的条件更新为 valueMap 中 ICMP 的最新克隆。
    {
        auto* term = bodyBB->getTerminator();
        if (term && term->getOpcode() == IR::Instruction::Opcode::COND_BR) {
            auto* succ1 = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
            auto* succ2 = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
            bool isSelfLoop = (succ1 == bodyBB || succ2 == bodyBB);
            if (isSelfLoop) {
                auto* condInst = dynamic_cast<IR::Instruction*>(term->getOperand(0));
                if (condInst) {
                    auto vIt = valueMap.find(condInst);
                    if (vIt != valueMap.end()) {
                        term->setOperand(0, vIt->second);
                    }
                }
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
