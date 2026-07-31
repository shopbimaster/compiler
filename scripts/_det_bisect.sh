#!/usr/bin/env bash
# Which pass makes the compiler nondeterministic?
#
# Compiles a case N times with each pass disabled in turn (OPT_DISABLE=<pass>).
# If disabling a pass makes the output stable, that pass is the culprit.
#
# Usage: _det_bisect.sh [case] [N]
set -u
cd "$(dirname "$0")/.."
CASE=${1:-crc1}
N=${2:-4}
CC=./build_wsl/compiler
SRC=test/performance/$CASE.sy
T=/tmp/_detb; mkdir -p $T

stable_with() {   # $1 = OPT_DISABLE value ("" for none)
    for i in $(seq "$N"); do
        OPT_DISABLE="$1" $CC --emit-ir -O1 -o "$T/x.$i.ll" "$SRC" >/dev/null 2>&1 || return 2
    done
    for i in $(seq 2 "$N"); do
        cmp -s "$T/x.1.ll" "$T/x.$i.ll" || return 1
    done
    return 0
}

echo "case=$CASE N=$N"
if stable_with ""; then
    echo "baseline is already STABLE -- nothing to bisect"; exit 0
fi
echo "baseline: NONDETERMINISTIC, bisecting..."

# Pass names as accepted by OPT_DISABLE (see Optimizer.cpp).
PASSES=$(grep -oE '"[a-zA-Z][a-zA-Z0-9]*"' src/opt/Optimizer.cpp \
         | tr -d '"' | sort -u)

for p in $PASSES; do
    stable_with "$p"; rc=$?
    case $rc in
        0) echo "  STABLE without: $p   <<< culprit" ;;
        2) : ;;   # compile failed, pass name probably not valid
        *) : ;;   # still unstable
    esac
done
echo "done"
