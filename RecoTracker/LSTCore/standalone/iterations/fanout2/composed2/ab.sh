#!/bin/bash
# Composed-angle A/B driver (M15 judge). Isolated: writes only inside composed2/.
set -u
TAG="$1"; shift
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout2/composed2
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P=$S/prototype
OUT=$D/ab_${TAG}.root; H=$D/ab_${TAG}_hists.root; J=$D/ab_${TAG}.json; L=$D/ab_${TAG}.log
rm -f "$OUT" "$H"
$D/bin/chainproto -m hybrid -i $S/LSTNtuple_PU200RelVal_300evt.root -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ -o "$OUT" "$@" > "$L" 2>&1 || { echo "$TAG PROTO FAIL"; exit 1; }
createPerfNumDenHists -i "$OUT" -o "$H" >> "$L" 2>&1 || { echo "$TAG HIST FAIL"; exit 1; }
python3 $P/compare_ab.py --proto "$H" --base $P/base300_hists.root --json "$J" >> "$L" 2>&1 || { echo "$TAG CMP FAIL"; exit 1; }
rm -f "$OUT"
echo "$TAG done"
