#!/bin/bash
# P2.6d: compare any two fully pinned toolchains on the chain + TC sidecars.
# usage: run_pair.sh <dirA> <dirB> <cpu|cuda> [nevents] [tag]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26d_ref"
A="$REF/${1:?dirA}"
B="$REF/${2:?dirB}"
BK="${3:-cpu}"
N="${4:-10}"
TAG="${5:-pair}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
echo "########## $TAG : $(basename $A) vs $(basename $B) on $BK ##########"
md5sum "$A/liblst_$BK.so" "$B/liblst_$BK.so"
cmp -s "$A/liblst_$BK.so" "$B/liblst_$BK.so" && { echo "  CONTROL FAIL: identical libraries"; exit 1; }

for leg in A B; do
  D=$A; [ "$leg" == "B" ] && D=$B
  LD_LIBRARY_PATH="$D:$LD_LIBRARY_PATH" \
  LST_CHAIN_TC_DUMP="$REF/${TAG}_${BK}_${leg}_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/${TAG}_${BK}_${leg}_ch.bin" \
    "$D/lst_$BK" -i PU200RelVal -n "$N" -s 1 --use_chain_tracking \
    -o "$REF/${TAG}_${BK}_${leg}.root" > "$REF/${TAG}_${BK}_${leg}.log" 2>&1
done

python3 "$STANDALONE/p25_ref/p25_repro.py" tc    "$REF/${TAG}_${BK}_A_tc.bin" "$REF/${TAG}_${BK}_B_tc.bin" | tail -3 || true
python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/${TAG}_${BK}_A_ch.bin" "$REF/${TAG}_${BK}_B_ch.bin" | grep -E "IDENTITY|UPSTREAM" || true
python3 "$REF/p26d_chaindiff.py" "$REF/${TAG}_${BK}_A_ch.bin" "$REF/${TAG}_${BK}_B_ch.bin"
echo "PAIR_DONE $TAG"
