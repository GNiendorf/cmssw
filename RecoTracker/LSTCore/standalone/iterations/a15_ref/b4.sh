S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
B="-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2"
LSTN=$S/rebase_ref/LSTNtuple_instr_977evt.root BASEHISTS=$S/fin_ref/fin_base977_hists.root \
  bash $S/a15_ref/a15_run.sh W_XC4_T45 $B -XC4 1 -XCT 4.5 &
bash $S/a15_ref/a15_run.sh X4_T50 $B -XC4 1 -XCT 5 &
bash $S/a15_ref/a15_run.sh X4_T425 $B -XC4 1 -XCT 4.25 &
wait
echo B4DONE
