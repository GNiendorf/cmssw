#!/bin/bash
# at_scan2.sh -- batch 2. Two things at once:
#  (a) -RPS 1 variants: the ONLY flag that retires bare-pLS (type-8) rows at scale.
#      Batch 1 showed plain contention is near-inert under -RT5 1, because an attached
#      pLS's carried row is usually a type-7 that -RT5 already dropped: 894 attaches/evt
#      retired only 13 extra rows. -RPS drops the type-8 row of ANY pLS with a scored
#      pair above the margin, owner or not, so it is the lever that can actually reach
#      the "chain re-delivers a sim whose only LST TC was the pLS" cell.
#  (b) the two TIGHT frontier points (a=11, a=13) that batch 1 did not cover, plus a
#      re-run of the batch-1 points under the upgraded (target-level) instrument so the
#      confusion matrix is complete and consistent across every row of the report.
cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout4/compose_attach
run() { PROTO_ATTACH_CM=1 ./at_run.sh "$@" > "scan_$1.out" 2>&1; }
for A in 0 4 7 9; do run a${A}rps -a $A -RPS 1 & done
for A in 11 13; do run a$A -a $A & done
for A in 0 4 7 9; do run a$A -a $A & done
wait
echo BATCH2DONE > scan2.done
