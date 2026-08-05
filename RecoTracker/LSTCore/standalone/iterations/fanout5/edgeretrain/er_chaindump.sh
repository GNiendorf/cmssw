#!/bin/bash
# er_chaindump.sh <bintag> <theta>
# Re-dumps the chain-gate training population with a RETRAINED edge head, at the weld
# threshold that flagship config will actually use, on both training inputs.
set -e
BT="$1"; TH="$2"
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$S/fanout5/edgeretrain"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
T=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/
"$P/bin/chainproto_${BT}" -m chaindump -e "$TH" -L 0.5 \
  -i "$S/LSTNtuple_PU200RelVal_300evt.root" -t "$T" \
  -o "$P/chains_${BT}_300evt.root" > "$P/chaindump_${BT}_300.log" 2>&1 &
PID1=$!
"$P/bin/chainproto_${BT}" -m chaindump -e "$TH" -L 0.5 -n 498 \
  -i "$S/LSTNtuple_PU200RelVal_1000evt.root" -t "$T" \
  -o "$P/chains_${BT}_498evt.root" > "$P/chaindump_${BT}_498.log" 2>&1 &
PID2=$!
wait $PID1 $PID2
tail -2 "$P/chaindump_${BT}_300.log"
tail -2 "$P/chaindump_${BT}_498.log"
echo "CHAINDUMP DONE ${BT} theta=${TH}"
