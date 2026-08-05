#!/bin/bash
# m18b_taskB.sh -- M18b CLOSEOUT Task B: BK-hinge (a2 head) vs ve-head on the SAME
# flagship stack. The ve binary lives in compose_ve; the a2 binary in compose_attach.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
V=$S/fanout4/compose_ve
A=$S/fanout4/compose_attach
ATT="-a 8 -RPS 1 -RD 1"

# ve workspace: (1) same-flags gate vs Task A fl_balanced, (2) the ve-head order key,
# (3) an attach-free arm so the head swap can be judged with f11 out of the picture.
cd "$V" || exit 1
./at_run.sh ve_gate       -TT 1.2 $ATT            > $V/m18b_ve_gate.out 2>&1 &
./at_run.sh ve_balanced   -TT 1.2 $ATT -BK 0 -B 80 > $V/m18b_ve_balanced.out 2>&1 &
./at_run.sh ve_noatt      -TT 1.2                 > $V/m18b_ve_noatt.out 2>&1 &
# a2 reference for the attach-free arm.
cd "$A" || exit 1
./at_run.sh fl_noatt      -TT 1.2                 > $A/m18b_fl_noatt.out 2>&1 &
wait
echo M18B_TASKB_DONE
