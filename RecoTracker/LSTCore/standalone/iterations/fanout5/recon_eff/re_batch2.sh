#!/bin/bash
# re_batch2.sh -- RECON C: flood ceiling + the -FS second-ownership-layer ladder + -DD
# structural-overlap diagnostics.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_eff
GATEOFF="-M4 -1e9 -M4D -1e9 -M5 -1e9 -M6 -1e9 -MD -1e9 -MR -1e9 -MRI -1e9 -C25 -1e9 -C25D -1e9 -ZM4 0 -ZM4D 0"
CLAIMOFF="-F 2 -W 0 -FC -1 -PU 0 -a 999"
THETAOFF="-T4 -1e9 -T5 -1e9 -T6 -1e9 -U4 -1e9 -U5 -1e9 -U6 -1e9"

"$P/re_run.sh" b0n   $GATEOFF $CLAIMOFF              > "$P/drv_b0n.out"   2>&1 &
"$P/re_run.sh" alln  $GATEOFF $CLAIMOFF $THETAOFF -P > "$P/drv_alln.out"  2>&1 &
"$P/re_run.sh" fs035 -FS 0.35                        > "$P/drv_fs035.out" 2>&1 &
"$P/re_run.sh" fs05  -FS 0.5                         > "$P/drv_fs05.out"  2>&1 &
"$P/re_run.sh" fs10  -FS 1.01                        > "$P/drv_fs10.out"  2>&1 &
"$P/re_run.sh" dd1   -DD 1                           > "$P/drv_dd1.out"   2>&1 &
"$P/re_run.sh" dd2   -DD 2                           > "$P/drv_dd2.out"   2>&1 &
wait
echo "BATCH2 DONE"
