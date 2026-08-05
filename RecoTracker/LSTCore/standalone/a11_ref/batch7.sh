#!/bin/bash
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R="$S/a11_ref/a11_run.sh"
# -RPSA alone at the SHIPPED attach margin -a 6.875: pure seed-retirement knob.
# Delivery is untouched, so every chain TC (and hence every displaced band) must be
# bit-identical to F010; only bare type-8 rows disappear.
bash "$R" R_F10R60 -XCD 2 -T3F 0.10 -RPSA 6.0 &
bash "$R" R_F10R55 -XCD 2 -T3F 0.10 -RPSA 5.5 &
bash "$R" R_F10R50 -XCD 2 -T3F 0.10 -RPSA 5.0 &
bash "$R" R_F10R45 -XCD 2 -T3F 0.10 -RPSA 4.5 &
bash "$R" R_F10R40 -XCD 2 -T3F 0.10 -RPSA 4.0 &
wait
echo "[a11] BATCH 7 COMPLETE"
