#!/bin/bash
# re_batch3.sh -- RECON C: attach-off family for the second-ownership-layer sizing plus
# the per-lever decomposition of "claim off" (-F/-FC budget vs -W braid vs -PU pre-claim).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_eff

"$P/re_run.sh" fs10n -FS 1.01 -a 999             > "$P/drv_fs10n.out" 2>&1 &
"$P/re_run.sh" fs05n -FS 0.5  -a 999             > "$P/drv_fs05n.out" 2>&1 &
"$P/re_run.sh" f2n   -F 2 -FC -1 -PU 0 -a 999    > "$P/drv_f2n.out"   2>&1 &
"$P/re_run.sh" w0n   -W 0 -a 999                 > "$P/drv_w0n.out"   2>&1 &
"$P/re_run.sh" pu0n  -PU 0 -a 999                > "$P/drv_pu0n.out"  2>&1 &
"$P/re_run.sh" fcn   -FC 1e9 -a 999              > "$P/drv_fcn.out"   2>&1 &
wait
echo "BATCH3 DONE"
