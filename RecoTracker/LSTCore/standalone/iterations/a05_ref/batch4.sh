#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
bash "$S/a05_ref/a05_scan.sh" 3 \
  "F10R7X35:$B -AT3F 0.1 -RP3 7 -XCT 3.5" \
  "F10R7X375:$B -AT3F 0.1 -RP3 7 -XCT 3.75" \
  "F10X375:$B -AT3F 0.1 -XCT 3.75"
