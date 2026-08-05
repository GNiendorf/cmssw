#!/bin/bash
# P2.6d JOB 1 control: the SAME pinned toolchain run twice. Any branch that mismatches here is
# uninitialized-heap garbage in the -d debug ntuple, not a difference between the two states.
# usage: run_ctl.sh <cpu|cuda> <p25|head> [nevents]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26d_ref"
BK="${1:-cpu}"
WHICH="${2:-head}"
N="${3:-10}"
D="$REF/bin_${WHICH}_d"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
for leg in A B; do
  LD_LIBRARY_PATH="$D:$LD_LIBRARY_PATH" \
  LST_CHAIN_TC_DUMP="$REF/ctl_${BK}_${WHICH}_${leg}_tc.bin" \
  LST_CHAIN_CHAIN_DUMP="$REF/ctl_${BK}_${WHICH}_${leg}_ch.bin" \
    "$D/lst_$BK" -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
    -o "$REF/ctl_${BK}_${WHICH}_${leg}.root" > "$REF/ctl_${BK}_${WHICH}_${leg}.log" 2>&1
done
echo "########## CONTROL $BK/$WHICH : same toolchain twice ##########"
python3 "$STANDALONE/p20_bitcheck.py" "$REF/ctl_${BK}_${WHICH}_A.root" "$REF/ctl_${BK}_${WHICH}_B.root" || true
python3 "$STANDALONE/p25_ref/p25_repro.py" tc "$REF/ctl_${BK}_${WHICH}_A_tc.bin" "$REF/ctl_${BK}_${WHICH}_B_tc.bin" || true
python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/ctl_${BK}_${WHICH}_A_ch.bin" "$REF/ctl_${BK}_${WHICH}_B_ch.bin" || true
echo "CTL_DONE $BK $WHICH"
