#!/bin/bash
# Quick batch test - run all functional tests and report results
# 使用 -O1（大写）对应测评服务器级别（全部优化 OALL = O1+O2+O3）
# 小写 -o1/-o2/-o3 仅用于本地逐级调试
set +e
cd /mnt/d/VSCodeProjects/compiler
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
TMPDIR=/tmp/func3
mkdir -p $TMPDIR

total=0 pass=0 compile_fail=0 link_fail=0 diff_fail=0 segfault=0 timeout=0

for src in test/functional/*.sy; do
  name=$(basename "$src" .sy)
  asm="$TMPDIR/${name}.S"
  bin="$TMPDIR/${name}_bin"
  infile="test/functional/${name}.in"
  outfile="test/functional/${name}.out"
  total=$((total+1))
  
  # Compile
  ./build/compiler -S "$src" -o "$asm" -O1 2>/dev/null
  if [ $? -ne 0 ]; then
    echo "COMPILE_FAIL: $name"
    compile_fail=$((compile_fail+1))
    continue
  fi
  
  # Link
  $GCC -march=rv64gc -mabi=lp64d -static -o "$bin" "$asm" build/libsylib.a 2>/dev/null
  if [ $? -ne 0 ]; then
    echo "LINK_FAIL: $name"
    link_fail=$((link_fail+1))
    continue
  fi
  
  # Run
  ret=0
  if [ -f "$infile" ]; then
    timeout 5 $QEMU "$bin" < "$infile" > "$TMPDIR/${name}_out.txt" 2>/dev/null
    ret=$?
  else
    timeout 5 $QEMU "$bin" > "$TMPDIR/${name}_out.txt" 2>/dev/null
    ret=$?
  fi
  
  if [ $ret -eq 124 ]; then
    echo "TIMEOUT: $name"
    timeout=$((timeout+1))
  elif [ $ret -eq 139 ]; then
    echo "SEGFAULT: $name"
    segfault=$((segfault+1))
  elif [ ! -f "$outfile" ]; then
    pass=$((pass+1))
  else
    head -n -1 "$outfile" > "$TMPDIR/${name}_expect.txt" 2>/dev/null
    if [ ! -s "$TMPDIR/${name}_expect.txt" ]; then
      > "$TMPDIR/${name}_expect.txt"
    fi
    printf '%s\n' "$(cat "$TMPDIR/${name}_out.txt")" > "$TMPDIR/${name}_act.txt"
    printf '%s\n' "$(cat "$TMPDIR/${name}_expect.txt")" > "$TMPDIR/${name}_expect.txt"
    if diff -q "$TMPDIR/${name}_act.txt" "$TMPDIR/${name}_expect.txt" > /dev/null 2>&1; then
      pass=$((pass+1))
    else
      echo "DIFF: $name"
      diff_fail=$((diff_fail+1))
    fi
  fi
done

echo ""
echo "============================================"
echo "Functional Tests (O1) Results:"
echo "  Total:       $total"
echo "  Pass:        $pass"
echo "  CompileFail: $compile_fail"
echo "  LinkFail:    $link_fail"
echo "  DiffFail:    $diff_fail"
echo "  Segfault:    $segfault"
echo "  Timeout:     $timeout"
echo "============================================"