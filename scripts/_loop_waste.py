#!/usr/bin/env python3
"""Find innermost loops in generated RISC-V asm and report concrete waste.

Why not just total instruction count: that measures code size, which tracks
source complexity, not runtime.  What matters is the innermost loop body,
because that is what executes N^depth times.

For each function we build the basic-block graph from labels and branches,
find natural loops via backward branches, compute nesting depth, then for the
INNERMOST loops report:
  depth   - loop nesting depth (1 = outermost only)
  size    - instructions in the loop body
  ld/st   - memory operations (candidates for LICM / redundant-load removal)
  spill   - sp/fp-relative accesses (register allocator pressure)
  addr    - address arithmetic (slli/add chains = GEP not strength-reduced)

Usage: _loop_waste.py file.s [file.s ...]
"""
import sys, re, os
from collections import defaultdict

LABEL = re.compile(r'^([A-Za-z_.$][\w.$]*)\s*:')
INSN = re.compile(r'^\s+([a-z][\w.]*)\s*(.*)$')
FUNC_START = re.compile(r'^\s*\.globl\s+(\S+)')
# branches: cond branches have target as last operand; j/jal single operand
BR_COND = re.compile(r'^\s+(beq|bne|blt|bge|bltu|bgeu|beqz|bnez|bltz|bgez|blez|bgtz)\b\s+(.*)$')
BR_UNCOND = re.compile(r'^\s+(j|jr|tail)\b\s+(\S+)')

MEM_LD = {'lw','ld','lb','lbu','lh','lhu','flw','fld'}
MEM_ST = {'sw','sd','sb','sh','fsw','fsd'}
ADDR_OPS = {'slli','add','addi','sub','mul'}  # only counted when feeding a mem op


def parse(path):
    lines = open(path).read().splitlines()
    return lines


def analyze(path):
    lines = parse(path)
    # Map label -> line index
    label_at = {}
    for i, l in enumerate(lines):
        m = LABEL.match(l)
        if m:
            label_at[m.group(1)] = i

    # Find backward branches -> loop (header_line, latch_line)
    loops = []
    for i, l in enumerate(lines):
        tgt = None
        m = BR_COND.match(l)
        if m:
            ops = [o.strip() for o in m.group(2).split(',')]
            if ops:
                tgt = ops[-1]
        else:
            m = BR_UNCOND.match(l)
            if m:
                tgt = m.group(2)
        if tgt and tgt in label_at and label_at[tgt] < i:
            loops.append((label_at[tgt], i))

    if not loops:
        return []

    # Nesting: loop A contains loop B if A.header <= B.header and A.latch >= B.latch
    def contains(a, b):
        return a[0] <= b[0] and a[1] >= b[1] and a != b

    results = []
    for lp in loops:
        depth = 1 + sum(1 for other in loops if contains(other, lp))
        innermost = not any(contains(lp, other) for other in loops)
        if not innermost:
            continue
        h, t = lp
        body = lines[h:t + 1]
        size = ld = st = spill = addr = mul = div = 0
        for b in body:
            m = INSN.match(b)
            if not m:
                continue
            op, args = m.group(1), m.group(2)
            size += 1
            base = op
            if base in MEM_LD:
                ld += 1
                if re.search(r'\((sp|s0|fp)\)', args):
                    spill += 1
            elif base in MEM_ST:
                st += 1
                if re.search(r'\((sp|s0|fp)\)', args):
                    spill += 1
            elif base in ('slli', 'sll'):
                addr += 1
            elif base in ('mul', 'mulw'):
                mul += 1
            elif base in ('div', 'divw', 'rem', 'remw'):
                div += 1
        results.append(dict(depth=depth, size=size, ld=ld, st=st,
                            spill=spill, addr=addr, mul=mul, div=div,
                            header=h + 1))
    return results


def main():
    rows = []
    for path in sys.argv[1:]:
        name = os.path.basename(path).replace('.s', '')
        for r in analyze(path):
            r['case'] = name
            # hotness proxy: body size scaled by nesting depth
            r['hot'] = r['size'] * (10 ** (r['depth'] - 1))
            rows.append(r)
    rows.sort(key=lambda r: -r['hot'])
    print(f"{'case':<26}{'dep':>4}{'size':>6}{'ld':>4}{'st':>4}{'spill':>6}"
          f"{'shift':>6}{'mul':>4}{'div':>4}{'line':>6}")
    print('-' * 76)
    for r in rows[:40]:
        print(f"{r['case']:<26}{r['depth']:>4}{r['size']:>6}{r['ld']:>4}"
              f"{r['st']:>4}{r['spill']:>6}{r['addr']:>6}{r['mul']:>4}"
              f"{r['div']:>4}{r['header']:>6}")


if __name__ == '__main__':
    main()
