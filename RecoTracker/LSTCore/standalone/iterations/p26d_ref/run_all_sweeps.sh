#!/bin/bash
# P2.6d JOB 2 driver. Six sweeps, strictly sequential, nothing else on the machine.
# usage: run_all_sweeps.sh <bindir> [nevents]
REF=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/p26d_ref
BIND="${1:?bindir}"
N="${2:-200}"

for BK in cpu cuda; do
  for CFG in base hybrid preview; do
    "$REF/run_sweep.sh" "$BK" "$CFG" "$BIND" "$N"
  done
done
echo ALL_SWEEPS_DONE
