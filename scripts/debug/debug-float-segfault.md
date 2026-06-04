# Debug Session: float-segfault
- **Status**: [OPEN]
- **Issue**: 95_float test case segfaults in qemu-riscv64
- **Debug Server**: http://127.0.0.1:PORT/event
- **Log File**: .dbg/trae-debug-log-float-segfault.ndjson

## Reproduction Steps
1. Compile: `./build/sysyc -S test/functional/95_float.sy -o ~/tmp/95_float.S -O0`
2. Link: `riscv64-linux-gnu-gcc -static -o ~/tmp/95_bin ~/tmp/95_float.S build/libsylib.a`
3. Run: `echo 0 | qemu-riscv64 ~/tmp/95_bin`

## Hypotheses & Verification
| ID | Hypothesis | Likelihood | Effort | Evidence |
|----|------------|------------|--------|----------|
| A | Float global variable access (LA + FLW) segfaults because of incorrect type handling | High | Low | Pending |
| B | Float function call argument passing (fa0-fa7) causes segfault | High | Low | Pending |
| C | Float array operation (getfarray/putfarray) causes segfault | Med | Low | Pending |
| D | Stack frame layout error for float variables (wrong offset/size) | Med | Low | Pending |
| E | float_abs function uses wrong instructions for float negation | Low | Low | Pending |

## Log Evidence
[Key log entries]

## Verification Conclusion
[Pre-fix vs post-fix comparison]