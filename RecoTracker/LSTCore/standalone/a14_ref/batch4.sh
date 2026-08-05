#!/bin/bash
# A14 batch 4 -- THE ENDCAP DELIVERY MARGIN, also in the reversed direction.
# Reading the GLOBAL -AT3 6 -> 8 move in raw counts per band (bandcounts.py FINBASE
# E_A8X4) says the two ends of the detector want -AT3 moved in OPPOSITE directions:
#   barrel      eff -107   fake -1916   dup   +94      17.9 fakes per sim match
#   transition  eff  -26   fake  -962   dup  +105      37.0
#   endcap      eff  +26   fake -1231   dup +1651      efficiency GAINS -- it is free
# In the endcap the pT3-class delivery is a WORSE cover for its sim than the bare pLS row
# the -RPS predicate retires in its place, so raising the endcap margin buys efficiency
# AND fake rate and only costs duplicate rate. And duplicate rate is exactly what a
# tighter endcap -XCT3 buys back at the cheapest exchange rate in the detector (batch 3).
# So -AT33 high x -XCT3 low is the natural pair. This batch measures the pair.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a14_ref/a14_run.sh"
export BIN="$S/protoA14/bin/chainproto_v1"
B="-XC 3 -T3E 1 -CC 1 -CCN 1 -CCR 2"
go () { setsid nohup bash "$R" "$@" > /dev/null 2>&1 < /dev/null & }
go P_E8      $B -XCT 4 -AT3 6 -AT33 8            -RGD 1      # endcap margin alone
go P_E8X3    $B -XCT 4 -XCT3 3   -AT3 6 -AT33 8  -RGD 1      # the pair
go P_E8X25   $B -XCT 4 -XCT3 2.5 -AT3 6 -AT33 8  -RGD 1
go P_E8X2    $B -XCT 4 -XCT3 2   -AT3 6 -AT33 8  -RGD 1 -XCD 2
go P_E10X25  $B -XCT 4 -XCT3 2.5 -AT3 6 -AT33 10 -RGD 1      # margin -> off in the endcap
go P_E8TX25  $B -XCT 4 -XCT3 2.5 -AT3 6 -AT32 8 -AT33 8 -RGD 1  # transition with the endcap
sleep 2
echo "[a14] BATCH 4 LAUNCHED (detached)"
