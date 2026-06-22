#!/bin/bash
/mnt/d/VSCodeProjects/compiler/build/compiler -S -O1 /mnt/d/VSCodeProjects/compiler/test/performance/huffman-01.sy -o /tmp/h1_raw.S
echo "=== Hot loop body (inline_read_bits_while_body_71_1) ==="
grep -n -A 20 'inline_read_bits_while_body_71_1:' /tmp/h1_raw.S | head -30
echo "=== mv instructions ==="
grep -n 'mv' /tmp/h1_raw.S | head -60
echo "=== or instructions ==="
grep -n 'or' /tmp/h1_raw.S | head -30