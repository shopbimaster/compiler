#!/bin/bash
# Count mechanical waste patterns in generated asm across the whole perf suite.
# Only counts instructions INSIDE loop spans (backward-edge regions), since
# that is where per-iteration cost actually matters.
#
# Patterns:
#   li_cmp   : `li tN, imm` feeding a compare -- loop-invariant constant
#              rematerialized every iteration (should be hoisted or use a
#              register held across the loop).
#   mv       : register-to-register copies (missed coalescing).
#   negw     : `li rX, 0` + `subw rD, rX, rY` (should be a single negw).
#   redundant_lw : the same `lw rD, off(rB)` repeated with no intervening
#              store/call to that base.
# usage: _waste.sh [dir-with-.s-files]
set -u
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1
dir=${1:-/tmp/_mm_all}

printf "%-22s %6s %6s %6s %6s %6s\n" case loopI li_cmp mv negw dupLw
tli=0; tmv=0; tneg=0; tlw=0; tin=0
for f in "$dir"/*.s; do
    [ -f "$f" ] || continue
    name=$(basename "$f" .s)
    read -r inl li mv neg dlw <<EOF
$(awk '
function isinstr(l) { return (l ~ /^[ \t]+[a-z]/ && l !~ /^[ \t]+\./) }
{ line[NR]=$0
  if ($0 ~ /^[^ \t#][^ \t]*:[ \t]*$/) { l=$0; sub(/:.*/,"",l); at[l]=NR } }
END {
    # mark every line that lies inside some backward-edge span
    for (i=1;i<=NR;i++) {
        if (!isinstr(line[i])) continue
        n=split(line[i],w,/[ \t,]+/); op=w[2]; tgt=w[n]
        if (op !~ /^(j|blt|bge|bne|beq|bltu|bgeu|bnez|beqz)$/) continue
        if (!(tgt in at)) continue
        s=at[tgt]; if (s>=i) continue
        for (k=s;k<=i;k++) inloop[k]=1
    }
    for (i=1;i<=NR;i++) {
        if (!inloop[i] || !isinstr(line[i])) continue
        ninloop++
        split(line[i],w,/[ \t,]+/); op=w[2]
        if (op=="mv") mv++
        if (op=="li") {
            # li feeding a compare within the next 2 instructions
            for (k=i+1;k<=i+2 && k<=NR;k++) {
                if (!isinstr(line[k])) continue
                split(line[k],v,/[ \t,]+/)
                if (v[2] ~ /^(blt|bge|bne|beq|bltu|bgeu)$/ && line[k] ~ w[3]) { li++; break }
            }
            if (w[4]=="0") { for (k=i+1;k<=i+2 && k<=NR;k++) {
                if (isinstr(line[k]) && line[k] ~ /subw?[ \t]/ && line[k] ~ w[3]) { neg++; break } } }
        }
        if (op=="lw") { key=w[3] " " w[4]
            if (key in seen) dlw++; else seen[key]=1 }
        if (op=="sw" || op=="call") delete seen
    }
    printf "%d %d %d %d %d\n", ninloop, li+0, mv+0, neg+0, dlw+0
}' "$f")
EOF
    printf "%-22s %6s %6s %6s %6s %6s\n" "$name" "$inl" "$li" "$mv" "$neg" "$dlw"
    tin=$((tin+inl)); tli=$((tli+li)); tmv=$((tmv+mv)); tneg=$((tneg+neg)); tlw=$((tlw+dlw))
done
echo "---"
printf "%-22s %6s %6s %6s %6s %6s\n" TOTAL "$tin" "$tli" "$tmv" "$tneg" "$tlw"
[ "$tin" -gt 0 ] && awk -v a="$tin" -v b="$((tli+tmv+tneg+tlw))" \
    'BEGIN{printf "waste = %d / %d in-loop instrs = %.1f%%\n", b, a, 100*b/a}'
