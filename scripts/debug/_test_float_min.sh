#!/bin/bash
GCC=riscv64-linux-gnu-gcc
QEMU=qemu-riscv64
BUILD_DIR=/mnt/d/VSCodeProjects/compiler/build
SYLIB_A=${BUILD_DIR}/libsylib.a
TMP=~/tmp2
export TMPDIR=$TMP

# Create a minimal test
cat > ${TMP}/_test_float.sy << 'EOF'
const float HEX2 = 0x.AP-3, FACT = -.33E+5, EPS = 1e-6;

float float_abs(float x) {
  if (x < 0) return -x;
  return x;
}

int float_eq(float a, float b) {
  if (float_abs(a - b) < EPS) {
    return 1;
  } else {
    return 0;
  }
}

int main() {
  int result = float_eq(HEX2, FACT);
  putint(result);
  putch(10);
  return 0;
}
EOF

${BUILD_DIR}/compiler -S ${TMP}/_test_float.sy -o ${TMP}/_tf.S -O0 2>&1
$GCC -march=rv64gc -mabi=lp64d -static -o ${TMP}/_tf_bin ${TMP}/_tf.S "$SYLIB_A" 2>&1
timeout 5 $QEMU ${TMP}/_tf_bin 2>&1
echo "Exit: $?"