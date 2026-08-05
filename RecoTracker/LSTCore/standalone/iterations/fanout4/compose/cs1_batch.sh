#!/bin/bash
# cs1_batch.sh -- SCANNER 1 stack ladder: launch the 9 remaining configs in parallel.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/compose"
R="bash $P/cs1_run.sh"

T5="-TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0"
G3="-ZR 1.55 -ZM4 -1.0 -ZM4D 99 -ZBA 20"
BK="-BK 1 -BT 5"

$R bkt5      $BK $T5                          > $P/cs1_drv_bkt5.txt 2>&1 &
$R bkt5b30   $BK $T5 -B 30                    > $P/cs1_drv_bkt5b30.txt 2>&1 &
$R bkt5g3    $BK $T5 $G3                      > $P/cs1_drv_bkt5g3.txt 2>&1 &
$R bkt5g3f   $BK $T5 $G3 -FCX 1 -FCE 1        > $P/cs1_drv_bkt5g3f.txt 2>&1 &
$R bkg3      $BK $G3                          > $P/cs1_drv_bkg3.txt 2>&1 &
$R t5g3      $T5 $G3                          > $P/cs1_drv_t5g3.txt 2>&1 &
$R full_bt4  $BK $T5 $G3 -FCX 1 -FCE 1 -BT 4  > $P/cs1_drv_full_bt4.txt 2>&1 &
$R full_bt3  $BK $T5 $G3 -FCX 1 -FCE 1 -BT 3  > $P/cs1_drv_full_bt3.txt 2>&1 &
$R fsdd      $BK $T5 -FS 0.5 -DD 4            > $P/cs1_drv_fsdd.txt 2>&1 &
wait
echo "CS1_BATCH_DONE" > $P/cs1_batch.done
