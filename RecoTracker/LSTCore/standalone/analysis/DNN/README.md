# Which script produced each SHIPPED network

**Read this before running anything in this directory.** The three `train_*.py` scripts here are the
ORIGINAL (pre-NN-loop) trainers. They are kept for lineage, but they DO NOT reproduce the weights
currently shipped: every head has been retrained since, by scripts that live next to their round's
work. This file is the authoritative map from a deployed weights header to the script and invocation
that produced it. Keep it updated in the SAME commit that ships a weights header.

## Currently deployed (as of `04c6e122e68`, jet-core round 2)

| deployed header | produced by | recipe |
|---|---|---|
| `src/alpaka/ChainNetworkWeights.h` | `standalone/g2_ref/train3mix2.py` (imports `COND`, `build_inputs`, `event_split` from `standalone/nnloop_ref/s2_work/train3.py`) | gun-enriched 3-class chain gate, arch 25->32->32->3 |
| `src/alpaka/EdgeNetworkWeights.h` | `standalone/nnloop_ref/s1_work/train_s1.py` | NN-loop arm G, on-policy, 3-class equal class weights, arch 40->32->32->3 |
| `src/alpaka/AttachNetworkWeights.h` | `standalone/nnloop_ref/s3_work/train_s3.py` | NN-loop S3, 22 inputs (`Chain2NetworkWeights.h` deleted, no surrogate) |

### Chain gate — exact invocation, verified (`standalone/g2_ref/trainq.sh:16`)

```
python3 g2_ref/train3mix2.py \
  --lab pu=p3_ref/lab/pu --lab jet=p3_ref/lab/jet --lab c5hp=g2_ref/lab/c5hp \
  --share jet=0.25 --share c5hp=0.02 --flat c5hp --tag A02
```

The FIRST `--lab` is the reference sample whose train split fits the standardization (always `pu=`).
`--flat` is MANDATORY for gun samples: they are 99.8% true, so the shipped `pos_weight = nFake/nTrue`
convention evaluates to 0.011 and would hand a gun's entire loss share to its handful of fake rows.
Bars are refit on PU200 rows only. Jets rows are events 0-499 ONLY (500-999 is the sealed holdout).

### Edge and attach — invocations

Recorded with their rounds in `standalone/PLAN_NN_LOOP.md` and `standalone/FINDINGS_NN.md`, not
reproduced here (they have not been re-verified line-for-line in this commit). `train_s1.py`'s own
docstring states its recipe is the shipped v3 recipe (`prototype/edge_norm_v3.json` train_args +
`analysis/DNN/train_edge.py`) run against the on-policy round-1 dump.

## Lineage, so the chain is not lost

```
analysis/DNN/train_chain.py      (M6 era, "plan 5a")
  -> nnloop_ref/s2_work/train3.py    (NN loop: GM12F, on-policy, m12 tiered displaced weighting)
    -> p3_ref/train3mix.py           (jet round 1: jet-core rows at L=0.25)
      -> g2_ref/train3mix2.py        (jet round 2: + cube5_highPt gun rows, --share/--flat)  SHIPPED

analysis/DNN/train_edge.py       -> nnloop_ref/s1_work/train_s1.py   (arm G)                 SHIPPED
analysis/DNN/train_attach.py     -> nnloop_ref/s3_work/train_s3.py   (22-input)              SHIPPED
```

## Standing rule

The NN-loop commits (`49f54d8c971`, `81a9afe2d00`), jet round 1, and jet round 2's ship commit all
changed deployed weights WITHOUT touching this directory. That is the failure this file exists to
prevent: a stale script in a canonical location is worse than no script, because it looks
authoritative. When a weights header changes, update this map in the same commit.

## Known pending

An on-policy attach retrain is in progress behind the round-2 chain gate: the attach head reads the
gate's three raw logits as inputs 11-13 (`ChainAttach.h:607-609` <- `ChainGate.h:535-537`), so
retraining the gate moved its input distribution and its bars are still calibrated to the old logit
scale. When that lands, the attach row above changes.
