#!/bin/bash
P=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_dup
STACK="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5"
# Gate 1: defaults (ANCHOR+CTL only)
$P/rd_run.sh def
# Gate 2: FLAGSHIP, with per-TC OT hit dump for the decomposition
export PROTO_DUMP_TCHITS=1
$P/rd_run.sh fl $STACK -TT 1.2 -a 8 -RPS 1 -RD 1
touch $P/gates.done
