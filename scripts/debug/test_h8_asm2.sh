#!/bin/bash
# Inspect assembly around the select and surrounding blocks
set +e

echo "=== Full assembly around select (lines 380-450) ==="
sed -n '380,450p' /tmp/h8_after.S

echo ""
echo "=== Where is %slt defined? Look for the icmp that produces s0 ==="
# The SELECT uses s0 as trueVal. Find where s0 is defined.
grep -n "s0," /tmp/h8_after.S | head -30

echo ""
echo "=== Where is s11 (cond) defined? ==="
grep -n "s11," /tmp/h8_after.S | head -30

echo ""
echo "=== Look at the block before and_rhs_18 ==="
grep -n "merge_14\|and_rhs_18\|and_merge_19\|merge_17" /tmp/h8_after.S | head -20
