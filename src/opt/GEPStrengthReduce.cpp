// ================================================================
// GEP 强度削弱（GEP Strength Reduction）
// 将循环中以归纳变量为索引的 GEP 地址计算替换为累加指针
//
// 核心思想：
//   循环中 A[k] 的地址 = base + k * stride
//   每次 k 增加 step，地址增加 step * stride
//   用一个指针 PHI 每次迭代 += step * stride，消除乘法
//
// 支持模式（SSA 形式，IV 为 PHI 节点）：
//   模式1: GEP T* base, iv          — 单索引，iv 是归纳变量
//   模式2: GEP T* base, 0, iv       — 双索引，第一个为常量0，第二个为 iv
//
// 变换示例：
//   before:                         after:
//   header:                         header:
//     %iv = phi [0, pre], [next, latch]   %iv = phi [0, pre], [next, latch]
//     ...                                  %ptr = phi [init, pre], [inc, latch]
//   body:                           body:
//     %gep = GEP T* base, iv          /* GEP 已删除，用 %ptr 替代 */
//     load %gep                        load %ptr
//   latch:                          latch:
//     %next = add iv, step             %next = add iv, step
//                                        %inc = GEP T* %ptr, step
//   preheader:                      preheader:
//     br header                         %init = GEP T* base, 0  ; or iv_start
//                                      br header
//
// 安全检查：
//   1. IV 必须是 PHI 节点，且有常量步长
//   2. GEP 的基指针必须是循环不变量
//   3. GEP 的所有使用者必须在循环内
//   4. 循环必须有唯一的 preheader 和 latch
//   5. 非归纳变量索引必须是常量
// ================================================================

#include "opt/Optimizer.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {
namespace {

static int gepLsrCounter = 0;

using Opc = IR::Instruction::Opcode;

// ================================================================
// 在 BB 的 terminator 之前插入指令
// ================================================================
void insertBeforeTerminator(IR::BasicBlock* bb, IR::Instruction* inst) {
    auto it = bb->end();
    if (it != bb->begin()) {
        --it; // 指向最后一条指令（terminator）
    }
    bb->insert(it, inst);
}

// ================================================================
// 在 BB 开头（所有 PHI 之后）插入指令
// ================================================================
void insertAfterPhis(IR::BasicBlock* bb, IR::Instruction* inst) {
    auto it = bb->begin();
    while (it != bb->end() && (*it)->getOpcode() == Opc::PHI) {
        ++it;
    }
    bb->insert(it, inst);
}

// ================================================================
// 检查值是否是循环不变量
// ================================================================
bool isLoopInvariantValue(IR::Value* val, const NaturalLoop& loop) {
    // 常量是不变量
    if (dynamic_cast<IR::ConstantInt*>(val)) return true;
    if (dynamic_cast<IR::ConstantFloat*>(val)) return true;
    // 全局变量是不变量
    if (dynamic_cast<IR::GlobalVariable*>(val)) return true;
    // 参数是不变量
    if (dynamic_cast<IR::Argument*>(val)) return true;

    // 指令：检查是否定义在循环外
    if (auto* inst = dynamic_cast<IR::Instruction*>(val)) {
        auto* bb = inst->getParent();
        if (!bb) return true; // 无父 BB，视为不变量
        return !loop.body.count(bb);
    }
    return true;
}

// ================================================================
// GEP 强度削弱候选
// ================================================================
struct GEPCandidate {
    IR::Instruction* gep;
    int ivPos;           // IV 在 GEP 操作数中的位置（1 或 2）
    IR::Type* pointee;   // 基指针的 pointee 类型
    int64_t constOffset = 0;  // IV+const 中的常量偏移（0 = 直接 IV）
};

// ================================================================
// 对单个循环执行 GEP 强度削弱
// allHeaders: 函数中所有循环的 header 集合，用于检测嵌套循环
// ================================================================
bool reduceGEPsInLoop(const NaturalLoop& loop, IR::Function* func,
                      const std::unordered_set<IR::BasicBlock*>& allHeaders) {
    auto info = analyzeLoopInduction(loop, func);
    if (!info.var || !info.start || !info.step) return false;

    // IV 必须是 PHI 节点（SSA 形式）
    auto* ivInst = dynamic_cast<IR::Instruction*>(info.var);
    if (!ivInst || ivInst->getOpcode() != Opc::PHI) return false;

    // 步长必须是常量
    auto* stepCI = dynamic_cast<IR::ConstantInt*>(info.step);
    if (!stepCI) return false;

    // 步长不能为 0
    if (stepCI->getValue() == 0) return false;

    auto* startVal = info.start;
    auto* ivPhi = info.var;

    // 查找 preheader（header 的唯一循环外前驱）
    auto preds = buildPredecessors(func);
    auto succs = buildSuccessors(func);
    IR::BasicBlock* preheader = nullptr;
    for (auto* p : preds[loop.header]) {
        if (!loop.body.count(p)) {
            if (preheader) return false; // 多个循环外前驱，无唯一 preheader
            preheader = p;
        }
    }
    if (!preheader) return false;

    // 检查多回边循环（while 含 continue 会产生多条回边）
    // 多回边循环有多个 latch（分支回 header 的块），PHI 需要来自所有 latch 的
    // incoming value。当前实现只为单个 latch 创建 incGEP，会导致 PHI 缺少
    // incoming value → SEGFAULT（19_search 根因）。
    // 安全策略：跳过多回边循环。
    std::vector<IR::BasicBlock*> latches;
    for (auto* bb : loop.body) {
        for (auto* s : succs[bb]) {
            if (s == loop.header) {
                latches.push_back(bb);
                break;
            }
        }
    }
    if (latches.size() != 1) return false; // 跳过多回边循环
    auto* latch = latches[0];

    // 仅对递减外层循环跳过 LSR
    // 原因：递减循环（SUB 模式，step < 0）的 LSR 在 header 创建 lsr.ptr PHI，
    // 其活跃区间从 header 延伸到 latch，横跨整个循环体包括内层循环。
    // 递减循环的指针递减方向与通常的地址增长相反，寄存器分配器可能更难处理，
    // 导致更多溢出。实测：h-5 LU 分解的递减 i 循环 LSR 导致 +100-200ms 回退。
    // 而递增循环（ADD 模式，step > 0）的外层 LSR 是有益的，不跳过。
    if (stepCI->getValue() < 0) {
        for (auto* bb : loop.body) {
            if (bb == loop.header) continue;
            if (allHeaders.count(bb)) return false; // 递减外层循环 → 跳过
        }
    }

    // 收集所有可强度削弱的 GEP
    std::vector<GEPCandidate> candidates;

    for (auto* bb : loop.body) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() != Opc::GETELEMENTPTR) continue;
            auto* gep = inst.get();
            unsigned numOps = gep->getNumOperands();

            // 判断 GEP 模式并确定 IV 位置
            // 支持三种索引形式：
            //   1. 直接 IV:       GEP base, iv       或 GEP base, 0, iv
            //   2. IV+const:      GEP base, (iv+C)   或 GEP base, 0, (iv+C)
            //   3. const+IV:      GEP base, (C+iv)   或 GEP base, 0, (C+iv)
            int ivPos = -1;
            int64_t constOffset = 0;

            // 辅助函数：检测值是否是 IV 或 (IV+const)，返回是否匹配
            // 注意：仅支持 ADD(iv, const)，不支持 SUB(iv, const)。
            // 原因：sub(iv, const) 如 i-1 常出现在递归函数（如 knapsack_naive）中，
            // 在递归函数中创建 LSR 指针会增加寄存器保存开销（2^N 放大），
            // 实测 knapsack_naive +102ms 回归。ADD(iv, const) 如 k+1 出现在
            // 嵌套循环中（如 h-8），LSR 收益大于开销。
            auto matchIvOrOffset = [&](IR::Value* idx) -> bool {
                // 直接 IV
                if (idx == ivPhi) {
                    constOffset = 0;
                    return true;
                }
                // add(iv, const) 或 add(const, iv)
                if (auto* addInst = dynamic_cast<IR::Instruction*>(idx)) {
                    if (addInst->getOpcode() == Opc::ADD) {
                        auto* op0 = addInst->getOperand(0);
                        auto* op1 = addInst->getOperand(1);
                        if (op0 == ivPhi) {
                            if (auto* c = dynamic_cast<IR::ConstantInt*>(op1)) {
                                constOffset = c->getValue();
                                return true;
                            }
                        } else if (op1 == ivPhi) {
                            if (auto* c = dynamic_cast<IR::ConstantInt*>(op0)) {
                                constOffset = c->getValue();
                                return true;
                            }
                        }
                    }
                }
                return false;
            };

            if (numOps == 2) {
                // 模式1: GEP T* base, idx
                if (matchIvOrOffset(gep->getOperand(1))) ivPos = 1;
            } else if (numOps == 3) {
                // 模式2: GEP T* base, 0, idx
                auto* idx0 = dynamic_cast<IR::ConstantInt*>(gep->getOperand(1));
                if (idx0 && idx0->getValue() == 0 && matchIvOrOffset(gep->getOperand(2))) {
                    ivPos = 2;
                }
            }

            if (ivPos < 0) continue;

            // 检查基指针是循环不变量
            auto* base = gep->getOperand(0);
            if (!isLoopInvariantValue(base, loop)) continue;

            // 检查所有使用者都在循环内
            bool allUsesInLoop = true;
            for (auto& use : gep->getUses()) {
                auto* user = dynamic_cast<IR::Instruction*>(use.user);
                if (!user) { allUsesInLoop = false; break; }
                auto* userBB = user->getParent();
                if (!userBB || !loop.body.count(userBB)) {
                    allUsesInLoop = false;
                    break;
                }
            }
            if (!allUsesInLoop) continue;

            // 获取 pointee 类型
            auto* basePtrTy = dynamic_cast<IR::PointerType*>(base->getType());
            if (!basePtrTy) continue;
            auto* pointee = basePtrTy->getPointeeType();

            candidates.push_back({gep, ivPos, pointee, constOffset});
        }
    }

    if (candidates.empty()) return false;

    // GEP-LSR-2: Allow a small group of affine pointer recurrences in the
    // same loop.  Matrix and convolution kernels commonly index two or three
    // arrays with one IV, so requiring exactly one candidate leaves their hot
    // loops untouched.  Equivalent candidates share one recurrence.
    struct LSRCacheKey {
        IR::Value* base;
        int ivPos;
        IR::Type* gepType;
        int64_t constOffset;
        bool operator==(const LSRCacheKey& o) const {
            return base == o.base && ivPos == o.ivPos
                && gepType == o.gepType && constOffset == o.constOffset;
        }
    };
    struct LSRCacheKeyHash {
        size_t operator()(const LSRCacheKey& k) const {
            return reinterpret_cast<size_t>(k.base) ^ k.ivPos
                 ^ reinterpret_cast<size_t>(k.gepType)
                 ^ std::hash<int64_t>{}(k.constOffset);
        }
    };

    std::unordered_set<LSRCacheKey, LSRCacheKeyHash> distinctKeys;
    for (auto& cand : candidates) {
        distinctKeys.insert({cand.gep->getOperand(0), cand.ivPos,
                             cand.gep->getType(), cand.constOffset});
    }

    // Every chain adds a loop-carried pointer PHI.  Count recurrences already
    // created anywhere in this natural loop as well as the new group.  Loops
    // are processed innermost-first, so this preserves the hot inner chains
    // and prevents later outer-loop/iteration passes from exceeding the same
    // pressure budget in a nested region.
    constexpr size_t MAX_LSR_CHAINS = 3;
    size_t existingChains = 0;
    for (auto* bb : loop.body) {
        for (auto& inst : bb->getInstructions()) {
            if (inst->getOpcode() == Opc::PHI &&
                inst->getName().rfind("lsr.ptr.", 0) == 0) {
                ++existingChains;
            }
        }
    }
    if (existingChains + distinctKeys.size() > MAX_LSR_CHAINS) return false;

    // A call can clobber the caller-saved register holding each pointer and
    // turn a multi-chain reduction into repeated spill/reload traffic.  The
    // existing single-chain case remains allowed.
    if (distinctKeys.size() > 1) {
        for (auto* bb : loop.body) {
            for (auto& inst : bb->getInstructions()) {
                if (inst->getOpcode() == Opc::CALL) return false;
            }
        }
    }

    std::unordered_map<LSRCacheKey, IR::Instruction*, LSRCacheKeyHash> lsrCache;

    // 对每个候选执行强度削弱
    bool changed = false;
    for (auto& cand : candidates) {
        auto* gep = cand.gep;
        auto* base = gep->getOperand(0);
        auto* pointee = cand.pointee;
        auto* gepType = gep->getType();

        LSRCacheKey key{base, cand.ivPos, gepType, cand.constOffset};
        auto cacheIt = lsrCache.find(key);
        if (cacheIt != lsrCache.end()) {
            // 复用已有的 LSR 链
            gep->replaceAllUsesWith(cacheIt->second);
            gep->dropAllUses();
            auto* gepBB = gep->getParent();
            for (auto it = gepBB->begin(); it != gepBB->end(); ++it) {
                if (it->get() == gep) {
                    gepBB->erase(it);
                    break;
                }
            }
            changed = true;
            continue;
        }

        // 1. 在 preheader 中创建初始指针 GEP
        //    initial = GEP pointee base, ..., (iv_start + constOffset), ...
        //    当 constOffset != 0 时，需要先计算 start + constOffset
        IR::Value* initIdx = startVal;
        if (cand.constOffset != 0) {
            // 尝试常量折叠：如果 start 是常量，直接计算
            if (auto* startCI = dynamic_cast<IR::ConstantInt*>(startVal)) {
                int64_t folded = startCI->getValue() + cand.constOffset;
                initIdx = IR::ConstantInt::get(IR::IntegerType::I32, folded);
            } else {
                // 创建 ADD 指令：start + constOffset
                auto* offsetCI = IR::ConstantInt::get(IR::IntegerType::I32, cand.constOffset);
                auto* addInst = IR::Instruction::createBinOp(
                    Opc::ADD, IR::IntegerType::I32,
                    "lsr.off." + std::to_string(gepLsrCounter),
                    startVal, offsetCI);
                insertBeforeTerminator(preheader, addInst);
                initIdx = addInst;
            }
        }

        std::vector<IR::Value*> initIndices;
        if (cand.ivPos == 1) {
            // 模式1: GEP base, (iv_start + offset)
            initIndices.push_back(initIdx);
        } else {
            // 模式2: GEP base, 0, (iv_start + offset)
            initIndices.push_back(IR::ConstantInt::get(IR::IntegerType::I32, 0));
            initIndices.push_back(initIdx);
        }
        auto* initGEP = IR::Instruction::createGetElementPtr(
            pointee, base, initIndices, "lsr.init." + std::to_string(gepLsrCounter));
        insertBeforeTerminator(preheader, initGEP);

        // 2. 在 header 中创建指针 PHI（在所有现有 PHI 之后）
        //    先只添加 preheader 的 incoming value，latch 的稍后添加
        auto* ptrPhi = IR::Instruction::createPhi(gepType, "lsr.ptr." + std::to_string(gepLsrCounter), 2);
        ptrPhi->addOperand(initGEP);    // 来自 preheader 的值
        ptrPhi->addOperand(preheader);  // 来自 preheader
        insertAfterPhis(loop.header, ptrPhi);

        // 3. 在 latch 中创建增量 GEP
        //    ★ 关键：lsr.inc 必须使用 lsr.ptr 的类型（= gepType）的 pointee，
        //    而非原始 base 的 pointee！
        //    原因：lsr.ptr 的类型是原始 GEP 的结果类型（如 i32*），
        //    而原始 base 的类型可能是 [30 x i32]*。如果用原始 pointee
        //    创建 GEP [30 x i32]* %lsr.ptr, 0, step，但 lsr.ptr 实际是 i32*，
        //    会导致类型不匹配 → 代码生成器计算错误的 stride → SEGFAULT。
        //    修复：始终使用 Mode 1（单索引）在 lsr.ptr 上递增，
        //    pointee 取自 gepType（lsr.ptr 的类型）的 pointee。
        auto* gepPtrTy = dynamic_cast<IR::PointerType*>(gepType);
        if (!gepPtrTy) continue; // 不应发生，GEP 结果总是指针
        auto* gepPointee = gepPtrTy->getPointeeType();
        std::vector<IR::Value*> incIndices;
        incIndices.push_back(stepCI);
        auto* incGEP = IR::Instruction::createGetElementPtr(
            gepPointee, ptrPhi, incIndices, "lsr.inc." + std::to_string(gepLsrCounter));
        insertBeforeTerminator(latch, incGEP);

        // 4. 添加 PHI 的第二个 incoming value（来自 latch）
        ptrPhi->addOperand(incGEP);
        ptrPhi->addOperand(latch);

        // 5. 缓存此 LSR 链供后续相同模式的 GEP 复用
        lsrCache[key] = ptrPhi;
        gepLsrCounter++;  // 唯一命名

        // 6. 用指针 PHI 替换 GEP 的所有使用
        gep->replaceAllUsesWith(ptrPhi);
        gep->dropAllUses();

        // 7. 删除原始 GEP
        auto* gepBB = gep->getParent();
        for (auto it = gepBB->begin(); it != gepBB->end(); ++it) {
            if (it->get() == gep) {
                gepBB->erase(it);
                break;
            }
        }

        changed = true;
    }

    return changed;
}

} // namespace

// ================================================================
// 入口：对所有函数的所有循环执行 GEP 强度削弱
// 从最内层循环开始处理
// ================================================================
bool gepStrengthReduce(IR::Module* mod) {
    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;

        // 迭代直到收敛（强度削弱可能暴露新的机会）
        for (int iter = 0; iter < 3; ++iter) {
            bool iterChanged = false;
            auto loops = getLoopsInnermostFirst(func.get());
            // 收集所有循环 header，用于检测嵌套循环
            std::unordered_set<IR::BasicBlock*> allHeaders;
            for (auto& loop : loops) {
                allHeaders.insert(loop.header);
            }
            for (auto& loop : loops) {
                if (reduceGEPsInLoop(loop, func.get(), allHeaders)) {
                    iterChanged = true;
                }
            }
            if (iterChanged) {
                changed = true;
            } else {
                break;
            }
        }
    }
    return changed;
}

} // namespace Opt
