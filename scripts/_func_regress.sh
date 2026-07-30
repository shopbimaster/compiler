#!/bin/bash
# Full functional regression over test/functional + test/h_functional at -O1.
# Adapted from run_func_tests.sh (which hardcodes an old /mnt/d path and
# ./build) to this checkout's paths and build_wsl output.
set +e
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64-static
COMPILER=${COMPILER:-./build_wsl/compiler}
SYLIB=./build_b/libsylib.a
TMPDIR=${FUNC_REGRESS_TMPDIR:-/tmp/func_regress}
rm -rf "$TMPDIR" && mkdir -p "$TMPDIR"

total=0 pass=0 compile_fail=0 link_fail=0 diff_fail=0 segfault=0 timeouts=0

run_suite() {
  local dir="$1"
  echo "=== suite: $dir ==="
  for src in "$dir"/*.sy; do
    [ -e "$src" ] || continue
    local name=$(basename "$src" .sy)
    local asm="$TMPDIR/${name}.S"
    local bin="$TMPDIR/${name}_bin"
    local infile="${dir}/${name}.in"
    local outfile="${dir}/${name}.out"
    total=$((total+1))

    $COMPILER -S "$src" -o "$asm" -O1 >/dev/null 2>&1
    if [ $? -ne 0 ]; then
      echo "COMPILE_FAIL: $name"
      compile_fail=$((compile_fail+1))
      continue
    fi

    $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" "$SYLIB" 2>/dev/null
    if [ $? -ne 0 ]; then
      echo "LINK_FAIL: $name"
      link_fail=$((link_fail+1))
      continue
    fi

    local ret=0
    if [ -f "$infile" ]; then
      timeout 15 $QEMU "$bin" < "$infile" > "$TMPDIR/${name}_out.txt" 2>/dev/null
      ret=$?
    else
      timeout 15 $QEMU "$bin" > "$TMPDIR/${name}_out.txt" 2>/dev/null
      ret=$?
    fi

    if [ $ret -eq 124 ]; then
      echo "TIMEOUT: $name"
      timeouts=$((timeouts+1))
      continue
    fi
    if [ $ret -eq 139 ]; then
      echo "SEGFAULT: $name"
      segfault=$((segfault+1))
      continue
    fi
    if [ ! -f "$outfile" ]; then
      pass=$((pass+1))
      continue
    fi

    # SysY convention: expected output is the program's stdout followed by
    # its exit code on the final line. The program's stdout often lacks a
    # trailing newline, so add one before appending the exit code (otherwise
    # the code gets glued onto the last output line and every such test
    # spuriously "fails").
    {
      if [ -s "$TMPDIR/${name}_out.txt" ]; then
        cat "$TMPDIR/${name}_out.txt"
        [ -n "$(tail -c 1 "$TMPDIR/${name}_out.txt")" ] && echo
      fi
      echo "$ret"
    } | sed -e 's/[[:space:]]*$//' > "$TMPDIR/${name}_act.txt"
    sed -e 's/[[:space:]]*$//' "$outfile" > "$TMPDIR/${name}_exp.txt"
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
echo "============================================"
echo "Functional regression (-O1):"
echo "  Total:       $total"
echo "  Pass:        $pass"
echo "  CompileFail: $compile_fail"
echo "  LinkFail:    $link_fail"
echo "  DiffFail:    $diff_fail"
echo "  Segfault:    $segfault"
echo "  Timeout:     $timeouts"
echo "============================================"
