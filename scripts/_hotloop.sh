#!/bin/bash
# Print the tightest loop bodies of an asm file so we can eyeball what the
# innermost hot loop costs per iteration.
#
# A loop is detected as a branch/jump whose target label appears EARLIER in the
# file (a backward edge). The reported body is the span [target label .. branch],
# counting only real instructions (directives and labels excluded). Note the
# latch usually jumps back to a separate header block, so the span -- not the
# single block -- is what matters.
#
# usage: _hotloop.sh <file.s> [max_body_instrs]
f=$1
max=${2:-24}
awk -v max="$max" '
function isinstr(l) { return (l ~ /^[ \t]+[a-z]/ && l !~ /^[ \t]+\./) }
# pass 1: remember where each label sits and buffer the file
{
    line[NR] = $0
    if ($0 ~ /^[^ \t#][^ \t]*:[ \t]*$/) { lbl = $0; sub(/:.*/, "", lbl); at[lbl] = NR }
}
END {
    for (i = 1; i <= NR; i++) {
        l = line[i]
        if (!isinstr(l)) continue
        n = split(l, w, /[ \t,]+/)
        op = w[2]                       # w[1] is empty (leading blank)
        if (op !~ /^(j|blt|bge|bne|beq|bltu|bgeu|bgt|ble|bnez|beqz)$/) continue
        tgt = w[n]
        if (!(tgt in at)) continue
        s = at[tgt]
        if (s >= i) continue            # forward branch, not a loop
        cnt = 0
        for (k = s; k <= i; k++) if (isinstr(line[k])) cnt++
        if (cnt > max) continue
        printf "=== loop %s  (%d instrs/iter, lines %d-%d)\n", tgt, cnt, s, i
        for (k = s; k <= i; k++) if (isinstr(line[k]) || line[k] ~ /:[ \t]*$/) print line[k]
        print ""
    }
}
' "$f"
