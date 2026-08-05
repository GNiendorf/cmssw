#!/bin/bash
# M16 fast scan: run chainproto -m hybrid on the first N events and judge with the
# extended compare_types.py (which truncates the baseline to the same N by entry order,
# so no separate base file is needed). Used to locate the -a / -AT3 / -RD operating
# point before spending a 300-event A/B on it.
#
# Usage: m16_scan.sh <tag> <nevents> [extra chainproto args...]
set -u
TAG="$1"; N="$2"; shift 2
STANDALONE=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P="$STANDALONE/fanout3/m16"
ANCHOR="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 3.5 -M5 1e9 -M6 1e9 -M4D -0.75 -MD 1e9 -MR -1.800 -MRI 0.5 -U4 0 -U5 0 -U6 0 -B 10 -H 1 -W 0.50 -FC 1 -PU 2 -C25 2.0 -C25D -2.0"
rm -f "$P/scan_${TAG}.root"
"$P/bin/chainproto" -m hybrid \
  -i "$STANDALONE/LSTNtuple_PU200RelVal_300evt.root" \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  -o "$P/scan_${TAG}.root" -n "$N" $ANCHOR "$@" > "$P/scan_${TAG}.log" 2>&1 || { tail -20 "$P/scan_${TAG}.log"; exit 1; }
grep -E "M16 attach|M16 delivery|M16 suppression|output TCs" "$P/scan_${TAG}.log"
python3 "$P/compare_types.py" --proto "$P/scan_${TAG}.root" --json "$P/scan_${TAG}.json" 2>/dev/null \
  | sed -n '/M16 DELIVERY-CLASS TABLE/,$p'
