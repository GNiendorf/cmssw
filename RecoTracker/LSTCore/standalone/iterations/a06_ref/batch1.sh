#!/bin/bash
# A06 BATCH 1 -- single-flag length levers off the assembled baseline (300 evts).
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a06_ref/a06_run.sh"
bash "$R" E_E3   -EX 3                 &
bash "$R" E_S0   -EXS 0                &
bash "$R" E_J2   -EXJ 2                &
bash "$R" E_N2   -EXN 2                &
bash "$R" E_W50  -EXW 0.50 -EXR 4.0    &
bash "$R" T_TR0  -TR 0                 &
bash "$R" T_TA3  -TA 3.0               &
wait
echo "[a06] BATCH1 COMPLETE"
