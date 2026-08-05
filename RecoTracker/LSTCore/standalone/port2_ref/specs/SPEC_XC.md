# SPEC_XC -- SEED CROSSCLEAN (`-XC` family): port spec for the Alpaka tree

Scope: `-XC`, `-XCT`, `-XCT2`, `-XCT3`, `-XCR2`, `-XCW2`, `-XCG`, `-XC4`, `-XC4T` (and the
inert diagnostic `-XCD`). Everything below is derived from the prototype only; no need to
re-read it.

Path shorthand: `S` = `/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone`,
`P` = `/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore`.
Prototype = `S/protoFINAL2/`. LST = `P/src/alpaka/`.

Winner command line (`S/synth_ref/syn_run.sh:16-22` frozen prefix + BASE + overrides, as
recorded in `S/synth_ref/r_F3W977.cmd`):
`-XC 3 -XCT 4 ... -XC4 1 -XCT 3.75 -XCT2 3.5` -> effective `-XC 3 -XCT 3.75 -XCT2 3.5
-XCT3 3.75 -XCR2 1e-6 -XCW2 0.02 -XCG 0 -XC4 1 -XC4T follow -XCD 0`.

---

## 0. ONE-PARAGRAPH SUMMARY OF THE MECHANISM

`-XC` is a structural port of LST's `CrossCleanpLS` kernel
(`P/src/alpaka/TrackCandidate.h:327-421`) onto the prototype's own track candidates. It
produces exactly one per-event output: `xcRetired[pLS row]` (a `char` mask,
`S/protoFINAL2/main.cc:4928`). Nothing else in the pipeline is mutated by it. Two
consumers apply the mask, both on the BARE-SEED (type-8) channel only
(`main.cc:5075-5088` and `main.cc:5254-5262`). It is a bitmask of two independent arms:

* **bit 0 (`-XC 1`) -- pixel-anchored arm** = LST's `pT5` + `pT3` arms fused. Retire a bare
  seed that shares >= 1 pixel hit row with, or sits within `dR^2 < -XCR2` of, the seed of
  ANY track candidate we delivered with a seed.
* **bit 1 (`-XC 2`) -- bare-chain arm** = LST's `T5` arm. Retire a bare seed within
  `dR^2 < -XCW2` of a delivered SEEDLESS chain TC whose attach-head logit for that exact
  `(seed, chain)` pair is `>= -XCT` (eta-binned). This is the ONE substitution: LST's
  pLS/T5 embedding-distance-vs-working-point test is replaced by the attach head's own
  pair logit at a separate, looser dedup threshold, because the pLS/T5 embedding nets are
  in the P2.7 deletion set.
* **`-XC 3` = both** (the winner).

---

## 1. EXACT CONTROL FLOW / PIPELINE POSITION

### 1.1 Prototype per-event ordering (`main.cc`, all inside the per-event loop)

| line | stage | relation to XC |
|---|---|---|
| 3189 | `ga.recordPairs = attachCM \|\| xcChain \|\| ccsOn` | XC bit 1 FORCES the pair log on |
| 3479-3489 | `plsPixelHits(p, hits)` lambda (pLS row -> its Pixel-type `trk.see_hitIdx` rows) | XC pixel arm consumer |
| 3491-3505 | `rowPls(it, ty)` lambda (carried TC row -> its pLS row) | XC channel (a) consumer |
| 3557-3568 | `m16RowSuppressed` wholesale init (`-RT5`/`-RT3`) | -- |
| 3570-3594 | `m16RefreshSupp()` lambda: contention + `-RPS`/`-RPSA` predicate on carried rows | runs BEFORE XC; XC only ADDS to the same mask |
| 3640 | `k9ArbitrateTwoPass(...) -> accepted` | -- |
| 3788-3810 | **attach STAGE A** (`gaStageChains`) over `accepted`, `nLayers >= 5` | produces `ga.pairLog`, `ga.plsOwned`, `ga.plsBestChainLogit` |
| **3811-3834** | **`-XC4` score-only 4-layer pass** (appends to `ga.pairLog`) | XC4 |
| 3835-3880 | `-a4` stage A2 (OFF in winner: `a4Theta` unset) | suppresses `recordPairs` while it runs |
| 3910 | `chainAttachPls = ga.chainPls` | -- |
| 3914-4003 | `-EX` chain extension; **mutates `chains.nLayers`** (`Extend.cc:444`) | trap, see 6.4 |
| 4005 | `k10AssembleChainTCs(...) -> chainTCs` | chain TC eta/phi frozen here |
| 4084-4209 | **output TC vector build**: `outTCs` / `outTCChain` lockstep, incl. in-place type-4/9 -> type-7 upgrade and `-CCS` suppression | XC bare-chain arm reads BOTH vectors |
| 4211-4497 | STAGE B (`gaStageT3`), `-RD`/`-RDT` seed dedup, `-CC` pT3-class hit-overlap contention, type-5 delivery push (`outTCChain.push_back(-1)`), final `m16RefreshSupp()` | all BEFORE XC |
| 4841-4906 | `-XP3` carried-pT3 crossclean | before XC |
| **4908-5091** | **SEED CROSSCLEAN (`-XC`)** | THIS SPEC |
| 5093-5287 | `-ZP8` post-deletion bare-pLS additions; consumes `xcRetired` at 5257-5262 | XC consumer (b) |
| 5288-5361 | `-XCD` truth partition (diagnostic, writes only counters) | inert |
| 5368 | `writer.fillEventHybrid(ev, trk, outTCs, ..., rowMaskOut = &m16RowSuppressed)` | final emission |

So, answering the four questions asked:

* **(a) attach scoring/contention** -- XC runs strictly AFTER both attach stages and after
  all contention (`ga.plsOwned` final, `ga.pairLog` final).
* **(b) chain TC emission** -- XC runs strictly AFTER `outTCs` is fully built (chain TCs,
  type-7 upgrades, type-5 pT3-class deliveries, K7-lite, `-XP3`). This is deliberate and
  matches LST: `CrossCleanpLS` is launched at `P/src/alpaka/LSTEvent.dev.cc:3203` -- after
  `AddT5asTrackCandidate` (3157), `CrossCleanT4` (3170), `AddT4asTrackCandidate` (3188),
  and immediately BEFORE `AddpLSasTrackCandidate` (3227). Same slot.
* **(c) the `-RPS`/`-RPSA` seed-retirement predicate** -- runs BEFORE XC, inside
  `m16RefreshSupp()` (`main.cc:3580-3584`). XC is a strictly ADDITIONAL retirement channel:
  `m16RefreshSupp()` is idempotent and only ever ADDS rows, and XC writes into the same
  `m16RowSuppressed` mask. Order does not matter for the final row set, but it DOES matter
  for the `-XCD` diagnostic's fate column (`main.cc:5348-5357`: `consumed` > `RPSblock` >
  `XCretire` > `survive`, first match wins).
* **(d) the output TC vector build** -- XC does NOT touch `outTCs`. It only sets
  `m16RowSuppressed[carried type-8 row] = 1` (channel a) and blocks `-ZP8` synthetic
  type-8 pushes (channel b). Carried type-7 and type-5 rows are explicitly NOT touched
  (`main.cc:5079`: `ev.tc_type[it] != 8` -> continue).

### 1.2 The lockstep invariants that must be preserved

The bare-chain arm iterates the OUTPUT TC vector, not the chain array:

```cpp
// main.cc:5037-5045
for (std::size_t j = 0; j < outTCs.size() && j < outTCChain.size(); ++j) {
  const int c = outTCChain[j];
  if (c < 0) continue;              // a delivered pT3-class row: pixel-anchored, not here
  if (outTCs[j].type == 7) continue;// chain TC WITH a seed: pixel-anchored branch above
  const auto f = chainPairs.find(c);
  if (f == chainPairs.end()) continue;
```

Invariants:

* `outTCChain[j]` is the source chain row for emitted TC `j`, or `-1` for a delivered
  pT3-class (type-5) row (`main.cc:4087`, pushed `-1` at `main.cc:4487`). Every
  `push_back` to `outTCs` is paired with exactly one `push_back` to `outTCChain`
  (`main.cc:4206-4207`, `4486-4487`, `5283` -- note `-ZP8` at 5283 pushes to `outTCs`
  WITHOUT `outTCChain`, which is safe only because it runs AFTER XC and the loop is bounded
  by `min(outTCs.size(), outTCChain.size())`).
* K7-lite (`main.cc:4836-4837`) swaps BOTH vectors together.
* In the assembly loop the `chainTCs`/`accepted` lockstep is separate: `tcPos` advances for
  every `accepted` chain with `nLayers >= 4` even when the row is `-CCS`-suppressed
  (`main.cc:4176-4179`: `++nCcsSuppressed; continue;` AFTER `ChainTC& ctc = chainTCs[tcPos++]`
  at 4084-4090). **A `-CCS`-suppressed chain therefore never enters `outTCs` and can never
  retire a seed.** See 6.2.
* `xcRetired` is indexed by pLS row and sized `ev.pLS_pt.size()` (`main.cc:4933`).
  Monotone: once 1, never cleared; every candidate loop early-outs on `xcRetired[p]`.

---

## 2. THE EXACT PREDICATES

Common definitions (`main.cc:4931-4939`):

```cpp
if (attachMode == 4 && xcBits != 0) {           // -A 4 only
  const int nPlsXc = ev.pLS_pt.size();
  xcRetired.assign(nPlsXc, 0);
  auto isAnchor = [&](int p) { return p >= 0 && p < ga.plsOwned.size() && ga.plsOwned[p] != 0; };
```

`isAnchor(p)` == "the delivery consumed this seed" == "this seed is not a bare row in the
first place". Anchor set = every pLS with `ga.plsOwned != 0` after BOTH attach stages,
`-RD`/`-RDT` seed-family dedup, and `-CC` revocation (`-CCR 1/2` clears `plsOwned`,
`main.cc:4441`).

### 2.1 Pixel-anchored arm (`-XC` bit 0) -- LST pT5 arm + pT3 arm fused

Candidate universe: `ev.pLS_isQuad[p] && !xcRetired[p] && !isAnchor(p)` (`main.cc:4965-4966`).

Test 1 -- **shared pixel hit** (`main.cc:4967-4977`):
retire if any of `p`'s Pixel-type hit rows equals any Pixel-type hit row of ANY anchor seed.
Implemented as one `unordered_map<int,char> anchorHit` filled from all anchors
(`main.cc:4949-4959`), probed with `p`'s <= 4 rows. This IS `pixelHitsOverlapAny`
(`P/src/alpaka/TrackCandidate.h:154-170`), the **>= 1 shared row** criterion -- NOT the
`>= 2` seed-family criterion used by `-RD`.

Test 2 -- **anchor dR^2** (`main.cc:4979-5004`):
retire if, for some anchor seed `q`,
`(eta_p - eta_q)^2 + wrapDPhi(phi_p - phi_q)^2 < xcDR2Pix` with `xcDR2Pix = 1e-6`.
`eta`/`phi` are `ev.pLS_eta` / `ev.pLS_phi` of BOTH sides (the SEEDS, not the TCs).
Implemented with an `(eta, phi)` hash grid, cell width
`cellW = max(0.01f, sqrt(xcDR2Pix)) = 0.01`, `nPhiCell = floor(2*pi / cellW) = 628`, phi
cells wrapped; the 3x3 neighbourhood is exact because `cellW >= sqrt(window)`.

Test 1 is evaluated first and `continue`s on success (counted separately).

#### Cross-check against the real LST kernel

`P/src/alpaka/TrackCandidate.h`, `struct CrossCleanpLS`, lines 327-421.

| LST line(s) | LST behaviour | prototype |
|---|---|---|
| 344 | grid-stride over `nPixels` pLS (`uniform_elements_y`) | `for (int p = 0; p < nPlsXc; ++p)` |
| 345 | skip `!pixelSeeds.isQuad()[pixelArrayIndex]` or already `pixelSegments.isDup()` | `ev.pLS_isQuad[p]` kept **verbatim**; the `isDup` pre-skip is NOT applied -- see 2.4 |
| 348-349 | `eta1/phi1 = pixelSeeds.eta()/phi()[pixelArrayIndex]` | `ev.pLS_eta/phi[p]` -- verbatim |
| 350 | `prefix = ranges.segmentModuleIndices()[pixelModuleIndex]` (global seg idx -> pixel array idx) | N/A (prototype indexes pLS rows directly) |
| 353-356, 377-382 | pLS embedding + `t5Embed` squared distance vs `dnn::plsembdnn::kWP[bin]` | **SUBSTITUTED** -- see 2.2 |
| 359-361 | `bin_idx = (|eta1|>2.5) ? kEtaBins-1 : |eta1|/kEtaSize`, `kEtaSize = 0.25`, `kEtaBins = 10`, `kWP = {0.9235, 0.8974, 0.9061, 0.9431, 0.8262, 0.7998, 0.7714, 0.7017, 0.6749, 0.6624}` (`P/interface/alpaka/Common.h:66-72`) | **provenance for the eta-binning idea only**; the prototype uses 3 bins (1.1 / 1.7) on the SAME quantity (`|seed eta|`) |
| 364 | inner loop over ALL `nTrackCandidates` | replaced by O(1)-per-candidate hash set + hash grid; see 5 |
| 365-366 | `type = candsBase.trackCandidateType()[i]`, `innerTrackletIdx = candsExtended.objectIndices()[i][0]` | replaced by `ga.plsOwned` / `outTCs[j].type` / `outTCChain[j]` |
| **402-416 (pT5 arm)** | `pLSIndex = innerTrackletIdx`; `pixelHitsOverlapAny(pixelArrayIndex, pLSIndex - prefix, ...)` -> isDup; then `eta2/phi2 = pixelSeeds.eta()/phi()[pLSIndex - prefix]`, `dEta = abs(eta1-eta2)`, `dPhi = deltaPhi(phi1,phi2)`, `dR2 = dEta^2+dPhi^2`, `dR2 < 0.000001f` -> isDup | **VERBATIM**, with `-XCR2` exposing the `1e-6` constant. Anchor eta/phi = the pT5's own seed eta/phi == `ev.pLS_eta/phi[q]`. |
| **386-401 (pT3 arm)** | `pLSIndex = pixelTriplets.pixelSegmentIndices()[pT3Index]`; same `pixelHitsOverlapAny` -> isDup; then `eta2/phi2 = __H2F(pixelTriplets.eta_pix()/phi_pix()[pT3Index])`, same `dR2 < 0.000001f` -> isDup | **VERBATIM in structure and window.** ONE numerical substitution: the prototype uses the seed's `ev.pLS_eta/phi` where LST uses the pT3's stored half-precision `eta_pix/phi_pix`. These are the same physical quantity (the pixel-segment direction of the pT3's seed); the difference is fp16 rounding + the pT3 fit. |
| 383, 390, 400, 405, 415 | verdict is `pixelSegments.isDup()[pixelArrayIndex] = true` | verdict is `xcRetired[p] = 1` |
| 157-158 | `pixelHitsOverlapAny` returns `true` when `ix == jx`, so a pT5's OWN seed is marked isDup | **relocated, not dropped**: the prototype excludes anchors from candidacy (`isAnchor -> continue`) and the "seed consumed by a delivery" suppression is done by the contention rule in `m16RefreshSupp()` (`main.cc:3579`, `drop = ga.plsOwned[pls] != 0`). Net effect identical. |

**Why the two LST arms collapse into one prototype arm** (`main.cc:925-932`): both LST arms
anchor on the TC's own pLS, and in the prototype's delivery a TC has a seed iff
`ga.plsOwned` marks that seed. So the per-type dispatch is preserved but the two anchor
sets are the same set. NOTE the precondition: this is exact only because the winner runs
`-RT5 1 -RT3 1` (wholesale replacement), so NO carried baseline type-7/type-5 row survives
to be an un-modelled anchor; the only surviving carried rows are type-8, which are
candidates. **At real integration every pixel-anchored TC is one of our own deliveries, so
the collapse is unconditional.**

### 2.2 Bare-chain arm (`-XC` bit 1) -- LST T5 arm with the WP substitution

Gate: `xcChain && !ga.pairLog.empty()` (`main.cc:5013`).

Prefilter of the pair log into a per-chain CSR (`main.cc:5028-5036`):

```cpp
float xcThetaMin = std::min(xcTheta, std::min(xcThetaT, xcThetaE));
if (xcT4 >= 0.5f && xcT4Theta < 1e8f) xcThetaMin = std::min(xcThetaMin, xcT4Theta);
auto xcThetaOf = [&](float eta) {                       // main.cc:5022-5025
  const float ae = std::fabs(eta);
  return (ae < 1.1f) ? xcTheta : ((ae < 1.7f) ? xcThetaT : xcThetaE);
};
std::unordered_map<int, std::vector<std::pair<int,float>>> chainPairs;   // chain row -> (pLS row, logit)
for (const auto& sp : ga.pairLog) {
  if (sp.ttype != kAttachTargetChain) continue;         // drop bare-T3-target pairs
  if (xcGlobalScore < 0.5f && !(sp.logit >= xcThetaMin)) continue;  // no prefilter at -XCG 1
  chainPairs[sp.tgtRow].emplace_back(sp.plsRow, sp.logit);
}
```

Per emitted TC (loop quoted in 1.2), per logged pair `(p, logit)`:

```cpp
// main.cc:5046-5069
const bool t4Target = (c < chains.nLayers.size()) && chains.nLayers[c] < 5;   // post-EX nLayers
for (const auto& pr : f->second) {
  const int p = pr.first;
  if (p < 0 || p >= nPlsXc || xcRetired[p] || !ev.pLS_isQuad[p] || isAnchor(p)) continue;
  const float sc  = (xcGlobalScore >= 0.5f && p < ga.plsBestChainLogit.size())
                      ? ga.plsBestChainLogit[p] : pr.second;
  const float thr = (t4Target && xcT4Theta < 1e8f) ? xcT4Theta : xcThetaOf(ev.pLS_eta[p]);
  if (!(sc >= thr)) continue;
  const float dEta = ev.pLS_eta[p] - outTCs[j].eta;
  const float dPhi = wrapDPhi(ev.pLS_phi[p] - outTCs[j].phi);
  if (dEta * dEta + dPhi * dPhi < xcDR2Chain) { xcRetired[p] = 1; ++nXcChainKill; if (t4Target) ++totXc4Kill; }
}
```

Predicate, spelled out:

> Retire bare quad seed `p` iff there exists an emitted TC `j` with
> `outTCChain[j] = c >= 0` and `outTCs[j].type != 7` (a **delivered seedless chain TC**,
> type 4 or 9) such that the attach head logged a `(chain c, seed p)` pair with
> `logit >= xcThetaOf(|ev.pLS_eta[p]|)` (or `>= xcT4Theta` when `nLayers[c] < 5` and
> `-XC4T` is set), AND
> `(ev.pLS_eta[p] - eta_TC)^2 + wrapDPhi(ev.pLS_phi[p] - phi_TC)^2 < xcDR2Chain (0.02)`.

LST correspondence (`TrackCandidate.h:367-385`):

| LST | prototype | verbatim? |
|---|---|---|
| `type == LSTObjType::T5`, `innerTrackletIdx` = T5 index | `outTCChain[j] >= 0 && outTCs[j].type != 7` (seedless chain TC) | structural equivalent (our T5-class object is the chain) |
| `eta2/phi2 = __H2F(quintuplets.eta()/phi()[iT5])` | `outTCs[j].eta/phi` = the chain's **innermost member T3** eta/phi (`K9K10.cc:423-426`) | **SUBSTITUTED**: no T5 fit exists; innermost-T3 direction is used. Half-precision vs fp32 also differs. |
| `dR2 < 0.02f` (line 375) | `dR2 < xcDR2Chain`, default/winner `0.02` | **VERBATIM** |
| `d2 = sum_k (plsEmbed[k] - t5Embed[k])^2`, `d2 < kWP[bin]^2` (lines 376-382) | `attach-head logit for THAT (seed, chain) pair >= xcThetaOf(|seed eta|)` | **THE SUBSTITUTION.** Forced: `plsEmbed` (`Segment.h:307`) and `t5Embed` die with the P2.7 deletion set. Rationale (`main.cc:915-923`, maintainer): attaching is a construction decision and needs precision; retiring a redundant seed is bookkeeping and can be looser -- hence `-XCT` (3.75) sits well BELOW the delivery margin `-a` (5.0). Nothing is recomputed: the logits are the ones `AttachDelivery` already produced. |
| `bin_idx` on `pixelSeeds.eta()` (line 360), 10 bins of 0.25 | 3 bins on `ev.pLS_eta[p]` at 1.1 / 1.7 (the scoreboard's own regions) | binning ON THE SAME QUANTITY (the SEED's eta, not the TC's), coarser. `main.cc:938-947` records why: endcap dup saturates immediately while endcap eff is where we lead, barrel/transition dup keeps falling. |
| `pixelSegments.isDup() = true` | `xcRetired[p] = 1` | -- |

**`-XCG` (score source), `main.cc:951-957, 5051-5053`:** `0` (default, winner) = the logit of
THAT `(seed, chain)` pair -- LST's own structure, since its T5 arm compares the seed
against the T5 in front of it, not the whole event. `1` = `ga.plsBestChainLogit[p]`, the
per-seed best over ANY chain (the `-RPSA` quantity), strictly looser; at `-XCG 1` the
pair-log logit prefilter is inadmissible and is skipped.

### 2.3 The two verdict channels (`xcRetired` consumers)

**(a) carried type-8 rows** (`main.cc:5073-5088`) -- LST's own admitted bare-seed set:

```cpp
for (std::size_t it = 0; it < ev.tc_type.size(); ++it) {
  if (m16RowSuppressed[it] || ev.tc_type[it] != 8) continue;
  const int p = rowPls(it, 8);
  if (p >= 0 && p < nPlsXc && xcRetired[p]) { m16RowSuppressed[it] = 1; ++nM16SuppT8; ++nXcCarried; }
}
```

**(b) `-ZP8` synthetic additions** (`main.cc:5254-5262`) -- the seeds LST's own
`CrossCleanpLS` used to kill, added back so the post-deletion universe can be scored:

```cpp
bool drop = p < ga.plsOwned.size() && ga.plsOwned[p] != 0;
if (!drop && replPls >= 0.5f && p < ga.plsBestChainLogit.size())
  drop = ga.plsBestChainLogit[p] >= rpsThetaChain || ga.plsBestT3Logit[p] >= rpsThetaT3;   // -RPS/-RPSA/-RPST
if (!drop && p < xcRetired.size() && xcRetired[p]) { drop = true; ++totXcZp8; }
```

Both channels live in the post-deletion universe (`isQuad && pLS_isDupAlgPass2 == 0`), so
the rule is applied UNIFORMLY to it -- necessary, because post-deletion LST's own
`CrossCleanpLS` no longer exists to have pre-filtered channel (a) (`main.cc:4913-4919`).

### 2.4 Deliberate deviation: no `isDup` pre-skip on the candidate

LST skips a seed whose `pixelSegments.isDup()` is already set (line 345). The prototype
computes `xcRetired` for EVERY `isQuad` seed and lets the consumers apply the admission
filter. This is why the ledger's "seeds retired" (3428/evt) is two orders of magnitude
larger than the effective row deltas (8.5 + 35.4 = 43.9 rows/evt): most retirements are
inert because the seed was never going to be emitted. **At integration, gating candidacy on
the surviving-seed mask (post-`CheckHitspLS` pass 1) reproduces the same output rows at
~1/70th the work.**

---

## 3. WHAT THE MECHANISM CONSUMES, AND WHERE EACH IS PRODUCED

| quantity | exact meaning | produced at |
|---|---|---|
| `ga.pairLog` : `vector<ScoredPair{int8_t ttype; int tgtRow; int plsRow; float logit}>` | every SCORED attach pair, as the delivery decision saw it. `ttype = kAttachTargetChain(0)` -> `tgtRow` is a **CHAIN ROW** (not a position in the target list); `ttype = kAttachTargetT3(1)` -> `tgtRow` is a t3 row (ignored by XC) | `AttachDelivery.h:116-122` decl; written `AttachDelivery.cc:85-86` (stage A) and `:158-159` (stage B); appended by the `-XC4` pass at `main.cc:3826-3830`. Enabled by `ga.recordPairs`, forced on by `xcChain` at `main.cc:3189`. |
| `logit` | `attachLogit(pr.f)` -- the attach MLP head on the `kAttachFeat` pair feature vector (`AttachInference.h`, weights `attach_mlp_weights.h`) | `AttachDelivery.cc:82` / `:156` |
| `ga.plsOwned` : `vector<char>` per pLS row | 1 once ANY target owns the seed (cross-type exclusivity). XC's ANCHOR SET and its candidate exclusion | `gaInit` (`AttachDelivery.cc:51`); set `AttachDelivery.cc:117` (stage A) / `:196` (stage B); CLEARED by `-CC` revoke (`main.cc:4441`) and by `-RD`/`-RDT` family dedup (`main.cc:4249`) |
| `ga.plsBestChainLogit` : `vector<float>` per pLS row | best chain-target pair logit for that seed BEFORE threshold and BEFORE contention. Used by XC ONLY at `-XCG 1` | `AttachDelivery.cc:87-89`, only when `params.writeBestLogit` |
| `ga.chainPls` -> `chainAttachPls` | per-chain owned pLS or -1 | `main.cc:3910` |
| `ev.pLS_eta[p]`, `ev.pLS_phi[p]` | **SEED** eta/phi. Used for: (i) both sides of the pixel-arm dR^2 test, (ii) the candidate side of the bare-chain dR^2 test, (iii) **the eta-bin threshold lookup** | ntuple (`NtupleReader`) |
| `outTCs[j].eta`, `outTCs[j].phi` | **TC** eta/phi of the anchor chain TC = the chain's **innermost member T3** eta/phi | `K9K10.cc:423-426` (`k10AssembleChainTCs`), copied at `main.cc:4093-4094` |
| `outTCs[j].type` | 4 (T5-class chain), 9 (T4-class chain), 7 (chain upgraded with a seed), 5 (pT3-class delivery), 8 (`-ZP8` addition) | `main.cc:4092`, `4184`, `4466` |
| `outTCChain[j]` | source chain row, or -1 for a pT3-class delivery | `main.cc:4207`, `4487` |
| `chains.nLayers[c]` | POST-`-EX` layer count (`Extend.cc:444`) -- decides `t4Target` | K6 weld / `Extend.cc` |
| `ev.pLS_isQuad[p]` | LST's `pixelSeeds.isQuad()` | ntuple |
| `plsPixelHits(p, out)` | the seed's Pixel-type hit rows: `ev.pLS_seedIdx[p]` -> `trk.see_hitIdx[seed]` filtered by `trk.see_hitType[seed] == Pixel`; <= 4 rows | `main.cc:3479-3489` |
| `rowPls(it, 8)` | `ev.tc_plsIdx[it]` for a carried type-8 row | `main.cc:3491-3505` |
| `wrapDPhi(d)` | wrap to `(-pi, pi]` | `main.cc:497` (`float wrapDPhi(float)`) -- equals `cms::alpakatools::deltaPhi` |

XC produces: `xcRetired` (the only output) + the ledger counters
`totXcPixHit / totXcPixDR / totXcChainKill / totXcCarried / totXcZp8 / totXc4Pairs /
totXc4Kill` (`main.cc:2686-2695`), printed at `main.cc:5600-5620`.

---

## 4. `-XC4` (and `-XC4T`)

### 4.1 The enumeration hole it closes

The bare-chain arm is driven from `ga.pairLog`, and stage A's enumeration guard is
`if (chains.nLayers[c] < params.minChainLayers) continue;` with `AttachParams::minChainLayers = 5`
(`PixelAttach.h:92`, applied `PixelAttach.cc:740-742`) -- the frozen "v1 scope: attach only
to pT5-class chains". Consequence: **a DELIVERED 4-layer chain TC is invisible to the arm**
-- `chainPairs.find(c)` misses and the seed in front of it survives at ANY `-XCT`
(`main.cc:958-968`, `S/a15_ref/STATUS.md:113-121`).

Measured hole (300 evts, `a15_ref/dec_PROTO300.txt`): the two cells `cPLS+chT4` (861 pairs)
and `chT4+zPLS` (857 pairs) = 1718 duplicate pairs / 300 evt = **5.7 pairs/evt, oracle
-.0062 duplicate rate, none of it reachable without `-XC4`.**

### 4.2 Exact added scope and implementation

`main.cc:3819-3834`, immediately after the stage-A `gaStageChains` call, BEFORE `-EX`:

```cpp
if (xcT4 >= 0.5f && ga.recordPairs) {
  std::vector<int> t4Targets;
  for (int c : accepted) if (chains.nLayers[c] < 5) t4Targets.push_back(c);
  if (!t4Targets.empty()) {
    AttachParams p4 = gap.pref;
    p4.minChainLayers = 1;                       // the ONLY difference
    std::vector<AttachPair> pairs4;
    k8EnumeratePrefilteredPairs(ev, chains, t4Targets, cfHyb, gateLogit, p4, pairs4);
    totXc4Pairs += pairs4.size();
    for (const AttachPair& pr : pairs4)
      ga.pairLog.push_back({kAttachTargetChain, t4Targets[pr.chainPos], pr.plsRow, attachLogit(pr.f)});
  }
}
```

Properties to preserve in the port:

* **Target set** = `accepted` chains with `nLayers < 5` at that point (i.e. exactly 4, since
  `k10AssembleChainTCs` drops `< 4` and K10 emits nothing shorter). NOT the whole chain
  array -- only K9-accepted chains.
* **SCORE-ONLY.** Writes `ga.pairLog` and `totXc4Pairs` and NOTHING else: not `chainPls`,
  not `plsOwned`, not `plsBestChainLogit` (so the `-RPS`/`-RPSA` predicate is bit-identical
  with and without `-XC4`), and no delivery.
* **Same head, same prefilter, same feature builder**: `k8EnumeratePrefilteredPairs` with
  `p4 = gap.pref` copied wholesale, so the prefilter windows (`prefDPhi = 0.4`,
  `prefDTanL = 0.6`, `PixelAttach.h:83-84`) and the `-CF`/`-CFC` candidate-finder settings
  are inherited identically; only `minChainLayers` differs.
* **Same downstream test**: the appended rows go through the identical `chainPairs` CSR,
  identical `dR^2 < -XCW2`, identical `-XCT` bins. No new code path in the arm.
* `-XC4 0` (default) is bit-identical; the no-op gate passed 33/33 branches
  (`a15_ref/STATUS.md:124-127`).
* Anti-double-count coupling: when `-a4` stage A2 runs, `ga.recordPairs` is saved/forced
  false/restored around it (`main.cc:3846`, `3868-3872`) precisely because `-XC4` already
  appended those pairs and the arm must not see them twice. **Any port that adds a
  4-layer DELIVERY stage must keep this mutual exclusion.**

### 4.3 Measured effect of `-XC4 1`

300 evts (`a15_ref/STATUS.md:135-155`), on top of the A15 baseline:

```
tag                       eff      dup     fake      nhB      nhT      nhE      nTC
A15GATE (-XC4 0)      0.80992  0.06230  0.05551  9.80254  9.87906  3.55956   618793
X4      (-XC4 1)      0.80979  0.05668  0.05562  9.80890  9.90786  3.57795   616531
X4T6    (-XC4T 6)     0.80988  0.05745  0.05562  9.80555  9.90478  3.57424   616984
X4T5    (-XC4T 5)     0.80979  0.05696  0.05563
X4T3    (-XC4T 3)     0.80957  0.05648  0.05560
LST(base)             0.80988  0.05179  0.04476 10.14804 10.01546  3.56248   608190
per region   dupB     dupT     dupE
A15GATE   0.04344  0.03431  0.08106
X4        0.04286  0.02921  0.07256      (endcap now BELOW LST's 0.08556)
```

`-XC4 1` buys **-.00562 dup for -.00013 eff and +.00011 fake**, makes tracks LONGER
(+.006/+.029/+.018 hits), closes 53% of the dup gap vs LST, and leaves displaced untouched.
`upgT5`/`delivT3` per event are byte-identical -- the pass really is score-only.

### 4.4 `-XC4T` -- CONFIRMED DEAD END, DELETE AT PORT

* Declared `main.cc:970-971`, sentinel `1e9` = "follow `-XCT`". Resolved lazily at the use
  site (`main.cc:5058`: `(t4Target && xcT4Theta < 1e8f) ? xcT4Theta : xcThetaOf(...)`) and
  in the prefilter floor (`main.cc:5020-5021`). There is NO post-getopt default assignment
  for it (unlike `-XCT2`/`-XCT3`).
* Verdict, `a15_ref/STATUS.md:293`: "Only 0.83/evt of the residue is anchored on a 4-layer
  chain, so a separate `-XC4T` threshold buys nothing further."
* Verdict, `a15_ref/STATUS.md:155-158`: `-XC4T 6` is a marginal efficiency-leaning variant
  but "it costs a SECOND tuned constant. `-XC4 1` alone adds ZERO tuned constants (it
  follows `-XCT`), which is why it is the recommendation."
* Verdict, `PLAN_lst_redesign_t3_onward.md:2752`: "`-RPST` and `-XC4T` dead ends, delete at
  port."

**Port `-XC4` as an unconditional behaviour (or a single bool), and do NOT port `-XC4T`.**
With `-XC4T` gone, `xcThetaMin` reduces to `min(xcTheta, xcThetaT, xcThetaE)` and the
`t4Target` computation disappears entirely from the hot loop (only the `totXc4Kill` ledger
counter needs it).

---

## 5. COMPLEXITY / DATA STRUCTURES FOR A GPU PORT

LST's kernel is O(nPixelSeeds x nTrackCandidates) -- the thing to NOT reproduce. Each
prototype branch is linear (`main.cc:4920-4927`):

### 5.1 Pixel-anchored arm -- two flat arrays, no pair log, no N^2

1. **Shared-hit test.** Build `anchorHit`: a bitmask over pixel hit rows (host: hash set;
   GPU: a `char`/bit array sized `nPixelHits`, or a sorted-unique list + binary search).
   Kernel 1: one thread per anchor seed, write its <= 4 rows (`Params_pLS::kHits == 4`).
   Kernel 2: one thread per candidate seed, probe its <= 4 rows. **O(4 * nPls) total,
   no atomics needed beyond the byte writes.**
2. **Anchor dR^2 test.** An `(eta, phi)` hash grid with `cellW = max(0.01, sqrt(XCR2))`
   (`= 0.01` at the winner value), `nPhiCell = floor(2pi/cellW) = 628`, phi wrapped, key
   `ie * 1000000LL + ip`. Because `cellW >= sqrt(window)`, the 3x3 neighbourhood is
   **exact** -- no approximation. GPU: counting sort of the anchor seeds into cells
   (CSR), then one thread per candidate scanning 9 cells. Anchor counts per cell are ~1
   at `cellW = 0.01` in a PU200 event, so this is effectively O(nPls).

Note: this arm needs no `outTCs` traversal at all -- its anchor set is exactly
`plsOwned != 0`.

### 5.2 Bare-chain arm -- avoid materialising a pair log

The prototype consumes the full `ga.pairLog` (host `std::vector`, appended per scored pair).
On GPU do NOT materialise it. The threshold test and the `dR^2` window are both
**per-pair quantities computable at attach-scoring time** (the chain's innermost-T3 eta/phi
and the seed's eta/phi are both known then), while the only post-hoc facts are
"is this chain's TC actually emitted" and "is it seedless / not `-CCS`-suppressed" and
"is the seed still non-anchor". So:

* **Pass 1 (inside the attach scoring kernel).** For each scored `(chain c, seed p)` pair,
  evaluate `logit >= min(xcTheta, xcThetaT, xcThetaE)` (the loosest bin; per-bin refinement
  happens in pass 2) AND `dEta^2 + dPhi^2 < xcDR2Chain` using the chain's innermost-T3
  eta/phi. On pass, `atomicAdd`-append `(c, p, logit)` to a compact candidate buffer. The
  eta-bin threshold can also be applied here in full (it depends only on `ev.pLS_eta[p]`),
  which shrinks the buffer further.
* **Pass 2 (after delivery / `-CCS` / `-CC` resolve).** One thread per buffered row:
  `if (chainEmittedSeedless[c] && !plsOwned[p] && isQuad[p]) atomicOr(retire[p], 1)`.
  `retire` is a byte array, so a plain store is fine.
* This is **linear in the pairs the delivery already scored**, needs no chain->pair CSR,
  no `unordered_map`, and no candidate-vs-candidate loop anywhere.
* Buffer sizing: bound by `nScoredPairs`; the measured pass-1 acceptance is small (see 5.3).
* If you keep a CSR instead (host-like port), the key is the **chain row**, not the
  target-list position -- `pairLog.tgtRow` is already a chain row
  (`AttachDelivery.h:118`, `AttachDelivery.cc:86` uses `targetChains[pr.chainPos]`).

### 5.3 Measured per-event magnitudes (winner config, 977 events)

From `S/synth_ref/r_F3W977.log:1003-1005`:

```
XC etabins      -XCT 3.75 (|eta|<1.1) -XCT2 3.5 (1.1-1.7) -XCT3 3.75 (>1.7) | score source -XCG 0 (this (seed,chain) pair)
XC crossclean   -XC 3 (pix=1 chain=1) -XCT 3.75 -XCR2 1e-06 -XCW2 0.02
                | seeds retired=3349326 mean=3428.2 [pixHit=2968189/3038.1 pixDR=5668/5.8 chain=375469/384.3]
                | carried type-8 rows killed=8288 mean=8.5 | -ZP8 additions blocked=34576 mean=35.4
XC 4-layer arm  -XC4 1 -XC4T follow -XCT | (seed,4-layer chain) pairs logged=5584334 mean=5716
                | of the chain retirements, BY a 4-layer target=246248 mean=252.0
```

Reading:

* `xcRetired` set for **3428.2 seeds/evt**, of which pixel-shared-hit 3038.1, pixel-dR 5.8,
  bare-chain 384.3 (of that, 252.0 by a 4-layer anchor -- i.e. **65% of all bare-chain
  retirements come from `-XC4`**).
* **Effective row deltas: 8.5 carried type-8 rows + 35.4 `-ZP8` additions = 43.9 rows/evt.**
  The 78x gap is the missing `isDup` pre-skip (2.4). A GPU port that restricts candidacy to
  the surviving-seed mask does ~44 useful decisions per event.
* `-XC4` adds **5716 pairs/evt** to the pair log. At 16 B/row (`ScoredPair`) that is ~91 kB
  per event just for the 4-layer half -- the strongest argument for the pass-1 filtered
  compaction of 5.2 rather than a full pair log.
* The pixel-arm dR test fires only 5.8/evt against the shared-hit test's 3038.1/evt. It is
  nearly redundant (LST has the same property) but it is a verbatim LST behaviour; keep it,
  it is cheap.

---

## 6. ORDERING CONSTRAINTS AND TRAPS vs THE OTHER POST-M19 MECHANISMS

### 6.1 Hard ordering requirements

1. XC MUST run after **both attach stages** and after every mutation of `ga.plsOwned`
   (stage A `AttachDelivery.cc:117`, `-RD`/`-RDT` dedup `main.cc:4249`, stage B
   `AttachDelivery.cc:196`, `-CC` revoke `main.cc:4441`). Its anchor set and its candidate
   exclusion are both `plsOwned`.
2. XC MUST run after `outTCs`/`outTCChain` are final for chain and pT3-class rows -- i.e.
   after `-CCS` suppression, after the type-7 upgrade, after `-CC`, after K7-lite, after
   `-XP3`. It reads the EMITTED set, not the accepted set.
3. XC MUST run before the type-8 channel is realised: before the writer
   (`main.cc:5368`) and before the `-ZP8` push loop (`main.cc:5265-5285`).
4. `-XC4`'s score-only pass MUST run inside the attach block (before `-EX`, before
   assembly) because it needs `gap.pref` and the stage-A target machinery; and it MUST be
   mutually exclusive with any 4-layer DELIVERY stage's pair recording (`-a4`; see 4.2).
5. `ga.recordPairs` must be enabled whenever bit 1 of `-XC` is on (`main.cc:3189`). It is
   shared with `-CCS` and the attach confusion matrix.

### 6.2 TRAP -- a `-CCS`-suppressed chain can no longer retire seeds

`-CCS`/`-CCS2`/`-CCS3` (winner: 6.0 / 5.0 / unset) suppress an accepted BARE chain whose
best scored pair toward a pLS owned by a DIFFERENT chain is `>= ` the band bar
(`main.cc:4102-4123` build, `4167-4180` apply). The suppression is a `continue` BEFORE the
`outTCs.push_back` -- so that chain is absent from `outTCs` and the XC bare-chain arm cannot
see it. **`-CCS` and `-XC` bit 1 compete for the same duplicate cell
(seedlessChain + barePLS): every chain `-CCS` deletes is one fewer XC anchor.** The two
were tuned together at their winner values; retuning one requires re-measuring the other.
`-CCS` deliberately does not touch `plsBestChainLogit` (it was written at scoring time) so
`-RPSA` is unaffected (`main.cc:4170-4173`).

### 6.3 TRAP -- `-CC` + `-CCR 2` un-anchors a seed

`-CC 1 -CCN 1 -CCR 2` (winner) revokes a pT3-class delivery on hit overlap and, at
`-CCR 2`, sets `ga.plsOwned[p] = 0` AND `ga.plsBestT3Logit[p] = -inf` (`main.cc:4441-4451`).
Two XC consequences: (i) that seed **stops being a pixel-arm ANCHOR** and (ii) it **becomes
an XC CANDIDATE**. `-CCR 2` is exactly what makes the seed's carried type-8 row survivable,
so XC is the mechanism that then decides its fate. This is a real coupling, not a nuisance:
`-CCR 2` was chosen for its efficiency and XC is what keeps the duplicate cost bounded.

### 6.4 TRAP -- `-EX` mutates `nLayers` after `-XC4` picked its targets

`-XC4` selects `nLayers < 5` targets at `main.cc:3821-3822`; `-EX` chain extension runs
LATER (`main.cc:3914-4003`) and does `chains.nLayers[c] += nAddedIn + nAddedOut`
(`Extend.cc:444`), with `exp.minLayers = max(4, -EXL)` = 4 so 4-layer chains ARE extension
candidates. The XC arm's `t4Target` test reads the **post-extension** `nLayers`
(`main.cc:5056`). So a chain that was a 4-layer `-XC4` target and got extended to 5 layers
is in the pair log but is priced with `xcThetaOf(...)` rather than `-XC4T`. Harmless at the
winner config because `-XC4T` follows `-XCT` (so the two branches are the same number), and
harmless for the `totXc4Kill` ledger only to within that reclassification. **If `-XC4T`
were ever revived, this ordering would have to be pinned.**
Related: `-EX` never changes the chain's `eta`/`phi` used by the dR^2 test as long as the
INNERMOST member is unchanged -- the measured winner config has `inner=0` extensions
(`r_F3W977.log:997`: `outer=75855 inner=0`), so the scoring-time and emission-time chain
directions coincide. **A port that enables inner extension must recompute the chain
direction at emission time, or the pass-1 filtering of 5.2 becomes approximate.**

### 6.5 `-RPS` / `-RPSA` / `-RPST` interaction

`-RPS 1` (winner) plus `-RPSA 5.5` / `-RPST` (unset -> follows `-AT3`) is a SEPARATE seed
retirement channel: "the seed had a scored pair above its class margin but is not the
owner" (`main.cc:3580-3584`, `5257-5258`). Relationships:

* Both write into the same places (`m16RowSuppressed` / the `-ZP8` `drop` flag), and
  `m16RefreshSupp()` only ever ADDS, so the two are a union -- order-independent for the
  final row set.
* `-RPSA` keys on `ga.plsBestChainLogit` (per-seed best over any chain); XC bit 1 at
  `-XCG 0` keys on the per-PAIR logit against a specific delivered chain. `-XCG 1` collapses
  XC's score source onto the `-RPSA` quantity, which is why it is "looser by construction"
  (`main.cc:951-957`) -- and why the winner uses `-XCG 0`.
* `-RPSA 5.5 > -XCT 3.75`: the seed-retirement margin is TIGHTER than the crossclean
  threshold, deliberately (`main.cc:883` notes the two available fates of a bare seed are
  "`-XCT` via the A15 `-XC4` pair-log hole, or `-RPSA`, or nothing").
* Neither `-XC4` nor `-a4` stage A2 may touch `plsBestChainLogit`
  (`GeneralAttachParams::writeBestLogit`, `AttachDelivery.h:74-79`) so `-RPSA` stays
  independently priceable.

### 6.6 `-T3E` interaction

`-T3E 1` (winner) forces the bare-T3 stage B ON (`main.cc:2655`:
`doT3Stage = (t3StageEnable < -0.5f) ? (replT3 >= 0.5f) : (t3StageEnable >= 0.5f)`).
XC depends on stage B in two ways:

* Stage B is what produces the **type-5 pT3-class deliveries**, which XC treats as
  pixel-anchored (they are `outTCChain[j] == -1`, skipped by the bare-chain arm at
  `main.cc:5039`, and their seeds are anchors via `plsOwned`). This IS the LST pT3 arm.
* Stage B and `-CC` run BEFORE XC and can both add and remove anchors. XC must not be
  hoisted above them.
* `-XCD 2` exists precisely because the delivered pT3-class rows count as "covering" a sim
  whenever stage B is on (`main.cc:5322-5326`); `-XCD 1` would misattribute. Diagnostic only
  -- verified inert 33/33 (`a15_ref/STATUS.md:126`).

### 6.7 `-XCD` is inert

`-XCD 0|1|2` (`main.cc:950`, block `main.cc:5288-5361`) reads sim truth
(`ev.pLS_simIdxAll`, `ev.pLS_isDupAlgPass2`) and writes only `xcTruth[3][4]`. It changes no
decision and no output row (gate: 33/33 identical). **DO NOT PORT.**

---

## 7. EVERY NUMERIC CONSTANT, WINNER VALUE, AND SENTINEL

| flag | prototype var (`main.cc`) | default | **winner** | sentinel / resolution |
|---|---|---|---|---|
| `-XC` | `xcMode` (:936) | 0 (OFF, bit-identical) | **3** | integerised at `main.cc:2682`: `xcBits = (xcMode >= 0.5f) ? int(xcMode + 0.5f) : 0`; `xcPix = xcBits & 1`, `xcChain = xcBits & 2` (:2683-2684). Whole mechanism additionally gated on `attachMode == 4` (:4931). |
| `-XCT` | `xcTheta` (:937) | 0 | **3.75** | none (a real value). Barrel bin `\|eta\| < 1.1`. Set to 4 by the assembled BASE, overridden to 3.75. |
| `-XCT2` | `xcThetaT` (:946) | `1e9` | **3.5** | **`1e9` = "follow `-XCT`"**, resolved post-getopt at `main.cc:1359-1360`: `if (xcThetaT >= 1e8f) xcThetaT = xcTheta;`. Bin `1.1 <= \|eta\| < 1.7`. |
| `-XCT3` | `xcThetaE` (:947) | `1e9` | **3.75** (unset -> follows `-XCT`) | **`1e9` = "follow `-XCT`"**, resolved at `main.cc:1361-1362`: `if (xcThetaE >= 1e8f) xcThetaE = xcTheta;`. Bin `\|eta\| >= 1.7`. |
| `-XCR2` | `xcDR2Pix` (:948) | `1e-6` | **1e-6** | none. **LST's own value** (`TrackCandidate.h:399` pT3 arm, `:414` pT5 arm: `dR2 < 0.000001f`). |
| `-XCW2` | `xcDR2Chain` (:949) | `0.02` | **0.02** | none. **LST's own value** (`TrackCandidate.h:375`: `dR2 < 0.02f`). |
| `-XCG` | `xcGlobalScore` (:957) | 0 | **0** | `>= 0.5f` = on (:5051). 0 = this pair's logit; 1 = `ga.plsBestChainLogit[p]`. |
| `-XC4` | `xcT4` (:969) | 0 (bit-identical) | **1** | `>= 0.5f` = on (:3819, :5020). |
| `-XC4T` | `xcT4Theta` (:970) | `1e9` | **unset (follow `-XCT`)** | **`1e9` = "follow `-XCT`"**, resolved LAZILY at the use sites, NOT post-getopt: `main.cc:5020-5021` (join the prefilter floor) and `main.cc:5058` (`xcT4Theta < 1e8f ? xcT4Theta : xcThetaOf(...)`). **DEAD END -- do not port (4.4).** |
| `-XCD` | `xcDiag` (:950) | 0 | **0** | `>= 0.5f` = on; `>= 1.5f` = mode 2. Diagnostic, do not port. |
| (hardcoded) | eta-bin boundaries | -- | **1.1 and 1.7** | `main.cc:5023-5024` (`xcThetaOf`), on `\|ev.pLS_eta[p]\|`. The scoreboard's own regions; LST's analogue is `dnn::kEtaSize = 0.25` x `kEtaBins = 10` (`P/interface/alpaka/Common.h:66-67`). |
| (hardcoded) | pixel-arm grid cell | -- | **`max(0.01f, sqrt(xcDR2Pix))` = 0.01** | `main.cc:4942`; `nPhiCell = max(1, int(6.28318531f / cellW))` = 628 (:4943); cell key `ie * 1000000LL + ip` (:4958, :5001). Implementation detail only -- the 3x3 scan is exact for any `cellW >= sqrt(window)`. |
| (hardcoded) | pixel-hit overlap threshold | -- | **>= 1 shared row** | `main.cc:4967-4972`; LST `pixelHitsOverlapAny`, `TrackCandidate.h:154-170`. NOT the `>= 2` used by `-RD` (`main.cc:3522-3534`, `if (++shareCnt[q] >= 2) return true;` at :3530). |
| (inherited) | `-XC4` prefilter windows | `prefDPhi = 0.4`, `prefDTanL = 0.6` | same | `PixelAttach.h:83-84`; `-XC4` copies `gap.pref` wholesale and changes only `minChainLayers` 5 -> 1 (`main.cc:3823-3824`). |

Related non-XC winner values XC is coupled to (for the record): `-a 5.0 -a2 5.0 -a3 6.0`
(delivery margins; note `-XCT 3.75 << -a 5.0`, the deliberate looseness),
`-RPSA 5.5`, `-CCS 6.0 -CCS2 5.0`, `-CC 1 -CCN 1 -CCR 2`, `-T3E 1`, `-T3F 0.10`.

---

## 8. MINIMAL PORT CHECKLIST

1. Keep the launch slot: after all TC emission, immediately before the bare-pLS TC append
   (LST's own `CrossCleanpLS` slot, `LSTEvent.dev.cc:3203`).
2. Output = one `char` per pLS (`xcRetired`), consumed only by the bare-pLS TC append.
3. Pixel-anchored arm: anchor set = seeds our deliveries own. Shared-hit bitmask (>= 1 of
   <= 4 rows) OR `(eta,phi)` grid `dR^2 < 1e-6`, both on SEED eta/phi. Verbatim LST windows.
4. Bare-chain arm: filtered pair compaction at attach-scoring time (threshold on
   `|seed eta|`-binned `-XCT`, window `dR^2 < 0.02` against the chain's innermost-T3
   direction), resolved after delivery against "emitted && seedless && not `-CCS`-suppressed"
   and "seed still not owned && isQuad".
5. `-XC4` folded in unconditionally: enumerate 4-layer accepted chains into the same
   candidate stream, score-only. Do NOT port `-XC4T`, `-XCD`, or `-XCG 1`.
6. Restrict candidacy to the surviving-seed mask to drop ~78x of inert work (2.4, 5.3).
7. Constants to hardcode: `1e-6`, `0.02`, `>= 1` shared pixel hit, eta bins `1.1 / 1.7`,
   thresholds `3.75 / 3.5 / 3.75`. That is **three tuned numbers** (two distinct) plus two
   LST-provenance windows.
