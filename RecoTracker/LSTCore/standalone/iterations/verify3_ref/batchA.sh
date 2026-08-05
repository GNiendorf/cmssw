#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$S/verify3_ref/v3_run.sh
BIN=$S/protoA03/bin/chainproto_v2 bash $R A03GATE                        > $S/verify3_ref/o_A03GATE.out 2>&1 &
BIN=$S/protoA03/bin/chainproto_v2 bash $R A03BEST -ATS 1.0 -XCT 3.75     > $S/verify3_ref/o_A03BEST.out 2>&1 &
BIN=$S/protoA13b/bin/chainproto   bash $R A13GATE                        > $S/verify3_ref/o_A13GATE.out 2>&1 &
BIN=$S/protoA13b/bin/chainproto   bash $R A13BEST -RPSA 5.5              > $S/verify3_ref/o_A13BEST.out 2>&1 &
BIN=$S/protoA08/bin/chainproto    bash $R A08GATE                        > $S/verify3_ref/o_A08GATE.out 2>&1 &
wait
echo "BATCH A COMPLETE"
