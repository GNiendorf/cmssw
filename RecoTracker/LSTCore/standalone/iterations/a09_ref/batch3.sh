#!/bin/bash
# A09 batch 3: THE DECISIVE FRONTIER COMPARISON. Batch 1 showed every candidate deletion
# gains efficiency and pays duplicate rate, so the only fair question is efficiency AT
# MATCHED DUPLICATE RATE (equivalently, class-B retirements at matched class-A survivors).
# Each mode therefore gets its own -XCT curve, and the baseline curve is EXTENDED upward
# (-XCT 5) so the comparison is against measured points instead of an extrapolation.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
cd "$S" || exit 1
export BIN="$S/protoA09/bin/chainproto_a09"
launch() { tag="$1"; shift; nohup bash a09_ref/a09_run.sh "$tag" "$@" > "a09_ref/nohup_$tag.out" 2>&1 & }
launch D_R1X5     -XCT 5 -XCD 2                     # baseline curve extension (-RPS 1)
launch D_R0X3     -RPS 0 -XCT 3 -XCD 2
launch D_R0X25    -RPS 0 -XCT 2.5 -XCD 2
launch D_R2X35    -RPS 2 -XCT 3.5 -XCD 2
launch D_R2X3     -RPS 2 -XCT 3 -XCD 2
launch D_R2A7     -RPS 2 -AT3 7 -XCD 2              # the decoupled fake knob
launch D_R2A7X35  -RPS 2 -AT3 7 -XCT 3.5 -XCD 2     # ... brought back to baseline duplicates
launch D_G1       -XCG 1 -XCD 2                     # one-flag frontier probe on the baseline
wait
echo "BATCH3 ALL DONE"
