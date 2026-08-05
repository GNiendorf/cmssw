#!/bin/bash
# tw_scan.sh -- sizing + quality-bar scan of the capped 2-way ownership pass.
# Sequential on purpose (timing is reported per run; parallel runs contaminate it).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_twoway
cd "$P" || exit 1
# SIZING: bar wide open, cap 2, braid kept -> the upper bound of the capped mechanism.
./tw_run.sh size2   -TW -1e9 -TWC 2
# SIZING (no braid): the strict upper bound of "hit may be owned twice" with no other test.
./tw_run.sh size2nb -TW -1e9 -TWC 2 -TWB 0
# QUALITY-BAR SCAN at cap 2, braid kept.
./tw_run.sh b0      -TW 0   -TWC 2
./tw_run.sh b2      -TW 2   -TWC 2
./tw_run.sh b4      -TW 4   -TWC 2
./tw_run.sh b6      -TW 6   -TWC 2
./tw_run.sh b8      -TW 8   -TWC 2
echo "[tw] SCAN COMPLETE"
