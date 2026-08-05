#!/bin/bash
# Quick N-event probe: no hists, just the run + its summary lines.
TAG="$1"; shift
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/t3attach_ref"
EXE="${BIN:-$P/bin/chainproto_p25}"
NEV="${NEV:-10}"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
CTL="-A 4 -a 999 -D 5 -RT5 1"
FLAGSHIP="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1"
M19="-a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
T0=$(date +%s.%N)
"$EXE" -m hybrid -i "$S/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -n "$NEV" -o "$P/p_${TAG}.root" $ANCHOR $CTL $FLAGSHIP $M19 "$@" > "$P/p_${TAG}.log" 2>&1
RC=$?
T1=$(date +%s.%N)
echo "[probe] $TAG rc=$RC wall=$(echo "$T1 - $T0" | bc)s  args: $*"
grep -E 'M20 |M16 attach|M16 delivery|time mean' "$P/p_${TAG}.log"
