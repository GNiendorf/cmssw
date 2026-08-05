#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
pushd "$S/protoA07" > /dev/null
make -j 12 2>&1 | tail -20
md5sum "$S/protoA07/bin/chainproto"
