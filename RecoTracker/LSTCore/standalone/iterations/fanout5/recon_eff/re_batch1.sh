#!/bin/bash
# re_batch1.sh -- RECON C ceiling ladder, batch 1 (cheap points, run in parallel).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_eff
GATEOFF="-M4 -1e9 -M4D -1e9 -M5 -1e9 -M6 -1e9 -MD -1e9 -MR -1e9 -MRI -1e9 -C25 -1e9 -C25D -1e9 -ZM4 0 -ZM4D 0"
CLAIMOFF="-F 2 -W 0 -FC -1 -PU 0 -a 999"
THETAOFF="-T4 -1e9 -T5 -1e9 -T6 -1e9 -U4 -1e9 -U5 -1e9 -U6 -1e9"

"$P/re_run.sh" noatt -a 999                         > "$P/drv_noatt.out"  2>&1 &
"$P/re_run.sh" g0    $GATEOFF                       > "$P/drv_g0.out"     2>&1 &
"$P/re_run.sh" g0n   $GATEOFF -a 999                > "$P/drv_g0n.out"    2>&1 &
"$P/re_run.sh" t0n   $THETAOFF -a 999               > "$P/drv_t0n.out"    2>&1 &
"$P/re_run.sh" tr0   -TR 0                          > "$P/drv_tr0.out"    2>&1 &
"$P/re_run.sh" c0n   $CLAIMOFF                      > "$P/drv_c0n.out"    2>&1 &
wait
echo "BATCH1 DONE"
