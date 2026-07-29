#!/usr/bin/env bash
# Compile every performance case to asm, then rank innermost loop bodies.
# Exists as a file because PowerShell eats $(...) and $VAR when this is
# passed inline through wsl -e bash -lc.
set -u
cd "$(dirname "$0")/.."
T=/tmp/_srank
mkdir -p "$T"
for f in test/performance/*.sy; do
    n=$(basename "$f" .sy)
    ./build_wsl/compiler -S -O1 -o "$T/$n.s" "$f" 2>/dev/null
done
echo "asm files: $(ls "$T"/*.s | wc -l)"
python3 scripts/_loop_waste.py "$T"/*.s
