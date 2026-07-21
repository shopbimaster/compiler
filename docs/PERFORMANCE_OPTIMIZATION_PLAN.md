# Performance Optimization Plan

> Status: architecture and analysis only. This document does not implement an
> optimization and does not change the current compiler or test suite.
>
> Analysis date: 2026-07-21. Target: SysY2022 to RV64GC assembly, evaluated on
> BOOM v3. The official performance entry point is `compiler -S -o out.s in.sy
> -O1`.

## 1. Current Baseline

The latest official result supplied by the team is the correctness baseline for
all future performance work:

| Suite | Result |
|---|---:|
| Functional | 100/100 AC |
| H_Functional | 40/40 AC |
| Performance | 60/60 AC |
| Total score | 100 |
| Performance total runtime | about 810 s |

The runtime is concentrated in a small number of families. Optimization work
must therefore be judged by their aggregate runtime, not by already sub-ms
cases.

| Priority | Family | Three-case aggregate | Share of the 810 s baseline |
|---:|---|---:|---:|
| 1 | `many_mat_cal` | about 322 s | about 39.8% |
| 2 | `knapsack_naive` | about 151 s | about 18.6% |
| 3 | `conv2d` | about 73 s | about 9.0% |
| 4 | `huffman` | about 59 s | about 7.3% |
| 5 | `matmul` | about 45 s | about 5.6% |
| | Total of these families | about 650 s | about 80.2% |

The design target is to reduce these general costs while preserving all 200
official AC results. A local performance win is not acceptable if it weakens
the existing protections for dynamic GEPs, aliases, call side effects, 32-bit
integer semantics, PHI lowering, the RISC-V ABI, or large stack offsets.

### 1.1 Evidence collection scope

Only three representative sources were compiled; no performance workload and
no full suite was executed:

- `test/performance/many_mat_cal-1.sy`
- `test/performance/knapsack_naive-1.sy`
- `test/performance/conv2d-1.sy`

The current working tree was successfully rebuilt with Clang 18 under Ubuntu
24.04. Optimized IR and post-peephole assembly were generated with:

```bash
compiler -o case.ir test/performance/case.sy -O1
compiler -S -o case.s test/performance/case.sy -O1
```

The temporary artifacts were not committed. Their SHA-256 values make the
observations below reproducible:

| Artifact | Lines | SHA-256 |
|---|---:|---|
| `many_mat_cal-1.ir` | 338 | `0fc7e4bc931e96b9a45cb1ed1012e1dd96ff45af4d9258e6b501cb85d6ff5ba5` |
| `many_mat_cal-1.s` | 430 | `cc4c1a3504f00d3bec6e13e4eb3a09781cd0f1cd7f2c3c12f5ae64a9517b98d9` |
| `knapsack_naive-1.ir` | 100 | `c4ddcc3203287aa89a2d4cd0623c3a801af8885e0f7d3aa4830a9ac021e5ed4f` |
| `knapsack_naive-1.s` | 176 | `a37ba6265631c7e6d8a0ef374804ddad9cabe293a4229a3141234d064893073f` |
| `conv2d-1.ir` | 660 | `ab8cc6da278ab9eccb677907e66017bf051e666fdf0c54137b2e2ad581723575` |
| `conv2d-1.s` | 932 | `ec3ee3767e986496b3aeabae79bcbc61170da4d9f8e855929e233112a61009a2` |

The runtime estimates in this plan are projections, not local measurements.
Only the official platform can validate BOOM timing.

## 2. Compiler Pipeline

### 2.1 Front end and IR

`src/main.cpp:31-54` parses the official flags. Uppercase `-O1` maps to `OALL`,
not merely to the internal O1 stage (`src/main.cpp:40-42`).

`src/Compiler.cpp:37-45` invokes `IRBuilder`, then dumps or optimizes a
`Module`. The front end is a direct ANTLR parse-tree-to-IR visitor; there is no
separate AST. The IR is typed, LLVM-like, and partly SSA:

- `Module -> Function -> BasicBlock -> Instruction` ownership;
- explicit def-use chains and typed values in `include/ir/IR.h`;
- arithmetic, compare, branch, call, load/store, GEP, cast, select and PHI
  instructions;
- local declarations initially use `alloca/load/store`; Mem2Reg later promotes
  eligible entry-block scalar allocas;
- `if` and `while` become explicit basic blocks and branches;
- multidimensional indexing is emitted as one GEP per dimension
  (`src/ir/IRBuilder.cpp:573-617`).

Local scalar declarations are currently allocated in the active block, not the
entry block (`src/ir/IRBuilder.cpp:209-235`). Short-circuit `&&` and `||` also
materialize an alloca/store/load temporary (`src/ir/IRBuilder.cpp:1018-1097`),
even though the backend now supports PHI edge copies. This is central to the
hot-loop stack traffic discussed below.

### 2.2 Effective `-O1` pass order

`src/Compiler.cpp:24-29` runs the following five stages for the official
uppercase `-O1`:

```text
IRBuilder
  -> runO1
  -> runO2
  -> runO3
  -> runP0
  -> runP3
  -> RISC-V instruction selection / register allocation
  -> assembly peephole
```

The effective pass sequence is:

| Stage | Order | Scope / category |
|---|---|---|
| O1 | ConstantFolding -> DCE -> local CSE -> ConstantFolding -> DCE | Function/instruction level |
| O2 structure | TreeShaking -> BitOpPatternRecognition -> TailRecursionElimination -> InlineExpansion -> Mem2Reg -> InlineExpansion -> Mem2Reg -> GlobalVariablePromotion -> local Mem2Reg -> GlobalConstantPropagation | Module and function construction |
| O2 simplify, max 2 rounds | InstCombine -> DeadStoreElimination -> SimplifyCFG | Function, memory, CFG |
| O2 arithmetic | MagicDivision -> AlgebraicSimplification -> Reassociate -> LoadElimination | Function, arithmetic, memory |
| O2 propagation, max 2 rounds | SCCP -> SimplifyCFG -> CopyPropagation | SSA and CFG |
| O2 feedback | InstCombine -> MagicDivision -> AlgebraicSimplification -> DSE -> SimplifyCFG -> SCCP | Function and CFG |
| O2 loop | LICM -> LoadElimination -> local CSE | Loop and memory |
| O2 cleanup | IfConversion -> ADCE -> CodeSink -> BasicBlockReordering -> local CSE | CFG and code placement |
| O3 loop | LoopInterchange -> LoopStrengthReduce -> LoopFullUnroll -> LoopUnrolling -> GEPStrengthReduce | Loop and array/address |
| O3 cleanup if changed | SCCP/SimplifyCFG -> CSE -> InstCombine -> DSE -> LoadElimination -> LICM -> CSE -> BasicBlockReordering | Function, loop and CFG |
| P0 | RecursiveMulToNative -> BitOpPatternRecognition -> CF/DCE, with conditional InstCombine/SCCP/SimplifyCFG/CSE | Pattern recognition |
| P3 | InstructionScheduling -> CF -> DCE | Basic-block scheduling |

The exact scheduler is in `src/opt/Optimizer.cpp:23-520`. Dominator, predecessor,
successor and natural-loop utilities are shared through
`include/opt/Optimizer.h` and `src/opt/DominatorAnalysis.cpp` /
`src/opt/LoopFind.cpp`.

### 2.3 Pass classification

| Area | Existing mechanisms | Main files |
|---|---|---|
| Function/SSA | Inline expansion, Mem2Reg, SCCP, copy propagation, reassociation, InstCombine | `src/opt/InlineExpansion.cpp`, `Mem2Reg.cpp`, `SCCP.cpp`, `CopyPropagation.cpp`, `Reassociate.cpp`, `InstCombine.cpp` |
| Memory/array | Load elimination, DSE, global promotion/constant propagation, GEP strength reduction | `src/opt/LoadElimination.cpp`, `DeadStoreElimination.cpp`, `GlobalVariablePromotion.cpp`, `GlobalConstantPropagation.cpp`, `GEPStrengthReduce.cpp` |
| Loop | Loop discovery, SCEV, LICM, scalar/GEP strength reduction, full/partial unroll, interchange | `src/opt/LoopFind.cpp`, `SCEVAnalysis.cpp`, `LICM.cpp`, `LoopStrengthReduce.cpp`, `GEPStrengthReduce.cpp`, `LoopFullUnroll.cpp`, `LoopUnrolling.cpp`, `LoopInterchange.cpp` |
| Control flow | SimplifyCFG, IfConversion, ADCE, CodeSink, block reordering, tail recursion elimination | corresponding files under `src/opt/` |
| Backend | Direct instruction selection, PHI edge copies, linear-scan allocation, stack layout, peephole | `src/backend/TargetCodeGen.cpp`, `src/backend/RegisterAllocator.cpp`, `src/opt/PeepholeOptimizer.cpp` |

The compiler therefore already has inlining, LICM, scalar and GEP strength
reduction, induction/SCEV analysis, loop interchange and unrolling. The main
gap is not the absence of pass names; it is the restricted legality/coverage
of those passes and the backend cost model after they create longer live
ranges.

### 2.4 RISC-V backend

`TargetCodeGen::emitInstruction` directly selects RISC-V text per IR opcode
(`src/backend/TargetCodeGen.cpp:930-990`). Important behavior:

- integer arithmetic uses 32-bit-result RV64 forms where appropriate;
- PHIs are not lowered to stack by `PhiLowering`; parallel edge copies are
  emitted by `emitPhiMovesForEdge` (`src/Compiler.cpp:58-65`);
- single-use, same-basic-block GEP+load/store pairs may be folded, but nested or
  cross-block GEP folding is conservatively rejected
  (`src/backend/TargetCodeGen.cpp:670-709`);
- dynamic GEP strides 2/4/8/powers of two use `slli`; other strides use `mul`
  (`src/backend/TargetCodeGen.cpp:489-515`);
- global addresses and frequent large constants may consume callee-saved
  registers; a limited number of scalar allocas are separately promoted to
  callee-saved registers.

The allocator is a custom linear scan over instruction IDs. It extends live
intervals for loops and PHI edges, then uses 12 integer `s` registers plus
`t3-t6`, and 12 `fs` plus `ft2-ft11` (`src/backend/RegisterAllocator.cpp:11-25`).
Its free set is a `std::set`, so the declared vector order is not retained; the
lexically first free register is selected (`RegisterAllocator.cpp:532-559`).
Spill priority is `loopDepth*10000 + useCount*100 + intervalLength`
(`RegisterAllocator.cpp:577-624`). Spill slots are 8 bytes.

Stack layout reserves slots for allocas, arguments and every virtual operand
and result before allocation (`src/backend/TargetCodeGen.cpp:339-432`). Actual
spilled values are loaded/stored by `loadToReg` / `storeFromReg`; large offsets
are materialized using `li + add` (`TargetCodeGen.cpp:460-483`). Prologue and
epilogue code saves every used callee-saved register, while a call site saves
only caller-saved registers reported live across that call
(`TargetCodeGen.cpp:519-662`, `RegisterAllocator.cpp:760-776`).

## 3. Hotspot Evidence

### 3.1 Summary table

Counts below include the named assembly interval, including loop-control
instructions. “Stack” counts only explicit `offset(sp)` accesses; address
materialization through a temporary base makes the true stack-related count
higher in some regions.

| Case / hot function | Hot loop or path | Main redundant work | Stack evidence | Address evidence | Branch/call evidence | Related source |
|---|---|---|---|---|---|---|
| `many_mat_cal-1`, `main` | `i/j/k` multiply, source lines 75-87; asm 302-326 | Per `k`: repeated loads/stores for `k` and `sum`; three index scales | 8 memory ops in header+body, at least 3 direct `sp` accesses; IR keeps `%k` and `%sum` as body allocas | 3 `slli` + 3 `add` per `k` for `C[i][k]` and `A[k][j]` | 3 loop branches/jumps, no call | `test/performance/many_mat_cal-1.sy:75-87`; `IRBuilder.cpp`, `Mem2Reg.cpp`, `GEPStrengthReduce.cpp` |
| `many_mat_cal-1`, `main` | repeated `R*i*j` checksum, source lines 92-103; asm 373-394 | Duplicate row GEPs and stack-resident `j`/`total` | 8 memory ops in header+body | 1 `slli` + 2 `add` for two same-row loads | 3 branches/jumps, no call | same files; local CSE deliberately excludes GEP |
| `knapsack_naive-1`, `knapsack_naive` | every recursive invocation, source lines 7-23; asm 33-126 | Whole-frame save/restore dominates a small recursive body | 400-byte frame; 12 `sd` + 12 `ld` for `s0-s11`, plus `ra`; function region has 35 memory ops, 32 direct stack ops | Both global bases occupy `s0/s1`; local temporaries also consume `s` registers | Two recursive calls; one live `t3` is separately saved/restored around second call | `test/performance/knapsack_naive-1.sy:7-23`; `RegisterAllocator.cpp`, `TargetCodeGen.cpp` |
| `conv2d-1`, `conv2d` | five-deep repeat/r/c/kr/kc nest, source lines 54-79; asm 396-472 | Short-circuit booleans and temporaries repeatedly materialized on stack; two array addresses recomputed | Inner `kc` CFG has 24 memory ops, 19 explicit `sp` accesses; function frame is 768 bytes | Taken path uses two `slli+add` pairs for `In` and `K` | 11 branch/jump instructions in the `kc` CFG, no calls after `idx` inlining | `test/performance/conv2d-1.sy:51-79`; `IRBuilder.cpp`, `Mem2Reg.cpp`, `GEPStrengthReduce.cpp` |

### 3.2 `many_mat_cal`: stack-resident loop state and repeated GEPs

Optimized IR still allocates `k` and `sum` inside the `j` body:

```llvm
; generated IR lines 246-269
%k = alloca i32
store i32 0, i32* %k
%sum = alloca i32
store i32 0, i32* %sum
...
%t115 = getelementptr i32* %t113, 0, %t114
%t118 = getelementptr [1024 x i32]* %A, 0, %t114
%t120 = getelementptr i32* %t118, 0, %t119
...
store i32 %t123, i32* %sum
store i32 %t125, i32* %k
```

The matching inner-loop assembly contains six address instructions and repeated
stack state traffic:

```asm
; generated assembly lines 307-325
lw      t4, 8(sp)       # k
lw      t5, 16(sp)      # sum
slli    t1, t4, 2
add     t6, s9, t1      # C[i][k]
lw      s11, 0(t6)
slli    t1, t4, 12
add     t6, s0, t1      # A[k] row
slli    t1, s4, 2
add     t4, t6, t1      # A[k][j]
lw      s8, 0(t4)
mulw    t4, s11, s8
addw    s11, t5, t4
sw      s11, 16(sp)
...
sw      s8, 0(t1)       # updated k
```

This is consistent with two explicit limitations:

1. Mem2Reg accepts only entry-block allocas (`src/opt/Mem2Reg.cpp:118-135`),
   while the front end places block-local declarations at the declaration site.
2. GEP strength reduction returns without changing a loop unless it sees
   exactly one candidate (`src/opt/GEPStrengthReduce.cpp:256-260`). This loop
   has independent `C[i][k]` and `A[k][j]` recurrences.

The `R*i*j` checksum also retains duplicate row GEPs in IR (generated lines
302-315). Local CSE explicitly rejects all GEPs because longer live ranges have
previously regressed allocation (`src/opt/CSE.cpp:55-67`). The root problem is
therefore address/live-range handling, not simply a missing hash-table lookup.

### 3.3 `knapsack_naive`: recursive prologue/epilogue cost

The optimized IR contains only two recursive calls (generated IR lines 52 and
60), but each dynamic invocation saves and restores all twelve integer
callee-saved registers:

```asm
; generated assembly lines 33-47
knapsack_naive:
  addi sp, sp, -400
  sd   ra, 392(sp)
  sd   s1, 384(sp)
  sd   s0, 376(sp)
  sd   s4, 368(sp)
  sd   s3, 360(sp)
  sd   s2, 352(sp)
  sd   s10, 344(sp)
  sd   s11, 336(sp)
  sd   s5, 328(sp)
  sd   s6, 320(sp)
  sd   s7, 312(sp)
  sd   s8, 304(sp)
  sd   s9, 296(sp)
```

Generated lines 112-125 perform the symmetric twelve loads, `ra` load, stack
adjustment and return. Thus every recursion node pays 24 callee-save memory
operations independent of which path is taken. The allocator nominally has
`t3-t6` for short-lived values, but its `std::set` selection chooses `s*` first.
The result is especially costly in an exponential recursion tree. The one
caller-saved value that truly crosses the second call (`t3`) is already handled
locally by `sd t3, 288(sp)` / `ld t3, 288(sp)` (generated lines 85 and 89),
showing that the backend already has the mechanism needed for a safer policy.

### 3.4 `conv2d`: short-circuit allocas and spill amplification

After inlining `idx`, the optimized IR correctly hoists some row arithmetic,
but the four-part bounds check still carries three alloca/store/load
temporaries (generated IR lines 293-310). The taken computation then contains
two independent GEPs (lines 320-323).

In the generated inner `kc` CFG (assembly lines 396-472), 19 instructions
directly access `offset(sp)`, including stores at 304, 312, 376, 392, 416, 424,
440, 456, 464, 472, 480, 488, 504 and 520. The taken path reloads several of
those values before the multiply. It also performs:

```asm
slli t1, t1, 2
add  t0, s10, t1       # In[index]
...
slli t1, t1, 2
add  t0, s8, t1        # K[index]
```

`IRBuilder.cpp:1022` and `:1066` still state that PHI is unsupported, while the
current backend directly supports PHI edge copies. Replacing these temporary
memory values with SSA is therefore an architectural cleanup with direct hot
loop evidence, but it must retain exact short-circuit behavior.

## 4. Optimization Roadmap

Recommended implementation order:

| Order | Priority | ID | Optimization | Primary families | Dependency |
|---:|---|---|---|---|---|
| 1 | P0 | RA-CALL-1 | Call-aware register preference and callee-save minimization | `knapsack_naive`, `huffman` and other call-heavy code | None |
| 2 | P0 | SSA-SCALAR-1 | Entry placement and SSA promotion of non-escaping scalar locals | `many_mat_cal`, `conv2d`, `matmul` | Prefer RA-CALL-1 first because SSA can increase register pressure |
| 3 | P1 | GEP-LSR-2 | Safe grouped affine pointer recurrences | `many_mat_cal`, `conv2d`, `matmul` | RA-CALL-1; validate PHI/RA robustness |
| 4 | P1 | RA-LOOP-2 | Frequency-weighted spilling and limited interval splitting | all five slow families | Build on RA-CALL-1 |
| 5 | P2 | LOOP-TILE-1 | Dependence-checked interchange and cache tiling | `many_mat_cal`, `matmul`, `conv2d` | Alias/dependence analysis plus stable SSA/RA |

This order first removes demonstrably unnecessary ABI traffic, then removes
stack-form scalar state, then allows address recurrences. Tiling is deliberately
last: it has the largest theoretical cache benefit but the broadest legality
surface.

## 5. Detailed Design Cards

### 5.1 RA-CALL-1 — Call-aware register preference and callee-save minimization (P0)

1. **Current problem.** Linear scan chooses the lexically first free register
   from a `std::set`; short-lived values consume `s*` registers and force
   prologue/epilogue saves even when they do not survive a call.
2. **Evidence.** `knapsack_naive` saves/restores all 12 integer `s` registers
   in every 400-byte recursive frame (generated assembly lines 33-47 and
   112-125). `RegisterAllocator.cpp:538-559` discards the declared pool order.
3. **Affected families.** Highest confidence: `knapsack_naive`; likely
   `huffman` and other recursive/call-heavy functions. Matrix kernels should be
   neutral except for smaller one-time prologues.
4. **Modules/files.** `include/backend/RegisterAllocator.h`,
   `src/backend/RegisterAllocator.cpp`; a backend-aware regression in
   `tests/test_integration.cppx` and its `CMakeLists.txt` link if needed.
5. **Required analysis.** Existing live intervals and call instruction IDs;
   compute a per-interval `crossesCall` property. No alias analysis is needed.
6. **Implementation steps.** Record call IDs while building intervals; mark an
   interval call-crossing only when `start < callId < end`; preserve register
   pool order rather than constructing a sorted set; for a proven call-local
   integer value prefer `t3-t6`, otherwise prefer `s0-s11`; mirror the policy
   for `ft*`/`fs*`; retain current call-site save logic as a safety net; prune
   unused callee-saved registers and compare assembly counts.
7. **Preconditions.** CFG/loop interval extensions and PHI incoming liveness
   must have run before `crossesCall` is computed. A caller-saved register may
   be preferred only when the value is dead at every reachable call.
8. **Conservative fallback.** If interval order, PHI-edge liveness, a loop
   backedge or an indirect uncertainty makes the proof incomplete, use a
   callee-saved register or spill exactly as today.
9. **Correctness risks.** A false call-local classification can clobber a live
   value across calls; float/int class confusion violates the ABI; recursion
   magnifies either bug. Arguments/returns and reserved scratch registers must
   remain excluded. Callee-saved registers used anywhere must still be saved.
10. **Minimal regression.** A small function with one value live across a call
    and several values dead before it. Assert: the live value remains correct,
    call-local values choose caller-saved registers, and the callee-save count
    is below the old count. Add recursive execution and mixed int/float cases.
11. **Target performance tests.** First run only `knapsack_naive-1/2/3`; then
    `huffman-1/2/3`; finally the official full suites after local correctness.
12. **Expected benefit.** Projected 15-35% for `knapsack_naive`, depending on
    recursion shape; smaller call-heavy wins elsewhere. This estimate is based
    on removing up to 24 callee-save memory operations per recursive node, not
    on a local runtime measurement.
13. **Recommended commits.** One implementation-plus-regression commit:
    `perf(riscv): prefer caller-saved registers for call-local values`; a later
    measurement-only documentation commit if needed.
14. **Dependencies.** Independent. It should precede SSA-SCALAR-1 and
    GEP-LSR-2 because both can increase live values.
15. **Acceptance gate.** Correct output, no ABI violation, materially fewer
    `sd/ld s*` pairs in `knapsack_naive`, and no extra call-site saves that
    cancel the prologue win.

### 5.2 SSA-SCALAR-1 — Entry placement and SSA promotion of non-escaping scalar locals (P0)

1. **Current problem.** Scalars declared in loop bodies remain dynamic allocas,
   because Mem2Reg safely skips non-entry allocas. Hot induction variables,
   accumulators and short-circuit temporaries therefore generate stack traffic.
2. **Evidence.** `many_mat_cal` IR lines 246-269 retain `%k/%sum` allocas and
   corresponding per-iteration loads/stores. `conv2d` inner `kc` has 19 direct
   stack accesses. Front-end placement is at `IRBuilder.cpp:209-235`; the
   entry-only guard is at `Mem2Reg.cpp:118-135`.
3. **Affected families.** `many_mat_cal`, `conv2d`, `matmul`; likely loops in
   `huffman` as well.
4. **Modules/files.** `src/ir/IRBuilder.cpp`, possibly
   `include/ir/IRBuilder.h`, `src/opt/Mem2Reg.cpp`, and focused IR/integration
   tests. Do not weaken Mem2Reg globally.
5. **Required analysis.** Type and escape checks, dominance, dominance frontier
   and scope/lifetime placement. Existing Mem2Reg analysis is reusable.
6. **Implementation steps.** Add an entry-alloca insertion helper; place only
   scalar int/float storage in entry while leaving each initializer store at
   the original declaration point; keep arrays and pointer-valued allocas on
   the conservative path; convert short-circuit merge values directly to PHIs
   or make their scalar storage entry-promotable; run existing Mem2Reg and
   cleanup; inspect PHI count and assembly stack accesses.
7. **Preconditions.** The scalar address must not escape; allocation identity
   must be unobservable in SysY; declaration stores must dominate every valid
   source-level use; shadowed names remain separate IR values.
8. **Conservative fallback.** Keep the current block-local alloca if type,
   escape, initialization, dominance or PHI budget cannot be proved. Never
   hoist arrays merely because their address is locally used.
9. **Correctness risks.** Reusing a scalar value across loop iterations before
   its initializer, merging branch-local lifetimes, PHI explosion, and the
   prior crypto/Mem2Reg failure mode. I1/I32 stores for short-circuit values
   must be consistently converted. Preserve 32-bit wrap/truncation semantics.
10. **Minimal regression.** Loop-local initialized scalar, conditional-only
    declaration, nested shadowing, `continue`, two independent loop invocations,
    `&&`/`||` with side-effecting RHS, and a non-promotable array control case.
11. **Target performance tests.** `many_mat_cal-1/2/3`, then `conv2d-1/2/3`,
    then `matmul-1/2/3`.
12. **Expected benefit.** Projected 10-30% on affected scalar-heavy loops;
    potentially higher for `conv2d` if the 19 inner-CFG stack accesses collapse.
13. **Recommended commits.** `refactor(ir): place non-escaping scalar allocas in entry`;
    then, if kept separate, `perf(ir): lower short-circuit values with phi`.
14. **Dependencies.** No hard dependency, but RA-CALL-1 should land first to
    absorb increased SSA pressure. GEP-LSR-2 benefits from cleaner loop IR.
15. **Acceptance gate.** Target IR no longer contains `%k/%sum` or boolean
    temporaries as hot body allocas; all side-effect/short-circuit tests remain
    correct; PHI count remains bounded; target assembly stack operations fall.

### 5.3 GEP-LSR-2 — Safe grouped affine pointer recurrences (P1)

1. **Current problem.** GEP strength reduction rejects a loop unless it has
   exactly one candidate. The slow matrix/convolution loops naturally have two
   or more arrays, so all candidates are skipped.
2. **Evidence.** `GEPStrengthReduce.cpp:256-260` implements the one-candidate
   gate. `many_mat_cal` inner `k` emits three shifts and three adds; `conv2d`
   emits separate `slli+add` pairs for `In` and `K`.
3. **Affected families.** `many_mat_cal`, `conv2d`, `matmul`.
4. **Modules/files.** `src/opt/GEPStrengthReduce.cpp`, loop/SCEV utilities,
   `src/backend/RegisterAllocator.cpp`, PHI-edge code in
   `src/backend/TargetCodeGen.cpp`, and focused IR/backend tests.
5. **Required analysis.** Canonical induction variables, constant step/stride,
   natural loop/preheader/single latch, dominance, invariant bases, use
   containment and a register-pressure estimate. Alias analysis is not needed
   merely to compute addresses, but is required for any memory-value reuse.
6. **Implementation steps.** Canonicalize and deduplicate candidates by
   `(base, IV, index position, element type, constant offset)`; build all
   initial pointers in the preheader; insert grouped pointer PHIs at the header;
   update every recurrence in the latch; replace only exact GEP uses; verify
   parallel PHI copies; reject transformations whose pressure estimate exceeds
   a conservative budget; run cleanup once after the group.
7. **Preconditions.** One canonical IV and one latch; constant nonzero step;
   invariant base; all uses dominated and inside the loop; exact GEP type and
   64-bit pointer arithmetic preserved. Dynamic indices other than the proven
   affine IV disqualify that candidate.
8. **Conservative fallback.** Keep the original GEP if any recurrence is
   ambiguous. A failing candidate must not cause structurally related but
   unproved GEPs to be merged. Initially cap the group size and require no call
   in the recurrence loop.
9. **Correctness risks.** The previous `h-5` failures show that multiple pointer
   PHIs can interact with PHI copies and allocation. Other risks are incorrect
   dynamic-index reuse, loop-backedge value selection, pointer-width errors and
   pressure-induced spills. Do not infer memory equality from pointer shape.
10. **Minimal regression.** Two arrays indexed by the same IV; two-dimensional
    mixed row/column indexing; aliasing bases; dynamic secondary index; negative
    step; multiple latches/`continue`; a call in the body; and the six historical
    `h-5`/`crypto` correctness cases.
11. **Target performance tests.** `many_mat_cal-1/2/3`, `conv2d-1/2/3`, and
    `matmul-1/2/3` only during tuning.
12. **Expected benefit.** Projected 5-20% in address-heavy loops, with the
    largest opportunity in `many_mat_cal` and `matmul`. Reject the change if
    pointer PHIs merely trade shifts for spills.
13. **Recommended commits.** `test(opt): cover multiple affine gep recurrences`;
    then `perf(opt): reduce safe grouped gep recurrences`; then a separate
    backend commit only if PHI-copy or pressure handling must change.
14. **Dependencies.** Prefer RA-CALL-1 and SSA-SCALAR-1 first. Closely related
    to RA-LOOP-2.
15. **Acceptance gate.** Fewer dynamic shifts/multiplies and no extra hot-loop
    spill traffic; historical `h-5`/`crypto` tests and all target outputs pass.

### 5.4 RA-LOOP-2 — Frequency-weighted spilling and limited interval splitting (P1)

1. **Current problem.** Static max loop depth and unweighted use count cannot
   distinguish a use in a deep hot block from a cold exit. Long intervals are
   given higher retention cost even if splitting them would be cheaper.
2. **Evidence.** The cost formula is fixed at
   `loopDepth*10000 + useCount*100 + length` (`RegisterAllocator.cpp:577-585`).
   `conv2d` still spills 19 explicit stack values in its innermost CFG; O2's GVN
   is disabled because longer live ranges caused a measured 1319 ms aggregate
   regression (`Optimizer.cpp:333-346`).
3. **Affected families.** All five slow families, especially `conv2d`, matrix
   kernels and call-heavy control flow.
4. **Modules/files.** `include/backend/RegisterAllocator.h`,
   `src/backend/RegisterAllocator.cpp`, `src/backend/TargetCodeGen.cpp`, and
   backend allocation tests.
5. **Required analysis.** Natural-loop depth, estimated block frequency,
   per-use positions, call sites and next-use information. Later phases may use
   liveness sets rather than one continuous interval.
6. **Implementation steps.** Weight each use by bounded loop depth; calculate
   spill cost from weighted uses rather than max depth; prefer spilling values
   whose next use is farthest; first add only call/boundary splits; reuse spill
   slots for non-overlapping split ranges; compare dynamic hot-loop stack ops,
   not only total assembly size.
7. **Preconditions.** Frequency weights affect profitability only, never
   liveness correctness. Splits must insert reload/store at dominating legal
   points and preserve PHI edge semantics.
8. **Conservative fallback.** If a split crosses a PHI edge, call, loop
   backedge or ambiguous layout, keep the current whole interval. Clamp weights
   to avoid integer overflow.
9. **Correctness risks.** Missing loop-carried liveness, reload after clobber,
   caller/callee class mistakes, float width errors, and large-offset spill
   emission. A profitability mistake may also regress BOOM despite correctness.
10. **Minimal regression.** Nested hot/cold uses, value live over a backedge,
    value live over a call, PHI incoming split, integer/float pressure, and a
    frame exceeding the 12-bit immediate range.
11. **Target performance tests.** `conv2d-1/2/3`, one `many_mat_cal`, one
    `huffman`, then the five slow families after stability.
12. **Expected benefit.** Projected 5-15% where spills dominate; it also
    unlocks later GEP/GVN transformations.
13. **Recommended commits.** `perf(riscv): weight spill costs by loop uses`;
    later `perf(riscv): split live ranges at safe boundaries`. Do not combine
    the two algorithms in one commit.
14. **Dependencies.** Extend the metadata introduced by RA-CALL-1. GEP-LSR-2
    should be retuned after this lands.
15. **Acceptance gate.** Lower hot-loop stack-access counts without larger
    call-save traffic, correct large-frame handling, and no target regression.

### 5.5 LOOP-TILE-1 — Dependence-checked interchange and cache tiling (P2)

1. **Current problem.** `many_mat_cal` multiplies with `A[k][j]`, a 4096-byte
   stride for `[1024 x i32]`, while `C[i][k]` is contiguous. Existing loop
   interchange recognizes narrow alloca-based shapes and has no general memory
   dependence proof.
2. **Evidence.** Source lines 75-87 form `i/j/k`; generated assembly lines
   313-316 compute `k<<12` then `j<<2` for `A[k][j]`. This is a cache-locality
   issue that scalar address cleanup alone cannot remove.
3. **Affected families.** `many_mat_cal`, `matmul`, and selected `conv2d` loops.
4. **Modules/files.** `src/opt/LoopInterchange.cpp`, new or extended loop
   dependence/alias analysis, `SCEVAnalysis.cpp`, `LoopFind.cpp`,
   `Optimizer.cpp`, plus substantial loop tests.
5. **Required analysis.** Canonical loop nests, affine subscripts, trip bounds,
   dependence direction/distance, base alias sets, overflow-safe bound math and
   target cache/tile cost model.
6. **Implementation steps.** Normalize perfect nests; derive affine access
   matrices; prove permutation legality; implement interchange first; then add
   fixed conservative tiles with cleanup loops; rerun LICM/GEP LSR/SSA cleanup;
   tune tile sizes only from general target parameters, never case names.
7. **Preconditions.** All reordered side effects have proven dependence order;
   calls are absent or proven pure; bases are non-aliasing or dependences permit
   the transformation; loop bounds/steps are canonical and finite.
8. **Conservative fallback.** Skip on unknown alias, non-affine index, call,
   early exit, multiple latch, uncertain 32-bit overflow or incomplete nest.
9. **Correctness risks.** Reordered stores/loads under aliasing, changed call
   side effects, overflow in tile bounds, wrong remainder handling, dynamic GEP
   reuse, and RA pressure from extra induction variables.
10. **Minimal regression.** Legal matrix interchange, loop-carried dependence
    that forbids interchange, aliased arrays, non-divisible tile remainder,
    negative/zero bounds, 32-bit boundary values, calls and `break/continue`.
11. **Target performance tests.** First one `matmul`, then one
    `many_mat_cal`; after correctness, all three variants of both and one
    `conv2d`.
12. **Expected benefit.** Potentially 1.5-5x for cache-bound matrix kernels, but
    highly hardware- and shape-dependent. This is intentionally not counted as
    a near-term guaranteed gain.
13. **Recommended commits.** Separate commits for affine access analysis,
    legality-only tests, interchange, tiling, and remainder generation.
14. **Dependencies.** Stable SSA/PHI, alias/dependence infrastructure,
    GEP-LSR-2 and RA-LOOP-2.
15. **Acceptance gate.** A machine-checkable legality proof for every changed
    nest, correct remainder code, fewer cache-unfriendly strides, and official
    full regression before enabling by default.

## 6. Recommended First Task

Implement **RA-CALL-1: call-aware register preference and callee-save
minimization** first.

It meets the handoff constraints:

- the core change is limited to the allocator's interval metadata and register
  selection;
- it can be committed and reverted independently;
- it has a structural assembly test: callee-save count plus a value live across
  a call;
- it uses existing call IDs and live intervals, so it needs no new alias or
  loop framework;
- it targets the second-largest family, `knapsack_naive` (about 151 s);
- the fallback is naturally conservative: uncertain values continue to use
  callee-saved registers;
- the evidence is unusually strong: 24 unnecessary-looking `s*` memory
  operations occur at every recursive node.

Expected implementation files:

```text
include/backend/RegisterAllocator.h
src/backend/RegisterAllocator.cpp
tests/test_integration.cppx
CMakeLists.txt                 # only if the existing integration target needs backend linkage
```

Do not combine this task with spill splitting, GEP changes, Mem2Reg changes or
stack-slot compaction.

## 7. Developer Handoff Checklist

- [ ] Start from a clean feature branch based on the 100-point baseline.
- [ ] Record the current hashes and official timing table.
- [ ] Build with Ubuntu 24.04 / Clang 18 and the official uppercase `-O1` path.
- [ ] Add the minimal structural/correctness regression before changing policy.
- [ ] For RA-CALL-1, dump interval `start/end/call IDs` only in a local debug
      mode; never commit logs or case-name conditions.
- [ ] Treat “not proven call-local” as call-crossing.
- [ ] Keep `a0-a7`, `fa0-fa7`, `t0-t2`, `ft0-ft1` and reserved cache registers
      out of the general pool unless separately proven safe.
- [ ] Verify PHI incoming values on both normal and backedges.
- [ ] Inspect prologue, epilogue and each call site before timing.
- [ ] Compare dynamic-relevant instruction counts, not just `.s` file size.
- [ ] Run only the optimization's target family while iterating.
- [ ] Before merging, run local correctness regressions including historical
      `h-5` and `crypto`; use the website for the complete official result.
- [ ] Use one Conventional Commit per independently verified optimization.
- [ ] Update this plan with measured BOOM results and any rejected variant.

## 8. Review Checklist

- [ ] No test name, source filename, function name, fixed input or output
      triggers an optimization.
- [ ] Every transform has explicit legality conditions and a skip path.
- [ ] Dynamic GEPs are keyed by actual SSA operands, type and loop context; no
      accidental reuse across changing indices or backedges.
- [ ] Stores and calls invalidate memory facts unless alias/effect analysis
      proves otherwise.
- [ ] Pointer/array aliasing is conservative.
- [ ] Arithmetic preserves SysY i32 wrap/truncation and signedness.
- [ ] Arithmetic and logical right shifts remain distinct.
- [ ] RISC-V caller/callee-save rules, argument/return registers and float
      classes are preserved.
- [ ] Values live across calls survive; call returns are not overwritten by
      restore sequences.
- [ ] PHI edge copies are parallel-copy safe and cover every predecessor.
- [ ] Loop liveness covers every backedge and `continue` edge.
- [ ] Stack frames remain 16-byte aligned; 32/64-bit accesses and large offsets
      use the correct helpers.
- [ ] Profitability includes added PHIs, live-range growth, spills, branches,
      prologue traffic and code size.
- [ ] Minimal regressions test both the transformed and conservative paths.
- [ ] Official Functional, H_Functional and Performance results are reported
      separately from local target tests.

## 9. Rejected or Deferred Ideas

| Idea | Decision | Reason |
|---|---|---|
| Permanently disable all risky optimizations | Rejected | Preserves neither the performance goal nor a useful architecture; safety must come from legality and fallback checks. |
| Replace naive knapsack with dynamic programming | Rejected | This changes the user's algorithm and is not a semantics-preserving compiler optimization. |
| Case-name or input-specific fast paths | Rejected | Explicitly non-general and non-compliant. |
| Re-enable unrestricted cross-BB GVN now | Deferred | Existing source records a 1319 ms aggregate regression from live-range growth; improve allocation first. |
| Enable multiple GEP recurrences by deleting the `size()!=1` guard | Rejected | Historical `h-5` failures demonstrate PHI/RA correctness risk; grouped construction, pressure checks and regressions are required. |
| Promote every non-entry alloca in Mem2Reg | Rejected | Changes dynamic lifetime and repeats the class of bug fixed for `crypto`; canonical scalar entry placement or region proof is required. |
| Treat calls as pure by default | Rejected | Calls may modify arrays and globals; purity/effect summaries would need a separate conservative interprocedural design. |
| Unconditional loop interchange/tiling | Rejected | Aliases, dependences, calls, remainder loops and overflow make it unsafe without analysis. |
| General RVV vectorization as an early task | Deferred | Requires target support, alignment/tail handling and a cost model; current scalar stack/address issues offer safer near-term wins. |
| Optimize already sub-ms cases | Deferred | They do not materially affect the 810 s total and can distort the design toward code-size noise. |

The roadmap should be revisited after each official timing submission. If the
first task fails to reduce `knapsack_naive` despite fewer callee saves, retain
the correctness improvement only if it is neutral overall; otherwise revert it
cleanly and move to SSA-SCALAR-1 with the measured counter-evidence recorded.
