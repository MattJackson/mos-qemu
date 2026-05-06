#!/bin/bash
# Phase 2 regression test — drives `./mos test 2` on classe (the canonical
# regression-chain runner in mos-docker). Replaces the older "compose up qemu-ph2"
# pattern that was retired in mos-docker's 2026-05-06 architecture refactor.
#
# Local validate-baseline.sh remains valid; this just kicks the test image,
# captures the baseline, then you run validate-baseline.sh 2 locally.
set -euo pipefail

echo "=== qemu-mos15 phase 2 test ==="
ssh docker "cd /home/matthew/mos-docker && sudo ./mos test 2"
