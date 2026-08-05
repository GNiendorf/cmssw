#!/bin/bash
# Speculative FULL-977 run of the batch-2 front-runner, detached so it survives cleanup.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
export LSTN="$S/rebase_ref/LSTNtuple_instr_977evt.root"
export BASEHISTS="$S/fin_ref/fin_base977_hists.root"
exec bash "$S/a06_ref/a06_run.sh" "$@"
