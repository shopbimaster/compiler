#!/bin/bash
/mnt/d/VSCodeProjects/compiler/build/compiler -S -O1 /mnt/d/VSCodeProjects/compiler/test/performance/huffman-01.sy -o /tmp/h1_opt.S
echo "=== Hot loop body ==="
grep -n -A 25 'inline_read_bits_while_body_71_1:' /tmp/h1_opt.S | head -35
echo "=== Total lines ==="
wc -l /tmp/h1_opt.S
echo "=== all mv instructions ==="
grep -c 'mv' /tmp/h1_opt.S