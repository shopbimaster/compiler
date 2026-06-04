#pragma once

// ================================================================
// O1 优化器 — 常量折叠 + 死代码消除 + 窥孔优化
// O2 优化器 — 函数内联 + CSE + 循环不变量外提
// O3 优化器 — 代数化简 + 循环展开 + 尾递归消除
// ================================================================

#include "ir/IR.h"
#include <string>

namespace Opt {

// ================================================================
// O1 Pass
// ================================================================
void constantFolding(IR::Module* mod);
void deadCodeElimination(IR::Module* mod);
std::string peepholeOptimize(const std::string& asmCode);
void runO1(IR::Module* mod);

// ================================================================
// O2 Pass
// ================================================================
void inlineExpansion(IR::Module* mod);
void commonSubexpressionElimination(IR::Module* mod);
void loopInvariantCodeMotion(IR::Module* mod);
void runO2(IR::Module* mod);

// ================================================================
// O3 Pass
// ================================================================
void algebraicSimplification(IR::Module* mod);
void loopUnrolling(IR::Module* mod);
void loopInterchange(IR::Module* mod);
void tailRecursionElimination(IR::Module* mod);
void instructionScheduling(IR::Module* mod);
void runO3(IR::Module* mod);

// ================================================================
// P3 Pass — 高级优化
// ================================================================
void runP3(IR::Module* mod);

// ================================================================
// P0 Pass — 语义级优化
// ================================================================
void recursiveMulToNative(IR::Module* mod);
void bitOpPatternRecognition(IR::Module* mod);
void runP0(IR::Module* mod);

} // namespace Opt