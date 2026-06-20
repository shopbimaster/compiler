#!/bin/bash
B=/mnt/d/VSCodeProjects/compiler/build
for c in transpose0 shuffle0 sl2 fft0; do
    ${B}/compiler -S /mnt/d/VSCodeProjects/compiler/test/performance/${c}.sy -o /tmp/${c}_o2.S -o2 2>/dev/null
    ${B}/compiler -S /mnt/d/VSCodeProjects/compiler/test/performance/${c}.sy -o /tmp/${c}_o3.S -o3 2>/dev/null
    lines=$(diff /tmp/${c}_o2.S /tmp/${c}_o3.S 2>/dev/null | wc -l)
    echo "${c}: O2 vs O3 diff lines = ${lines}"
done