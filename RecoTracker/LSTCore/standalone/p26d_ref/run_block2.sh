#!/bin/bash
# P2.6d: bisect comparisons + the production no-op proof + the 300-event scoreboard.
# All CPU, strictly sequential.
REF=/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/p26d_ref

echo "############ BISECT: P2.5 -> P2.6a ############"
"$REF/run_pair.sh" bin_p25_d  bin_p26a_d cpu 10 bis_a  > "$REF/bis_a.out" 2>&1
grep -E "IDENTITY|differing|chains total|CONTROL FAIL" "$REF/bis_a.out"

echo "############ BISECT: P2.6a -> P2.6b ############"
"$REF/run_pair.sh" bin_p26a_d bin_p26b_d cpu 10 bis_b > "$REF/bis_b.out" 2>&1
grep -E "IDENTITY|differing|chains total|CONTROL FAIL" "$REF/bis_b.out"

echo "############ BISECT: P2.6b -> HEAD (P2.6c) ############"
"$REF/run_pair.sh" bin_p26b_d bin_head_d cpu 10 bis_c > "$REF/bis_c.out" 2>&1
grep -E "IDENTITY|differing|chains total|CONTROL FAIL" "$REF/bis_c.out"

echo "############ PRODUCTION NO-OP PROOF: pristine vs skip-capable, env unset ############"
"$REF/run_pair.sh" bin_prod_pristine bin_prod_skip cpu 10 noop > "$REF/noop.out" 2>&1
grep -E "IDENTITY|differing|chains total|CONTROL FAIL" "$REF/noop.out"

echo "############ 300-EVENT SCOREBOARD, P2.5 vs HEAD ############"
"$REF/run_scoreboard.sh" cpu 32 > "$REF/j1_scoreboard.out" 2>&1
tail -40 "$REF/j1_scoreboard.out"
echo BLOCK2_DONE
