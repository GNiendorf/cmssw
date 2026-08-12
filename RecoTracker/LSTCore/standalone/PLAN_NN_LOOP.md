# PLAN_NN_LOOP.md -- ON-POLICY TRAINING LOOP FOR THE CHAIN NETWORKS

**Owner: the coordinator (not an agent). Maintainer decision 2026-08-10.**
**Goal: 4 networks -> 3, with trainings we actually understand.**

--------------------------------------------------------------------------------------------------

## THE PROBLEM THIS FIXES

The four heads form a cascade. Each head's INPUT DISTRIBUTION is produced by the heads upstream of
it, but every head was trained greedily on a FROZEN DUMP from some earlier configuration, and the
provenance drifted. Two concrete, documented consequences:

 * The shipped 3-class gate was trained on `prototype/chains_m12_{300,498}evt.root`, which were
   dumped by a **retrained-edge** chainproto whose edge head is **NOT the shipped one**
   (`FINDINGS_CUBE.md` item 3). **Chain features 2/3/4/18 ARE edge logits**, so those rows sit on a
   different scale than the ones the shipped gate actually sees at reco time.
 * The 3-class gate and the (to-be-deleted) 2-class head were trained on **different dumps
   entirely** -- `prototype/chains_m12_*` vs `fanout/a2_gatecapacity/chains_a2_*` -- i.e. two
   different upstream snapshots.

What we currently do about it is re-fit the DOWNSTREAM BARS when an upstream head changes. That
absorbs a SCALE shift in a downstream input. It does nothing about a DISTRIBUTION shift in which
objects exist at all.

## WHY NOT AN OFFLINE END-TO-END REPLICA

Rejected deliberately. The decisive steps are non-differentiable: the mutual-best argmax weld, K9's
greedy hit claim, the ownership cleaning, and every threshold; and the target metric
(eff/dup/fake AFTER greedy arbitration) is not differentiable at all. A Python replica also drifts
from the alpaka implementation. Cost and risk both high, benefit mostly obtainable from the loop
below.

**Cheap piece worth stealing from the idea:** weight each head's training examples by DOWNSTREAM
CONSEQUENCE (e.g. weight an edge row by whether its chain survived and was delivered) rather than
by its own local label only. Captures the cost of a decision without differentiating anything.

--------------------------------------------------------------------------------------------------

## THE LOOP

The enabling fact: **all four row-sets can be emitted from ONE run of ONE binary.** The dump
sidecars already exist and are env-gated:

    LST_CHAIN_EDGE_DUMP    edge rows      (+ LST_CHAIN_FEAT_DUMP for K5's 14 edge floats)
    LST_CHAIN_NODE_DUMP    node rows
    LST_CHAIN_CHAIN_DUMP   chain rows     (LSTEvent::dumpChains)
    LST_CHAIN_TC_DUMP      TC-level rows
    (attach pairs: the M16 general pair dump behind train_attach_gen.py, pairs_gen_c*.root)

### Per round, in TOPOLOGICAL ORDER, re-dumping between stages

    1. dump edges   with current binary  ->  retrain EDGE head
       -> re-derive edge working point at FIXED SIGNAL EFFICIENCY  -> rebuild
    2. dump chains  with THAT binary     ->  retrain 3-CLASS GATE
       -> re-derive gate working points, prompt-class and displaced-class SEPARATELY -> rebuild
    3. dump pairs   with THAT binary     ->  retrain ATTACH head (22 inputs, three raw gate logits,
                                             `Chain2NetworkWeights.h` DELETED, no surrogate)
       -> re-derive delivery bars at fixed signal efficiency -> rebuild

Re-dumping between stages is REQUIRED, not optional: a working point decides which objects exist
downstream, so a downstream head trained before the upstream WP is re-derived is off-policy again.

### Convergence

Iterate rounds until a round is a NO-OP (round N+1's retrain barely moves the metrics). Expect 2-3.
If it oscillates, stop and report -- that is a finding.

### Provenance, non-negotiable

Every dump file records the binary's git hash AND the hash of every weight header in that build.
Any training run whose dump provenance does not match the binary it will be deployed in is invalid.
This is the single rule that prevents the bug class described at the top.

--------------------------------------------------------------------------------------------------

## SAMPLE SPLIT (maintainer decision, 2026-08-10)

**TRAIN on `event_1000.root` only** (1000 events; the current shipped dumps used 300+498 = 798, so
this is a modest increase and keeps the full held-out set intact).

    TRAIN    /data2/segmentlinking/CMSSW_12_5_0_pre3/RelValTTbar_14TeV_CMSSW_12_5_0_pre3/event_1000.root
    HOLDOUT  event_2000 .. event_7000  (6000 events, A5's paired reference runs live in a5_ref/big/)

Rejected for now: training on event_1000+3000+4000 (3000 evt) at the cost of shrinking the holdout.
Revisit only if data volume proves binding again after the loop is on-policy.

## GATES (unchanged)

PU200 tune (`event_1000`) + the pooled 6000-event holdout with paired McNemar (`a5_ref/paired.py` --
the writer emits events in STREAM-COMPLETION order, so naive entry-wise pairing is nonsense) + BOTH
cube samples + conversion rate `conv_T5` reported as an output (.6871 shipped) + ship-verification
bit-identical on all 35 judge fields + timing per scope rule 6 if volume changes.

**Acceptance: physics equal or better. Neutral is the floor, improved is the target.**

--------------------------------------------------------------------------------------------------

## WHAT WE ALREADY KNOW GOING IN (do not re-derive)

 * **B1's control, the most important result so far.** A 20-input head that KEEPS `gateLogit2`,
   shipped recipe, trained on the same halved 52.4M-pair cache, is at **FPR ratio 1.3944** vs
   shipped. The 22-input head with the network DELETED, same data, is at **1.1408**. So at equal
   footing the deleted-network head is **15% BETTER on background** (1.1408/1.3944 = 0.818), and the
   whole "14% excess" A4 attributed to the deletion is a **TRAINING-DATA DEFICIT** -- every arm was
   trained on 46% of the shipped head's data. A4's "root cause is the head" is RETRACTED.
 * **Widening the hidden layers buys nothing** (B1). A4's top-ranked lever is falsified.
 * `gateLogit2` scores **.336 on displaced-vs-prompt -- inverted, worse than random.** The shipped
   attach head is fed a signal anti-correlated with displacement, so the deletion should IMPROVE
   displaced performance, not merely preserve it. A4 measured +.0032 vxy[1,5) at the pair level.
 * `mX = max(zPrompt,zDisp) - zFake` reproduces that same inverted defect (.331) because max()
   discards WHICH class won. Do not reduce the three logits to a hand-derived scalar.
 * Calibration: only **per-bin fixed signal efficiency** isolates head quality. Pair acceptance rate
   and per-target conversion rate are MIXED quantities; matching them has already failed twice
   (7.8 points of conversion lost, then `dup_barrel` doubled).
 * **PU200 is the binding gate for an attach-head change**, not cube50_highPt (both cube samples
   came back clean or bit-identical for every A4 arm).
 * The 3-class gate must STAY: bars sit directly on its outputs (`m3Theta4` on mX, `m3Theta4D` on
   mD, the `m3ThetaR*` family, `c25Theta`/`c25ThetaD`) and `marginX` is read by the K9 ORDER KEY
   (`ChainArbitrate.h:141`).
 * Traps: `lst_make_tracklooper` prints success even when a TU fails -- grep the FRESH
   `.make.log.<timestamp>` for `error:` WITH the colon. `cmsenv` is an alias and no-ops in a script.
   `cube50_highPt` segfaults in the writer above `-s 4`. Env hooks must be in the LSTEvent
   constructor (the standalone driver never calls `LST::run`) and the override must be confirmed to
   have PRINTED.

## STATE AT HANDOFF

Round-5 physics candidates are committed and unshipped (`4ea386942ca`): `a6_ref/a6_candidate_C1.patch`,
`a5_ref/candidate_r5far8.patch`, `a3_ref/P.patch`, `a5_ref/candidate_Z0.patch`. Three combining
agents were planned for those and never launched. This NN loop is the active task; the physics
combination round is queued behind it.
