#!/usr/bin/env python3
"""Where do the in-loop `mv` copies sit relative to control flow?

The hypothesis after reading 01_mm1.s by hand: most are loop-latch PHI
copies, e.g.

    addiw   s1, s3, 1
    mv      s3, s1        <-- copy back into the PHI register
    j       .Lmm_while_cond_15

i.e. coalescePhis failed to coalesce s1 into s3, so the increment lands in
a scratch register and is copied back on every iteration.

Run: python3 scripts/_mv_latch.py /tmp/_mm_all
"""
import re
import sys
import glob
import os
import collections

MV = re.compile(r"^mv\s+([a-z0-9]+),\s*([a-z0-9]+)$")


def read(path):
    out = []
    for line in open(path):
        s = line.split("#")[0].strip()
        if s:
            out.append(s)
    return out


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "/tmp/_mm_all"
    pos = collections.Counter()
    # For latch copies: was rs defined by the immediately preceding instr?
    feeder = collections.Counter()
    samples = []

    for f in sorted(glob.glob(os.path.join(root, "*.s"))):
        L = read(f)
        for i, l in enumerate(L):
            m = MV.match(l)
            if not m:
                continue
            rd, rs = m.groups()
            nxt = L[i + 1] if i + 1 < len(L) else ""
            prv = L[i - 1] if i > 0 else ""

            if re.match(r"^j\s", nxt):
                pos["mv then `j`  (loop-latch / phi copy)"] += 1
                pm = re.match(r"^(\w[\w.]*)\s+([a-z0-9]+),", prv)
                if pm and pm.group(2) == rs:
                    feeder["rs defined by the immediately preceding instr"] += 1
                    if len(samples) < 6:
                        samples.append(
                            (os.path.basename(f), i, prv, l, nxt))
                else:
                    feeder["rs defined earlier / elsewhere"] += 1
            elif nxt.endswith(":") or nxt.startswith("."):
                pos["mv then label/directive"] += 1
            elif re.match(r"^b\w+\s", nxt):
                pos["mv then cond branch"] += 1
            elif re.match(r"^(call|tail|ret|jr|jalr)\b", nxt):
                pos["mv then call/ret"] += 1
            else:
                pos["mv mid-block"] += 1

    total = sum(pos.values())
    print(f"total mv in all asm: {total}\n")
    for k, v in pos.most_common():
        print(f"{v:6d}  {100.0*v/max(total,1):5.1f}%  {k}")

    lat = sum(feeder.values())
    if lat:
        print(f"\nof the {lat} latch copies:")
        for k, v in feeder.most_common():
            print(f"{v:6d}  {100.0*v/lat:5.1f}%  {k}")
    print("\n--- latch samples (prev / mv / next) ---")
    for s in samples:
        print(f"  {s[0]}:{s[1]}")
        for line in s[2:]:
            print(f"      {line}")


main()
