#!/bin/bash
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_len
R=$D/runf.sh
run4 () { for a in "$@"; do eval "$R $a" & done; wait; }
run4 "ll_L30 -L 3.0" "ll_L50 -L 5.0" "ll_L20bt4 -L 2.0 -BT 4" "ll_L20tt25 -L 2.0 -TT 2.5"
run4 "ll_L30bt4tt25 -L 3.0 -BT 4 -TT 2.5" "ll_L20bt4tt20 -L 2.0 -BT 4 -TT 2.0" "ll_L100 -L 10.0" "ll_L15 -L 1.5"
echo BATCH4DONE
