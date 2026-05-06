#!/bin/bash
# Phase 4 regression test — drives `./mos test 4` on classe (the canonical
# regression-chain runner in mos-docker). Replaces the older "compose up qemu-ph4"
# pattern that was retired in mos-docker's 2026-05-06 architecture refactor.
#
# Local validate-baseline.sh remains valid; this just kicks the test image,
# captures the baseline, then you run validate-baseline.sh 4 locally.
set -euo pipefail

echo "=== qemu-mos15 phase 4 test ==="
ssh docker "cd /home/matthew/mos-docker && sudo ./mos test 4"
