#!/bin/bash
# W5: one isolated process per event, four chain sidecars, reduce, delete the dumps.
#   usage: run_wr.sh <first> <last> <slot> <jet|pu>
# no set -u: setup.sh dereferences unset vars
FIRST=$1; LAST=$2; SLOT=$3; SAMPLE=${4:-pu}
SA=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
cd $SA || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1

if [ "$SAMPLE" = "jet" ]; then
  IN=$SA/jet_ref/trackingNtuple_jets_1000.root
  JFLAG="-J"
  OUTD=$SA/w5_ref/dj
else
  IN=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root
  JFLAG=""
  OUTD=$SA/w5_ref/dp
fi
mkdir -p $OUTD
W=$SA/w5_ref/work$SLOT
for i in $(seq $FIRST $LAST); do
  if [ -f $OUTD/e$i.npz ]; then continue; fi
  rm -rf $W; mkdir -p $W
  LST_CHAIN_EDGE_DUMP=$W/edges.bin \
  LST_CHAIN_NODE_DUMP=$W/nodes.bin \
  LST_CHAIN_CHAIN_DUMP=$W/chains.bin \
  LST_CHAIN_FEAT_DUMP=$W/feat.bin \
  $SA/bin/lst_cpu -i "$IN" -x $i -n -1 -s 1 -v 2 -w 1 --allobj $JFLAG \
      -o $W/out.root > $W/run.log 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then echo "EVT $i RUN rc=$rc" >> $OUTD/errors.log; rm -rf $W; continue; fi
  python3 $SA/w5_ref/py/wr.py $i $W/out.root $W $OUTD/e$i.npz $SAMPLE \
      > $OUTD/e$i.log 2>&1
  rc=$?
  if [ $rc -ne 0 ]; then echo "EVT $i REDUCE rc=$rc" >> $OUTD/errors.log; fi
  rm -rf $W
done
echo "slot $SLOT done $FIRST..$LAST"
