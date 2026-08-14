#!/bin/bash
# W5: one arm of the weld cross-family study.
#   usage: gates.sh <TAG> <all|jet|pu|cubes|cube50|cubehi5>
# The arm is entirely in the ENVIRONMENT; one frozen binary supplies every arm, so no comparison
# can be a build artefact.  Unset variables == the shipped weld, bit for bit.
#   LST_CHAIN_WELD_TIE   key mode (5 = E2 first, 8 = calibrated, 9 = E2 first above the knee,
#                        10 = calibrated above the knee)
#   LST_CHAIN_WELD_CAL   path to the 80-float calibration table (calA[40] then calB[40])
#   LST_CHAIN_WELD_KNEE  junction degree-product knee for modes 9 / 10
#   LST_CHAIN_WELD_SWEEPS / _FAMOFF / _FAMBIN / _TIE_UNTIL  (N3's instrument, unchanged)
# Jets = events 0-499 (TUNE).  PU200 = event_1000.root BY EXPLICIT PATH (never -i PU200RelVal,
# which is the DIRECTORY and globs the SEALED event_2000..7000).  no `set -u`.
TAG=$1; WHICH=${2:-jet}
O=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$O/w5_ref/runs
G=/mnt/data1/gsn27/here/gpu_wt/g3/src/RecoTracker/LSTCore/standalone
A=${W5_BIN:-$G}
PU=/data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root
mkdir -p $R $O/w5_ref/logs
cd $G && source setup.sh >/dev/null 2>&1 && eval $(scramv1 runtime -sh) >/dev/null 2>&1 && source setup.sh >/dev/null 2>&1
cd $G
export LD_LIBRARY_PATH=$A/LST:$LD_LIBRARY_PATH
WANT=$(md5sum $A/LST/liblst_cpu.so | cut -d' ' -f1)
GOT=$(ldd $A/bin/lst_cpu 2>/dev/null | awk '/liblst_cpu.so/{print $3}')
[ -z "$GOT" ] && { echo "FATAL $TAG: liblst_cpu.so did not resolve"; exit 7; }
HAVE=$(md5sum $GOT | cut -d' ' -f1)
echo "$TAG bin $(md5sum $A/bin/lst_cpu | cut -d' ' -f1) lib $GOT md5 $HAVE (want $WANT)"
[ "$HAVE" != "$WANT" ] && { echo "FATAL $TAG: WRONG liblst_cpu.so"; exit 7; }
echo "  arm env: TIE=${LST_CHAIN_WELD_TIE:-unset} KNEE=${LST_CHAIN_WELD_KNEE:-unset} CAL=${LST_CHAIN_WELD_CAL:-unset} SWEEPS=${LST_CHAIN_WELD_SWEEPS:-unset} FAMOFF=${LST_CHAIN_WELD_FAMOFF:-unset} FAMBIN=${LST_CHAIN_WELD_FAMBIN:-unset}"

run() { rm -f $R/$1.root; $A/bin/lst_cpu "${@:2}" -o $R/$1.root > $R/$1.log 2>&1
        echo "RUN_EXIT=$?" >> $R/$1.log; }

if [ "$WHICH" = "all" ] || [ "$WHICH" = "jet" ]; then
  run ${TAG}_jet  -i $O/jet_ref/trackingNtuple_jets_1000.root -n 500 -s 8 -p 0.8 -J -w 1
  python3 $O/m3_ref/jetphys.py $R/${TAG}_jet.root --json $R/${TAG}_jet.json > $R/${TAG}_jet.jphys 2>&1
fi
if [ "$WHICH" = "all" ] || [ "$WHICH" = "pu" ]; then
  run ${TAG}_pu   -i $PU -n 1000 -s 8 -p 0.8 -w 1
  python3 $O/d3_ref/pu_judge.py $R/${TAG}_pu.root --json $R/${TAG}_pu.json > $R/${TAG}_pu.judge 2>&1
fi
if [ "$WHICH" = "all" ] || [ "$WHICH" = "cubes" ] || [ "$WHICH" = "cube50" ]; then
  run ${TAG}_cube50 -i cube50 -n -1 -s 4 -p 0.8 -w 1
  python3 $O/d3_ref/pu_judge.py $R/${TAG}_cube50.root --json $R/${TAG}_cube50.json > $R/${TAG}_cube50.judge 2>&1
fi
if [ "$WHICH" = "all" ] || [ "$WHICH" = "cubes" ] || [ "$WHICH" = "cubehi5" ]; then
  run ${TAG}_cubehi5 -i cube50_highPt -n 5000 -s 4 -p 0.8 -w 1
  python3 $O/d3_ref/pu_judge.py $R/${TAG}_cubehi5.root --json $R/${TAG}_cubehi5.json > $R/${TAG}_cubehi5.judge 2>&1
fi
echo "$TAG GATES($WHICH) DONE $(date -Is)" >> $O/w5_ref/logs/gates.status
