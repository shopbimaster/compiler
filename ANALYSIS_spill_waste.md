# Innermost-Loop Spill Waste Analysis — 2026-07-30

## Method

Static analysis via `scripts/_loop_waste.py` + `scripts/_spill_audit.py`.
Hotness proxy = body_size × 10^(depth−1).  No dynamic counts available
(qemu 8.2.2 has no TCG plugin support).

## Top findings

### 1. `sl` — depth-3 innermost loop, 56 insns, 22 sp-relative accesses

Loop body: lines 152–208 of `/tmp/_srank/sl1.s`

```
live spill stores  :  8   (slot is read back somewhere in the function)
DEAD spill stores  :  6   (slot is NEVER read — pure waste)
  line 172: sd  t0, 384(sp)
  line 178: sd  t0, 400(sp)
  line 183: sd  t0, 416(sp)
  line 191: sw  t0, 440(sp)
  line 196: sw  t0, 456(sp)
  line 206: sw  t0, 488(sp)

Idle registers in this function: t2  a2  a3  a4  a5  a6  a7  (7 regs)
Loop is call-free → all 7 are freely usable inside the loop body.
```

Root cause: the register allocator is spilling live values to the stack and
then computing intermediate sums into fresh stack slots that are never
reloaded.  With 7 idle registers available and no calls in the loop, every
one of these stores is avoidable.

### 2. `03_sort` — depth-3 innermost loop, 25 insns, 10 sp-relative accesses

Loop body: lines 697–722

```
live spill stores  :  2
DEAD spill stores  :  5   (pure waste)
  line 705: sw  t0, 584(sp)
  line 707: sw  t0, 592(sp)
  line 709: sw  t0, 600(sp)
  line 713: sw  t0, 616(sp)
  line 715: sw  t0, 624(sp)

Idle registers: a4  a5  a6  a7  (4 regs)
```

Same pattern: intermediate values written to stack slots that are never
consumed.

## What to fix

Both cases point to the same allocator deficiency: when the allocator runs
out of its preferred register set it spills, but it does not consider the
full caller-saved set (a2–a7, t2–t6) before spilling.  In a call-free loop
those registers are as cheap as any other.

Concrete next step: in `RegisterAllocator.cpp`, when selecting a spill
candidate inside a loop body, first check whether any caller-saved register
is currently unallocated.  If yes, assign there instead of emitting a
store/load pair.  The dead stores will disappear automatically once the
values stay in registers.

Secondary: the dead stores themselves (intermediate sums written to stack
and never read) suggest that some IR values are being materialised as stack
slots by the IR builder rather than as virtual registers.  Check whether
`Mem2Reg` is running before register allocation on these functions.
