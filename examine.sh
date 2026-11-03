#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ ${DX_LR32_POWER_AND_WIRING_VERIFIED:-} != yes ]]; then
  echo 'STOP: verify target supply, common ground, SWD wiring and actual programmer pin functions first.' >&2
  exit 2
fi
mkdir -p logs
# No retries. Preserve the first failure for diagnosis.
timeout 20s openocd -f configs/generic-examine.cfg 2>&1 | tee logs/swd-examine.log
