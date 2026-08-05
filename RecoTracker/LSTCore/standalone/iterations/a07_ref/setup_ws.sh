#!/bin/bash
# A07 (fake-rate explorer) workspace setup: copy protoFIN -> protoA07, verify binary.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
set -e
rm -rf "$S/protoA07"
cp -a "$S/protoFIN" "$S/protoA07"
mkdir -p "$S/a07_ref"
echo "protoA07 binary md5:"
md5sum "$S/protoA07/bin/chainproto"
echo "protoFIN binary md5:"
md5sum "$S/protoFIN/bin/chainproto"
