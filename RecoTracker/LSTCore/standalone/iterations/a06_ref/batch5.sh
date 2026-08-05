#!/bin/bash
# A06 BATCH 5 -- D_R4 (-EXR 4.0 alone) came in at EXACTLY the baseline efficiency with
# length up and duplicate rate DOWN, so the rz half of the window is the cheap half
# (Extend.h says so: a 2S strip is ~5 cm long in z but ~100 um in r-phi, and one COMBINED
# window is forced to the worse of the two). Push rz alone, then price the ambiguity guard
# on the safe core.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a06_ref/a06_run.sh"; P="$S/a06_ref"
launch() { tag="$1"; shift; setsid nohup bash "$R" "$tag" "$@" > "$P/nh_${tag}.out" 2>&1 < /dev/null & }
launch H_R6      -EXR 6.0
launch H_R10     -EXR 10.0
launch H_W50R6   -EXW 0.50 -EXR 6.0
launch H_W50U    -EXW 0.50 -EXR 4.0 -EXU 0.25
launch H_W50L5   -EXW 0.50 -EXR 4.0 -L 5.0
launch H_WWU5F   -EXW 1.00 -EXR 6.0 -EXU 0.50 -EXF 1.2
sleep 5
echo "[a06] BATCH5 LAUNCHED"
