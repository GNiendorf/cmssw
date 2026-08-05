#!/bin/bash
# re_batch6.sh -- RECON C: does the displaced headroom in -M4D survive once its fake cost
# is paid back by a lever that does NOT touch the displaced branch (-C25 cell kill /
# -M4 IP T4-class)? Composite probes on the full flagship.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_eff
"$P/re_run.sh" cmp1 -M4D -2.0 -C25 1.5          > "$P/drv_cmp1.out" 2>&1 &
"$P/re_run.sh" cmp2 -M4D -2.0 -C25 2.5          > "$P/drv_cmp2.out" 2>&1 &
"$P/re_run.sh" cmp3 -M4D -2.0 -M4 4.5           > "$P/drv_cmp3.out" 2>&1 &
"$P/re_run.sh" cmp4 -M4D -1.8 -C25 1.0          > "$P/drv_cmp4.out" 2>&1 &
"$P/re_run.sh" c25a -C25 1.5                    > "$P/drv_c25a.out" 2>&1 &
"$P/re_run.sh" c25b -C25 2.5                    > "$P/drv_c25b.out" 2>&1 &
wait
echo "BATCH6 DONE"
