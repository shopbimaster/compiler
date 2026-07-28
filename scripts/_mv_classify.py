#!/usr/bin/env python3
"""Classify in-loop `mv rd, rs` copies in generated asm to see which are
removable and by what kind of reasoning.

Run: python3 scripts/_mv_classify.py /tmp/_mm_all
"""
import re
import sys
import glob
import os
import collections

MV = re.compile(r"^mv\s+([a-z0-9]+),\s*([a-z0-9]+)$")
LABEL = re.compile(r"^\.?[A-Za-z_.$][\w.$]*:$")
BRANCH = re.compile(r"^(b\w+|j|jr|jal|jalr|ret|call|tail)\b")


def read(path):
    out = []
    for line in open(path):
        s = line.split("#")[0].strip()
        if s:
            out.append(s)
    return out


def loop_ranges(lines):
    """Backward-branch heuristic: label at i, branch to it at j>i => [i, j]."""
    label_at = {}
    for i, l in enumerate(lines):
        if LABEL.match(l):
            label_at[l[:-1]] = i
    spans = []
    for j, l in enumerate(lines):
        m = re.match(r"^(?:b\w+|j)\s+.*?([.\w$]+)$", l)
        if not m:
            continue
        t = m.group(1)
        if t in label_at and label_at[t] < j:
            spans.append((label_at[t], j))
    return spans


def in_loop_flags(lines):
    flag = [False] * len(lines)
    for a, b in loop_ranges(lines):
        for k in range(a, b + 1):
            flag[k] = True
    return flag


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "/tmp/_mm_all"
    kinds = collections.Counter()
    samples = collections.defaultdict(list)
    total_mv = 0

    for f in sorted(glob.glob(os.path.join(root, "*.s"))):
        lines = read(f)
        flags = in_loop_flags(lines)
        base = os.path.basename(f)
        for i, l in enumerate(lines):
            m = MV.match(l)
            if not m or not flags[i]:
                continue
            total_mv += 1
            rd, rs = m.groups()

            if rd == rs:
                kinds["self_mv (pure noise)"] += 1
                samples["self_mv (pure noise)"].append((base, i, l))
                continue

            # Scan forward within the straight-line run for the fate of rd/rs.
            fate = "other"
            for j in range(i + 1, min(i + 12, len(lines))):
                nxt = lines[j]
                if LABEL.match(nxt) or BRANCH.match(nxt):
                    fate = "run_end_before_use"
                    break
                m2 = MV.match(nxt)
                if m2 and m2.group(1) == rs and m2.group(2) == rd:
                    fate = "mv_back (rs<-rd): redundant round trip"
                    break
                toks = re.split(r"[\s,()]+", nxt)
                if rd in toks[1:]:
                    fate = "rd_used (real copy)"
                    break
                if toks and toks[0] not in ("sw", "sd", "sh", "sb", "fsw", "fsd") \
                        and len(toks) > 1 and toks[1] == rd:
                    fate = "rd_redefined_unused (DEAD copy)"
                    break
            kinds[fate] += 1
            if len(samples[fate]) < 4:
                samples[fate].append((base, i, l))

    print(f"in-loop mv total: {total_mv}\n")
    for k, v in kinds.most_common():
        print(f"{v:6d}  {100.0*v/max(total_mv,1):5.1f}%  {k}")
    print("\n--- samples ---")
    for k, ss in samples.items():
        print(f"[{k}]")
        for s in ss:
            print(f"    {s[0]}:{s[1]}  {s[2]}")


main()
