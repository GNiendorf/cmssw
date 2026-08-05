#!/bin/bash
# xfl.sh -- run with the FLAGSHIP override stack, extra overrides appended last (they win).
#   BIN=golden|local  xfl.sh <tag> [extra overrides...]
TAG="$1"; shift
FL="-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0 -ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1"
exec bash /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/ex_dupcc/xr.sh "$TAG" $FL "$@"
