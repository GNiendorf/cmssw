#!/bin/bash
# A09 batch 4. Two questions:
#  (1) -XCG 1 (bare-chain arm keyed on the seed's BEST chain logit instead of the pair's
#      own) sat ABOVE the baseline -XCT curve at the baseline's own operating point, and it
#      is a SIMPLIFICATION -- the arm stops needing the per-pair log. Pin its own -XCT curve
#      so the comparison is measured, not interpolated off one point.
#  (2) does -XCG 1 stack with the -RPS simplification at the fake-leaning point, i.e. can it
#      buy back the duplicate rate that deleting the bare-T3 term costs?
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
cd "$S" || exit 1
export BIN="$S/protoA09/bin/chainproto_a09"
launch() { tag="$1"; shift; nohup bash a09_ref/a09_run.sh "$tag" "$@" > "a09_ref/nohup_$tag.out" 2>&1 & }
launch E_G1X45   -XCG 1 -XCT 4.5 -XCD 2
launch E_G1X5    -XCG 1 -XCT 5   -XCD 2
launch E_G1X35   -XCG 1 -XCT 3.5 -XCD 2
launch E_R2G1A7  -RPS 2 -XCG 1 -AT3 7 -XCD 2
launch E_R3G1A7  -RPS 3 -XCG 1 -AT3 7 -XCD 2
launch E_R2G1    -RPS 2 -XCG 1 -XCD 2
wait
echo "BATCH4 ALL DONE"
