#!/bin/bash
# re_batch4.sh -- RECON C: per-lever gate sub-ablations (which threshold owns the GATE
# class) + terminal-trim relaxations (trim is measured to COST track length) + the
# unbounded-second-layer upper bound.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_eff

"$P/re_run.sh" gM4    -M4 -1e9 -ZM4 0        -a 999 > "$P/drv_gM4.out"    2>&1 &
"$P/re_run.sh" gM4D   -M4D -1e9 -ZM4D 0      -a 999 > "$P/drv_gM4D.out"   2>&1 &
"$P/re_run.sh" gMRI   -MRI -1e9              -a 999 > "$P/drv_gMRI.out"   2>&1 &
"$P/re_run.sh" gMR    -MR -1e9               -a 999 > "$P/drv_gMR.out"    2>&1 &
"$P/re_run.sh" gC25   -C25 -1e9 -C25D -1e9   -a 999 > "$P/drv_gC25.out"   2>&1 &
"$P/re_run.sh" fs10nw -FS 1.01 -W 0          -a 999 > "$P/drv_fs10nw.out" 2>&1 &
"$P/re_run.sh" tt20   -TT 2.0                       > "$P/drv_tt20.out"   2>&1 &
"$P/re_run.sh" tt30   -TT 3.0                       > "$P/drv_tt30.out"   2>&1 &
"$P/re_run.sh" tl6    -TL 6                         > "$P/drv_tl6.out"    2>&1 &
wait
echo "BATCH4 DONE"
