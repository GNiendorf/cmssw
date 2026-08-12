#!/bin/bash
# VOID_1000_EVENT -- kept as the record of what was submitted before the [COORDINATOR 12:50]
# restart, NOT to be re-run: it is a 1000-event jet job and the rule is now 100-event max.
# The compliant replacements are m1jc1.sh (CPU isolated), m1jc2.sh (CPU batch), m1jg1.sh (GPU).
echo "VOID: 1000-event job, superseded by m1jc1.sh / m1jc2.sh / m1jg1.sh (100-event rule)"; exit 3
# M1J2 -- THE JET GATE, CPU side, CAP 256 (the shipping arm). 1000 events, ONE process, s=1.
# Today this run does not exist: 8 of the 1000 events silently truncate a >4 GiB byte extent and
# SIGSEGV inside ChainBuildEdges, so the process dies on the first of them (event 5, i.e. inside the
# first ten). The baseline arm (cap OFF, guard only) is the separate job M1J3 -- split so a 50-minute
# arm does not sit on the queue in front of the other agents. Both arms print PER-EVENT rows, so the
# comparison is made on the COMMON event set rather than on two means over different denominators.
M=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$M/m1_ref
V=$R/frozenvar
G=/mnt/data1/gsn27/here/gpu_wt/g2/src/RecoTracker/LSTCore/standalone
J1000=$M/jet_ref/trackingNtuple_jets_1000.root
cd $G || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1
( cd $V && md5sum -c MD5 ) || { echo "*** MD5 MISMATCH -- VOID ***"; exit 1; }

/usr/bin/time -v env LST_CHAIN_DEG_CAP=256 LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH \
    $V/lst_cpu -i $J1000 -n 1000 -s 1 -v 1 -w 0 > $R/j2_cpu_c256.log 2> $R/j2_cpu_c256.err
echo "rc=$?"
echo -n "avg line: "; grep -E '^\s+avg' $R/j2_cpu_c256.log | tail -1
echo "per-event rows: $(grep -cE '^ +[0-9]+ +[0-9]' $R/j2_cpu_c256.log)"
echo "[CHAIN OVERFLOW] skips: $(grep -c 'CHAIN OVERFLOW' $R/j2_cpu_c256.log $R/j2_cpu_c256.err | awk -F: '{s+=$2} END {print s}')"
grep 'CHAIN OVERFLOW' $R/j2_cpu_c256.log $R/j2_cpu_c256.err | head -5
echo "peak RSS kB: $(grep 'Maximum resident' $R/j2_cpu_c256.err | tr -dc '0-9')"
grep -E "Elapsed \(wall clock\)" $R/j2_cpu_c256.err
python3 $R/perev.py $R/j2_cpu_c256.log 2>&1 | tail -20
echo DONE M1J2
