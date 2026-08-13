#!/bin/bash
# g2_ref/trainq.sh -- G's training queue. One head per line, serialised on the GPU.
# no `set -u`: setup.sh dereferences unset vars.
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
P=$O/g2_ref
cd $O && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
cd $O
T="python3 $P/train3mix2.py --lab pu=$O/p3_ref/lab/pu --lab jet=$O/p3_ref/lab/jet"
TC="$T --lab c5hp=$P/lab/c5hp"

run() { TAG=$1; shift; echo "== $TAG $(date -Is)" >> $P/logs/train.status
        "$@" --tag $TAG > $P/logs/train_$TAG.log 2>&1
        echo "$TAG rc=$? $(date -Is)" >> $P/logs/train.status; }

# ---- arm (a): the isolated-high-pT-displaced gun in the mix, share scanned ------------------
run A02 $TC --share jet=0.25 --share c5hp=0.02 --flat c5hp
run A05 $TC --share jet=0.25 --share c5hp=0.05 --flat c5hp
run A10 $TC --share jet=0.25 --share c5hp=0.10 --flat c5hp
# ---- arm (b): the dup-aware loss on same-sim chain groups -----------------------------------
run BN  $T  --share jet=0.25 --dup-mode norm   --dup-on pu,jet
run BD  $T  --share jet=0.25 --dup-mode demote --dup-f 1.0 --dup-on pu,jet
run BD5 $T  --share jet=0.25 --dup-mode demote --dup-f 0.5 --dup-on pu,jet
echo "TRAINQ DONE $(date -Is)" >> $P/logs/train.status
