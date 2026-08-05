#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
bash "$S/a05_ref/a05_scan.sh" 8 \
  "GATE3:$B" \
  "F05:$B -AT3F 0.05" \
  "F02:$B -AT3F 0.02" \
  "F10X35:$B -AT3F 0.1 -XCT 3.5" \
  "F10X3:$B -AT3F 0.1 -XCT 3" \
  "F10A5:$B -AT3F 0.1 -AT3 5" \
  "F10RP7:$B -AT3F 0.1 -RP3 7" \
  "D00:$B -AT3D 0.0"
