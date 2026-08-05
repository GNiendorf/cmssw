#!/bin/bash
# A14 batch 3 -- THE REVERSED DIRECTION, which the raw band COUNTS predict is the real
# calibration. Batches 1/2 scanned "tighten the barrel, loosen the endcap" because the
# per-region RATE table says every dup/fake excess vs LST is barrel+transition and the
# endcap is already better. The raw counts say that reading is backwards for the GLOBAL
# scoreboard: over the global -XCT 4.5 -> 3.0 move the numerators move
#   barrel  -36 sim matches for -2294 dup rows =  63.7 : 1
#   transit -15                 for -1335      =  89.0 : 1
#   endcap   -8                 for -1318      = 164.8 : 1
# and since eff and dup share one global denominator each, the endcap buys 2.6x more
# global duplicate rate per unit of global efficiency than the barrel does. So: TIGHTEN
# THE ENDCAP. The RE* family is the ONE-EXTRA-CONSTANT form (-XCT3 alone, barrel and
# transition left at the shipped 4); the RB* family spends the recovered budget on a
# looser barrel.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a14_ref/a14_run.sh"
export BIN="$S/protoA14/bin/chainproto_v1"
B="-XC 3 -T3E 1 -CC 1 -CCN 1 -CCR 2"
go () { setsid nohup bash "$R" "$@" > /dev/null 2>&1 < /dev/null & }
go RE3    $B -XCT 4 -XCT3 3   -RGD 1
go RE25   $B -XCT 4 -XCT3 2.5 -RGD 1
go RE2    $B -XCT 4 -XCT3 2   -RGD 1
go RE0    $B -XCT 4 -XCT3 0   -RGD 1 -XCD 2
go RB5E3  $B -XCT 5 -XCT2 5 -XCT3 3   -RGD 1
go RB45E2 $B -XCT 4.5 -XCT2 4.5 -XCT3 2 -RGD 1
sleep 2
echo "[a14] BATCH 3 LAUNCHED (detached)"
