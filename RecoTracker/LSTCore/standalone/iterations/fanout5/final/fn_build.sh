#!/bin/bash
# fn_build.sh -- rebuild BOTH binaries of the M19 final tree from the two attach heads.
#   bin/chainproto     <- attach_mlp_weights_g1.h  (RESIDENT head; this is the binary that must
#                          pass the two repro gates bit-identically against the golden binary)
#   bin/chainproto_r2  <- attach_mlp_weights_r2.h  (RETRAINED r2 head; THE FREEZE CANDIDATE BINARY)
# Every other source file is shared, so the two binaries differ ONLY in that one header.
set -e
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
popd > /dev/null
cd "$S/fanout5/final"
cp attach_mlp_weights_r2.h attach_mlp_weights.h
make -j 16
cp bin/chainproto bin/chainproto_r2
cp attach_mlp_weights_g1.h attach_mlp_weights.h
make -j 16
md5sum attach_mlp_weights_g1.h attach_mlp_weights_r2.h bin/chainproto bin/chainproto_r2
echo "built: bin/chainproto (g1, gate binary) and bin/chainproto_r2 (r2, FREEZE binary)"
