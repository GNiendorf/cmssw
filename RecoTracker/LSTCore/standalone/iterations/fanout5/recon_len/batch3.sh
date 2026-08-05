#!/bin/bash
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_len
R=$D/runf.sh
run4 () { for a in "$@"; do eval "$R $a" & done; wait; }
run4 "cb_bt4 -BT 4" "cb_tt25 -TT 2.5" "cb_bt4tt20 -BT 4 -TT 2.0" "cb_ta15 -TA 1.5"
run4 "cb_bt45tt20 -BT 4.5 -TT 2.0" "cb_tt20L10 -TT 2.0 -L 1.0" "cb_bt4ta15 -BT 4 -TA 1.5" "cb_bt45 -BT 4.5"
echo BATCH3DONE
