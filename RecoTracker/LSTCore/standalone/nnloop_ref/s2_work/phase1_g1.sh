O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s2_work
G=/mnt/data1/gsn27/here/gpu_wt/g1
for spec in "GM12D G_m12cos bar_G_m12cos.json dual" "GM12F G_m12cos bar_G_m12cos.json fkm" "GLSTD G_lstcos bar_G_lstcos.json dual" "GLSTF G_lstcos bar_G_lstcos.json fkm"; do
  set -- $spec
  bash $W/deploy.sh $G $1 $2 $3 $4 -i PU200RelVal -n 1000 -s 8 -p 0.8 >> $W/logs/phase1_g1.log 2>&1
  echo "$1 finished" >> $W/logs/phase1.status
done
echo "G1 PHASE1 DONE" >> $W/logs/phase1.status
