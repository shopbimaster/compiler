// ================================================================
// P9: 软件流水（Software Pipelining）—— 跨迭代 LOAD 预取
// ----------------------------------------------------------------
// 在 IR 层将单 BB 自循环的 LOAD 预取到上一迭代末尾，隐藏 BOOM 4 周期
// load-use 延迟。ROB=16 较小，编译器暴露跨迭代 ILP 边际收益大。
//
// 算法（body-split + 守卫预取）：
//   原始（post-LoopRotation 自循环）：
//     guard: if(!(i<N)) goto exit
//     body:  t=load a[i]; ...use t...; i=i+1; if(i<N) goto body; goto exit
//
//   变换后：
//     guard: if(!(i<N)) goto exit
//            t_pref=load a[i]                 // ★ 首迭代预取（安全：i<N 已检查，tripCount>=2）
//     body:  t=phi(t_pref from guard, t_pref_next from body.prefetch)
//            ...use t...; i=i+1; if(i<N) goto body.prefetch; goto exit
//     body.prefetch: t_pref_next=load a[i]; goto body   // ★ 下迭代预取（i 已自增）
//
// 正确性要点：
//   - 仅 tripCount >= 2 的计数循环（SCEV 已知）→ 首迭代预取 a[init] 安全
//   - body.prefetch 的预取在 body.use 的 COND_BR 检查 i<N 之后执行 → 无越界
//   - LOAD 地址用原 GEP，移到 body.prefetch 后 i 已是下迭代值（IV PHI 的 backVal）
//   - 所有 body 内 PHI 的 [val, body] 自环 incoming 更新为 [val, body.prefetch]
//
// 候选约束（保守）：
//   - 自循环（loop.body = {header}, header == latch == body）
//   - body 无 CALL、无 COND_BR（除末尾 latch）、IV step 是常量
//   - IV 可以是 SSA PHI 或 alloca LOAD（LoopRotation 已支持 alloca IV 旋转）
//   - body 恰好 1 个数据 LOAD（跳过 alloca LOAD），其地址传递依赖 IV
//   - body 的 STORE（若有）基址与 LOAD 基址不同（简单别名分析）
//   - body 指令数 ≤ 40，LOAD 的所有 use 都在 body 内
//
// 开关：SWP_OFF=1 或 OPT_DISABLE=softwarePipelining
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

bool swpDisabled() {
    if (const char* v = std::getenv("SWP_OFF"))
        return std::string(v) == "1";
    return false;
}

// ---- 递归克隆地址计算链，将 ivPhi 替换为 subst ----
// 克隆 inst 及其在 body 内、传递依赖 ivPhi 的操作数。
// 不依赖 ivPhi 的 body 内指令也克隆（保持独立计算，DCE 后清理）。
// 返回克隆指令；遇不支持的 opcode 返回 nullptr。
// 克隆的指令按依赖序（叶子优先）收集到 order。
IR::Instruction* cloneAddrChain(
    IR::Instruction* inst,
    IR::Value* ivPhi,
    IR::Value* subst,
    IR::BasicBlock* body,
    std::unordered_map<IR::Instruction*, IR::Instruction*>& cache,
    std::vector<IR::Instruction*>& order) {

    auto it = cache.find(inst);
    if (it != cache.end()) return it->second;

    auto op = inst->getOpcode();
    IR::Instruction* cloned = nullptr;

    auto cloneOp = [&](IR::Value* v) -> IR::Value* {
        if (v == ivPhi) return subst;
        // ★ ALLOCA-based IV：从该 alloca 的 LOAD 结果直接替换为 subst（预取值），
        //   不克隆 LOAD 指令（避免从 subst 地址加载，那会读错地址）。
        if (auto* opInst = dynamic_cast<IR::Instruction*>(v)) {
            if (opInst->getOpcode() == Opc::LOAD && opInst->getNumOperands() > 0 &&
                opInst->getOperand(0) == ivPhi) return subst;
        }
        // BasicBlock 操作数（PHI/BR）不克隆
        if (dynamic_cast<IR::BasicBlock*>(v)) return v;
        if (auto* opInst = dynamic_cast<IR::Instruction*>(v)) {
            if (opInst->getParent() == body) {
                return cloneAddrChain(opInst, ivPhi, subst, body, cache, order);
            }
        }
        return v;  // 常量/全局/参数/外部 BB 指令 → 共享
    };

    // ★ ALLOCA-based IV 的 LOAD 替换由 cloneOp 处理（操作数级替换为 subst），
    //   cloneAddrChain 不会直接被调用于 alloca-LOAD（cloneOp 会先拦截返回 subst）。

    if (op == Opc::LOAD) {
        cloned = IR::Instruction::createLoad(
            inst->getType(), cloneOp(inst->getOperand(0)),
            inst->getName() + ".swp");
    } else if (op == Opc::GETELEMENTPTR) {
        std::vector<IR::Value*> indices;
        for (unsigned i = 1; i < inst->getNumOperands(); ++i)
            indices.push_back(cloneOp(inst->getOperand(i)));
        auto* ptrTy = dynamic_cast<IR::PointerType*>(inst->getOperand(0)->getType());
        IR::Type* pointee = ptrTy ? ptrTy->getPointeeType() : IR::IntegerType::I32;
        cloned = IR::Instruction::createGetElementPtr(
            pointee, cloneOp(inst->getOperand(0)), indices, inst->getName() + ".swp");
    } else if (op == Opc::ADD || op == Opc::SUB || op == Opc::MUL ||
               op == Opc::AND || op == Opc::OR || op == Opc::XOR ||
               op == Opc::SHL || op == Opc::ASHR || op == Opc::SDIV || op == Opc::SREM) {
        cloned = IR::Instruction::createBinOp(
            op, inst->getType(), inst->getName() + ".swp",
            cloneOp(inst->getOperand(0)),
            inst->getNumOperands() >= 2 ? cloneOp(inst->getOperand(1)) : nullptr);
    } else if (op == Opc::ZEXT || op == Opc::SEXT || op == Opc::TRUNC ||
               op == Opc::SITOFP || op == Opc::FPTOSI) {
        cloned = IR::Instruction::createCast(
            op, inst->getType(), cloneOp(inst->getOperand(0)), inst->getName() + ".swp");
    } else {
        return nullptr;  // 不支持的 opcode（含 PHI/CALL/STORE/BR 等）
    }

    cache[inst] = cloned;
    order.push_back(cloned);
    return cloned;
}

// ---- 检查 value 是否传递依赖 ivPhi（仅在 body 内追踪）----
bool dependsOnIv(IR::Value* v, IR::Value* ivPhi, IR::BasicBlock* body,
                 std::unordered_set<IR::Value*>& visited) {
    if (v == ivPhi) return true;
    // ★ ALLOCA-based IV（SCEV 返回 ALLOCA 本身）：从该 alloca 的 LOAD 即为 IV 使用
    if (auto* loadInst = dynamic_cast<IR::Instruction*>(v)) {
        if (loadInst->getOpcode() == Opc::LOAD && loadInst->getNumOperands() > 0 &&
            loadInst->getOperand(0) == ivPhi) return true;
    }
    if (visited.count(v)) return false;
    visited.insert(v);
    auto* inst = dynamic_cast<IR::Instruction*>(v);
    if (!inst) return false;
    if (inst->getParent() != body) return false;
    for (unsigned i = 0; i < inst->getNumOperands(); ++i) {
        if (dynamic_cast<IR::BasicBlock*>(inst->getOperand(i))) continue;
        if (dependsOnIv(inst->getOperand(i), ivPhi, body, visited))
            return true;
    }
    return false;
}

// ---- 找到指针的"基址"：GlobalVariable / ALLOCA / nullptr(未知) ----
IR::Value* findBase(IR::Value* ptr) {
    if (auto* gv = dynamic_cast<IR::GlobalVariable*>(ptr)) return gv;
    if (auto* inst = dynamic_cast<IR::Instruction*>(ptr)) {
        if (inst->getOpcode() == Opc::GETELEMENTPTR) {
            return findBase(inst->getOperand(0));
        }
        if (inst->getOpcode() == Opc::ALLOCA) return inst;
    }
    return nullptr;
}

// ---- 候选信息 ----
struct PipelineCandidate {
    IR::BasicBlock* body = nullptr;       // 自循环体（header == latch == body）
    IR::BasicBlock* guard = nullptr;      // 唯一非自环前驱（preheader/guard）
    IR::BasicBlock* exitBB = nullptr;     // 循环出口
    int selfOpIdx = -1;                   // COND_BR 中自环目标的操作数下标（1 或 2）
    IR::Instruction* ivPhi = nullptr;     // IV PHI
    IR::Value* initVal = nullptr;         // IV 初始值（来自 guard）
    IR::Value* backVal = nullptr;         // IV 回边值（%i.next，ADD 结果）
    IR::ConstantInt* step = nullptr;      // IV 步长常量
    int64_t tripCount = -1;               // 迭代次数（SCEV）
    IR::Instruction* loadInst = nullptr;  // 待流水的 LOAD
};

// ---- 分析候选 ----
bool analyzeCandidate(IR::Function* func, const NaturalLoop& loop,
                      PipelineCandidate& out) {
    static const bool dbg = [] {
        const char* v = std::getenv("DBG_SWP");
        return v && std::string(v) == "1";
    }();
    auto dbgReason = [&](const char* reason) {
        if (dbg) fprintf(stderr, "[swp] reject %s: %s\n",
                         func->getName().c_str(), reason);
    };

    // 1. 自循环：body = {header}, header == latch
    if (loop.body.size() != 1) { dbgReason("body.size()!=1"); return false; }
    out.body = loop.header;
    if (loop.latch != out.body) { dbgReason("latch!=header"); return false; }

    // 2. body 末尾是 COND_BR，某一后继是 body 自身（自环），另一是 exit
    auto* term = out.body->getTerminator();
    if (!term || term->getOpcode() != Opc::COND_BR) { dbgReason("no COND_BR term"); return false; }
    auto* thenBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
    auto* elseBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
    if (!thenBB || !elseBB) { dbgReason("bad cond_br operands"); return false; }
    if (thenBB == out.body && elseBB != out.body) {
        out.exitBB = elseBB; out.selfOpIdx = 1;
    } else if (elseBB == out.body && thenBB != out.body) {
        out.exitBB = thenBB; out.selfOpIdx = 2;
    } else {
        dbgReason("not self-loop");
        return false;
    }

    // 3. 找唯一非自环前驱（guard/preheader）
    auto preds = buildPredecessors(func);
    auto it = preds.find(out.body);
    if (it == preds.end()) { dbgReason("no preds"); return false; }
    IR::BasicBlock* guard = nullptr;
    int nonSelfCount = 0;
    for (auto* p : it->second) {
        if (p != out.body) { guard = p; ++nonSelfCount; }
    }
    if (nonSelfCount != 1 || !guard) { dbgReason("no single guard"); return false; }
    out.guard = guard;

    // 4. SCEV 归纳分析：要求 tripCount >= 2，IV 是 PHI / LOAD / ALLOCA，step 是常量
    //    SCEV 对 alloca-based IV 返回 ALLOCA 指针本身（非 LOAD），需单独处理。
    auto scev = analyzeLoopInduction(loop, func);
    if (scev.tripCount < 2) { dbgReason("tripCount<2"); return false; }
    out.ivPhi = dynamic_cast<IR::Instruction*>(scev.var);
    if (!out.ivPhi) { dbgReason("IV not inst"); return false; }
    auto ivOp = out.ivPhi->getOpcode();
    bool ivIsLoad = (ivOp == Opc::LOAD);
    bool ivIsAlloca = (ivOp == Opc::ALLOCA);
    if (ivOp != Opc::PHI && !ivIsLoad && !ivIsAlloca) { dbgReason("IV not PHI/LOAD/ALLOCA"); return false; }
    // ALLOCA 在 entry 块，不在 body 中；PHI/LOAD 必须在 body 中
    if (!ivIsAlloca && out.ivPhi->getParent() != out.body) { dbgReason("IV not in body"); return false; }
    out.initVal = scev.start;
    out.step = dynamic_cast<IR::ConstantInt*>(scev.step);
    if (!out.step) { dbgReason("no const step"); return false; }
    out.tripCount = scev.tripCount;

    // 5. 找 IV 的回边值
    //    ALLOCA/LOAD-form：在 body 中找对同一 alloca 的 STORE，其源值即回边值
    //    PHI-form：找 [val, body] incoming
    out.backVal = nullptr;
    if (ivOp == Opc::PHI) {
        for (unsigned i = 0; i + 1 < out.ivPhi->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(out.ivPhi->getOperand(i + 1));
            if (predBB == out.body) {
                out.backVal = out.ivPhi->getOperand(i);
                break;
            }
        }
    } else {
        // LOAD-form: alloca = ivPhi->getOperand(0)
        // ALLOCA-form: alloca = ivPhi 本身
        IR::Value* allocaPtr = ivIsAlloca ? out.ivPhi : out.ivPhi->getOperand(0);
        for (auto& inst : out.body->getInstructions()) {
            if (inst->getOpcode() == Opc::STORE && inst->getNumOperands() >= 2 &&
                inst->getOperand(1) == allocaPtr) {
                out.backVal = inst->getOperand(0);
                break;
            }
        }
    }
    if (!out.backVal) { dbgReason("no backVal"); return false; }

    // 5b. LOAD/ALLOCA-form IV：若 SCEV 未给出 initVal，从 guard 块的 alloca LOAD 获取
    if (ivOp != Opc::PHI && !out.initVal) {
        IR::Value* allocaPtr = ivIsAlloca ? out.ivPhi : out.ivPhi->getOperand(0);
        for (auto& inst : out.guard->getInstructions()) {
            if (inst->getOpcode() == Opc::LOAD && inst->getOperand(0) == allocaPtr) {
                out.initVal = inst.get();
                break;
            }
        }
    }
    if (!out.initVal) { dbgReason("no initVal"); return false; }

    // 6. body 大小限制
    if (out.body->size() > 40) { dbgReason("body too large"); return false; }

    // 7. body 无 CALL、无额外 COND_BR（除末尾 latch）
    //    统计数据 LOAD（跳过 alloca LOAD），找恰好 1 个 IV 依赖的 LOAD
    IR::Instruction* ivLoad = nullptr;
    int totalLoads = 0;
    for (auto& inst : out.body->getInstructions()) {
        auto op = inst->getOpcode();
        if (op == Opc::CALL) { dbgReason("has CALL"); return false; }
        if (op == Opc::COND_BR && inst.get() != term) { dbgReason("extra COND_BR"); return false; }
        if (op == Opc::BR) { dbgReason("has BR"); return false; }
        if (op == Opc::LOAD) {
            // 跳过 alloca LOAD（IV load / latch load，非数据 LOAD）
            auto* ptr = inst->getOperand(0);
            auto* ptrInst = dynamic_cast<IR::Instruction*>(ptr);
            if (ptrInst && ptrInst->getOpcode() == Opc::ALLOCA) continue;

            ++totalLoads;
            std::unordered_set<IR::Value*> visited;
            if (dependsOnIv(inst->getOperand(0), out.ivPhi, out.body, visited)) {
                ivLoad = inst.get();
            }
        }
    }
    if (totalLoads != 1 || !ivLoad) { dbgReason("not exactly 1 IV-LOAD"); return false; }
    out.loadInst = ivLoad;

    // 8. LOAD 的所有 use 都在 body 内
    for (auto& use : ivLoad->getUses()) {
        auto* userInst = dynamic_cast<IR::Instruction*>(use.user);
        if (!userInst) { dbgReason("LOAD use not inst"); return false; }
        if (userInst->getParent() != out.body) { dbgReason("LOAD use outside body"); return false; }
    }

    // 9. STORE 别名检查：LOAD 基址与所有 STORE 基址不同
    IR::Value* loadBase = findBase(ivLoad->getOperand(0));
    if (!loadBase) { dbgReason("LOAD base unknown"); return false; }
    for (auto& inst : out.body->getInstructions()) {
        if (inst->getOpcode() != Opc::STORE) continue;
        if (inst->getNumOperands() < 2) continue;
        IR::Value* storeBase = findBase(inst->getOperand(1));
        if (!storeBase) { dbgReason("STORE base unknown"); return false; }
        if (storeBase == loadBase) { dbgReason("STORE aliases LOAD"); return false; }
    }

    if (dbg) fprintf(stderr, "[swp] ACCEPT %s: body=%s tc=%lld\n",
                     func->getName().c_str(), out.body->getName().c_str(),
                     (long long)out.tripCount);
    return true;
}

// ---- 执行变换 ----
bool doPipeline(IR::Function* func, const PipelineCandidate& cand) {
    auto* body = cand.body;
    auto* guard = cand.guard;
    auto* loadInst = cand.loadInst;

    // === 1. 克隆 LOAD + 地址链用于 guard 预取（ivPhi → initVal）===
    std::unordered_map<IR::Instruction*, IR::Instruction*> cacheGuard;
    std::vector<IR::Instruction*> guardOrder;
    IR::Instruction* tPref = cloneAddrChain(loadInst, cand.ivPhi, cand.initVal,
                                             body, cacheGuard, guardOrder);
    if (!tPref) return false;  // 地址链含不支持的 opcode

    // === 2. 创建 body.prefetch BB ===
    auto* bodyPrefetch = func->createBlock(body->getName() + ".swp_prefetch");

    // === 3. 克隆 LOAD + 地址链用于 body.prefetch（ivPhi → backVal）===
    std::unordered_map<IR::Instruction*, IR::Instruction*> cachePref;
    std::vector<IR::Instruction*> prefOrder;
    IR::Instruction* tPrefNext = cloneAddrChain(loadInst, cand.ivPhi, cand.backVal,
                                                 body, cachePref, prefOrder);
    if (!tPrefNext) return false;

    // === 4. 在 guard 的 terminator 之前插入首迭代预取 ===
    {
        auto termIt = guard->end(); --termIt;
        auto guardTerm = std::move(*termIt);
        guard->erase(termIt);
        for (auto* inst : guardOrder) {
            guard->pushBack(inst);
        }
        guard->pushBack(guardTerm.release());
    }

    // === 5. 填充 body.prefetch：克隆的地址链 + LOAD + br body ===
    for (auto* inst : prefOrder) {
        bodyPrefetch->pushBack(inst);
    }
    bodyPrefetch->pushBack(IR::Instruction::createBr(body));

    // === 6. 在 body 起始插入 t PHI ===
    //    incoming: [tPref, guard], [tPrefNext, body.prefetch]
    auto* tPhi = IR::Instruction::createPhi(loadInst->getType(), "t.swp", 4);
    tPhi->addOperand(tPref);
    tPhi->addOperand(guard);
    tPhi->addOperand(tPrefNext);
    tPhi->addOperand(bodyPrefetch);
    body->insert(body->begin(), tPhi);

    // === 7. 替换原 LOAD 的所有 use 为 t PHI ===
    loadInst->replaceAllUsesWith(tPhi);

    // === 8. 改写 body 末尾 COND_BR：自环目标 body → body.prefetch ===
    auto* term = body->getTerminator();
    term->setOperand(cand.selfOpIdx, bodyPrefetch);

    // === 9. 更新 body 内所有 PHI 的 [val, body] 自环 incoming → [val, body.prefetch] ===
    //    （IV PHI 及归约累加器 PHI 等；扫描全块，遵循"PHI 不必在块首"教训）
    for (auto& inst : body->getInstructions()) {
        if (inst->getOpcode() != Opc::PHI) continue;
        for (unsigned i = 0; i + 1 < inst->getNumOperands(); i += 2) {
            auto* predBB = dynamic_cast<IR::BasicBlock*>(inst->getOperand(i + 1));
            if (predBB == body) {
                inst->setOperand(i + 1, bodyPrefetch);
            }
        }
    }

    // 原 LOAD 及其专属地址计算已无 use（tPhi 取代 LOAD），留给 DCE 清理。
    return true;
}

// ---- 扫描自循环：BB 末尾 COND_BR 某一后继是自身 ----
// ★ 不依赖 findNaturalLoops（后者用 strictlyDominates 会漏检自循环 B→B，
//   且全局修复会影响 LoopUnrolling/ReductionSplitting 等其他 Pass 导致
//   matmul3 等用例误编译）。SWP 在此独立扫描自循环，隔离影响范围。
std::vector<NaturalLoop> findSelfLoops(IR::Function* func) {
    std::vector<NaturalLoop> result;
    for (auto& bb : func->getBlocks()) {
        auto* term = bb->getTerminator();
        if (!term || term->getOpcode() != Opc::COND_BR) continue;
        auto* thenBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(1));
        auto* elseBB = dynamic_cast<IR::BasicBlock*>(term->getOperand(2));
        if (thenBB == bb.get() || elseBB == bb.get()) {
            NaturalLoop loop;
            loop.header = bb.get();
            loop.latch = bb.get();
            loop.body.insert(bb.get());
            result.push_back(std::move(loop));
        }
    }
    return result;
}

} // namespace

bool softwarePipelining(IR::Module* mod) {
    if (swpDisabled()) return false;

    static const bool dbg = [] {
        const char* v = std::getenv("DBG_SWP");
        return v && std::string(v) == "1";
    }();

    bool changed = false;
    for (auto& func : mod->getFunctions()) {
        if (func->isExternal()) continue;

        // ★ 直接扫描自循环，不依赖 findNaturalLoops（避免影响其他 Pass）
        auto loops = findSelfLoops(func.get());
        if (dbg && !loops.empty()) {
            fprintf(stderr, "[swp] func %s: %zu self-loops\n",
                    func->getName().c_str(), loops.size());
        }
        for (auto& loop : loops) {
            if (dbg) fprintf(stderr, "[swp]   self-loop header=%s bodySize=%zu latch=%s\n",
                             loop.header ? loop.header->getName().c_str() : "?",
                             loop.body.size(),
                             loop.latch ? loop.latch->getName().c_str() : "?");

            PipelineCandidate cand;
            if (analyzeCandidate(func.get(), loop, cand)) {
                if (doPipeline(func.get(), cand)) {
                    changed = true;
                }
            }
        }
    }
    return changed;
}

} // namespace Opt
