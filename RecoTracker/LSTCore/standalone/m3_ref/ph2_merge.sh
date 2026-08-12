#!/bin/bash
# M3: merge the per-event isolated arms onto the COMMON event set and run the physics comparison.
# Only events that every arm produced are kept, so the arms are paired track for track. Pure IO +
# offline python (no lst_cpu), so this is broker-exempt.
S=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone
M=$S/m3_ref
mkdir -p $M/ph2
cd $S || exit 9
source setup.sh > /dev/null 2>&1
eval $(scramv1 runtime -sh) 2>/dev/null
source setup.sh > /dev/null 2>&1

ARMS="${*:-OFF C256 C512}"
REF=$(echo $ARMS | cut -d" " -f1)
echo "arms: $ARMS (reference = $REF)"
python3 - "$ARMS" <<'PY' > $M/ph2/common.txt
import glob, os
S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/m3_ref"
sets = {}
import sys
for a in sys.argv[1].split():
    # rc == 0 ONLY: a crashed event still leaves a truncated evt*.root behind (the writer dies
    # after the file is opened), and hadd -k silently drops it, which would desynchronise the arms.
    rc = dict()
    for line in open("%s/pe100_%s.rc" % (S, a)):
        i, r = line.split()
        rc[int(i)] = int(r)
    sets[a] = set(i for i, r in rc.items() if r == 0)
    print("#", a, len(sets[a]), "events; missing:", sorted(set(range(100)) - sets[a]))
common = sorted(set.intersection(*sets.values()))
print("# common", len(common))
print(" ".join(str(x) for x in common))
PY
grep '^#' $M/ph2/common.txt
COMMON=$(grep -v '^#' $M/ph2/common.txt | tail -1)
for a in $ARMS; do
  rm -f $M/ph2/jets_${a}_iso.root
  LIST=""
  for i in $COMMON; do LIST="$LIST $M/pe100_$a/evt$i.root"; done
  hadd -f -k $M/ph2/jets_${a}_iso.root $LIST > $M/ph2/hadd_$a.log 2>&1
  echo "$a hadd rc=$? $(du -h $M/ph2/jets_${a}_iso.root | cut -f1)"
done
echo "=== per-arm rates on the common event set ==="
FILES=""
for a in $ARMS; do FILES="$FILES $M/ph2/jets_${a}_iso.root"; done
python3 $M/jetphys.py $FILES --json $M/ph2/phys_iso_$(echo $ARMS | tr ' ' '_').json
for a in $ARMS; do
  [ "$a" = "$REF" ] && continue
  echo "=== paired: $REF -> $a ==="
  python3 $M/jetcmp.py $M/ph2/jets_${REF}_iso.root $M/ph2/jets_${a}_iso.root --labels $REF,$a
done
