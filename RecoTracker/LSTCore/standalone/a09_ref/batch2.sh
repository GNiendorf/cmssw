#!/bin/bash
# A09 batch 2 (NEW binary): no-op gate, the unification map, the -RPS variants and a first
# -XCT bracket for the leading structural variant.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
cd "$S" || exit 1
# The A09 binary is built under a SEPARATE name so protoA09/bin/chainproto stays the
# unmodified protoFIN binary (md5 519b0abc...) that batch 1 ran on.
export BIN="$S/protoA09/bin/chainproto_a09"
launch() { tag="$1"; shift; nohup bash a09_ref/a09_run.sh "$tag" "$@" > "a09_ref/nohup_$tag.out" 2>&1 & }
launch G2       -XCD 2                    # no-op gate on the new binary: must == A_NOOP
launch A_UM     -XCD 2 -UM 1              # the seed-retirement unification map
launch B_R2     -RPS 2 -XCD 2             # chain half only (score test)
launch B_R3     -RPS 3 -XCD 2             # STRUCTURAL, both halves
launch B_R3C1   -RPS 3 -CCR 1 -XCD 2      # prediction: bit-identical to B_R3
launch B_R4     -RPS 4 -XCD 2             # structural T3 half, shipped chain half
launch B_R3A7   -RPS 3 -AT3 7 -XCD 2      # is -AT3 now a CLEAN fake knob?
launch B_R3X3   -RPS 3 -XCT 3 -XCD 2      # buy the released duplicates back
wait
echo "BATCH2 ALL DONE"
