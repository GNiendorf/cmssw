#!/bin/bash
# A06 BATCH 3 -- price the extension's efficiency/fake cost against its refit chi2 guard,
# and split the two window knobs that E_W50 moved together.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a06_ref/a06_run.sh"; P="$S/a06_ref"
launch() { tag="$1"; shift; setsid nohup bash "$R" "$tag" "$@" > "$P/nh_${tag}.out" 2>&1 < /dev/null & }
launch D_F10  -EX 3 -EXW 0.50 -EXR 4.0 -EXF 1.0
launch D_F08  -EX 3 -EXW 0.50 -EXR 4.0 -EXF 0.8
launch D_WF12 -EXW 0.50 -EXR 4.0 -EXF 1.2
launch D_W50  -EXW 0.50
launch D_R4   -EXR 4.0
launch D_E3F  -EX 3 -EXF 1.0
sleep 5
echo "[a06] BATCH3 LAUNCHED"
