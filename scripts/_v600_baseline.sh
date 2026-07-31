#!/usr/bin/env bash
# Build v6.0.0 in a worktree and run the same functional regression to
# establish a baseline for the 8 known failures.
set +e
REPO=/mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x
WT=/tmp/wt_v600
BUILD=$WT/build

cd "$REPO"
git worktree remove --force "$WT" 2>/dev/null || true
git worktree add "$WT" v6.0.0

cmake -S "$WT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release > /tmp/v600_cmake.log 2>&1
cmake --build "$BUILD" -j8 > /tmp/v600_build.log 2>&1
echo "build rc=$?"

# Run regression using the v6.0.0 compiler binary
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64-static
COMPILER=$BUILD/compiler
SYLIB=$REPO/build_b/libsylib.a
TMPDIR=/tmp/v600_regress
rm -rf "$TMPDIR" && mkdir -p "$TMPDIR"

total=0 pass=0 diff_fail=0 timeout=0 segfault=0 compile_fail=0

run_suite() {
  local dir="$REPO/$1"
  for src in "$dir"/*.sy; do
    [ -e "$src" ] || continue
    local name=$(basename "$src" .sy)
    local asm="$TMPDIR/${name}.S"
    local bin="$TMPDIR/${name}_bin"
    local infile="${dir}/${name}.in"
    local outfile="${dir}/${name}.out"
    total=$((total+1))
    $COMPILER -S "$src" -o "$asm" -O1 >/dev/null 2>&1 || { compile_fail=$((compile_fail+1)); continue; }
    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB" 2>/dev/null || continue
    local ret=0
    if [ -f "$infile" ]; then
      timeout 15 $QEMU "$bin" < "$infile" > "$TMPDIR/${name}_out.txt" 2>/dev/null; ret=$?
    else
      timeout 15 $QEMU "$bin" > "$TMPDIR/${name}_out.txt" 2>/dev/null; ret=$?
    fi
    [ $ret -eq 124 ] && { echo "TIMEOUT: $name"; timeout=$((timeout+1)); continue; }
    [ $ret -eq 139 ] && { echo "SEGFAULT: $name"; segfault=$((segfault+1)); continue; }
    [ -f "$outfile" ] || { pass=$((pass+1)); continue; }
    {
      if [ -s "$TMPDIR/${name}_out.txt" ]; then
        cat "$TMPDIR/${name}_out.txt"
        [ -n "$(tail -c 1 "$TMPDIR/${name}_out.txt")" ] && echo
      fi
      echo "$ret"
    } | sed 's/[[:space:]]*$//' > "$TMPDIR/${name}_act.txt"
    sed 's/[[:space:]]*$//' "$outfile" > "$TMPDIR/${name}_exp.txt"
    if diff -q -B "$TMPDIR/${name}_act.txt" "$TMPDIR/${name}_exp.txt" >/dev/null 2>&1; then
      pass=$((pass+1))
    else
      echo "DIFF: $name"
      diff_fail=$((diff_fail+1))
    fi
  done
}

run_suite test/functional
run_suite test/h_functional

echo ""
echo "=== v6.0.0 baseline ==="
echo "Total=$total Pass=$pass CompileFail=$compile_fail DiffFail=$diff_fail Timeout=$timeout Segfault=$segfault"
