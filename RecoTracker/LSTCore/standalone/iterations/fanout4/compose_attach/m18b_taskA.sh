#!/bin/bash
# m18b_taskA.sh -- M18b CLOSEOUT Task A: the FLAGSHIP composition runs.
# Repro gate (defaults) + 3 named trims x ATTACH ADD-ON + 2 owed lever probes.
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose_attach
cd "$P" || exit 1
ATT="-a 8 -RPS 1 -RD 1"

./at_run_nostack.sh fl_repro                                  > $P/m18b_fl_repro.out 2>&1 &
./at_run.sh fl_balanced     -TT 1.2              $ATT         > $P/m18b_fl_balanced.out 2>&1 &
./at_run.sh fl_effmax       -TT 0.5 -TA 0.5      $ATT         > $P/m18b_fl_effmax.out 2>&1 &
./at_run.sh fl_ratemin      -TT 1.5 -TA 3.0 -ZM4 0.0 $ATT     > $P/m18b_fl_ratemin.out 2>&1 &
./at_run.sh fl_balanced_fce -TT 1.2 $ATT -FCX 1 -FCE 1        > $P/m18b_fl_balanced_fce.out 2>&1 &
./at_run.sh fl_balanced_fs  -TT 1.2 $ATT -FS 0.5 -DD 4        > $P/m18b_fl_balanced_fs.out 2>&1 &
wait
echo M18B_TASKA_DONE
