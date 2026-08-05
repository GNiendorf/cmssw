#!/bin/bash
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_len
R=$D/runf.sh
run4 () { for a in "$@"; do eval "$R $a" & done; wait; }
run4 "lv_bk6 -BK 6" "lv_bk11 -BK 11 -BT 1" "lv_bt3 -BT 3" "lv_bt2 -BT 2"
run4 "lv_tl6 -TL 6" "lv_tt20 -TT 2.0" "lv_ta5 -TA 5.0" "lv_L10 -L 1.0"
run4 "lv_bt8 -BT 8" "lv_L20 -L 2.0" "lv_tt15 -TT 1.5" "lv_ta2 -TA 2.0"
run4 "lv_bk6tl6 -BK 6 -TL 6" "lv_tl6ta3 -TL 6 -TA 3.0" "lv_bk11b2 -BK 11 -BT 2" "lv_tp0tl6 -TL 6 -TT 1.0"
echo BATCH2DONE
