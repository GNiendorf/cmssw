#!/bin/bash
# A06 BATCH 4 -- the inner end is where the displaced cost lives (C_A minus E_W50 has
# IDENTICAL outer counts, so the whole -.0268 d15 / -.0128 v1030 is the inner arm).
# Push the OUTER-ONLY arm instead: find the -EXW/-EXR knee and try to buy the wide-window
# efficiency back with the two idle guards.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a06_ref/a06_run.sh"; P="$S/a06_ref"
launch() { tag="$1"; shift; setsid nohup bash "$R" "$tag" "$@" > "$P/nh_${tag}.out" 2>&1 < /dev/null & }
launch G_W75   -EXW 0.75 -EXR 5.0
launch G_WWU   -EXW 1.00 -EXR 6.0 -EXU 0.25
launch G_WWF   -EXW 1.00 -EXR 6.0 -EXF 1.2
launch G_WWUF  -EXW 1.00 -EXR 6.0 -EXU 0.25 -EXF 1.2
launch G_L8    -EXW 0.50 -EXR 4.0 -L 8.0
sleep 5
echo "[a06] BATCH4 LAUNCHED"
