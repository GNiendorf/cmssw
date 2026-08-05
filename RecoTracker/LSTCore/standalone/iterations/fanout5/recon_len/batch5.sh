#!/bin/bash
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_len
R=$D/runf.sh
for a in "jt_L25bt45 -L 2.5 -BT 4.5" "jt_L30bt45 -L 3.0 -BT 4.5" "jt_L30bt4 -L 3.0 -BT 4" "jt_L20bt45 -L 2.0 -BT 4.5"; do eval "$R $a" & done; wait
echo BATCH5DONE
