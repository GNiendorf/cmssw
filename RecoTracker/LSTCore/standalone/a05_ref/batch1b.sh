#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
bash "$S/a05_ref/a05_scan.sh" 4 \
  "RP3_5:$B -RP3 5" \
  "RP3_7:$B -RP3 7" \
  "AT3Z5:$B -AT3Z 5" \
  "AT3T7:$B -AT3T 7"
