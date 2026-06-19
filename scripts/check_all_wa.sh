#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

for case in conv2d-1 h-8-01 h-9-01 h-1-01 many_mat_cal-1 matmul1; do
  echo "=== $case ==="
  bash "${SCRIPT_DIR}/bisect_wa.sh" "$case" 60 2>&1 | grep -E "(Level: o3|Level: O1|MATCH|DIFFER)"
  echo ""
done