#!/bin/bash
# B02 runner: protoB02 binary through the frozen synth_ref harness.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
TAG="$1"; shift
BIN=$S/protoB02/bin/chainproto bash $S/synth_ref/syn_run.sh "$TAG" "$@"
