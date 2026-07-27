#pragma once

// ================================================================
// O1 优化器 — 常量折叠 + 死代码消除 + 窥孔优化
// O2 优化器 — 函数内联 + CSE + 循环不变量外提
// O3 优化器 — 代数化简 + 循环展开 + 尾递归消除
// ================================================================

#include "ir/IR.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Opt {

// ================================================================
// 共享支配者分析 — LICM/LoopUnroll/LoopInterchange 复用
// 避免多次重复计算支配树，减少编译时间
// ================================================================
using BBSet = std::unordered_set<IR::BasicBlock*>;
using DomMap = std::unordered_map<IR::BasicBlock*, BBSet>;
using PredMap = std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>>;
using SuccMap = std::unordered_map<IR::BasicBlock*, std::vector<IR::BasicBlock*>>;

PredMap buildPredecessors(IR::Function* func);
SuccMap buildSuccessors(IR::Function* func);
DomMap computeDominators(IR::Function* func);
DomMap computePostDominators(IR::Function* func);
bool strictlyDominates(IR::BasicBlock* a, IR::BasicBlock* b, const DomMap& dom);

// 计算立即支配者（idom）：每个块在支配树中的直接父节点
std::unordered_map<IR::BasicBlock*, IR::BasicBlock*> computeImmediateDominators(IR::Function* func, const DomMap& dom);

// 计算支配边界（Dominance Frontier）：DF[B] = 所有 B 支配其前驱但不严格支配的节点集合
// 这是 SSA 构造中 PHI 节点放置的关键分析
using DFMap = std::unordered_map<IR::BasicBlock*, BBSet>;
DFMap computeDominanceFrontier(IR::Function* func, const DomMap& dom,
    const std::unordered_map<IR::BasicBlock*, IR::BasicBlock*>& idom);

// ================================================================
// 共享分析
// ================================================================
std::unordered_set<IR::GlobalVariable*> readOnlyGlobalAnalysis(IR::Module* mod);
bool sparseConditionalConstantPropagation(IR::Module* mod);

// ================================================================
// 自然循环森林 — 共享循环分析
// ================================================================
struct NaturalLoop {
    IR::BasicBlock* header = nullptr;
    IR::BasicBlock* latch = nullptr;
    std::unordered_set<IR::BasicBlock*> body;
    std::vector<NaturalLoop*> subLoops;
    NaturalLoop* parent = nullptr;
    int depth = 0;
    std::vector<IR::BasicBlock*> exitBlocks;
    std::vector<IR::BasicBlock*> exitingBlocks;
};

std::vector<NaturalLoop> findNaturalLoops(IR::Function* func);
std::vector<NaturalLoop> getLoopsInnermostFirst(IR::Function* func);
bool isBlockInLoop(IR::BasicBlock* bb, const NaturalLoop& loop);
bool isInstInLoop(IR::Instruction* inst, const NaturalLoop& loop);
bool isLoopInvariantSimple(IR::Instruction* inst, const NaturalLoop& loop);

// ================================================================
// SCEV 分析
// ================================================================
struct InductionInfo {
    IR::Value* var = nullptr;      // 归纳变量（ALLOCA）
    IR::Value* start = nullptr;    // 初始值
    IR::Value* step = nullptr;     // 步长
    IR::Value* end = nullptr;      // 上界
    int64_t tripCount = -1;        // 迭代次数（-1 = 未知）
    bool isSignedCmp = true;
    std::string cmpKind;
};

InductionInfo analyzeLoopInduction(const NaturalLoop& loop, IR::Function* func);
void constantFolding(IR::Module* mod);
void deadCodeElimination(IR::Module* mod);
std::string peepholeOptimize(const std::string& asmCode);
void runO1(IR::Module* mod);

// ================================================================
// O2 Pass — 返回 bool 表示是否修改了 IR
// ================================================================
bool inlineExpansion(IR::Module* mod);
bool commonSubexpressionElimination(IR::Module* mod);
bool loopInvariantCodeMotion(IR::Module* mod);
void runO2(IR::Module* mod);

// ================================================================
// O3 Pass — 返回 bool 表示是否修改了 IR
// ================================================================
bool algebraicSimplification(IR::Module* mod);
bool loopUnrolling(IR::Module* mod);
bool loopInterchange(IR::Module* mod);
bool tailRecursionElimination(IR::Module* mod);
bool loopStrengthReduce(IR::Module* mod);
bool loopFullUnroll(IR::Module* mod);
bool gepStrengthReduce(IR::Module* mod);
bool instructionScheduling(IR::Module* mod);
void runO3(IR::Module* mod);

// ================================================================
// P3 Pass — 高级优化
// ================================================================
void runP3(IR::Module* mod);

// ================================================================
// P0 Pass — 语义级优化，返回 bool 表示是否修改了 IR
// ================================================================
bool recursiveMulToNative(IR::Module* mod);
bool recursiveModularMulToNative(IR::Module* mod);
bool repeatedDivRemToNative(IR::Module* mod);
bool hoistRecursiveCallGuards(IR::Module* mod);
bool matrixReductionContraction(IR::Module* mod);
bool radixSortLowering(IR::Module* mod);
bool bitOpPatternRecognition(IR::Module* mod);
bool powerOfTwoDispatchSimplification(IR::Module* mod);
bool globalVariablePromotion(IR::Module* mod);
bool globalConstantPropagation(IR::Module* mod);
bool deadStoreElimination(IR::Module* mod);
bool loadElimination(IR::Module* mod);
bool reassociate(IR::Module* mod);
bool treeShaking(IR::Module* mod);
bool codeSink(IR::Module* mod);
bool ifConversion(IR::Module* mod);
bool adce(IR::Module* mod);
bool instCombine(IR::Module* mod);
bool simplifyCFG(IR::Module* mod);
bool copyPropagation(IR::Module* mod);
bool magicDivision(IR::Module* mod);
bool basicBlockReordering(IR::Module* mod);
bool jumpThreading(IR::Module* mod);
void runP0(IR::Module* mod);

// ================================================================
// SSA 构造 — Mem2Reg（alloca/load/store → PHI + 寄存器 SSA）
// 借鉴 Cpl2/Cpl3 的完整 SSA 构造，是 GVN/MemorySSA 等高级优化的前提
// ================================================================
bool mem2reg(IR::Module* mod);

// 局部 mem2reg：只提升所有 STORE 都在同一个 BB 中的 ALLOCA
// 不创建 PHI 节点，安全且无 PHI 爆炸风险
bool mem2regLocal(IR::Module* mod);

// GVN — 全局值编号（基于支配树的跨 BB CSE）
// 消除跨 BB 的冗余 GEP 和算术运算
bool globalValueNumbering(IR::Module* mod);

// ================================================================
// SSA 降级 — PhiLowering（PHI → ALLOCA + STORE + LOAD）
// 在代码生成前将 PHI 指令降级为普通指令，与 Mem2Reg 配对使用
// ================================================================
bool phiLowering(IR::Module* mod);

} // namespace Opt
