#!/bin/bash
# P2.6d JOB 1: bit-identity of the P2.5 state against HEAD with BOTH toolchains fully pinned.
#
# The defect this script exists to close: lst_cpu / lst_cuda are not self-contained. They dynamically
# link liblst_<backend>.so, which is where LSTEvent and every kernel live, and the executable has no
# RPATH entry for it -- resolution goes through LD_LIBRARY_PATH. p26_ref/bin_before and
# p26b_ref/bin_before snapshotted ONLY the executables, so replaying them after a rebuild loaded the
# NEW library: those legs compared the new algorithm against itself.
#
# Here each leg runs its OWN executable AND its OWN library out of a private snapshot directory, and
# the script prints ldd proof of which liblst each leg actually resolved.
#
# usage: run_bitcheck.sh <cpu|cuda> [nevents] [tag]
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
REF="$STANDALONE/p26d_ref"
A="$REF/bin_p25_d"      # P2.5   f41c6abb8a4
B="$REF/bin_head_d"     # HEAD   37bf47dd4c8
BK="${1:-cpu}"
N="${2:-10}"
TAG="${3:-bit}"

cd "$STANDALONE" && source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

set -e
rm -f "$REF/${TAG}_${BK}_p25.root" "$REF/${TAG}_${BK}_head.root"
rm -f "$REF/${TAG}_${BK}_p25_tc.bin" "$REF/${TAG}_${BK}_head_tc.bin"
rm -f "$REF/${TAG}_${BK}_p25_ch.bin" "$REF/${TAG}_${BK}_head_ch.bin"

echo "########## toolchain proof ##########"
md5sum "$A/lst_$BK" "$A/liblst_$BK.so" "$B/lst_$BK" "$B/liblst_$BK.so"
if cmp -s "$A/liblst_$BK.so" "$B/liblst_$BK.so"; then
  echo "  CONTROL FAIL: the two liblst_$BK.so are byte-identical - the comparison would be vacuous"
  exit 1
else
  echo "  ok: the two liblst_$BK.so differ"
fi
echo "-- ldd of the P2.5 leg (must resolve liblst inside bin_p25_d) --"
LD_LIBRARY_PATH="$A:$LD_LIBRARY_PATH" ldd "$A/lst_$BK" | grep "liblst"
echo "-- ldd of the HEAD leg (must resolve liblst inside bin_head_d) --"
LD_LIBRARY_PATH="$B:$LD_LIBRARY_PATH" ldd "$B/lst_$BK" | grep "liblst"

echo "########## leg P2.5 ##########"
LD_LIBRARY_PATH="$A:$LD_LIBRARY_PATH" \
LST_CHAIN_TC_DUMP="$REF/${TAG}_${BK}_p25_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/${TAG}_${BK}_p25_ch.bin" \
  "$A/lst_$BK" -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
  -o "$REF/${TAG}_${BK}_p25.root" > "$REF/${TAG}_${BK}_p25.log" 2>&1

echo "########## leg HEAD ##########"
LD_LIBRARY_PATH="$B:$LD_LIBRARY_PATH" \
LST_CHAIN_TC_DUMP="$REF/${TAG}_${BK}_head_tc.bin" LST_CHAIN_CHAIN_DUMP="$REF/${TAG}_${BK}_head_ch.bin" \
  "$B/lst_$BK" -i PU200RelVal -n "$N" -s 1 --allobj --use_chain_tracking \
  -o "$REF/${TAG}_${BK}_head.root" > "$REF/${TAG}_${BK}_head.log" 2>&1

echo "########## $BK : ntuple branch-by-branch ##########"
python3 "$STANDALONE/p20_bitcheck.py" "$REF/${TAG}_${BK}_p25.root" "$REF/${TAG}_${BK}_head.root" || true
echo "########## $BK : TC sidecar ##########"
python3 "$STANDALONE/p25_ref/p25_repro.py" tc "$REF/${TAG}_${BK}_p25_tc.bin" "$REF/${TAG}_${BK}_head_tc.bin" || true
echo "########## $BK : chain sidecar ##########"
python3 "$STANDALONE/p25_ref/p25_repro.py" chain "$REF/${TAG}_${BK}_p25_ch.bin" "$REF/${TAG}_${BK}_head_ch.bin" || true
echo "BITCHECK_DONE $BK"
