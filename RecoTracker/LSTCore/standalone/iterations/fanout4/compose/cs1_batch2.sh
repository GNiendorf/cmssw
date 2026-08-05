#!/bin/bash
# cs1_batch2.sh -- SCANNER 1 decomposition isolates for the interaction ledger.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout4/compose"
R="bash $P/cs1_run.sh"

T5="-TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0"
BK="-BK 1 -BT 5"

# t5only: the T5 flag pack alone at -B 10 (no -BK). Completes the additivity ledger:
#         the cheapwins t5 reference carries -B 30, so it is NOT the right solo term.
$R t5only  $T5                        > $P/cs1_drv_t5only.txt 2>&1 &
# bkt5f: is -FCX/-FCE a no-op on the winner too, or only under the g3 band levers?
$R bkt5f   $BK $T5 -FCX 1 -FCE 1      > $P/cs1_drv_bkt5f.txt 2>&1 &
# bkt5dd: post-arbitration structural dedup alone (dup lever, priority #2).
$R bkt5dd  $BK $T5 -DD 4              > $P/cs1_drv_bkt5dd.txt 2>&1 &
# bkt5fs: the subordinate share pass alone (decomposes the fsdd result).
$R bkt5fs  $BK $T5 -FS 0.5            > $P/cs1_drv_bkt5fs.txt 2>&1 &
wait
echo "CS1_BATCH2_DONE" > $P/cs1_batch2.done
