#!/bin/bash
# ta_pd.sh -- bare-T3 PAIRDUMP runner. Same anchor/stack/M19 pipeline as ta_run.sh but
# WITHOUT -A 4 (pairdump is not a delivery path; -A 4 is rejected there).
#   [NEV=<n>] ta_pd.sh <tag> [overrides...]
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/t3attach_ref"
EXE="${BIN:-$S/prototype/bin/chainproto}"
NEV="${NEV:--1}"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
FLAGSHIP="$STACK -TT 1.2 -a 8"
M19="-a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
rm -f "$P/pd_${TAG}.root"
echo "BIN=$EXE NEV=$NEV OVERRIDES: $*" > "$P/pd_${TAG}.cmd"
T0=$(date +%s.%N)
"$EXE" -m pairdump -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -n "$NEV" -o "$P/pd_${TAG}.root" $ANCHOR $FLAGSHIP $M19 "$@" > "$P/pd_${TAG}.log" 2>&1 \
  || { echo "PAIRDUMP FAILED $TAG"; tail -20 "$P/pd_${TAG}.log"; exit 1; }
T1=$(date +%s.%N)
echo "WALL_SECONDS $(echo "$T1 - $T0" | bc)" >> "$P/pd_${TAG}.cmd"
ls -l "$P/pd_${TAG}.root" >> "$P/pd_${TAG}.cmd"
echo "[pd] DONE $TAG wall=$(echo "$T1 - $T0" | bc)s"
