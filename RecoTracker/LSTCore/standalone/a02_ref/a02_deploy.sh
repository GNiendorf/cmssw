#!/bin/bash
# a02_deploy.sh <model-prefix>
# export the trained dedup head -> parity-check the generated header against torch ->
# rebuild protoA02 with the head compiled in. Stops at the first failure; the binary is
# only rebuilt if parity passes.
set -e
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
M="${1:-$S/a02_ref/dedup_v1}"
pushd "$S" > /dev/null && source setup.sh > /dev/null 2>&1
cmsenv > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
python3 "$S/protoA02/export_dedup_weights.py" --model "$M.pt" --norm "${M}_norm.json" \
        --out "$S/protoA02/dedup_mlp_weights.h"
python3 "$S/protoA02/dedup_parity.py" --header "$S/protoA02/dedup_mlp_weights.h" \
        --model "$M.pt" --norm "${M}_norm.json" --pairs "$S/a02_ref/pairs_TEST300.txt" --n 20000
cd "$S/protoA02" && make -j 12 2>&1 | grep -iE " error|Error 1" && { echo BUILD_FAILED; exit 1; }
md5sum "$S/protoA02/bin/chainproto"
echo DEPLOY_OK
