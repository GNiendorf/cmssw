#!/bin/bash
# re_batch5.sh -- RECON C: REACHABLE frontier of the two gate levers that own the whole
# gate-class efficiency headroom (-M4 = IP T4-class mX floor, -M4D = exempt T4-class mD
# floor), on the FULL flagship (attach on) so the numbers are directly floor-checkable.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_eff

"$P/re_run.sh" m4_38   -M4 3.8   > "$P/drv_m4_38.out"   2>&1 &
"$P/re_run.sh" m4_35   -M4 3.5   > "$P/drv_m4_35.out"   2>&1 &
"$P/re_run.sh" m4_30   -M4 3.0   > "$P/drv_m4_30.out"   2>&1 &
"$P/re_run.sh" m4d_15  -M4D -1.5 > "$P/drv_m4d_15.out"  2>&1 &
"$P/re_run.sh" m4d_20  -M4D -2.0 > "$P/drv_m4d_20.out"  2>&1 &
"$P/re_run.sh" m4d_30  -M4D -3.0 > "$P/drv_m4d_30.out"  2>&1 &
"$P/re_run.sh" zm4d_06 -ZM4D 0.6 > "$P/drv_zm4d_06.out" 2>&1 &
"$P/re_run.sh" zm4d_00 -ZM4D 0.0 > "$P/drv_zm4d_00.out" 2>&1 &
wait
echo "BATCH5 DONE"
