#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
bash "$S/a05_ref/a05_scan.sh" 8 \
  "GATE2:$B" \
  "F90:$B -AT3F 0.9" \
  "F70:$B -AT3F 0.7" \
  "F50:$B -AT3F 0.5" \
  "F30:$B -AT3F 0.3" \
  "F10:$B -AT3F 0.1" \
  "F50R:$B -AT3F 0.5 -AT3R 1" \
  "RP3A7:$B -AT3 7 -RP3 6"
