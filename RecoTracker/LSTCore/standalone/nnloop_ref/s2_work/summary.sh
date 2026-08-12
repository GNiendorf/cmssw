#!/bin/bash
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s2_work
cd $O && source setup.sh >/dev/null 2>&1
A=""; for t in CTLM12D CTLM12F CTLLSTD; do [ -f $W/runs/$t.judge ] && A="$A $t=$W/runs/$t.judge"; done
B=""; for t in GM12D GM12F GLSTD GLSTF; do [ -f $W/runs/$t.judge ] && B="$B $t=$W/runs/$t.judge"; done
echo "############ CONTROL ARM vs shipped (shipped edge + shipped gate)"
python3 $W/table.py $O/ship_ref/B2_shipped.judge $A
echo; echo "############ CANDIDATE ARM vs arm G + SHIPPED gate (what the gate retrain did)"
python3 $W/table.py $O/nnloop_ref/s1_work/runs/S1G_tune.json $B
echo; echo "############ ROUND LEVEL: candidate arms vs the SHIPPED heads"
python3 $W/table.py $O/ship_ref/B2_shipped.judge "G+shipgate"=$O/nnloop_ref/s1_work/runs/S1G_tune.json $B
