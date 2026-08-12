# PAIRDUMP_FORMAT.md -- the `LST_CHAIN_PAIR_DUMP` attach pair sidecar

The on-policy pair-row instrument for step 3 of `PLAN_NN_LOOP.md`: it emits, from the DEPLOYED
binary, one row per SCORED attach pair, carrying the 20 standardized head inputs **as the head
consumed them** and the logit the head produced. It exists because no pair-level sidecar did: the
attach head's shipped training rows came from an offline prototype whose flag string is pre-B2, and
that provenance drift is exactly the bug class the loop kills.

    LST_CHAIN_PAIR_DUMP=<path>     enable; append-mode binary, one record per event
    LST_CHAIN_PAIR_CAP=<rows>      device row capacity per event   (default 1000000, = 96 MB)
    LST_CHAIN_PAIR_DSB=<n>         stage-B keep factor, 1 = keep all (default 16)

With `LST_CHAIN_PAIR_DUMP` unset nothing is allocated, no kernel argument is non-null and no byte is
written. Verified: 35/35 judge fields bit-identical to `ship_ref/B2_shipped.judge` with the variable
unset AND with it set (see `standalone/FINDINGS_NN.md`, `[PD]` entries).

Writer: `LSTEvent::dumpChainPairs`, `src/alpaka/LSTEvent.dev.cc`.
Reader: `standalone/nnloop_ref/read_pairs.py`.
Record type: `ChainAttachPairRow`, `src/alpaka/ChainAttach.h`.

--------------------------------------------------------------------------------------------------

## BYTE LAYOUT

Little-endian throughout (x86 / CUDA host order); no padding anywhere -- the record is
`static_assert`ed to be exactly `4 * uint32 + 20 * float` = 96 bytes.

The file is a bare concatenation of per-event records. There is no file-level header and no index:
read the 40-byte event header, then `nRows * 96` bytes, then the next event header, until EOF.

### Event header -- 10 x uint32, 40 bytes

| off | type   | name      | meaning |
|-----|--------|-----------|---------|
| 0   | uint32 | `magic`   | `0x50414952` = `'PAIR'` |
| 4   | uint32 | `version` | format version, currently `1` |
| 8   | uint32 | `ievt`    | sequential event counter, `static std::atomic` in the writer -- the SAME counting as every other chain sidecar, so record *i* of `pairs.bin` is record *i* of `chains.bin` from the same run. Meaningful only for a single-stream run. |
| 12  | uint32 | `nChains` | `nChainCount_` for this event: the number of chain rows, i.e. the exclusive upper bound on a stage-0 / stage-2 `target`, and the same `nC` the `'P22C'` chain dump writes |
| 16  | uint32 | `nT3`     | the Triplets SoA size: the exclusive upper bound on a stage-1 `target` |
| 20  | uint32 | `nRows`   | records that follow, `min(cursor, cap)` |
| 24  | uint32 | `nDrop`   | pairs dropped because the cursor ran past `cap`. **A non-zero value means the event is incomplete** -- raise `LST_CHAIN_PAIR_CAP`. Never a partial record. |
| 28  | uint32 | `dsA`     | stage-A downsample factor, always `1` (stage A is kept whole) |
| 32  | uint32 | `dsB`     | stage-B downsample factor (default 16). One stage-B row stands for `dsB` scored pairs; weight accordingly. |
| 36  | uint32 | `nFeat`   | head input width, `kAttachFeatures` = 20 |

### Pair record -- 96 bytes, repeated `nRows` times

| off | type       | name     | meaning |
|-----|------------|----------|---------|
| 0   | uint32     | `stage`  | see the stage table below |
| 4   | uint32     | `target` | the target identity (see the stage table) |
| 8   | uint32     | `pls`    | the pLS row, `AttachPlsPre::row` |
| 12  | float32    | `logit`  | the head output for exactly the `x` in this record |
| 16  | float32[20]| `x`      | the standardized head inputs, in head-input order 0..19 |

### `stage`

| code | source kernel | target list | `target` means | in the `[CHAIN K8] scored=` census |
|------|---------------|-------------|----------------|-----------------------------------|
| 0 | `ChainAttachScore` (`ChainAttach.h`) | chain targets with `nLayers >= kAttachMinLayers` (5) -- the delivery-eligible stage-A list | the **chain index**, the row the `LST_CHAIN_CHAIN_DUMP` (`'P22C'`) file enumerates in the same order | YES, exactly |
| 1 | `ChainAttachT3Score` (`ChainAttachT3.h`) | bare-T3 targets (stage B) | the **sparse triplet index** (`AttachTargetPre::chain` for the bare-T3 kind), the numbering the `--allobj` ntuple's `t3_rawIdx` branch uses | no -- stage B has its own `[CHAIN K8B] scored=` |
| 2 | `ChainAttachScore`, same launch as 0 | the aux 4-layer (`-XC4`) tail of the stage-A target array -- **score-only** targets that write no delivery verdict | the **chain index**, as for 0 | NO. The stage-A census stops before these, which is why they carry their own code: mixing them into 0 would break the count cross-check. |

Stage 0 and stage 2 rows are the same kind of object scored by the same kernel with the same
features; they differ only in whether the target could win a delivery. Keep or drop stage 2
deliberately.

--------------------------------------------------------------------------------------------------

## WHAT `x` IS, AND HOW TO GET RAW FEATURES BACK

`x` is captured out of the batch staging buffer at flush time, `xT[i * kB + b]`, i.e. it is the
vector the MLP actually multiplied -- **after** any per-kind overwrite. That matters for stage B,
which rewrites input 18 with the bare-T3 target type AFTER `attachEvalPairX` has filled the frozen
19 (`ChainAttachT3.h`); capturing inside `attachEvalPairX` would have recorded the chain-kind value
for a bare-T3 pair. Proof that the capture is the consumed vector: replaying the head over the
dumped `x` reproduces the dumped `logit` to `< 1e-5` absolute on logits up to |29|
(`read_pairs.py --roundtrip`).

So `x` is **standardized, not raw**. The transform is `attachStdz<i>` in `ChainAttach.h`, whose
constants live in `src/alpaka/AttachNetworkWeights.h`; per input `i`, in this order:

    v = raw
    if kLog10p1[i]:  v = log10(1 + v)
    v = clip(v, kClipLo[i], kClipHi[i])
    v = (v - kFeatMean[i]) / kFeatStd[i]

Inverting gives the pre-clip value only where the clip did not bite, which is the intended
contract: a retrain consumes the standardized vector directly and does not need the raw one. **Any
retrained head must ship the norm constants it was fitted with**, since `x` is only meaningful
against the constants that produced it -- record the hash of `AttachNetworkWeights.h` with the dump
(`PLAN_NN_LOOP.md`, "Provenance, non-negotiable").

The 20 inputs, from `attachEvalPairX` (`ChainAttach.h:~738`) and the two `*Pre` builders:

    0  log10(pLS pt)              7  target fitKappa
    1  pLS ptErr / pt             8  target tanLambda
    2  pLS etaErr                 9  target innermost layer
    3  pLS charge                10  target nLayers (3 for a bare T3)
    4  pLS isQuad                11  target chain gate logit (0 for a bare T3)
    5  log10(pLS circle radius)  12  chargeAgree            16  centerDist
    6  pLS deltaPhi              13  dKappa                 17  zResid
                                 14  dTanLambda             18  targetType (0 chain / bare-T3 kind)
                                 15  dPhi                   19  rphiResidInwards

--------------------------------------------------------------------------------------------------

## SEMANTICS OF THE ROW SET

**The rows are the SCORED stream, not the delivered one.** A record exists for every pair that
`attachEvalPairX` accepted -- i.e. that passed the two analytic prefilter windows -- and that was
therefore pushed through the head. Emission happens before every verdict: before the banded
delivery threshold, before the `-XC` crossclean bar, before stage B's `theta` and before its
"stage A already owns this pLS" skip. Pairs the prefilter REJECTED are not in the dump (they never
reached the head, and their features were never completed).

**Row order is not defined.** Slots are handed out by an atomic cursor across threads, so the order
within an event varies with the launch shape even though the row SET does not. Sort offline if you
need determinism in order as well as in content.

**Stage-B downsampling is label-free, score-free and verifiable.** `attachPairKeep`
(`ChainAttach.h`) keeps a stage-B pair iff

    ((target * 2654435761u) ^ (pls * 40503u)) % dsB == 0

on `uint32` arithmetic, using the two identities the record itself carries -- so a rerun reproduces
the same subset bit-for-bit, and `read_pairs.py` re-derives the predicate from the dumped rows and
fails if any row should not be there. Stage A is kept whole because it is ~1.2e5 pairs/event against
stage B's ~1.0e6.

**Truth joining is the reader's job.** The dump carries identities, never labels: nothing in it
depends on sim matching, so it cannot leak a label into the keep decision. Join stage 0 / 2 rows to
`chains.bin` on `(ievt, target)`, stage 1 rows to the `t3_*` ntuple branches on the sparse triplet
index, and the pLS side through the pLS collection order (`AttachPlsPre` carries no `seedIdx`, so
there is no cheaper key at the call site).

## VOLUME

Measured, PU200RelVal, defaults (`dsB` = 16), 30 events:

    mean   stage A 1.23e5 rows/evt   stage B 6.5e4   aux4L 1.4e4   total 2.0e5 rows/evt (19 MB)
    max    one event reached 4.7e5 rows total (2.7e5 stage A) -- events vary by nearly 4x
    ->  580 MB for 30 events, i.e. ~19 GB for a 1000-event training dump

Stage A dominates and is not downsampled, so raising `dsB` does not buy much: at 256 an event is
still ~13 MB. Budget the disk before dumping the full `event_1000.root` training set (the box had
161 GB free when this was written), or dump in chunks.

The device-side buffer is `LST_CHAIN_PAIR_CAP` x 96 B, allocated once per LSTEvent (i.e. per stream)
on first use and reused. The default 1e6 rows is 96 MB, 2.1x the busiest event seen in 30. **For a
production training dump pass `LST_CHAIN_PAIR_CAP=2000000`**; a busier event than any of these 30
would otherwise lose rows. That loss is never silent: the writer counts it in `nDrop` and
`read_pairs.py` fails the file on any non-zero `nDrop`.
