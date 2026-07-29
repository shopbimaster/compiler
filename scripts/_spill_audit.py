#!/usr/bin/env python3
"""Audit spill quality inside one loop body.

Answers two questions the summary table cannot:
  1. Dead spills: sw/sd to a stack slot that is never read back anywhere in
     the enclosing function.  Pure waste, no correctness role.
  2. Idle registers: callee/caller-saved regs never mentioned in the whole
     function while the loop body is spilling.  If these exist, the spills
     were avoidable.

Usage: _spill_audit.py file.s START_LINE END_LINE
"""
import sys, re

ALL_REGS = (['t%d' % i for i in range(7)]
            + ['s%d' % i for i in range(12)]
            + ['a%d' % i for i in range(8)])

STORE = re.compile(r'^\s+(sw|sd|sb|sh|fsw|fsd)\s+(\w+),\s*(-?\d+)\((sp|s0|fp)\)')
LOAD = re.compile(r'^\s+(lw|ld|lb|lbu|lh|lhu|flw|fld)\s+(\w+),\s*(-?\d+)\((sp|s0|fp)\)')


def func_bounds(lines, line_no):
    """Crude: walk out to nearest enclosing .globl / .size pair."""
    start = 0
    for i in range(line_no, -1, -1):
        if re.match(r'^\s*\.globl\b', lines[i]):
            start = i
            break
    end = len(lines)
    for i in range(line_no, len(lines)):
        if re.match(r'^\s*\.size\b', lines[i]):
            end = i
            break
    return start, end


def main():
    path, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    lines = open(path).read().splitlines()
    fs, fe = func_bounds(lines, lo)
    func = lines[fs:fe]
    body = lines[lo - 1:hi]

    # every slot read anywhere in the function
    read_slots = set()
    for l in func:
        m = LOAD.match(l)
        if m:
            read_slots.add(m.group(3))

    dead = []
    live_spill = 0
    for i, l in enumerate(body):
        m = STORE.match(l)
        if m:
            if m.group(3) not in read_slots:
                dead.append((lo + i, l.strip()))
            else:
                live_spill += 1

    used_regs = set()
    for l in func:
        for r in re.findall(r'\b([tsa]\d{1,2})\b', l):
            used_regs.add(r)
    idle = [r for r in ALL_REGS if r not in used_regs]

    print(f'function lines {fs+1}..{fe}, loop body {lo}..{hi}')
    print(f'\nlive spill stores in body : {live_spill}')
    print(f'DEAD spill stores in body : {len(dead)}  (slot never loaded in function)')
    for ln, txt in dead:
        print(f'   line {ln}: {txt}')
    print(f'\nregisters never used in this function ({len(idle)}): {" ".join(idle)}')


if __name__ == '__main__':
    main()
