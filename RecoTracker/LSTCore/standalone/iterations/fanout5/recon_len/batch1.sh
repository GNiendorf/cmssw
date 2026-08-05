#!/bin/bash
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_len
R=$D/runf.sh
run4 () { for a in "$@"; do eval "$R $a" & done; wait; }
run4 "ab_notrim -TR 0" "ab_tt10 -TT 1.0" "ab_tt30 -TT 3.0" "ab_bk0 -BK 0 -BT 0"
run4 "ab_noatt -a 999 -RPS 0 -RD 0" "ab_norps -RPS 0" "ab_nord -RD 0" "ab_pu2 -PU 2"
run4 "ab_f030 -F 0.30" "ab_thr -M4 3.5 -M4D -0.75 -MRI 0.5" "ab_c25 -C25 2.0 -C25D -2.0" "ab_z0 -ZM4D 0 -ZM4 0"
run4 "ab_notrim_bk0 -TR 0 -BK 0 -BT 0" "ab_tl6 -TL 6" "ab_ta3 -TA 3.0" "ab_notrim_noatt -TR 0 -a 999 -RPS 0 -RD 0"
echo BATCH1DONE
