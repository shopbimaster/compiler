#!/bin/bash
/mnt/d/VSCodeProjects/compiler/build/compiler -S -O1 /mnt/d/VSCodeProjects/compiler/test/performance/huffman-01.sy -o /tmp/h1.S
echo "=== or/sllw/sraw/and instructions ==="
grep -nE '^\s+(or|sllw|sraw|and)\s' /tmp/h1.S | head -40
echo "=== mv instructions in decode_fixed_huffman ==="
grep -n 'mv' /tmp/h1.S | head -60