#!/bin/bash
# M1G1 -- THE PU200 GATE for the jet-round P0 guard + P2 degree cap.
#   [1] freeze proof (md5) of BOTH binaries: base = 81a9afe2d00, var = the patch.
#   [2] CPU bit-identity, base vs var with the cap OFF. MUST be 35/35: cap-off is min(deg, 1e9),
#       i.e. literally the same arithmetic, and the guard only ever refuses an allocation PU200
#       is 328x away from.
#   [3] THE ANTI-TAUTOLOGY CONTROL: var cap-OFF vs var cap-256 on the same binary. If [2] passes
#       and [3] shows no difference at all, the knob is dead and [2] proves nothing.
#   [4] PU200 physics with the cap ON vs OFF: the full eff / dup / fake scoreboard (pu_judge),
#       1000 events of PU200RelVal, so the shipping default is measured and not assumed.
#   [5] The per-event edge census on this binary: E (capped) vs Euncapped at cap 256, which is
#       the DIRECT measurement of what the cap removes on PU200 -- JR's 0.074% came from an
#       offline dump taken one commit back.
M=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
R=$M/m1_ref
B=$R/frozenbase
V=$R/frozenvar
G=/mnt/data1/gsn27/here/gpu_wt/g2/src/RecoTracker/LSTCore/standalone
cd $G || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) > /dev/null 2>&1
source setup.sh > /dev/null 2>&1

echo "=== [1] FROZEN BINARY PROOF (void if either md5sum -c fails) ==="
for D in $B $V; do ( cd $D && md5sum -c MD5 ) || { echo "*** MD5 MISMATCH in $D -- VOID ***"; exit 1; }; done
echo "base and var liblst_cpu.so md5s MUST differ:"
grep -H liblst_cpu $B/MD5 $V/MD5

run () {  # run <label> <dir> <extra env...>  : PU200 175 evt, ntuple out, CPU
  local lbl=$1 D=$2; shift 2
  rm -f $R/g1_$lbl.root
  env "$@" LD_LIBRARY_PATH=$D:$LD_LIBRARY_PATH $D/lst_cpu -i PU200 -n 175 -v 0 -s 1 \
      -o $R/g1_$lbl.root > $R/g1_$lbl.log 2>&1
  echo "  g1_$lbl rc=$? size=$(stat -c%s $R/g1_$lbl.root 2>/dev/null)"
}
echo
echo "=== [2] THE GATE: base(81a9afe2d00) vs var with LST_CHAIN_DEG_CAP=0 (cap OFF) ==="
run BASE $B
run VAROFF $V LST_CHAIN_DEG_CAP=0
python3 $M/rebase_ref/cmp_branches.py $R/g1_BASE.root $R/g1_VAROFF.root 2>&1 | head -12

echo
echo "=== [3] ANTI-TAUTOLOGY CONTROL: var cap OFF vs var cap 256, SAME binary ==="
run VAR256 $V LST_CHAIN_DEG_CAP=256
python3 $M/rebase_ref/cmp_branches.py $R/g1_VAROFF.root $R/g1_VAR256.root 2>&1 | head -45

echo
echo "=== [4] PU200 PHYSICS, PU200RelVal 1000 evt -s 8: cap OFF vs cap 256 (the shipping default) ==="
for A in OFF 256; do
  CAP=$A; [ $A = OFF ] && CAP=0
  rm -f $R/g1_pu_$A.root
  env LST_CHAIN_DEG_CAP=$CAP LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH $V/lst_cpu \
      -i PU200RelVal -n 1000 -s 8 -p 0.8 -o $R/g1_pu_$A.root > $R/g1_pu_$A.log 2>&1
  echo "  pu_$A rc=$?"
  python3 $M/d3_ref/pu_judge.py $R/g1_pu_$A.root --json $R/g1_pu_$A.json > $R/g1_pu_$A.judge 2>&1
done
echo "--- cap OFF ---"; head -20 $R/g1_pu_OFF.judge
echo "--- cap 256 ---"; head -20 $R/g1_pu_256.judge
echo "--- DELTA (256 minus OFF) ---"
python3 - "$R/g1_pu_OFF.json" "$R/g1_pu_256.json" <<'PY'
import json, sys
a = json.load(open(sys.argv[1])); b = json.load(open(sys.argv[2]))
for k in a:
    if not isinstance(a[k], (int, float)):
        continue
    d = b.get(k, 0) - a[k]
    flag = "" if abs(d) < 1e-12 else "   <<<"
    print(f"  {k:24s} {a[k]:12.6f} -> {b.get(k,0):12.6f}  delta {d:+.6f}{flag}")
PY

echo
echo "=== [5] THE EDGE CENSUS on THIS binary: E vs Euncapped, PU200 5 evt -v 2, cap 256 ==="
env LST_CHAIN_DEG_CAP=256 LD_LIBRARY_PATH=$V:$LD_LIBRARY_PATH $V/lst_cpu -i PU200 -n 5 -v 2 -w 0 \
    > $R/g1_census256.log 2>&1
grep -E "^\[CHAIN\] (MD/E1|LS/E2|nodes)" $R/g1_census256.log | head -20
echo "  (E = what K2 enumerates under the cap, Euncapped = what it would have enumerated;"
echo "   maxDegIn/maxDegOut on these events say whether the cap can bite PU200 at all.)"
echo DONE M1G1
