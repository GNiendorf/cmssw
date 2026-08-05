#!/bin/bash
# v1_run.sh -- ADVERSARIAL VERIFIER runner. Frozen prefix transcribed independently from
# fin_ref/fin_run.sh (byte-for-byte the same flag strings) but with a configurable BIN and
# all output landing in verify1_ref/. The assembled-baseline tail is NOT included here --
# it is passed explicitly by the caller so that nothing is hidden inside the script.
#   BIN=<path> [NEV=<n>] [LSTN=<ntuple>] [BASEHISTS=<h>] v1_run.sh <tag> [flags...]
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/verify1_ref"
EXE="${BIN}"
NEV="${NEV:--1}"
LSTN="${LSTN:-$S/rebase_ref/LSTNtuple_instr_300evt.root}"
BASEHISTS="${BASEHISTS:-$S/rebase_ref/rb_base300_hists.root}"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
FLAGSHIP="$STACK -TT 1.2 -a 8 -RPS 1 -RD 1"
M19="-a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0"
CFF="-CF 1 -CFC 1"
POSTDELP2="-ZPF 3 -ZP5 1 -RT3 1 -ZP8 6"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/r_${TAG}.root" "$P/r_${TAG}_hists.root"
echo "BIN=$EXE NEV=$NEV LSTN=$LSTN FLAGS: $*" > "$P/r_${TAG}.cmd"
T0=$(date +%s.%N)
"$EXE" -m hybrid -i "$LSTN" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -n "$NEV" -o "$P/r_${TAG}.root" $ANCHOR $CTL $FLAGSHIP $M19 $CFF $POSTDELP2 "$@" > "$P/r_${TAG}.log" 2>&1 \
  || { echo "RUN FAILED $TAG"; tail -30 "$P/r_${TAG}.log"; exit 1; }
T1=$(date +%s.%N)
echo "WALL_SECONDS $(echo "$T1 - $T0" | bc)" >> "$P/r_${TAG}.cmd"
createPerfNumDenHists -i "$P/r_${TAG}.root" -o "$P/r_${TAG}_hists.root" >> "$P/r_${TAG}.log" 2>&1
python3 "$S/prototype/compare_ab.py" --proto "$P/r_${TAG}_hists.root" --base "$BASEHISTS" \
  --json "$P/r_${TAG}.json" > "$P/r_agg_${TAG}.txt" 2>/dev/null
echo "[v1] DONE $TAG  wall=$(echo "$T1 - $T0" | bc)s"
