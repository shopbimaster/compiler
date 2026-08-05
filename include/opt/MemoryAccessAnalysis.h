#pragma once

// 注意：共享的指针访问分析（PointerAccess / AllocaArgumentMap /
// buildAllocaArgumentMap / collectPointerAccess / rootGlobal）已统一收纳在
// opt/LoopAnalysis.h 中（本编译器较早的净室重构将其并入 LoopAnalysis）。
// 本头文件仅保留 fix-matrix-reduction-proof-hardening 分支新增的
// 全局内存效应分析，避免与 LoopAnalysis.h 重复定义。
#include "opt/LoopAnalysis.h"

#include <vector>

namespace Opt {

// All direct memory operations and calls reached from a global object's
// pointer-def/use graph.  Analysis succeeds only while the address remains in
// forms that can be audited precisely (the global itself or GEP derivatives).
// Storing the address, merging it through PHI/select, returning it, or using
// it in any other unsupported instruction is conservatively treated as an
// escape and makes the analysis fail.
struct GlobalMemoryEffects {
    std::vector<IR::Instruction*> loads;
    std::vector<IR::Instruction*> stores;
    std::vector<IR::Instruction*> calls;
};

bool analyzeGlobalMemoryEffects(
    IR::GlobalVariable* global,
    GlobalMemoryEffects& effects);

} // namespace Opt