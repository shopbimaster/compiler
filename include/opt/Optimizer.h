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
bool strictlyDominates(IR::BasicBlock* a, IR::BasicBlock* b, const DomMap& dom);

// ================================================================
// O1 Pass
// ================================================================
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
bool bitOpPatternRecognition(IR::Module* mod);
bool globalVariablePromotion(IR::Module* mod);
void runP0(IR::Module* mod);

} // namespace Opt