#!/bin/bash
# M3 (a): jet physics baseline, 250 events, BOTH arms. Submitted through the broker so that the
# parallelism below is taken on an exclusive machine and contaminates nobody's timing (my own
# claims here are physics-only: -w 1, no timing quoted).
#   arm M = LST master (g3, b42d8f97ad5), one process, -s 16
#   arm O = ours (HEAD 81a9afe2d00), ONE PROCESS PER EVENT (the 4 GiB ChainEdges truncation
#           SIGSEGVs 3 of the first 250 events: 5, 85, 217), 8 at a time, then hadd
# Both arms carry -J so the ntuple gets the genjet branches the deltaR study needs.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
G3=/mnt/data1/gsn27/here/gpu_wt/g3/src/RecoTracker/LSTCore/standalone
M3=$S/m3_ref
IN=$S/jet_ref/trackingNtuple_jets_1000.root
N=250
mkdir -p $M3/ours250

echo "=== arm M: master 250 evt ==="
cd $G3 || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
/usr/bin/time -v ./bin/lst_cpu -i "$IN" -n $N -s 16 -v 1 -w 1 -J -o $M3/master_jets250.root \
   > $M3/master_jets250.log 2> $M3/master_jets250.err
echo "master rc=$?"

echo "=== arm O: ours, per-event isolated, 250 evt ==="
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1
: > $M3/ours250.rc
run_one() {
  i=$1
  ./bin/lst_cpu -i "$IN" -x $i -n -1 -s 1 -v 1 -w 1 -J -o $M3/ours250/evt$i.root \
      > $M3/ours250/evt$i.log 2>&1
  echo "$i $?" >> $M3/ours250.rc
}
export -f run_one
export IN M3
seq 0 $((N-1)) | xargs -P 8 -I{} bash -c 'run_one {}'
echo "ours: $(grep -c ' 0$' $M3/ours250.rc) ok of $N ; nonzero rc:"; grep -v ' 0$' $M3/ours250.rc | sort -n
ls $M3/ours250/evt*.root > $M3/ours250.list 2>/dev/null
echo "root files: $(wc -l < $M3/ours250.list)"
rm -f $M3/ours_jets250.root
hadd -f -k $M3/ours_jets250.root @$M3/ours250.list > $M3/ours250_hadd.log 2>&1
echo "hadd rc=$? size=$(du -sh $M3/ours_jets250.root 2>/dev/null)"
echo M3JOBDONE
