#!/bin/bash
# S3 timing: the DELETION removes a whole 25->32->32->1 network from K7, evaluated for every chain.
# Two binaries (the head shape is compile-time, so a same-binary config-only A/B does not exist),
# so TRAP 6 applies to any CPU TOTAL: the pLS column is printed first as the layout-band check.
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
W=$O/nnloop_ref/s3_work
run() {  # run <tag> <tree>
  S=$2/src/RecoTracker/LSTCore/standalone
  cd $S && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
  cd $S
  ./bin/lst_cpu -i PU200RelVal -n 200 -s 1 -p 0.8 -w 0 > $W/logs/timing_$1.log 2>&1
  echo "=== $1 (md5 $(md5sum bin/lst_cpu | cut -c1-12))"
  grep -E "Chain|Average" $W/logs/timing_$1.log | tail -30
}
run GM12F  /mnt/data1/gsn27/here/gpu_wt/g6
run S3A2   /mnt/data1/gsn27/here/gpu_wt/g7
