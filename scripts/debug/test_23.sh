#!/bin/bash
cd /mnt/d/VSCodeProjects/compiler
for lvl in o0 o1 o2 o3; do
    echo -n "O=$lvl  "
    timeout 10 ./build/compiler -S -$lvl test/h_functional/23_json.sy -o /tmp/23_test.S 2>/dev/null
    ec=$?
    if [ $ec -eq 124 ]; then
        echo "TIMEOUT"
    elif [ $ec -eq 0 ]; then
        echo "OK"
    else
        echo "FAIL=$ec"
    fi
done
