#!/bin/bash
# VOID_1000_EVENT -- kept as the record of what was submitted before the [COORDINATOR 12:50]
# restart, NOT to be re-run: it is a 1000-event jet job and the rule is now 100-event max.
# The compliant replacements are m1jc1.sh (CPU isolated), m1jc2.sh (CPU batch), m1jg1.sh (GPU).
echo "VOID: 1000-event job, superseded by m1jc1.sh / m1jc2.sh / m1jg1.sh (100-event rule)"; exit 3
# M1J3 -- THE JET GATE, CPU side, CAP OFF: the BASELINE arm, i.e. today's enumeration with nothing
# but the guard added. 1000 events, ONE process, s=1, ~50 minutes. This is the arm that measures what
# the guard alone buys (the run completes at all, and the 8 corrupting events are skipped with a
# census instead of killing the job) and it is the denominator for M1J2's cap-256 speedup. The two
# arms are in SEPARATE slots on purpose -- a 3x effect does not need slot-level noise control, and a
# 70-minute single slot would block the other three agents.
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

/usr/bin/time -v env LST_CHAIN_DEG_CAP=0 LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH \
    $V/lst_cpu -i $J1000 -n 1000 -s 1 -v 1 -w 0 > $R/j3_cpu_off.log 2> $R/j3_cpu_off.err
echo "rc=$?"
echo -n "avg line: "; grep -E '^\s+avg' $R/j3_cpu_off.log | tail -1
echo "per-event rows: $(grep -cE '^ +[0-9]+ +[0-9]' $R/j3_cpu_off.log)"
echo "[CHAIN OVERFLOW] skips: $(grep -c 'CHAIN OVERFLOW' $R/j3_cpu_off.log $R/j3_cpu_off.err | awk -F: '{s+=$2} END {print s}')"
grep 'CHAIN OVERFLOW' $R/j3_cpu_off.log $R/j3_cpu_off.err | head -5
echo "peak RSS kB: $(grep 'Maximum resident' $R/j3_cpu_off.err | tr -dc '0-9')"
grep -E "Elapsed \(wall clock\)" $R/j3_cpu_off.err
python3 $R/perev.py $R/j3_cpu_off.log 2>&1 | tail -20
echo DONE M1J3
