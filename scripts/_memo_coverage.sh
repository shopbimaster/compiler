#!/bin/bash
# Reports which test cases actually trigger the recursiveMemoization pass
# (i.e. contain __opt_memo_* symbols in the generated assembly), and for
# those, verifies output equivalence with the pass disabled.
set +e
cd /mnt/c/Users/whoever/Desktop/hust/game/compiler2026-x || exit 1

GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64-static
COMPILER=./build_wsl/compiler
SYLIB=./build_b/libsylib.a
TMPDIR=/tmp/memo_cov
rm -rf $TMPDIR && mkdir -p $TMPDIR

echo "=== scanning for cases where memoization fires ==="
hits=()
for dir in test/functional test/h_functional test/performance; do
  for src in "$dir"/*.sy; do
    [ -e "$src" ] || continue
    name=$(basename "$src" .sy)
    $COMPILER -S "$src" -o "$TMPDIR/${name}.on.s" -O1 >/dev/null 2>&1
    if grep -q '__opt_memo' "$TMPDIR/${name}.on.s" 2>/dev/null; then
      echo "MEMOIZED: $dir/$name"
      hits+=("$dir/$name")
    fi
  done
done
echo "total: ${#hits[@]} case(s) trigger memoization"

if [ ${#hits[@]} -eq 0 ]; then
  echo "No case exercises the pass; nothing to differentially verify."
  exit 0
fi

echo ""
echo "=== differential verification (memo ON vs OFF) ==="
for entry in "${hits[@]}"; do
  dir=$(dirname "$entry")
  name=$(basename "$entry")
  src="$dir/$name.sy"
  infile="$dir/$name.in"

  OPT_DISABLE=recursiveMemoization $COMPILER -S "$src" \
    -o "$TMPDIR/${name}.off.s" -O1 >/dev/null 2>&1

  $GCC -march=rv64gc -mabi=lp64d -static -o "$TMPDIR/${name}.on" \
    "$TMPDIR/${name}.on.s" "$SYLIB" 2>/dev/null || { echo "LINK_FAIL(on): $name"; continue; }
  $GCC -march=rv64gc -mabi=lp64d -static -o "$TMPDIR/${name}.off" \
    "$TMPDIR/${name}.off.s" "$SYLIB" 2>/dev/null || { echo "LINK_FAIL(off): $name"; continue; }

  if [ -f "$infile" ]; then
    timeout 60 $QEMU "$TMPDIR/${name}.on"  < "$infile" > "$TMPDIR/${name}.on.txt"  2>/dev/null; on_ret=$?
    timeout 60 $QEMU "$TMPDIR/${name}.off" < "$infile" > "$TMPDIR/${name}.off.txt" 2>/dev/null; off_ret=$?
  else
    timeout 60 $QEMU "$TMPDIR/${name}.on"  > "$TMPDIR/${name}.on.txt"  2>/dev/null; on_ret=$?
    timeout 60 $QEMU "$TMPDIR/${name}.off" > "$TMPDIR/${name}.off.txt" 2>/dev/null; off_ret=$?
  fi

  if [ "$on_ret" != "$off_ret" ]; then
    echo "MISMATCH(exit): $name  on=$on_ret off=$off_ret"
  elif ! diff -q "$TMPDIR/${name}.on.txt" "$TMPDIR/${name}.off.txt" >/dev/null 2>&1; then
    echo "MISMATCH(stdout): $name"
  else
    echo "MATCH: $name (exit=$on_ret)"
  fi
done
