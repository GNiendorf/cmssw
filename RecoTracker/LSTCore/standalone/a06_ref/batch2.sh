#!/bin/bash
# A06 BATCH 2 -- combine the two levers batch 1 showed are nearly free (-EX 3 inner end,
# and the wider residual windows), then try to buy back the small efficiency/fake cost with
# the extension's two IDLE quality guards (-EXU ambiguity, -EXC own-fit, -EXF refit chi2).
# Each run is setsid-detached so harness process-group cleanup cannot kill it mid-flight;
# a06_post.sh finishes any run whose post-processing is interrupted.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a06_ref/a06_run.sh"
P="$S/a06_ref"
launch() { tag="$1"; shift; setsid nohup bash "$R" "$tag" "$@" > "$P/nh_${tag}.out" 2>&1 < /dev/null & }
launch C_A   -EX 3 -EXW 0.50 -EXR 4.0
launch C_AU  -EX 3 -EXW 0.50 -EXR 4.0 -EXU 0.10
launch C_AC  -EX 3 -EXW 0.50 -EXR 4.0 -EXC 1.0
launch C_AF  -EX 3 -EXW 0.50 -EXR 4.0 -EXF 1.2
launch C_B   -EX 3 -EXW 1.00 -EXR 6.0
sleep 5
echo "[a06] BATCH2 LAUNCHED"
