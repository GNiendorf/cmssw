#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
export BIN=$S/protoA05b/bin/chainproto
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
bash "$S/a05_ref/a05_scan.sh" 3 \
  "GATE4:$B" \
  "CCK3:$B -CCK 3" \
  "F10CCK3:$B -AT3F 0.1 -CCK 3"
