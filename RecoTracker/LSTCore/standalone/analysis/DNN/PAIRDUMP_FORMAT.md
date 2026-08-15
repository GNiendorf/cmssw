# `LST_CHAIN_PAIR_DUMP` -- the attach pair sidecar

The on-policy training rows for the attach head: one record per SCORED (target, seed) pair,
carrying the standardized head inputs **as the head consumed them** and the logit the head
produced. It exists because the head's decisions change which pairs are ever scored, so rows
harvested from any other binary are off-policy against the one that will run.

    LST_CHAIN_PAIR_DUMP=<path>     enable; append-mode binary, one record per event
    LST_CHAIN_PAIR_CAP=<rows>      device row capacity per event   (default 1000000)
    LST_CHAIN_PAIR_DSB=<n>         stage-B keep factor, 1 = keep all (default 16)
    LST_CHAIN_JOIN_DUMP=<path>     the companion truth-join keys (see bottom)

With `LST_CHAIN_PAIR_DUMP` unset nothing is allocated, both scorers receive `nullptr`, and no byte
is written. Verified bit-identical to the binary built immediately before the instrument landed,
both with the dump off and with it on.

Writer: `LSTEvent::dumpChainPairs`, `src/alpaka/LSTEvent.dev.cc`.
Record type: `ChainAttachPairRow`, `src/alpaka/ChainAttach.h`.

--------------------------------------------------------------------------------------------------

## BYTE LAYOUT

Little-endian, no padding. The file is a bare concatenation of per-event records: read the event
header, then `nRows * rowBytes`, then the next header, until EOF. There is no file-level header and
no index.

**Take `rowBytes` from the header, never from a constant.** It is

    rowBytes = 4 * 4 + (nFeat + nProbe) * 4

### Event header -- 11 x uint32, 44 bytes

| off | name      | meaning |
|-----|-----------|---------|
| 0   | `magic`   | `0x50414952` = `'PAIR'` |
| 4   | `version` | **2**. Version 1 had no `nProbe` word and no probe columns; a v1 reader must REJECT v2 rather than mis-stride it. |
| 8   | `ievt`    | sequential event counter -- the same counting as every other chain sidecar, so record *i* here is record *i* of the chain dump from the same run. Meaningful only for a SINGLE-STREAM run. |
| 12  | `nChains` | chain rows this event: the exclusive upper bound on a stage-0 / stage-2 `target` |
| 16  | `nT3`     | Triplets SoA size: the exclusive upper bound on a stage-1 `target` |
| 20  | `nRows`   | records that follow, `min(cursor, cap)` |
| 24  | `nDrop`   | pairs dropped because the cursor ran past `cap`. **Non-zero means the event is incomplete** -- raise `LST_CHAIN_PAIR_CAP` and re-dump. Never a partial record. |
| 28  | `dsA`     | stage-A downsample factor, always `1` (stage A is kept whole) |
| 32  | `dsB`     | stage-B downsample factor. One stage-B row stands for `dsB` scored pairs; weight accordingly. |
| 36  | `nFeat`   | head input width = `kAttachFeatures` |
| 40  | `nProbe`  | probe floats following the head inputs on each row = `kAttachProbeColumns` |

### Row -- `4*uint32 + (nFeat + nProbe)*float`

| field   | type            | meaning |
|---------|-----------------|---------|
| `stage` | uint32          | `0` chain target at or above the attach layer floor; `1` bare-triplet target; `2` chain target from the auxiliary 4-layer tail (score-only, outside the stage-A census -- keep or drop deliberately) |
| `target`| uint32          | stage 0/2: the CHAIN row. stage 1: the SPARSE TRIPLET index. |
| `pls`   | uint32          | the pLS row. The record carries no seed index, so the join to the ntuple goes through the pLS collection order, or through the join sidecar. |
| `logit` | float           | the head output for exactly the `x` below |
| `x`     | float[`nFeat`]  | the STANDARDIZED head inputs as consumed, after any per-kind overwrite. De-standardize with the constants in `AttachNetworkWeights.h`. |
| probes  | float[`nProbe`] | see below |

### Probe columns (`nProbe`, currently 1)

Probe columns are **not head inputs**. They exist so a trainer can ask whether the head is missing
something LST has.

| idx | name    | meaning |
|-----|---------|---------|
| 0   | `dBeta` | master's tracklet closure for this pair: the angle the seed's momentum makes with the seed-to-target chord, minus the angle the target's first segment makes with it. Transcribed from `PixelTriplet.h` (`runTripletDefaultAlgoPPBB`/`PPEE` beta block, `runDeltaBetaIterations` at its `lIn == 0` branch). Needs no fit of either object. **`0` means NOT AVAILABLE** (degenerate tracklet, or a target with fewer than two anchors) -- it is a flag, not a small value, so filter it rather than feeding it as a number. Master's `dBetaCut2` acceptance threshold is deliberately not reproduced; only the quantity is dumped. |

--------------------------------------------------------------------------------------------------

## The join sidecar (`LST_CHAIN_JOIN_DUMP`)

The pair rows carry identities and no geometry a truth matcher can use. This companion closes that
gap and nothing else -- **no sim information and no label**. It is written from exactly the two
places the pair dump is, so record *i* corresponds, and `(nChains, nT3)` is repeated as a
fingerprint.

    magic 'PJ01', ievt, nChains, nT3, nNodes, nPls          -- 6 x uint32
    per node:  tripletIndex, then 3 x (anchor ph2 hit row, other ph2 hit row)   -- 7 x uint32
    per pLS:   see_* ntuple row (uint32), eta (float)
