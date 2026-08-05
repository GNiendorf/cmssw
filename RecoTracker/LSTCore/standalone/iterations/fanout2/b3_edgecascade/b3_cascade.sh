#!/bin/bash
# ANGLE-B3 driver: export the trained 3-class edge head, rebuild, re-run the anchor
# regression gate, then two A/Bs at the m14_j1 anchor shape.
set -e
D=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout2/b3_edgecascade
pushd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone > /dev/null
source setup.sh > /dev/null 2>&1
cmsenv
source setup.sh > /dev/null 2>&1
cd "$D"

python3 export_edge3_weights.py --model edge3_mlp_v1.pt --norm edge3_norm_v1.json
make -j 8 2>&1 | grep -E "error|Error" || true
ls -la bin/chainproto

ANCH="-e 0 -L 0.5 -F 0.3 -G 6 -X 0.5 -M4 2.5 -M5 1e9 -M6 1e9 -MRI 1.5 -U4 0 -U5 0 -U6 0 -MD 1e9 -MR -0.800 -M4D -0.75 -B 10 -H 1 -W 0.25"

echo "=== regression gate (anchor, -E3 off) after the export+rebuild ==="
./bin/chainproto -m hybrid -i ../../LSTNtuple_PU200RelVal_300evt.root \
  -t /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/ \
  $ANCH -n 20 -o reg_b3_anchor2.root > reg_b3_anchor2.log 2>&1
tail -3 reg_b3_anchor2.log

echo "=== A/B 1: anchor + -E3 1 (max-logit evidence) ==="
bash run_ab.sh b3_e1 $ANCH -E3 1

echo "=== A/B 2: anchor + -E3 1 -ED -1 (displaced OR-rescue) ==="
bash run_ab.sh b3_e1d $ANCH -E3 1 -ED -1

echo B3_CASCADE_DONE
