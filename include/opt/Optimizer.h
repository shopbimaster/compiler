#pragma once

// ================================================================
// O1 优化器 — 常量折叠 + 死代码消除 + 窥孔优化
// O2 优化器 — 函数内联 + CSE + 循环不变量外提
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

} // namespace Opt