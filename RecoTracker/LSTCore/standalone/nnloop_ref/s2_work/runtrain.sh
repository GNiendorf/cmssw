cd /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
source setup.sh >/dev/null 2>&1; eval $(scramv1 runtime -sh) >/dev/null 2>&1; source setup.sh >/dev/null 2>&1
W=nnloop_ref/s2_work; export OMP_NUM_THREADS=8
for spec in "C_m12cos r1 m12 cos" "G_m12const r2G m12 const" "G_m12cos r2G m12 cos" "C_lstcos r1 lst cos" "G_lstcos r2G lst cos"; do
  set -- $spec
  CUDA_VISIBLE_DEVICES=0 python3 $W/train3.py --lab $W/lab/$2 --tag $1 --loss $3 --sched $4 > $W/logs/train_$1.log 2>&1
  echo "$1 done" >> $W/logs/trainall.log
done
echo ALLDONE >> $W/logs/trainall.log
