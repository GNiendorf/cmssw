# P2 Alpaka Integration Port Map — Chain Tracking

**Status:** DRAFT, written during the M19 capstone round (2026-08-02) by the parallel P2-PREP
agent, per the maintainer directive in `PLAN_lst_redesign_t3_onward.md` §11
("one parallel P2-PREP agent drafts the Alpaka port mapping ... so integration starts
immediately at config freeze").

**This document is a plan, not a commitment to numbers.** Every threshold value quoted is the
*M18b flagship* (`fl_balanced`) value and is expected to move when M19 freezes the final config.
The *structure* (stage list, launch shapes, SoA layouts, config surface, landing order) is what
this document commits to; the numeric column is a placeholder that the freeze fills in.

**Sources of truth**
| what | where |
|---|---|
| Design contract | `standalone/PLAN_lst_redesign_t3_onward.md` §3, §3b, §4, §5a, §5b, §7, §10.3, §11 |
| Reference implementation (GOLDEN, read-only) | `standalone/fanout4/compose_attach/` |
| Canonical 3-class head | `fanout4/compose_attach/chain3_mlp_weights.h`, md5 `d170f1742ea52ce14d89b174b3ca0817` |
| Flagship command line | `fanout4/compose_attach/at_run.sh` + `m18b_taskA.sh` |
| Production kernel idioms | `src/alpaka/LSTEvent.dev.cc`, `src/alpaka/Kernels.h`, `src/alpaka/NeuralNetwork.h` |
| Production SoAs | `interface/*SoA.h`, `interface/alpaka/*DeviceCollection.h` |

---

## FREEZE ADDENDUM (2026-08-02, post-M19; supersedes the table above where they differ)

The maintainer blessed the M19 freeze candidate. The frozen sources are MERGED into
`standalone/prototype/` (git commit `cd07ebb531a` on `chain_tracking_proto`) — **that tree,
not fanout4/compose_attach, is now the reference implementation**; the frozen numbers were
re-verified there (eff .8129, dup .0518, fake .0463, nhitOT 9.895/9.886/3.467, d510 72/285).

FROZEN COMMAND (overrides on ANCHOR + CTL; later flags win; full rationale in
`standalone/fanout5/final/FREEZE_RECORD.txt`):
```
-BK 1 -BT 5 -TR 1 -TT 0.8 -TA 1.0 -F 0.20 -MRI -0.5 -M4 4.0 -M4D -1.2 -PU 1 -C25 0.0
-ZM4D 1.2 -ZM4 -0.5 -TT 1.2 -a 8 -RPS 1 -RD 1
-a 6.875 -WE 0.20 -WZ 1.5 -FBC 0 -EX 1 -EXW 0.25 -EXR 2.0 -EXS 1 -L 3.0
```

M19 deltas this port map must absorb (all small):
1. **Extend.cc/.h is a new TU** (chain extension at assembly, post-K9/pre-K10; flags
   -EX/-EXW/-EXR/-EXS on, remaining -EX* at defaults). One more kernel in the K6b'/trim
   family: per accepted chain, test fit-consistent unclaimed hits at the outer end
   (LS-linked); needs the K9 owner map read-only (already exported via ownerOut).
2. **Band-aware braid** in K9 (-WE 0.20 -WZ 1.5, per-band claim tolerance -FBC 0): four new
   ArbitrationParams fields; the braid fraction becomes per-candidate (|eta| >= 1.5 uses the
   tight value). No new kernel — it rides the existing claim loop.
3. **Attach head = r2** (`attach_mlp_weights.h` md5 `eb4e1bf7...`; g1 kept as
   `attach_mlp_weights_g1.h.keep`) at threshold **-a 6.875** (was 8).
4. **-L 3.0** (chain score length weight; ANCHOR said 0.5).
5. Confirmed non-ports stay: no K3/K4, no K7 dedup kernel (-DD off in the freeze), no
   -KL/-TW (rejected), edge head unchanged (v3; displacement retrain was a measured no-go —
   dxy[10,30) is a DECLARED open regression, deferred to the cube/jet round).

## 0. What M18 actually built vs what plan §3 assumed

The port map must be written against the *implemented* pipeline, not the 2026-07-29 kernel table.
Four deltas, all simplifications:

1. **K3 NodeEncoder and K4 MessagePass were never built and are not needed.** The edge head
   (`edgemlp`, `kInput = 40`) consumes the raw 13-float inner-node + 13-float outer-node +
   14-float edge feature rows directly (`Features.h`, `edge_mlp_weights.h:63-66`). There is no
   learned node latent and no message-passing round. Plan §4 budgeted ~6 ms + ~12 ms for these;
   both are zero. **Do not port K3/K4.**
2. **K2 has no physics gate.** Plan §3's "~10-FLOP origin-free gate" was measured powerless
   (plan §10.4c M2: 0.6% fake rejection at 99.9% true-edge efficiency; `chargeAgree` unusable —
   27% of displaced true edges flip rotation sign). `k2BuildEdges` emits every enumerated pair.
   Consequence for the port: the edge buffer is **exactly** sized by degree arithmetic and needs
   **no compaction atomics at all** (see K2 below).
3. **K7 ChainDedup (embedding/bucket dedup) is not in the flagship.** The M17 `-DD`
   post-arbitration structural dedup exists but is off; `-K` K7-lite is `-A 2`-only. Duplicate
   control is entirely (a) the hit-level K9 claim (`-H 1`), (b) the owner-relative braid kill
   (`-W`), (c) claim-universe unification with the carried pixel rows (`-PU`), (d) the attach
   contention rule. **Do not port a dedup kernel** unless M19 revives one.
4. **A terminal-trim stage (K6b') exists that plan §3 did not name.** `Trim.h` / `Trim.cc`,
   run post-weld / pre-gate / pre-claim. It is on in the flagship (`-TR 1 -TT 1.2 -TA 1.0`).

Everything else maps 1:1. Plan §10.3's promise holds: each prototype stage is a pure function
over flat arrays; the port wraps it in a kernel and grid-strides the outer loop.

### Measured multiplicities the port is sized against
300-event `LSTNtuple_PU200RelVal_300evt.root`, pT > 0.8, flagship `fl_balanced`
(`fanout4/compose_attach/at_fl_balanced.log:312-324`; the M1 E-distribution is plan §10.4c
2026-08-01):

| quantity | mean/evt | p90 | p99 | max | 300-evt total |
|---|---|---|---|---|---|
| MDs | ~74k–90k | | | | |
| LSs | ~119k | | | | |
| **T3 nodes** | **43.3k** | | | 60.3k | |
| pLS seeds | 18.4k | | | | |
| E1 edges | 76.5k | | | 545k | |
| E2 edges | 33.3k | | | | |
| **edges total (raw E)** | **~110k** | 169k | 332k | **623k** | |
| **welded chains (pre-arb)** | **4,757** | | | | 1,427,138 |
| chains past 3-class gate | 2,543 | | | | 762,921 |
| chains past pixel-consumed drop | 2,485 | | | | 745,404 |
| **K9-accepted chains (claims)** | **1,010.6** | | | | 303,191 |
| — of those T5-class / T4-class | 979 / 32 | | | | 293,628 / 9,563 |
| attach pairs enumerated == scored | 130,998 | | | | 39,299,416 |
| attach deliveries (type-7 upgrades) | 481.9 | | | | 144,585 |
| carried pixel rows retired | 812.2 | | | | 243,656 |
| output TCs | 2,055.5 | | | | |

**Timing (CPU, single-thread, prototype):** `infer 42.5 / weld 7.5 / attach 201.8 /
arb+asm 1.6 / fill 11.4` ms = **~265 ms/evt, attach = 76%**. The attach scan is
`nTargets × nPLS ≈ 2.5k × 18.4k ≈ 4.6e7` `evalPair` probes/evt with a 0.28% survival rate.
The grid prefilter (§1, K8a) is the designed replacement and is quantified there.

---

## 1. Stage → kernel table

Conventions used below:
- `Acc1D` / `Acc2D` / `Acc3D` and `cms::alpakatools::make_workdiv<AccND>({grid}, {block})` exactly
  as `LSTEvent.dev.cc` uses them (e.g. `:415` single-block prefix `make_workdiv<Acc1D>(1, 1024)`;
  `:398` `make_workdiv<Acc3D>({nLowerModules_,1,1},{1,16,16})`).
- "flat" = `for (T i : cms::alpakatools::uniform_elements_x(acc, N))` — one grid-stride loop over
  a *global* index space, **no per-module blocking anywhere in the chain pipeline**. This is the
  whole point (plan §3: "No block-per-module serialization anywhere").
- `once_per_block(acc)` for scalar writes (`Kernels.h:309`, `:492`).
- MLP inference reuses `src/alpaka/NeuralNetwork.h`'s `linear_layer<IN,OUT>` /
  `relu_activation<N>` / `softmax_activation<N>` templates verbatim; weights come from generated
  headers with the same TRANSPOSED `wgt[in][out]` convention (`edge_mlp_weights.h:9-11`).

| # | kernel (proposed name) | prototype source | launch | index space | notes |
|---|---|---|---|---|---|
| **K0** | `CompactTriplets` | *(new; the plan §7 P0a "accept-bit + prefix compaction" gap)* | Acc1D flat + `make_workdiv<Acc1D>(1,1024)` prefix | `nLowerModules_` then `nTotalTrips` | LST stores T3s per-module-segmented (`ObjectRangesSoA::tripletRanges`, `TripletsOccupancy::nTriplets`). The chain graph needs a **dense global node index**. Count/prefix/scatter over module T3 counts → `globalT3Idx[]` and its inverse. One-time, ~0.2 ms. |
| **K1a** | *(fused into T3 builder)* | `Stages.cc:41-44` | — | — | Counts fused into `CreateTriplets`'s accept path (plan §3b step 1): `atomicAdd(&mdOutCount[md0],1)`, `atomicAdd(&mdInCount[md2],1)`, `atomicAdd(&lsOutCount[ls0],1)`, `atomicAdd(&lsInCount[ls1],1)`. **No separate counting pass ever runs.** |
| **K1b** | `PrefixIncidence` | `Stages.cc:11-25` (`buildCsr` prefix half) | `Acc1D`, `make_workdiv<Acc1D>(1,1024)` (existing single-block pattern, `LSTEvent.dev.cc:415`) | 4 arrays of length `nMD+1` / `nLS+1` | Exclusive prefix over the 4 count arrays **and**, in the same kernel, the per-key products `degIn*degOut` and *their* prefix → exact `E1`, `E2` and the edge-ID→key map, free (plan §3b step 2). Emit `nEdges` as a SOA_SCALAR. |
| **K1c** | `ScatterIncidence` | `Stages.cc:22-24` | `Acc1D` flat | `nT3` | `slot = atomicAdd(&cursor[key],1); items[slot] = t3`. Arrival order irrelevant (plan §3b step 3: the edge set is a cross product of slices, invariant under slice permutation). |
| **K2** | `BuildEdges` | `Stages.cc:47-116` | `Acc1D` flat, block 256 | **exactly `E1exact + E2exact`** | Each thread binary-searches the product-prefix (few hundred KB, L2-resident, ~17 cached reads), decodes the key by upper-bound, then `(i,j) = (rem / degOut, rem % degOut)`, reads `mdT3InItems[inOff+i]` / `mdT3OutItems[outOff+j]`. **Perfect load balance** — a 300×300 hot MD is 9e4 consecutive edge IDs scheduled like any other. Writes `(inner, outer, type)` at its own index. **No atomicAdd, no compaction:** self-edges (`inner==outer`) and the E2-dup-of-E1 case (`t3_md2[inner]==t3_md0[outer]`) write `type = 0` sentinel; both are structurally near-zero (plan §10.4c M1: E2-dup "proven structurally impossible" for well-formed T3s). This makes K2 fully deterministic and index-decoded — the plan §5b "no capped-reservation-vs-ungated-writer hazard class" property, for free. |
| **K3** | `NodeFeatures` | `Features.cc` / `Features.h:19-32` | `Acc1D` flat | `nT3` | 13 origin-free floats/node. `sanitizeOne` (NaN→0, ±Inf→±1e12) at the end, identical to prototype. ~60 FLOP/node. |
| **K5** | `EdgeInference` | `Features.cc` (edge half) + `EdgeInference.cc` | `Acc1D` flat, block 256 | `nEdges` | **FUSE the edge-feature build into the inference kernel.** Per edge: skip `type==0`; gather the two 13-float node rows; build the 14 edge features in registers; `edgemlp` 40→32→32→1 (~2.4k MAC). Store only the float logit. Saves 6–35 MB and a whole pass over `nEdges`. Cost at mean E: 2.6e8 FLOP ≈ 6.6 ms CPU @40 GFLOP/s, ≈0.02 ms GPU. |
| **K6a** | `WeldArgmax` (×3 sweeps) | `K6Weld.cc:49-68` | `Acc1D` flat, block 256 | `nEdges` | Per sweep: skip `logOdds < thetaEdge`, skip if `outWeld[n]!=-1 \|\| inWeld[m]!=-1`, then `atomicMax` a **packed uint64** into `bestOut[n]` and `bestIn[m]`. **Key packing reproduces `K6Weld.cc:26-32 beats()` exactly:** `key = (u64(orderFloat(logOdds)) << 32) \| u32(~edgeIdx)` where `orderFloat` is the standard monotone float→uint32 map (`b ^ ((b>>31) ? 0xFFFFFFFF : 0x80000000)`). Higher logit wins; equal logit → lower index wins (because `~e` is larger for smaller `e`). No float atomics, bit-exact tie-break, deterministic. |
| **K6b** | `WeldMutual` (×3 sweeps) | `K6Weld.cc:69-83` | `Acc1D` flat | `nT3` | `e = bestOut[n]; if (e>=0 && bestIn[edges[e].outer]==e) { outWeld[n]=e; inWeld[m]=e; }`. `bestOut` unique per tail and `bestIn` unique per head ⇒ conflict-free, applied wholesale, no atomics. **Run a fixed `kWeldSweeps = 3` with no host sync** — the prototype's `if (welded==0) break` early-exit is a CPU nicety; on device it would cost 3 stream drains for nothing. Zero-weld sweeps are idempotent. Reset `bestOut/bestIn` to −1 with `alpaka::memset` between sweeps. |
| **K6c** | `CountChains` | `K6Weld.cc:96-121` (walk, count half) | `Acc1D` flat | `nT3` | Head test: `inWeld[n]==-1 && outWeld[n]!=-1`. Walk `outWeld` counting nodes (path length ≤ ~10 by construction). Write `nodeCount[headSlot]`, `mdCountUpperBound = 3*nNodes`. |
| **K6d** | `PrefixChains` | — | `Acc1D`, `make_workdiv<Acc1D>(1,1024)` | `nHeads+1` | Exclusive prefixes for `offsets`, `mdOffsets`, `edgeOffsets`. |
| **K6e** | `EmitChains` | `K6Weld.cc:122-152` | `Acc1D` flat | `nHeads` | Second walk writes `items`, `edgeItems`, the first-appearance-ordered deduped `mdItems` (linear dedup over ≤21 entries, in registers), `layerMask` popcount → `nLayers`, `score = Σ logOdds + λ_len·nLayers`. Keep the `visited` cycle guard as a `#ifdef` assertion only (edges point strictly inward→outward; the prototype has never tripped it). |
| **K6f** | `TrimTerminals` (`-TR`) | `Trim.cc` / `Trim.h:62-70` | `Acc1D` flat | `nChains` | Per chain with ≥3 nodes: full-chain combined chi2/hit (Kasa xy + rz line, double accumulation, `ChainFeatures` features 5+6 arithmetic) plus the same over each terminal-dropped MD union; drop the end with the larger improvement factor iff `> ttFactor` **and** `chi2Full > absChi2Min` **and** the remainder keeps ≥ `minLayersAfter` layers. **Port as an endpoint move, not a rebuild:** store per-chain `[firstNode,lastNode)` and re-run K6c/K6d/K6e restricted to the surviving sub-range. `-TP > 1` = repeat. With `-TR 0` the whole block is skipped and `Chains` is K6e's object untouched (bit-exact). |
| **K7a** | `ChainFeatures` | `ChainFeatures.cc` / `.h:14-101` | `Acc1D` flat | `nChains` | 25 floats/chain (contract frozen; slots 0–15 are the M6 contract, 16–24 the a2 additions). Kasa circle fit + rz line fit + per-bridge circle fits, **all accumulated in double**, stored float. Degenerate-fit guards exactly as documented in `ChainFeatures.h:102-114` — no NaN/Inf may ever reach the head. **Emit `dcaXY` as a 26th output column here** (`k8ChainDcaXY`, `PixelAttach.h:120-128`, is literally the same Kasa fit) so the gate, the `-X` DCA split, the exempt masks and the attach eligibility never re-fit. The prototype memoizes it lazily (`main.cc:2205-2211`); the port should just emit it. |
| **K7b** | `ChainGate` | `ChainInference.cc` | `Acc1D` flat | `nChains` | Two heads per chain: `chain3mlp` 25→32→32→**3** (with the `kSrcCol` gather; col `-1` = `dcaXY`) → margins `mP,mD,mX`; **and** `chainmlp` (a2, 2-class) 25→32→32→1 → `gateLogit`. **The a2 head must ship even though `-BK 1` removes it from the order key** — it is attach feature f11 (`PixelAttach.cc:420`). See §4 and the M18b code-path finding in plan §11. |
| **K7c** | `ChainGateKill` | `main.cc:2262-2400` (the `-G 6` branch block) | `Acc1D` flat | `nChains` | Pure arithmetic, fuse with K7b. Branch code from `(nLayers, dcaXY vs -X, -Z)`; kill rules `mX < -M4` (T4 IP), `mD < -M4D` (T4 exempt), `mP < -M5/-M6` (5+ IP, vetoed by `mX >= -MRI`), `mD < -MD` (5+ exempt, vetoed by `mX >= -MR`); the C1 `(nNodes=2, nLayers=5)` cell rule (`-C25`/`-C25D`); the `-Z*` transition-band deltas applied when `\|eta_innermost\| ∈ [-ZE1, -ZE2)`. Kill = `score -= kGateKill`. Also builds the K9 **order key** (`-B`/`-BK`/`-BT`): `orderKey[c] = score − α·max(0, hinge − mX)` for `-BK 1`. |
| **K8a** | `BuildAttachGrid` | *(NEW — replaces the `PixelAttach.cc:462/482` inner `for p` scans)* | `Acc1D` flat + `make_workdiv<Acc1D>(1,1024)` prefix | `nPLS × nLayerRefs` | **THE 201 ms FIX.** See §1.1 below. |
| **K8b** | `AttachScore` | `PixelAttach.cc:379-431` (`evalPair`) + `AttachInference.cc` | `Acc1D` flat, block 128 | grid-candidate pairs | Per candidate: build the 19 features in registers, `attachmlp` 19→24→24→1 (~1.1k MAC), `atomicMax` a packed `(logit \| ~plsRow)` into `bestPerTarget[]`. Never materialize a pair buffer. |
| **K8c** | `AttachContend` | `AttachDelivery.cc` (`gaStageChains`) | `Acc1D` flat ×2 | `nTargets` then `nPLS` | One pLS, one owner: (1) each target proposes its best pLS (already in `bestPerTarget`); (2) `atomicMax((logit \| ~targetOrd))` into `bestPerPls[]`; (3) mutual check. Exactly the K6 mutual-best shape and the same packed-key determinism. Ties: lower pLS row for a target's best; earlier target position for a pLS — encode both into the low word. |
| **K8d** | `AttachSeedDedup` (`-RD`) | `main.cc` (`seed-family dedup revoked`) | `Acc2D` over attached pLS | ~482 attached/evt ⇒ ~2.3e5 pair tests | Among *attached* pLS only, one sharing ≥2 pixel hit rows with a higher-logit attached pLS loses its attachment. Reuse the production `pixelHitsOverlapAny` criterion from `CheckHitspLS` (`Kernels.h`), restricted to the attached set. Sim-blind by construction. Cheap. |
| **K9a** | `PreClaimPixelOwners` (`-PU 1`) | `K9K10.cc` (preClaim block) | `Acc1D` flat | Σ hit rows of surviving carried pixel TCs | Carried type-7 → `t5_hitIndices` (incl. the ExtendT5FromDupT5 layer-6/7 hits), type-5 → `pT3_otHitIndices`, type-8 owns nothing. Write a **max sentinel key** into `hitOwner[]` so no chain can ever outbid a carried row. Rows that attach REPLACED must be excluded — hence the `-A 4` order mandate (attach → suppression set → pre-claim → K9). |
| **K9b** | `ClaimPropose` / `ClaimVerify` (2–3 rounds) | `K9K10.cc:40-…` (serial greedy stand-in) | `Acc1D` flat | `nCandidates` | Plan §3 G3a propose-verify: each undecided candidate `atomicMax(hitOwner[h], packed(orderKey, ~chainIdx))` on all its hits; verify pass recounts foreign-owned hits and accepts iff `claimOk()` (`K9K10.cc:29-36`: `maxClaimedItems<0 ? !(frac>F) : (excl ? n<=items : (n<=items \|\| !(frac>F)))`); accepted chains lock their hits, losers back off one round. **This is the one stage whose semantics the prototype could NOT validate (plan §10.3). See §6 risk R3 and the pre-port measurement item.** |
| **K9c** | `BraidKill` (`-W`) | `K9K10.cc` (braid test) | `Acc1D` flat | `nAccepted` | Owner-relative: kill a candidate overlapping an already-accepted chain by ≥ `braidFrac` of *that owner's* hits. Needs per-owner claimed counts ⇒ a second pass after K9b. |
| **K10** | `AssembleChainTCs` | `K9K10.cc:…` (`k10AssembleChainTCs`) + `OutputWriter.cc` | `Acc1D` flat | `nAccepted` | `nLayers>=5 → LSTObjType 4 (T5)`, `==4 → 9 (T4)`, `<4` dropped; attached → `7 (pT5)` with the pLS's pixel hits prepended and `pt = pLS ptIn`; bare chains `pt =` lower median of member `t3_pt`, `eta/phi` from the innermost member. `nhitOT = 2·nMDs`. Writes into `TrackCandidatesBaseSoA` — **`Params_TC::kLayers = 13`, `kHitsPerLayer = 2` (`interface/Common.h:113-122`), so a 9-layer chain (+2 pixel layers = 11) fits natively with no truncation.** One `atomicAdd` on `nTrackCandidates` per row, plus the per-type counters in `TrackCandidatesExtendedSoA`. |

Host syncs across the whole chain pipeline: **2** (one after K1b to read `nEdges` for allocation,
one after K6d to read `nChains`) — or **0** if both are handled by a ceiling-sized allocation
(§2.7). Plan §3 target was ~4. No superbin host loops, no `nonZeroModules` host round-trip
(contrast `LSTEvent.dev.cc:461-497`).

### 1.1 K8a — the grid prefilter (the designed replacement for the ~201 ms attach scan)

**What is being replaced.** `PixelAttach.cc:456-495` is literally
`for each target { for (int p = 0; p < nPls; ++p) evalPair(...) }`. At the flagship that is
`2.5k × 18.4k ≈ 4.6e7` probes/evt (each doing a circle–circle propagation with `sqrt`/`atan2`),
of which **130,998 survive = 0.28%**. Measured 201.8 ms/evt = 76% of the prototype's 265 ms.
With `-RT3 1` (the pT3-class delivery that production needs) the bare-T3 universe adds
42.8k targets ⇒ 7.9e8 probes/evt ⇒ **8.1 s/evt** (measured, plan §11 M16 finding (b)).
**This is hard-blocking, not an optimization.**

**The exact predicate to preserve.** `evalPair` (`PixelAttach.cc:379-396`) accepts iff
`|dTanLambda| < prefDTanL (0.6)` **AND** `|dPhiAtInnermost| < prefDPhi (0.4)`, where
`dTanLambda = pLS.pz/pt − target.tanLambda` and `dPhiAtInnermost` compares the pLS circle
propagated to the **target's innermost anchor radius** against the target's innermost chord phi.

**Proposed grid (option A — window-faithful, recommended).** Key on
`(innermostLayer, tanLambdaBin, propagatedPhiBin)`:

- The target's innermost anchor is always on a detector layer, so `innermostLayer ∈ [1,11]` is an
  exact, free categorical (already `ChainFeatures` col 10 / `TargetPre::innermostLayer`).
- For each pLS, propagate its own stored helix once per layer reference radius `r_k`
  (11 propagations/pLS = 2.0e5/evt ≈ 1 ms CPU, ~µs GPU) → `phi_k`. This is exactly the
  "analytic helix propagation of the pLS's own stored helix" of plan §3 K8 — it *computes* what
  45,000 superbins tabulate.
- Bin `tanLambda` with width `prefDTanL` and `phi_k` with width `prefDPhi`; scan the 3×3
  neighbourhood at the target's exact layer. Cell widths ≥ the window widths + a ±1 cell scan ⇒
  the candidate set is a **strict superset** of the exact-window set. Within-layer radial spread
  is absorbed by widening the phi cell by `max|dphi/dr| · layerThickness` (or by binning two
  sub-radii per layer).
- CSR build = count/prefix/scatter over `nPLS × 11`, identical idiom to K1.

**Predicted cost.** `nTanLBins ≈ 20` (|tanλ| ≤ 6 at 0.6 width), `nPhiBins ≈ 16` (2π/0.4).
Expected occupancy per `(layer,tanL,phi)` cell = 18.4k / (11·20·16) ≈ 5.2 pLS; a 3×3 neighbourhood
≈ **47 candidates/target**. Sanity check against measurement: the flagship's survivor rate is
130,998 / 2,500 targets = **52 survivors/target** — i.e. essentially the whole neighbourhood
already survives the windows, so the grid neighbourhood is exactly right-sized and loses
nothing. Probes drop 4.6e7 → ~1.2e5 (**~390×**); remaining cost is the 131k head evaluations
(1.4e8 FLOP ≈ 3.5 ms CPU, ~0.01 ms GPU) plus the grid build.

**Predicted attach stage: 201.8 ms → ~5 ms CPU.** Total prototype-equivalent CPU
~265 ms → ~68 ms, which lands inside plan §4's "~60–80 ms" budget for the touched scope.

**With `-RT3 1`:** 45.3k targets × 47 = 2.1e6 candidates, ~2.4M head evals ≈ 65 ms CPU. Still the
dominant term. Mitigation to evaluate at M19/P2: a cheap first-tier scorer (distilled 19→8→1, or
an analytic `|dKappa|` pre-gate derived from the trained head's measured feature importance —
**never guessed**, per the plan §10.4c M2 maintainer addendum) ahead of the full head. Flagged
as an open item, not a solved one.

**Option B** (plan §3's pure-invariant grid on `(phi_c, tanλ, signed log|κ|)`) is target-agnostic
and cheaper still, but the mapping from those invariants to the `dPhiAtInnermost` window needs a
derived compatibility bound and is therefore harder to prove superset-safe. Recommendation:
land option A in Stage 1 (bit-parity provable against the prototype), consider option B as a
Stage-3 timing optimization.

**Parity protocol (do this BEFORE writing the kernel):** add an offline mode to the golden
prototype that builds the grid and diffs its candidate set against the exhaustive scan,
per event, and asserts `gridSet ⊇ exactSet` and reports `|gridSet| / |exactSet|`. If the superset
property holds on 300 events, the kernel port is a pure performance change with a bit-exact
decision set.

---

## 2. SoA layout proposals per intermediate

All layouts use `GENERATE_SOA_LAYOUT` / `GENERATE_SOA_BLOCKS` exactly as
`interface/TripletsSoA.h` does, with `*HostCollection.h` / `interface/alpaka/*DeviceCollection.h`
siblings. Byte estimates are **mean PU200**, with the p99 and jet-max columns from §0.

### 2.1 `ChainIncidenceSoA` (K1)

```
GENERATE_SOA_LAYOUT(ChainIncidenceSoALayout,
  SOA_COLUMN(uint32_t, mdT3OutOffsets),   // nMD+1
  SOA_COLUMN(uint32_t, mdT3InOffsets),    // nMD+1
  SOA_COLUMN(uint32_t, mdEdgeProdPrefix), // nMD+1, exclusive prefix of degIn*degOut
  SOA_COLUMN(uint32_t, lsT3OutOffsets),   // nLS+1
  SOA_COLUMN(uint32_t, lsT3InOffsets),    // nLS+1
  SOA_COLUMN(uint32_t, lsEdgeProdPrefix), // nLS+1
  SOA_SCALAR(uint32_t, nE1Exact),
  SOA_SCALAR(uint32_t, nE2Exact))
// item arrays (length nT3 each) in a second layout so they can be sized independently:
GENERATE_SOA_LAYOUT(ChainIncidenceItemsSoALayout,
  SOA_COLUMN(uint32_t, mdT3OutItems), SOA_COLUMN(uint32_t, mdT3InItems),
  SOA_COLUMN(uint32_t, lsT3OutItems), SOA_COLUMN(uint32_t, lsT3InItems))
```
`uint32_t` is safe for the prefixes: worst measured E = 623k ≪ 2^32.
**Bytes:** `6·(90k)·4 + 4·43.3k·4 ≈ 2.2 + 0.7 = **2.9 MB**` (p99 ~3.2 MB).

### 2.2 `ChainEdgesSoA` (K2 + K5)

```
GENERATE_SOA_LAYOUT(ChainEdgesSoALayout,
  SOA_COLUMN(uint32_t, inner),    // global T3 node index
  SOA_COLUMN(uint32_t, outer),
  SOA_COLUMN(uint8_t,  type),     // 0 = skipped slot, 1 = E1, 2 = E2
  SOA_COLUMN(float,    logOdds),  // K5 output; the quantity K6 sums
  SOA_SCALAR(uint32_t, nEdges))
```
**13 B/edge.** Mean 110k → **1.43 MB**; p99 332k → 4.3 MB; **jet max 623k → 8.1 MB**.
Compare plan §5b's "edge record ≈ 24–32 B" and "few×1e6 ⇒ 50–100 MB" honest worst case — the
measured distribution is ~5× kinder than the plan assumed (plan §10.4c 2026-08-01 already
recorded this). **Do not materialize `EdgeFeatures`** (14 floats × E would be 6.2 MB mean /
35 MB at jet max) — fuse into K5 (§1, K5).

### 2.3 `ChainNodesSoA` (K3)

```
GENERATE_SOA_LAYOUT(ChainNodesSoALayout,
  SOA_COLUMN(Params_Node::ArrayFxFeat, features))  // edm::StdArray<float,13>
```
**52 B/node × 43.3k = 2.25 MB.** Halves to 1.13 MB if stored as `__half`/`FPX`; the edge head
standardizes on load anyway so fp16 storage is defensible — **but only after a parity A/B**,
since `edgemlp` logits feed a summed chain score and a threshold.

### 2.4 `ChainsSoA` (K6)

```
GENERATE_SOA_LAYOUT(ChainsSoALayout,
  SOA_COLUMN(uint32_t, nodeOffsets),  SOA_COLUMN(uint32_t, mdOffsets),
  SOA_COLUMN(uint32_t, edgeOffsets),
  SOA_COLUMN(float,    score),        SOA_COLUMN(uint8_t,  nLayers),
  SOA_COLUMN(uint8_t,  nNodes),
  SOA_COLUMN(float,    dcaXY),        // K7a; also the -X split axis
  SOA_COLUMN(float,    gateLogit),    // K7b a2 2-class -> attach f11
  SOA_COLUMN(float,    m3fake), SOA_COLUMN(float, m3prompt), SOA_COLUMN(float, m3disp),
  SOA_COLUMN(float,    orderKey),     // K7c
  SOA_COLUMN(int32_t,  attachedPls),  // K8c, -1 = none
  SOA_COLUMN(uint8_t,  flags),        // bit0 killed, bit1 exempt, bit2 accepted, bit3 zBand
  SOA_SCALAR(uint32_t, nChains))
GENERATE_SOA_LAYOUT(ChainItemsSoALayout,
  SOA_COLUMN(uint32_t, nodeItems), SOA_COLUMN(uint32_t, mdItems), SOA_COLUMN(uint32_t, edgeItems))
GENERATE_SOA_LAYOUT(ChainFeaturesSoALayout,
  SOA_COLUMN(Params_ChainFeat::ArrayFxFeat, f))  // edm::StdArray<float,25>
```
At 4,757 chains/evt with mean ~2.5 nodes / ~6 MDs / ~1.5 edges:
per-chain block ~52 B → **0.25 MB**; item arrays (12k + 28k + 7k) × 4 B → **0.19 MB**;
features 25 × 4 B → **0.48 MB**. **Total ≈ 0.9 MB.** Negligible.

### 2.5 `AttachGridSoA` + attach decision state (K8)

```
GENERATE_SOA_LAYOUT(AttachGridSoALayout,
  SOA_COLUMN(uint32_t, cellOffsets))   // nCells+1, nCells ~ 11*20*16 = 3520
GENERATE_SOA_LAYOUT(AttachGridItemsSoALayout,
  SOA_COLUMN(uint32_t, plsRow))        // nPLS * nLayerRefs = 18.4k * 11
GENERATE_SOA_LAYOUT(AttachPlsPreSoALayout,   // hoisted per-pLS quantities, PixelAttach.cc:157-168
  SOA_COLUMN(Params_PlsPre::ArrayFx15, q))
GENERATE_SOA_LAYOUT(AttachStateSoALayout,
  SOA_COLUMN(uint64_t, bestPerTarget),  // packed (logit | ~plsRow)
  SOA_COLUMN(uint64_t, bestPerPls),     // packed (logit | ~targetOrd)
  SOA_COLUMN(uint8_t,  plsOwned))
```
Grid: `3.5k·4 + 202k·4 = 0.82 MB`. pLS pre-records: `18.4k · 60 B = 1.1 MB`.
Target pre-records (chains only): `2.5k · 60 B = 0.15 MB`; **with `-RT3 1` bare T3s push this to
45.3k · 60 B = 2.7 MB**. Decision state: `< 0.5 MB`. **Attach total ≈ 2.6 MB (5.2 MB with `-RT3 1`).**
**Never allocate a pair buffer** — 131k pairs × (19 floats + ids) would be 11.5 MB for nothing;
score and reduce in registers.

### 2.6 K9 claim map

Hit-level claim (`-H 1`, the flagship). Max ph2 hit row observed in MDs on 10 events =
**221,639**; size the array by the LST hit collection extent. Packed `uint64` owner
(`orderKey | ~chainIdx`) → **~250k · 8 B = 2.0 MB**. MD-level (`-H 0`) would be 0.7 MB, but the
flagship is hit-level and hit-level is what removes the chain-vs-chain duplicates an MD map
structurally cannot see (`Stages.h:177-180`).

### 2.7 Totals and the jet-safety sizing note (plan §5b)

| | mean | p99 | jet max (E=623k) |
|---|---|---|---|
| incidence | 2.9 MB | 3.2 MB | 3.5 MB |
| edges (+logit) | 1.4 MB | 4.3 MB | 8.1 MB |
| node features | 2.3 MB | 2.6 MB | 3.2 MB |
| chains | 0.9 MB | ~2 MB | ~4 MB |
| attach | 2.6 MB | 3 MB | 3.5 MB |
| claim map | 2.0 MB | 2.0 MB | 2.0 MB |
| **new total** | **~12 MB** | **~17 MB** | **~24 MB** |

Deleted in exchange (plan §3 "What gets DELETED", §5b item 1): `QuintupletsSoA`,
`QuadrupletsSoA`, `PixelTripletsSoA`, `PixelQuintupletsSoA`, `preAllocated*` scratch columns
(~20–40 MB), the `connectedPixels` map array (~4–40 MB), 12 pixelmap `.bin` files and the
`PixelMap` SoA (45,000 superbins). **Net memory strongly negative** against a ~250 MB/evt
baseline. Note: at Stage 1 the old SoAs still exist (hybrid mode carries baseline pixel rows),
so Stage 1 is a **+12 MB** change; the reduction only lands at Stage 4.

**Jet-safety sizing (plan §5b items 2–4):**
- Allocation is **exact by pure arithmetic** (`E = Σ_key degIn·degOut`, computed in K1b before any
  edge allocation). No count-kernel re-execution, no capped-reservation-vs-ungated-writer hazard
  — the bug family the 100k-T5 cap's Stage-1 guard had to fix **cannot exist** here.
- Keep per-collection **99.99th-percentile safety ceilings** for the HLT bounded-memory
  requirement with 8 concurrent streams. From the measured distribution set the edge ceiling at
  **1.0e6 edges (13 MB)** — 1.6× the observed max and ~3× p99, at trivial cost.
- If `E_raw` exceeds the ceiling: **deterministic physics-ranked trim**, not arrival-order
  truncation — drop the lowest-gate-margin edges (uniformly sheds the least track-like
  candidates) and bump a DQM overflow counter. Never silent.
- Chain / accepted-chain / TC ceilings likewise. `pLS` input cap and TC output ceilings unchanged.
- The pT3/pT5 caps (5000/15000) **die** at Stage 4 — attach is exactly counted.

---

## 3. Config surface

### 3.1 The FLAGSHIP line, resolved

`at_run.sh` composes `ANCHOR + CTL + STACK + per-run overrides` (later wins). For
`fl_balanced` (`m18b_taskA.sh`, the maintainer's preferred config) the **effective** settings are:

| flag | value | meaning | port target | freeze status |
|---|---|---|---|---|
| `-e` | 0 | `thetaEdge`, K6 weld eligibility | `double chainThetaEdge` | **tunable** |
| `-L` | 0.5 | `lambdaLen`, chain length prior | `double chainLambdaLen` | **tunable** |
| `-G` | 6 | 3-class gate mode | **compile-time** — mode 6 only, others are dead experiments | **FROZEN** |
| `-X` | 0.5 | `dcaSplit` (cm), IP-compatibility boundary | `double chainDcaSplit` | **tunable** |
| `-M4` | 4.0 | T4-class IP kill on `mX` | `double chainMarginT4` | **tunable** |
| `-M4D` | −1.2 | T4-class exempt kill on `mD` | `double chainMarginT4Disp` | **tunable** |
| `-M5` / `-M6` | 1e9 | 5+/6+ IP kill on `mP` | *inert at 1e9* | **FROZEN OFF** |
| `-MD` | 1e9 | 5+ exempt kill on `mD` | *inert at 1e9* | **FROZEN OFF** (revived by the B3 gate co-retrain — recheck at M19) |
| `-MR` | −1.800 | exempt `mX` OR-rescue | `double chainRescueDisp` | **tunable** |
| `-MRI` | −0.5 | IP-5+ `mX` OR-rescue | `double chainRescueIp` | **tunable** |
| `-U4/-U5/-U6` | 0 | exempt-branch legacy-scale thresholds | `double[3] chainThetaExempt` | **tunable** |
| `-T4/-T5/-T6` | 0 (default) | gate-scale per-length thresholds | `double[3] chainTheta` | **tunable** |
| `-C25` / `-C25D` | 0.0 / −2.0 | (nNodes=2, nLayers=5) cell margins | `double chainCell25`, `chainCell25Disp` | **tunable** |
| `-ZE1` / `-ZE2` | 1.1 / 1.7 | transition band \|eta\| window | `double[2] chainEtaBand` | **tunable** |
| `-ZM4` / `-ZM4D` | −0.5 / 1.2 | in-band deltas to `-M4` / `-M4D` | `double[2] chainEtaBandDelta` | **tunable** — the M19 primary target (dup at \|eta\| 1.5–3) |
| other `-Z*` | 0 | inert band levers | **drop from the port** | **FROZEN OFF** |
| `-B` | 10 | `fakeOrderAlpha` (K9 order key) | `double chainOrderAlpha` | **tunable** |
| `-BK` | 1 | order-key source = 3-class `mX` | **compile-time** — modes 0/2..11 are dead experiments | **FROZEN** (BK-hinge SWEEPS ve; ve retired) |
| `-BT` | 5 | order-key hinge | `double chainOrderHinge` | **tunable** |
| `-F` | 0.20 | `maxClaimedFrac` | `double chainMaxClaimedFrac` | **tunable** |
| `-FC` | 1 | absolute claim tolerance, MD units (⇒ **2 hits** at `-H 1`) | `int32 chainMaxClaimedItems` | **tunable** |
| `-FCX` / `-FCE` | 0 / 0 | strict-count mode | **drop** — measured **GENUINE NO-OP** on this stack (claimCount=2 subsumes the `-F 0.20` fraction) | **RETIRED** |
| `-H` | 1 | hit-level claim | **compile-time true** | **FROZEN** |
| `-W` | 0.50 | `braidFrac`, owner-relative kill | `double chainBraidFrac` | **tunable** |
| `-PU` | 1 | claim-universe unification mode | **compile-time 1** (mode 2 not in the flagship) | **FROZEN** |
| `-TR` | 1 | terminal trim on | `bool chainTerminalTrim` | **FROZEN ON** |
| `-TT` | 1.2 | trim chi2 improvement factor | `double chainTrimFactor` | **tunable** — note the panel finding: `-TT < 1` is physics-suspect (trims that *worsen* the fit); the valid regime is `≥ 1` |
| `-TA` | 1.0 | absolute chi2/hit floor for trim eligibility | `double chainTrimAbsChi2` | **tunable** |
| `-TL` | 5 (default) | min layers the remainder keeps | **compile-time 5** (at 4 the trim is net negative — measured) | **FROZEN** |
| `-TP` | 1 (default) | trim passes | **compile-time 1** | **FROZEN** |
| `-A` | 4 | general attach as the delivery path | **compile-time** — modes 0..3 are dead experiments | **FROZEN** |
| `-a` | 8 | chain-target attach margin | `double attachThetaChain` | **tunable** |
| `-AT3` | 6.0 | bare-T3-target attach margin | `double attachThetaT3` | **tunable** — *currently inert* (`-RT3 0`) |
| `-RT5` | 1 | full pT5-class replacement | `bool attachReplacePT5` | **FROZEN ON** |
| `-RT3` | 0 | pT3-class replacement | `bool attachReplacePT3` | **OPEN** — see §5 caveat |
| `-RPS` | 1 | bare-pLS contention suppression | `bool attachSuppressBarePLS` | **FROZEN ON** |
| `-RD` | 1 | seed-family dedup of attach owners | `bool attachSeedDedup` | **FROZEN ON** |
| `-D4` | 1e9 (default) | attach-eligibility dcaXY gate | *inert* — deliberately OFF (blocking displaced chains from attaching is exactly the upside plan §11 says to CLAIM) | **FROZEN OFF** |
| `-D`, `-K`, `-R`, `-S`, `-Y` | 5, —, —, —, — | `-A 2`-only levers | **drop from the port** | **RETIRED** |
| `-FS` / `-DD*` | 0 | share pass / post-arb dedup | **drop** — `-FS 0.5 -DD 4` measured a BAD TRADE (+.0010 eff for +.0064 fake +.0066 dup) and was DROPPED | **RETIRED** |
| `-OK`, `-V4/5/6`, `-Q4/-Q5`, `-Z`, `-MP` | defaults | unused levers | **drop** | **RETIRED** |
| `-P` | — | keep pixel-consumed chains | *inert* | **RETIRED** |

### 3.2 Constants with **no flag** that must become explicit config

These are `AttachParams` / `Stages.h` defaults with no command-line switch. They are part of the
frozen config and **must be named in the producer or as documented `constexpr`**, otherwise the
port silently changes physics:

| constant | value | site | port target |
|---|---|---|---|
| `prefDPhi` | 0.4 | `PixelAttach.h:77` | **grid phi cell width** — `constexpr` + documented |
| `prefDTanL` | 0.6 | `PixelAttach.h:78` | **grid tanλ cell width** — `constexpr` + documented |
| `kWeldSweeps` | 3 | `Stages.h:77` | `constexpr int kChainWeldSweeps = 3` |
| `kChainFeat` | 25 | `ChainFeatures.h:121` | frozen contract; must match every chain-head `kInput` |
| `kNodeFeat` / `kEdgeFeat` | 13 / 14 | `Features.h:33,50` | frozen; 13+13+14 = `edgemlp::kInput` 40 |
| `kAttachFeat` | 19 | `PixelAttach.h:63` | frozen; == `attachmlp::kInput` |
| chain `nLayers < 4` drop | — | `k10AssembleChainTCs` | `constexpr` |
| attach chain-target `nLayers >= 5` | — | `PixelAttach.cc:458` | `constexpr` — v1 scope decision |

### 3.3 Recommended producer surface

Add to `RecoTracker/LST/plugins/alpaka/LSTProducer.cc::fillDescriptions` (which today has only
`ptCut`, `clustSizeCut`, `nopLSDupClean`, `tcpLSTriplets`, `reduceMemByFullPrecompute`,
`verbose`):

```cpp
desc.add<bool>("useChainTracking", false)   // THE MASTER FLAG - Stage 1 lands with default false
    ->setComment("Run the chain-tracking pipeline (K1-K10) in place of the T5/T4/pT5 builders.");
edm::ParameterSetDescription chainDesc;
chainDesc.add<double>("thetaEdge", 0.0);
chainDesc.add<double>("lambdaLen", 0.5);
chainDesc.add<double>("dcaSplit", 0.5);
chainDesc.add<std::vector<double>>("marginT4", {4.0, -1.2});      // -M4, -M4D
chainDesc.add<std::vector<double>>("rescue",   {-0.5, -1.800});   // -MRI, -MR
chainDesc.add<std::vector<double>>("theta",       {0.0, 0.0, 0.0});
chainDesc.add<std::vector<double>>("thetaExempt", {0.0, 0.0, 0.0});
chainDesc.add<std::vector<double>>("cell25",   {0.0, -2.0});
chainDesc.add<std::vector<double>>("etaBand",  {1.1, 1.7});
chainDesc.add<std::vector<double>>("etaBandDelta", {-0.5, 1.2});  // -ZM4, -ZM4D
chainDesc.add<double>("orderAlpha", 10.0);
chainDesc.add<double>("orderHinge", 5.0);
chainDesc.add<double>("maxClaimedFrac", 0.20);
chainDesc.add<int32_t>("maxClaimedItems", 1);
chainDesc.add<double>("braidFrac", 0.50);
chainDesc.add<double>("trimFactor", 1.2);
chainDesc.add<double>("trimAbsChi2", 1.0);
chainDesc.add<double>("attachThetaChain", 8.0);
chainDesc.add<double>("attachThetaT3", 6.0);
chainDesc.add<bool>("attachReplacePT5", true);
chainDesc.add<bool>("attachReplacePT3", false);
desc.add<edm::ParameterSetDescription>("chain", chainDesc);
```

Rule of thumb applied above: anything the M14–M18 scans actually moved is a **`double`
parameter**; anything whose non-default values were measured as no-ops, bad trades, or dead
experiments is **dropped or made `constexpr`**. A 40-flag prototype surface must not become a
40-parameter producer.

---

## 4. Weight-header inventory

All headers follow the `src/alpaka/NeuralNetwork.h` convention: `constexpr const float` arrays,
weights stored **TRANSPOSED** as `wgt[in][out]`, per-input preprocessing
(`log10(1+x)` → clip → standardize) baked into the header, generated by a committed
`export_*.py` script from a `.pt` + norm `.json` — **never hand-copied**.

| header | md5 | namespace | arch | trained from | consumed at | status |
|---|---|---|---|---|---|---|
| `edge_mlp_weights.h` | `087c1f61…` | `edgemlp` | 40→32→32→1 | `edge_mlp_v3.pt` + `edge_norm_v3.json`, val AUC 0.9610 | **K5 EdgeInference** — the logit K6 sums | **SHIP.** M19 may replace with an E2-aware displacement-reweighted retrain (the traced dxy[10,30) fix). Port must accept either; only `kInput = 40` is structural. |
| `chain3_mlp_weights.h` | **`d170f174…`** | `chain3mlp` | 25→32→32→**3** (+`kSrcCol` gather, col −1 = `dcaXY`; drops col 19 `maxBridgeChi2`) | `chain3_mlp_m12.pt` + `chain3_norm_m12.json`; val AUC prompt 0.966 / displaced 0.937 | **K7b/K7c** — the `-G 6` 3-class gate (fake/prompt/displaced) and, via `mX`, the `-BK 1` order key | **SHIP — this is the canonical head** (the golden tree's identity md5). |
| `chain_mlp_weights.h` | `53ffb3b4…` | `chainmlp` | 25→32→32→1 | `chain_mlp_a2.pt` + `chain_norm_a2.json`, val AUC 0.9153 | **K7b** — `gateLogit`. **NOT** in the `-BK 1` order key and **NOT** a kill; but it **IS attach feature f11** (`PixelAttach.cc:420`) | **SHIP — mandatory.** M18b code-path finding: `-BK` removes the a2 head from key+claim but **not** from the attach MLP inputs; swapping it changed attach deliveries by −12.7% via feature drift. **Either ship this header as the f11 provider, or retrain the attach head without f11 — those are the only two options.** |
| `attach_mlp_weights.h` | `3f072f89…` | `attachmlp` | 19→24→24→1 | `attach_mlp_g1.pt` + `attach_norm_g1.json`, val AUC 0.99879 | **K8b** — the pair head, both target kinds | **SHIP.** Known training gap: only 12 displaced true pairs with vxy ≥ 1 and **zero** ≥ 5 (M18 attach finding) — an M19 retrain item. Higher precision moves the measured eff-crossover at head precision ~0.96, which permits a tighter `-a` and buys endcap chain-vs-pLS dup removal. |
| `attach_norm_g2.json` / `attach_g2_testauc.json` | — | — | the per-type variant | — | **not compiled** — g1 (single general head) beat it on chain pairs | **DO NOT SHIP.** |
| `chain3b_mlp_weights.h` | `d564ac2b…` | `chain3bmlp` | 25→32→32→3 | `chain3_mlp_m17b.pt` | `-BK 10` order-key experiment only | **DO NOT SHIP** — present-but-unused; the OKR ranking-swap experiment. |
| `mf_mlp_weights.h` | `76f76c60…` | `mfmlp` | 26→32→32→1 | `mf_mlp_v1.pt` | `-BK 7/8/9` matchFrac-regressor order keys | **DO NOT SHIP** — present-but-unused. |

**Header count at ship: 3 mandatory (edge, chain3, attach) + 1 structural (chain a2, as the f11
provider) = 4**, replacing the current six (`t5dnn`, `t4dnn`, `pt3dnn`, `t3dnn`, `t5embdnn`,
`plsembdnn`) — plan §3 "NN inventory after" said 3 nets; the a2 head is the honest fourth and
must be stated as such in any offer.

**Plan §6 blind spot 9 (weights out of compiled headers → EventSetup/conditions):** deferred to
P4, as the plan schedules. Stage 1 ships generated headers, exactly as production does today.
Record the retrain-cadence cost in the P4 ticket.

---

## 5. Landing plan

Plan §7's phase sequence, adapted to what M18 actually built. **Each phase is independently
revertible** — the revert is always "flip one flag off", and every phase's off-state must be
byte-identical to the previous phase's on-state.

### Phase P2.0 — `CompactTriplets` + incidence, no physics (zero risk)
Land K0/K1a/K1b/K1c behind `useChainTracking = false`. K1a's four `atomicAdd`s in
`CreateTriplets`'s accept path are the only touch to existing code and are provably
output-neutral (they write new counters, read nothing). A/B: **bit-identical TC output**,
timing delta ≤ 1 ms.
*Revert: delete the counter writes.*

### Phase P2.1 — graph + edge head, output-neutral (measurement only)
K2 + K3 + K5 behind the flag, writing `ChainEdgesSoA` and nothing else. No consumer.
A/B gate: **`nEdges` and the logit histogram must match the prototype on the same events to
float precision.** This is the first bit-parity checkpoint and the reason the prototype exists.
*Revert: flag off.*

### Phase P2.2 — weld + trim + gate, still output-neutral
K6a–K6f + K7a–K7c. A/B gate: **chain count, `nLayers` distribution, chain score, gate margins,
`dcaXY`, and the kill decision must match the prototype per event.** The K6 packed-atomicMax key
must reproduce `beats()` exactly (§1, K6a) — verify by diffing the welded edge set, not just the
chain count.
*Revert: flag off.*

### Phase P2.3 — K9 claim + K10 assembly, **first physics change**
This is the phase that changes the TC collection. Ship it in **hybrid mode**, i.e. exactly what
every offline number was measured on: the LST T5/T4/pT5/pT3/pLS producers still run, their TC
rows are carried, chain TCs are added, `partOfPT5`/`partOfPT3` chains are dropped, `-PU 1`
pre-claims the carried pixel rows' OT hits. **Emit directly into `TrackCandidatesBaseSoA`** —
`Params_TC::kLayers = 13` (`interface/Common.h:113`) holds a 9-layer chain plus 2 pixel layers
with no truncation, so plan §7 P2's "emit into the EXISTING T5/T4 SoAs (7-slot truncation)" is
**unnecessary and should not be done**: `Params_T5::kLayers = 7` would truncate exactly the long
chains that are the design's selling point, and routing through the T5 SoA would make the
online↔offline comparison inexact.
A/B gates: MTV eff / fake / dup on both backends; TC-type mix; `nhitOT` distribution
(prototype flagship: 9.79 / 9.70 / 3.42 vs LST 10.15 / 10.01 / 3.56); eff-vs-vxy/dxy in every
displaced band; **the 99th-percentile event time on a jet-enriched skim** (plan §6.12).
Kill: below the M19 frozen-config numbers on any displaced band, or dup above the frozen number.
*Revert: `useChainTracking = false` — the carried rows are untouched, so the revert is exact.*

### Phase P2.4 — attach with the grid prefilter
K8a–K8d + the type-7 in-place upgrade + `-RT5 1` suppression + `-RPS 1` + `-RD 1`.
**The grid must be superset-verified offline first (§1.1 parity protocol) before the kernel is
written.** A/B gates: pT5-class per-band efficiency contribution ≥ baseline's (the M16 (a)
replacement A/B, which PASSED offline: prompt +.010, vxy[1,5) +18% rel, vxy[5,10) 3.85×,
vxy[10,30) 98×); attach stage wall time (target ≤ 10 ms CPU, ≤ 0.1 ms GPU); dup delta
(−.0060 measured offline, identical on all three trims).
*Revert: `attachReplacePT5 = false` → the carried pixel rows come back and no chain upgrades.*

### Phase P2.4b — pT3-class replacement via the general attach (RESEQUENCED 2026-08-02: runs AFTER integration is complete)
**MAINTAINER DECISION (2026-08-02): integrate as-is with the pT3 code KEPT; do this phase only
after the integration (P2.5-P2.7 pT5-side) is complete, so there is no switching back and forth
between integration work and offline replacement work.** Consequence for P2.7: the deletion
tranche splits — the pT5-side machinery (T5/T4 builders, ExtendT5FromDupT5, their dup kernels,
the pT5 pixel map, crossCleanT5/pLS-vs-pT5) deletes at P2.7 as planned; the pT3-side machinery
(pT3 builder, its pixel map, pt3dnn, its crossclean) survives P2.7 and deletes only after this
phase passes. ALSO: every validation plot produced before this phase carries LST's own pT3 and
bare-pLS rows verbatim — STATE THIS ON EVERY PLOT/OFFER (M18 panel item 10; the maintainer was
not clearly told at the M19 presentation and should never have to discover it by asking).
The general attach head is ALREADY trained on both target types (chains AND bare T3s;
targetType is an input). The bare-T3 side — which replaces the pT3 class — was blocked at M16
purely by candidate-finding volume (~43k bare-T3 targets/evt -> ~6.5M analytic-prefilter
pairs/evt, ~200x the chain-target load), not by physics. P2.4's grid prefilter removes that
blocker. Sequence:
1. After the grid is superset-verified for chain targets (P2.4), extend the SAME offline
   verification to bare-T3 targets and measure the real per-event candidate volume.
2. Re-run the M16 bare-T3 replacement A/B offline at the now-feasible cost (the -AT3/-RT3
   machinery exists in the M16 tree; retrain/refresh the head on grid-selected pairs if the
   candidate distribution shifted), judged per the maintainer protocol: full triple + attach
   confusion matrix + displaced strata within the pT3-class slice.
3. Only on an offline PASS: flip the class in production (bare-T3 attach delivers pT3-class
   TCs; contention retires the seed rows), then DELETE the pT3 builder, its pixel map, and
   pt3dnn.
A/B gates: pT3-class per-band efficiency contribution >= baseline's (incl. displaced strata —
LST's pT3 path is displacement-blind, so low-vxy gain is expected, not just parity); global
triple within the frozen bars; attach stage time within the P2.4 budget.
Kill/fallback: if the offline A/B fails, the pT3 builder stays and the deletion list shrinks
accordingly — state it, do not force it.
*Revert: class flip is one flag; the builder code is not deleted until the flip has soaked.*

### Phase P2.5 — the physics kinks the prototype could not validate (plan §10.3 residual risk)
Atomic weld/claim race semantics vs the serial reference; CPU-vs-GPU score parity;
run-to-run reproducibility per the maintainer policy (free stable tie-break swap,
measured; else accept documented nondeterminism; NO sorting). **ROCm DESCOPED
(maintainer, 2026-08-02: "idc about ROCm"; no AMD hardware on this box anyway) — no
dedicated ROCm validation. The only ROCm consideration that remains is passive and free:
keep kernels warp-width-agnostic (already the style), so a ROCm build stays plausible for
whoever needs it later.** **STANDALONE-ONLY (maintainer resequencing, 2026-08-02): the
full CMSSW workflow validation previously gated here is DEFERRED to the new Phase P2.8 —
no agent runs cmsRun / the workflow matrix until integration is essentially complete.**

### Phase P2.7b — output/converter surface (explicit, per maintainer question 2026-08-02)
The infrastructure fallout, named so none of it is implied: (a) tc-side ntuple writer =
P2.3 scope (harness/plot stack unchanged by the impersonation design); (b) timing/
multiplicity printouts + lst_timing parsing evolve incrementally (add chain stage columns
during hybrid phases, drop retired columns at P2.7); (c) the P2.7 deletion sweep carries
its own writer/AccessHelper/Common.h/memory-report cleanup as phase content; (d)
**LSTOutputConverter (RecoTracker/LST) must learn chain TCs BEFORE P2.8 can run** — kept
small by the P2.3 decision to emit into the standard TrackCandidatesBaseSoA, but it is
real work and is a named prerequisite of P2.8, done here; (e) chain_* --allobj object
branches for future retraining dumps = post-integration item (cube/jet era).

### Phase P2.8 — CMSSW full-chain validation (LAST; moved out of P2.5 per maintainer, 2026-08-02)
After P2.7 (pT5-side deletion) is landed: the full CMSSW workflow runs (wf 24834.703/.704,
CPU + GPU) with (a) standard MTV plots, (b) hit-residual / fit-quality distribution
comparison (chi2/ndof, outlier-rejected-hit rates — catches "same tracks, subtly worse
hits" that MTV cannot see), (c) downstream track-selection MVA input/output distribution
comparison and pass rates (catches distribution shift that silently costs efficiency at
the selected working point; if shifted, it is a finding to raise with downstream, not to
hide). This is the integration sign-off gate before the post-integration queue
(simplifying pass -> P2.4b pT3 flip -> cube/jet round).

### Phase P2.6 — timing campaign (Stage 3)
Occupancy, kernel fusion, layout (fp16 node features), the tiered edge scorer as jet insurance,
stream behaviour. Judged by wall time against the 1.9 ms/evt GPU baseline with **bit-parity as
the guard rail**. Discipline carried over from the offline campaign: recon-first, never optimize
during a port.

### Phase P2.7 — deletion (plan P4)
Retire the ~25 kernels, the 12 pixelmap `.bin` files, `PixelMap` SoA, the 45,000 superbins, the
3 pixelTypes, `n_max_pixel_triplets=5000`, `n_max_pixel_quintuplets=15000`,
`kMaxCompatModules=40`, `kNTripletThreshold=1000`, the `preAllocated*` scratch columns, and the
five retired NNs. Weights → EventSetup. **This is where the memory win and the dup-artifact
floor actually land.**

### 5.1 Two caveats that must be stated in every offer and every A/B

**(a) `CheckHitspLS` / `nopLSDupClean` config matching (maintainer requirement, 2026-08-02).**
LST's pLS dedup is ONE kernel at TWO sites, both gated by one flag:
`pixelLineSegmentCleaning()` (`src/alpaka/LSTEvent.dev.cc:1044-1057`, `secondPass=false`) and a
stricter quad-aware pass inside `createTrackCandidates()` (`:599-611`, `secondPass=true`).
The frozen benchmark ntuple = standalone defaults = **cleaning ON (both passes), tc_pls_triplets
OFF, ptCut 0.8**, and the prototype carries LST's bare-pLS TC rows verbatim (dupcut verified
bit-identical, 188,230-row set) ⇒ pLS self-cleaning is identical by construction and today's
A/Bs are fair. **The catch:** the Phase-2 `seedingLST + trackingLST` combined config flips BOTH
(`nopLSDupClean = True`, `tcpLSTriplets = True`, `ptCut 0.6` —
`RecoTracker/LST/python/lstProducerTask_cff.py:17-21`), while the `trackingLST`-only workflow
keeps producer defaults (= our benchmark). **Any config we ship must run the SAME two
`CheckHitspLS` sites with the SAME flag values as the target workflow.** Maintainer decision
(2026-08-02): target-config choice **DEFERRED** — current work targets the **pT 0.8 offline
config** and that is what we benchmark; HLT runs 0.9 and a low-pT 0.6 config exists offline-only;
cross-threshold generalization is a far-later check, after the algorithm settles. If the target
turns out to be the seedingLST combo, the benchmark must be regenerated with
`--no_pls_dupclean --tc_pls_triplets` (both exist in `lst.cc`) and re-baselined — a symmetric
shift: the pLS-pLS dup cell grows (already 49% of our dup budget) and the bare-pLS universe
expands, raising the attach-contention lever's weight.

**(b) The pT3 class is still CARRIED, not replaced.** The flagship runs `-RT3 0`. **Only the pT5
class is replaced** by attach; **pT3 rows and bare-pLS rows are LST rows carried verbatim.**
M16 finding (b): the pT3 head is not the limiter — the target-universe economics are
(42.8k bare T3s/evt → 6.45M pairs/evt, 21× chain-only, enumeration 8.1 s/evt), and every
affordable margin sat below the LST pT3 slice. Enabling `-RT3 1` requires **both** the grid
prefilter (§1.1) **and** per-class margin re-derivation. Until then:
- the mode framing in any offer must say "pT5 class replaced; pT3 + bare pLS carried";
- `CreatePixelTripletsFromMap`, the pT3 superbin machinery and `pt3dnn` **cannot be deleted**
  at Phase P2.7 — they move to a P2.8 sub-phase conditional on `-RT3 1` landing;
- M16 (c): pLS contention reaches parity via contention alone (FR .0492 vs .0493) but the active
  suppress rule costs prompt and is rejected ⇒ `plsembdnn` / `CrossCleanpLS` are **not yet
  replaceable** either.

---

## 6. Open risks

**R1 — the d510 knife edge.** The dxy[5,10) displaced floor is pinned at **exactly 71/285 =
.2491 in every M18b configuration**, with a +0.58-track margin: **losing ONE track fails the
floor.** The floor is a 1-to-3-track decision with no stated uncertainty (the panel's block (4)).
Any port-induced non-determinism — atomic ordering in K6 or K9, an fp16 node-feature experiment,
a CPU-vs-GPU `exp`/`atan2` ULP difference — can flip it. **Mitigations:** (i) treat the floor as
advisory during P2.0–P2.2 and binding only from P2.3; (ii) quote a Poisson band (√71 ≈ 8.4
tracks ⇒ ±3%) alongside the point value in every P2 A/B, which the panel already required;
(iii) the packed-integer atomic keys specified in §1 (K6a, K8c, K9b) exist precisely so the port
has **no float atomics anywhere** and the tie-break is a total order — determinism is a design
property here, not a hope; (iv) re-derive the floor on the M19 frozen config with more events
before it gates anything.

**R2 — timing budget vs LST production.** The prototype is ~265 ms/evt CPU single-thread; the
grid prefilter takes that to a projected ~68 ms, inside plan §4's 60–80 ms for the touched scope
(replaced scope ≈ 235 ms). **But:** (i) the projection is arithmetic, not measured — the first
honest number arrives at Phase P2.4; (ii) the GPU baseline is **1.9 ms/evt for the whole event**,
and no chain-pipeline GPU number exists at all; plan §4's "dominant K5 ≈ 1e9 FLOP ≈ 0.05 ms" is
a FLOP-count estimate that ignores the gather-heavy edge-feature build and the K9 atomic traffic;
(iii) plan §6.5 (Amdahl): post-redesign the optimum shifts upstream (pLS 334, MD 91, LS 76 ms) so
event-level speedup saturates at ~1.5–2× regardless — **all claims must be stated on touched
scope**; (iv) `-RT3 1`, when it lands, re-opens the attach cost question (§1.1). **Mitigation:**
Phase P2.6 is a dedicated campaign and Stage-1 landing is *not* conditioned on beating the
baseline, only on not regressing the full-event GPU time by more than an agreed budget.

**R3 — K9 claim parallelization is the one un-validated physics step.** The prototype's K9 is a
**serial greedy walk** in order-key order; acceptance depends on what earlier chains claimed.
The propose-verify atomic form (§1, K9b) is *not* guaranteed to reach the same fixed point.
**Pre-port measurement item (do this in the golden tree, offline, before writing K9b):**
instrument `k9Arbitrate` to report, per event, how many acceptances would change if the claim
map were frozen at round boundaries rather than updated per chain — i.e. simulate the 2-round
and 3-round propose-verify semantics and diff the accepted set against the serial one. If the
divergence is ≪ the d510 margin, the port is safe; if not, K9 needs either more rounds or a
deterministic final serial sweep over the residue. This is the single highest-value pre-P2
offline experiment left.

**R4 — memory.** Low risk in steady state (§2.7: +12 MB mean, ~24 MB at jet max, against
40–80 MB of deletions) but **Stage 1 is additive** — the old SoAs still exist through P2.6, so
peak memory *rises* until P2.7. With 8 concurrent HLT streams that is ~+100–190 MB of headroom
consumed for the duration of the migration. **Mitigation:** the exact-count allocation makes the
new buffers precisely sized (no ceiling padding in the common case); set the edge ceiling at
1.0e6 (13 MB) and instrument a DQM overflow counter from day one; if headroom binds, gate
Stage 1 behind `useChainTracking` in a separate workflow rather than running both paths in the
default menu.

**R5 — heads trained on data that includes the OOS events.** `edge_mlp_v3`, `chain3_m12` and the
a2 head were trained on 649 events **including** the 349 used for the "true-OOS" check.
The M18b OOS result (trade profile stable; fake +.0018 / dup +.0057 reproducing in-sample deltas
almost exactly; displaced gains as large or larger — v510 +.0812, d15 +.0794) is therefore clean
for every cut/threshold and for the attach head, but **not** for the three heads. Fully clean OOS
needs retrained heads. Read deltas, not absolutes (LST's own numbers shift between event sets:
eff .8098 vs .8136). **This must be restated whenever a P2 A/B is compared to the offline
numbers.**

**R6 — the edge head's displaced weakness is a known, traced, unfixed defect.** M18's T4 trace on
dxy[10,30): upstream is CLEAN (30/30 nodes present, 30/30 E2 edges enumerated); the dominant loss
is **edge-MLP discrimination** (17/30 true edges below `-e 0`, median true-edge score −0.53;
positives 95.7% prompt while targets sit at vxy 16–53 cm) plus ~4/5 weld thefts being E2-vs-E2
misranking. The prescribed fix is an **E2-aware displacement-reweighted retrain (or a dedicated
E2 head)** — explicitly **not** lowering `thetaEdge`. If M19's retrain lands, the port must
accept a replacement `edge_mlp_weights.h` with no code change (only `kInput = 40` is structural);
if it does not land, dxy[10,30) at .0256 vs LST's .0470 is a known regression that must be
declared.

**R7 — plan §6 blind spots not yet addressed anywhere.** Carried forward, unowned:
legitimate shared hits (conversions / boosted τ / 2S-merged — the capped 2-way ownership
exemption is an M19 item and has never been measured); loopers/curl-back; electron/brems samples
in the kill suite; aging / killed-module stress; misalignment and beamspot-drift training
augmentation; the label-circularity second metric (sim find-rate at any purity). None of these
block Stage 1, all of them block a physics sign-off.

---

## 7. Immediate pre-P2 checklist (things to do while M19 runs)

1. **Grid-prefilter superset verification offline** (§1.1 parity protocol) — pure prototype work,
   no kernel needed, retires the largest timing unknown.
2. **K9 propose-verify divergence measurement** (R3) — the highest-value remaining offline
   experiment.
3. **Decide the a2/f11 question** (§4): ship `chain_mlp_weights.h` as the f11 provider, or retrain
   the attach head without f11. M19's attach-head retrain is the natural place to do the latter.
4. **Re-derive the d510 floor with uncertainties** on the M19 frozen config (R1).
5. **Confirm the target workflow config** with the maintainer (§5.1(a)) — currently DEFERRED to
   pT 0.8 offline, but the answer determines whether the benchmark must be regenerated.
6. **Freeze the config** and record the exact effective flag set (§3.1) in the plan's milestone
   log, with the golden tree's four shipping weight-header md5s next to it.
