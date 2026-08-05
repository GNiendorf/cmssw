#!/bin/bash
# DESCENT batch 1 -- axis probes on BASE = cs2 winner fm4d12
#   (ANCHOR + CTL + BASE2 + -ZM4D 1.2 -ZM4 -0.5).
# rep  : no override -> MUST reproduce cs2_fm4d12 bit-for-bit (provenance control).
# tl6  : NOVEL. -TL is the trim's "layers the remainder must keep" floor; nobody
#        scanned it. TL 6 admits only trims whose remainder still has >=6 layers,
#        i.e. mostly redundant arms -> trim volume down, track length preserved.
# tl4  : the same lever pushed the other way (eff-max direction).
# tt05 : more trim volume via the chi2 ratio + the concentrating guard (eff-max).
# bt2  : -BK 1 hinge below the BT>=3 range anyone has measured on a clean base.
# dd2  : NOVEL. cs1 proved -DD 4 is a no-op because two pass-1 chains share <=2 OT
#        hits; -DD 2 is therefore the FIRST -DD value that can actually fire without
#        -FS. Pure dup lever (ratemin direction).
# zr6  : the only -ZR variant that held the d510 floor in cs2 (fake garnish).
# rm1  : ratemin combo probe (low trim volume + low hinge + dup-tight -ZM4).
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/descent
cd "$P" || exit 1
./ds_run.sh rep                                            &
./ds_run.sh tl6  -TL 6                                     &
./ds_run.sh tl4  -TL 4                                     &
./ds_run.sh tt05 -TT 0.5 -TA 0.5                           &
./ds_run.sh bt2  -BT 2                                     &
./ds_run.sh dd2  -DD 2                                     &
./ds_run.sh zr6  -ZR6 1.3                                  &
./ds_run.sh rm1  -TT 1.5 -TA 3.0 -BT 3 -ZM4 0.0 -ZR6 1.3   &
wait
echo DS_BATCH1_DONE
