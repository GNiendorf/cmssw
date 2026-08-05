#!/bin/bash
# A06 BATCH 6 -- the residual gap is BARREL, where the outer arm saturates (f12B only .682
# even at 239.7 ext/evt). -EXN 2 was a no-op at the FROZEN window because the window was
# binding; retest it now that the window is open, and with -EXJ 2 so a chain can walk two
# layers outward one MD at a time.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a06_ref/a06_run.sh"; P="$S/a06_ref"
launch() { tag="$1"; shift; setsid nohup bash "$R" "$tag" "$@" > "$P/nh_${tag}.out" 2>&1 < /dev/null & }
launch J_N2    -EXW 0.50 -EXR 4.0 -EXN 2
launch J_N2J2  -EXW 0.50 -EXR 4.0 -EXN 2 -EXJ 2
launch J_L5N2  -EXW 0.50 -EXR 4.0 -L 5.0 -EXN 2
sleep 5
echo "[a06] BATCH6 LAUNCHED"
