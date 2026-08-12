O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s2_work
G=/mnt/data1/gsn27/here/gpu_wt/g4
for spec in "CTLM12D C_m12cos bar_C_m12cos.json dual" "CTLM12F C_m12cos bar_C_m12cos.json fkm" "CTLLSTD C_lstcos bar_C_lstcos.json dual"; do
  set -- $spec
  bash $W/deploy.sh $G $1 $2 $3 $4 -i PU200RelVal -n 1000 -s 8 -p 0.8 >> $W/logs/phase1_g4.log 2>&1
  echo "$1 finished" >> $W/logs/phase1.status
done
echo "G4 PHASE1 DONE" >> $W/logs/phase1.status
