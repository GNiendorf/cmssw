#!/bin/bash
# A06 workspace setup: copy protoFIN -> protoA06 (read-only source, no edits to protoFIN).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
if [ ! -d "$S/protoA06" ]; then
  cp -a "$S/protoFIN" "$S/protoA06"
fi
ls "$S/protoA06" | head
md5sum "$S/protoA06/bin/chainproto"
