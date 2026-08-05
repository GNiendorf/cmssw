# SPEC_T3E_CC — stage B (pT3-class delivery), attach contention (-CC/-CCN/-CCR), seed retirement (-RPS/-RPSA)

Port target: `P/src/alpaka` (P = `RecoTracker/LSTCore`). Reference: `S/protoFINAL2`
(`main.cc` 5995 lines, `AttachDelivery.{h,cc}`, `PixelAttach.{h,cc}`, `PixelAttachPairs.h`, `Stages.h`).
Winner = CHAINFINAL2 / F3W977. All line numbers are `protoFINAL2` unless prefixed.

Notation: **S** = `.../LSTCore/standalone`, **P** = `.../LSTCore`.

---

## 0. WINNER CONFIG — FULLY RESOLVED VALUES

Command line = `synth_ref/syn_run.sh` prefix + overrides. Assembly order (later wins):
`ANCHOR CTL FLAGSHIP M19 CFF POSTDELP2 BASE <overrides>`
(`syn_run.sh:16-24`; `BASE = -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`;
overrides = `-T3F 0.10 -XC4 1 -RPSA 5.5 -EXR 4.0 -a 5.0 -a2 5.0 -a3 6.0 -CCS 6.0 -CCS2 5.0 -XCT 3.75 -XCT2 3.5 -MRB -1.2 -MRT -1.2`).

Every constant in scope, post-resolution:

| flag | var (`main.cc`) | decl | winner value | notes |
|---|---|---|---|---|
| `-A` | `attachMode` | — | 4 | gates everything here |
| `-a` | `thetaAttach` (`gap.pref.thetaAttach`) | — | **5.0** | chain-target delivery margin, `\|eta\|<1.1`. getopt `a:`; `-a` given 4x (999/8/6.875/5.0), last wins |
| `-a2` | `aThetaT` | 864 | **5.0** | 1.1<=\|eta\|<1.7 |
| `-a3` | `aThetaE` | 865 | **6.0** | \|eta\|>=1.7 |
| `-a4` | `a4Theta` | 888 | **1e9 = OFF** | stage A2 is NEVER CONSTRUCTED. Do not port. Plan: "drop -a4 family" |
| `-AT3` | `thetaAttachT3` | 857 | **6.0** (default) | bare-T3 delivery margin, GLOBAL (no eta bands) |
| `-T3E` | `t3StageEnable` | 1002 | **1** | force stage B on |
| `-RT3` | `replT3` | 849 | **1** | wholesale drop of carried type-5 rows |
| `-T3F` | `t3FakeMax` | 899 | **0.10** | stage-B target admission on `t3_fakeScore` |
| `-RPS` | `replPls` | 856 | **1** | retire contested carried type-8 rows |
| `-RPSA` | `rpsThetaChain` | 896 | **5.5** | chain-side RETIREMENT bar (global; NOT banded) |
| `-RPST` | `rpsThetaT3` | 897 | **unset -> 6.0** (= `-AT3`) | DEAD END, see §5.4 |
| `-CC` | `ccMode` | 1012 | **1** | OT-side hit-overlap contention on stage-B deliveries |
| `-CCG` | `ccGran` | 1013 | **1** (default) | unit = MD row |
| `-CCN` | `ccMinShared` | 1016 | **1** | kill on >= 1 shared unit |
| `-CCP` | `ccPreclaim` | 1021 | **1** (default) | delivered TCs + surviving carried rows pre-claim |
| `-CCK` | `ccOrder` | 1022 | **0** (default) | keep-best key = attach logit desc |
| `-CCR` | `ccRelPls` | 1023 | **2** | revoke-release: release pLS AND erase its bare-T3 evidence |
| `-RD` | `seedDupClean` | 901 | **1** | seed-family dedup, stage A |
| `-RDT` | `rdT3` | 1030 | **-1 -> follows -RD = ON** | seed-family dedup, stage B |
| `-D4` | `dcaAttach4` | 900 | **1e9 = OFF** | attach dca eligibility gate |
| `-CF` | `candMode` | 995 | **1** = binned prefilter | Alpaka equivalent = the K8a grid |
| `-CFC` | `candChainToo` | 1001 | **1** | chain targets use the index too |
| `-PU` | `preClaimMode` | — | **1** | `-PU 2` in ANCHOR, `-PU 1` in STACK |
| `-XC` | `xcMode` | 936 | **3** (both bits) | seed crossclean (adjacent; §6) |
| `-XCT/-XCT2/-XCT3` | `xcTheta/T/E` | 937,946,947 | **3.75 / 3.5 / ->3.75** | |
| `-XC4/-XC4T` | `xcT4/xcT4Theta` | 969,970 | **1 / unset -> per-eta -XCT** | `-XC4T` DEAD END, delete |
| `-CCS/-CCS2/-CCS3` | `ccsTheta/T/E` | 877-879 | **6.0 / 5.0 / 1e9 = OFF** | chain-loser suppression (stage A side) |
| `-ZP8` | `auZp8` | 855 | **6** | post-deletion bare-pLS universe; **holds RPS predicate copy #2** |

### 0.1 Sentinel semantics (three distinct dialects — do not unify by accident)
- `1e9` written, tested as `>= 1e8f`: "unset, follow another flag" — `-a2/-a3/-a4/-a42/-a43`,
  `-RPSA/-RPST`, `-XCT2/-XCT3/-XC4T`, `-D4`.
- `1e9` written, tested as `< 1e8f`: "OFF, no fallback" — `-T3F` (`AttachDelivery.cc:135`),
  `-a4` (`main.cc:3849`), `-CCS/-CCS2/-CCS3` (`main.cc:4106`). **`-CCS3` deliberately has no
  follow-the-barrel fallback** (comment `main.cc:875-876`: the endcap dup rate is already below
  LST and must not move by accident).
- `-1` = "follow another flag": `-T3E` (`< -0.5f`), `-RDT` (`< -0.5f`).

### 0.2 getopt PRE-SCAN ordering constraints (verbatim from the prototype comments)
`main.cc:1040-1346` is a hand-rolled pre-scan that consumes `flag value` pairs and hands a
COMPACTED argv to `getopt(nArgs, args.data(), "i:t:o:n:m:l:e:L:T:F:G:A:a:B:W:H:D:K:R:S:X:Y:Z:Ph")`
(`main.cc:1379`). The scan uses EXACT STRING EQUALITY in an if/else-if chain, so relative order is
only documentation — except that every multi-char flag whose first char is a getopt option letter
MUST be in the pre-scan at all:
- `-a2/-a3/-a4/-a42/-a43` before getopt's `a:` — otherwise `-a2` parses as `-a` with value `"2"`
  and silently moves the BARREL margin (`main.cc:1228-1239`, explicit comment).
- `-RPSA/-RPST` longest-first before `-RPS` (`main.cc:1219-1225`); `-RT5/-RT3/-AT3` before getopt
  `R:`/`A:` (`main.cc:1213-1227`).
- `-CCS2/-CCS3/-CCS/-CCG/-CCN/-CCP/-CCK/-CCR` before `-CC` (`main.cc:1283-1304`); all before
  getopt's `C`-consuming letters.
- `-T3F` and `-T3E` before getopt `T:` (`main.cc:1277-1282`).
- `-XC4T/-XC4/-XCR2/-XCW2/-XCT2/-XCT3/-XCT/-XCD/-XCG` before `-XC` and before getopt `X:`
  (`main.cc:1307-1328`).
- `-MRB/-MRT/-M4B/-M4T` before `-MR`/`-M4` (`main.cc:1095-1106`).
- `-PU` handled FIRST, as an int, so getopt's no-arg `-P` can never swallow it (`main.cc:1044-1056`).
- `-CFM` is a PATH and is consumed explicitly, not through the float table (`main.cc:1255-1262`).

In the Alpaka port this whole layer disappears (values live in `ChainConfig`), but the
**resolution** layer does not — see §5.5.

---

## 1. FULL PER-EVENT STAGE ORDER (with file:line anchors and invariants)

All inside the `-A 4` (`attachMode == 4`) hybrid path. Ordering is the M16b staging fix
(rationale block `main.cc:3106-3147`).

| # | stage | anchor | notes |
|---|---|---|---|
| 0 | build `GeneralAttachParams gap` | `3148-3169` | `gap.pref.thetaAttach = -a`; `gap.thetaAttachT3 = -AT3`; `gap.thetaAttachT/E = aThetaT/aThetaE`; `gap.t3FakeMax = -T3F` (`3153`) |
| 1 | `gaInit(ev, chains, ga)` | `3183` | sizes/clears all decision state (§4.1) |
| 1a | `ga.recordPairs = attachCM \|\| xcChain \|\| ccsOn` | `3189` | **TRUE in the winner** (`-XC 3` sets `xcChain`, `-CCS` set). This is the pair log — see §7 for why it must NOT be ported as a log |
| 1b | `ap.dropPartOfPT5 = false` (`-RT5 1`), `ap.dropPartOfPT3 = false` (`-RT3 1`) | `3194-3197` | wholesale replacement removes the rows the pixel-consumed drop protects |
| 2 | helpers `plsPixelHits` / `rowPls` | `3478-3504` | `plsPixelHits(p)` = `trk.see_hitIdx[ev.pLS_seedIdx[p]]` entries with `see_hitType == Pixel`. `rowPls(it,ty)`: ty7 -> `pT5_plsIdx[tc_pt5Idx]`, ty5 -> `pT3_plsIdx[tc_pt3Idx]`, ty8 -> `tc_plsIdx` |
| 3 | seed-family map `hit2keptPls` + `seedFamilyDup/Keep` | `3519-3539` | pixel hit row -> kept-owner pLS list. `seedFamilyDup(p)` = p shares **>= 2** pixel hit rows with an already-kept owner. **The map PERSISTS across stage A and stage B** (comment `3515-3518`) — this is what stops a track delivered as pT5 from also being delivered as pT3 by a sibling seed |
| 4 | **wholesale suppression set** `m16RowSuppressed` | `3557-3566` | ty7 && `-RT5` -> 1; ty5 && `-RT3` -> 1. Attach-independent, so it can run pre-K9 |
| 5 | `m16RefreshSupp()` lambda DEFINED (RPS predicate copy #1) | `3569-3590` | §5.1 |
| 6 | pre-claim owner list (`-PU 1`) | `3592-3633` | built from POST-suppression carried rows only (`3602`). No attached chains appended (`3624-3628`) |
| 7 | **K9 arbitration** over ALL chains, no exclusions | `3644-3647` | produces `accepted` |
| 8 | `-Q4/-Q5` post-claim floors, `-DD` dedup | `3648-3786` | may shrink `accepted` |
| 9 | **STAGE A** `gaStageChains(ev, chains, thetaPass, cfHyb, gateLogit, gap, ga)` | `3798-3810` | `thetaPass` = accepted chains with `nLayers >= 5` and `chainDca(c) < -D4` (`3799-3807`) |
| 10 | `-XC4` score-only 4-layer pass | `3819-3834` | appends `(pLS, 4-layer chain)` rows to `ga.pairLog` ONLY. Writes NOTHING else: not `chainPls`, not `plsOwned`, not `plsBestChainLogit`. `p4.minChainLayers = 1` is the only param delta |
| 11 | stage A2 (`-a4`) | `3849-3876` | **INERT in winner** (`a4Theta = 1e9`). Do not port |
| 12 | `-RD` seed dedup, stage A | `3883-3909` | owners sorted (logit desc, `chains.stableKey` asc, index asc) (`3891-3897`); a dup owner gets `chainPls[c] = -1`, `plsOwned[p] = 0` |
| 13 | `chainAttachPls = ga.chainPls; m16RefreshSupp()` | `3910-3911` | **first** retirement refresh |
| 14 | `-EX` chain extension | `3928-4005` | reads `m16RowSuppressed` (`3969`) |
| 15 | `k10AssembleChainTCs` | `4006` | `chainTCs` in `accepted` order, skipping `nLayers < 4` |
| 16 | `-CCS` loser table build | `4106-4127` | invert stage-A grant table (pLS -> owning chain), one pass over `ga.pairLog` keeping per-chain best logit toward a pLS owned by a DIFFERENT chain |
| 17 | **assembly loop**: emit chain TCs; `-CCS` suppression; type-7 in-place upgrade | `4128-4209` | `-CCS` at `4173-4180` (bare chain only, band on emitted `\|eta\|`). Upgrade at `4181-4201`: `type = 7`, `pt = ev.pLS_pt[p]`, eta/phi stay the chain's, pixel hits PREPENDED |
| 18 | **STAGE B** `gaStageT3(ev, chains, accepted, cfHyb, gateLogit, gap, ga)` | `4221-4223` | gated by `doT3Stage` (`2655`) |
| 19 | `-RDT` seed-family dedup, stage B | `4231-4253` | owners sorted (logit desc, t3 row asc); dup -> `t3Pls[t] = -1`, `plsOwned[p] = 0` |
| 20 | **`-CC` contention** + **type-5 emission** (one fused loop) | `4293-4489` | §3 |
| 21 | `m16RefreshSupp()` | `4496` | **second** refresh; now sees stage-B ownership AND the bare-T3 evidence |
| 22 | attach confusion matrix | `4505-...` | `PROTO_ATTACH_CM` only, no decisions |
| 23 | K7-lite (`-A 2` only), `-XP3` | `4794-4906` | |
| 24 | `-XC` seed crossclean | `4931-5091` | §6 |
| 25 | `-ZP8` bare-pLS TC additions (RPS copy #2) | `5112-5286` | §5.2 |
| 26 | `-XCD` truth partition (RPS copy #3) | `5297-5361` | diagnostic only; `-XCD` unset in winner |
| 27 | `writer.fillEventHybrid(..., rowMaskOut = &m16RowSuppressed)` | `5366-5370` | the retirement is applied HERE, once, from the row mask |

### 1.1 Invariants between stages (all load-bearing)

- **I1 — ONE pLS, ONE OWNER, globally across target kinds.** `ga.plsOwned` is the single
  authority. Stage A sets it (`AttachDelivery.cc:117`); stage B refuses any pLS with
  `plsOwned != 0` (`AttachDelivery.cc:165-166`); stage B sets it on win
  (`AttachDelivery.cc:196`). Deliberately NOT a global logit sort: a pLS whose best T3 pair
  outscores its best chain pair still goes to the chain (per-length ordering,
  `AttachDelivery.h:36-49`).
- **I2 — bareness is defined against the K9-ACCEPTED set, not the welded set.** A T3 welded into
  a chain that K9 then REJECTED is still bare and still attachable
  (`PixelAttach.h:129-139`, `k8BuildBareT3Mask` at `PixelAttach.cc:804-817`). This is the
  population LST recovers with its superbin machinery; using the theta-passing set would throw
  it away.
- **I3 — stage B must run AFTER K9 and AFTER stage A's contention + `-RD` are final, and BEFORE
  the carried-row retirement.** Both because I2 needs `accepted`, and because the retirement
  (`m16RefreshSupp`) has to see the seeds stage B owns.
- **I4 — `plsBest*Logit` is written for EVERY SCORED PAIR, before any threshold and before
  contention.** Stage A: `AttachDelivery.cc:87-89` (before the band threshold at `97`).
  Stage B: `AttachDelivery.cc:163-164` (before the `plsOwned` skip at `165` and before the margin
  at `167`). This is what makes "had real OT evidence but is not the owner" expressible, and it
  is exactly what makes `-CCR 1` a no-op (§3.3).
- **I5 — the `-XC4` pass must not touch delivery state.** `writeBestLogit` semantics are
  reproduced by simply not writing (`main.cc:3830-3832` only pushes to `pairLog`), so `-RPSA`
  retires exactly the seeds it retired without `-XC4`, and the two mechanisms stay independently
  priceable.
- **I6 — `m16RefreshSupp` is monotone (only ever ADDS suppressions) and idempotent.** Already-set
  rows are skipped (`3571-3572`). It is therefore safe to call after every attach stage. This
  monotonicity is precisely why `-CCR 1` cannot un-retire anything.
- **I7 — `-CCR 2`'s erasure of `plsBestT3Logit` must be visible to ALL copies of the RPS
  predicate.** In the prototype this is automatic (shared `ga` state mutated at `4463` BEFORE the
  `m16RefreshSupp()` at `4496`). In the port this becomes an ordering requirement: revoke writes
  the shared per-pLS evidence array, and it must happen before the retirement kernel reads it.
- **I8 — the `chainTCs` / `accepted` lockstep.** `tcPos` advances even for a `-CCS`-suppressed
  row (`4173-4179`, `continue` AFTER `ChainTC& ctc = chainTCs[tcPos++]`). Any port that filters
  earlier must preserve this.
- **I9 — the residual approximations, stated not hidden** (`main.cc:3141-3147`,
  `4491-4495`): both attach stages decide after K9, so delivery pixel hits do not participate in
  the claim, and contention-suppressed carried rows DID pre-claim (chains faced a slightly
  stricter claim than the final output implies). Conservative direction. At real integration all
  universes can resolve before the single claim.

---

## 2. (A) STAGE B — THE pT3-CLASS DELIVERY

### 2.1 Enablement
```
doT3Stage = (t3StageEnable < -0.5f) ? (replT3 >= 0.5f) : (t3StageEnable >= 0.5f)   // main.cc:2655
```
Winner: `-T3E 1` -> true (and `-RT3 1` would give true anyway). `-T3E` exists because delivering
our pT3 class while LST's carried type-5 rows are also kept double-counts the class
(`main.cc:2652-2654`, `454-455`).

### 2.2 Target universe (three filters, in this order)
1. **Bareness** (`k8BuildBareT3Mask`, `PixelAttach.cc:804-817`):
   `mask.assign(nT3, 1)`, then for every `c` in `accepted`, for every node item
   `chains.items[chains.offsets[c]..offsets[c+1])`, `mask[t] = 0`.
   No other filter — in particular `t3_partOfPT3` / `t3_partOfPT5` triplets STAY IN
   (`PixelAttach.h:134-135`): those are exactly the tracks the general attach is meant to deliver
   itself.
2. **`-T3F` target admission** (`AttachDelivery.cc:132-144`), applied to the MASK so cut targets
   never reach the candidate finder, never get scored, and **never write `plsBestT3Logit`**:
   ```
   if (params.t3FakeMax < 1e8f)
     for each t with bareMask[t]:
       if (t >= nScore || !(ev.t3_fakeScore[t] <= params.t3FakeMax)) bareMask[t] = 0;
   ```
   Note the `!(x <= v)` form: NaN is REJECTED. Winner `t3FakeMax = 0.10`.
   `t3_fakeScore` is computed at T3 BUILD TIME and survives the P2.7 deletion set
   (`main.cc:392-393`) — this is why the lever adds no new dependency.
   **Alpaka equivalent: `triplets.fakeScore()[t3]`, already mirrored as chain node feature 12
   (`P/src/alpaka/ChainEdges.h:393`).**
3. **Chain-target list is intentionally EMPTY for stage B** (`AttachDelivery.cc:148-150`):
   `k8EnumeratePrefilteredPairsGeneral(ev, chains, kNoChains, cf, gateLogits, bareMask, pref, pairs)`.
   Stage A already resolved the chain universe and its decisions are final.

Winner sizes: ~15.9k bare-T3 targets/evt at `-T3F` OFF (`t3attach_ref/STATUS2.md` pairdump:
targets 15873, pairs 1.16e6/evt).

### 2.3 Target-side feature record (`makeT3Pre`, contract at `PixelAttach.h:28-45`)
Head input vector is 19 wide (`kAttachFeat = 19`), slots 0-17 IDENTICAL in definition to the chain
path. Target slots for a bare T3:

| slot | value |
|---|---|
| 7 `fitKappaSigned` | `rotSign / t3_radius`; `rotSign` = sign of z-component of `cross(md0->md1, md1->md2)`, collinear -> +1 (Features.cc node convention verbatim) |
| 8 `chainTanLambda` | `dz02 / ds02` over the three anchors (`ds02` = xy chord length md0->md2) |
| 9 `innermostLayer` | `md_layer[md0]` |
| 10 `nLayers` | **3** (constant) |
| 11 `chainGateLogit` | **0** — structurally absent; feature 18 is what tells the head slot 11 is a zero |
| 18 `targetType` | **1** = `kAttachTargetT3` (chain = 0) |

Prefilter geometry: `rtInner`/`zInner` = md0 anchor rt/z; `chordPhi` = `atan2(md1.anchor - md0.anchor)`
**in float** (the T3 path differs from the chain path here, which accumulates in double —
`ChainAttachT3.h:206-208`); centre = the T3's own circle-fit centre, non-finite -> `centerValid = 0`.

The prefilter is IDENTICAL for both target kinds: pLS circle propagated to the TARGET's innermost
anchor rt, then the same `|dTanLambda| <= prefDTanL (0.6)` / `|dPhiAtInnermost| <= prefDPhi (0.4)`
windows (`PixelAttach.h:25-27`, `AttachParams` `83-84`).

### 2.4 Scoring + admission (`gaStageT3`, `AttachDelivery.cc:155-176`) — exact order
For each prefiltered pair `pr`:
```
lo = attachLogit(pr.f);                    ++io.nScored;
if (recordPairs) pairLog.push_back({kAttachTargetT3, pr.t3Row, pr.plsRow, lo});
p = pr.plsRow;  if (p out of range) continue;
if (lo > io.plsBestT3Logit[p]) io.plsBestT3Logit[p] = lo;   // I4: BEFORE everything else
if (io.plsOwned[p]) continue;                               // I1
if (lo < params.thetaAttachT3) continue;                    // -AT3 = 6.0, GLOBAL, no eta bands
t = pr.t3Row;  if (t out of range) continue;
if (io.t3Pls[t] < 0 || lo > io.t3Logit[t]) { io.t3Pls[t] = p; io.t3Logit[t] = lo; }
```
Per-target pick is `>` (strict), so the FIRST-enumerated pair wins a tie. In the analytic scan and
in the binned index the candidate list is ascending in pLS row, so "first wins" == "lower pLS row
wins". **A GPU cell-walk is NOT ascending in `p`, so the port must make the tiebreak explicit:
`(lo == best && p < bestPls)`.** (The existing `ChainAttachT3Score` already does this correctly,
`P/src/alpaka/ChainAttachT3.h:340`.)

### 2.5 pLS contention among stage-B targets (`AttachDelivery.cc:178-199`)
1. Compact to the BIDDING targets (`t3Pls[t] >= 0`) in ascending T3 row, so the shared resolver's
   "earlier position keeps the tie" becomes "lower T3 row wins".
2. `resolveContention(tgtPls, tgtLogit)` (`AttachDelivery.cc:17-39`): map pLS -> holding position;
   higher logit wins, tie keeps the earlier position; the loser is set to `(-1, -inf)`.
3. Write back; on win set `plsOwned[p] = 1`, `++nT3Attached`.

### 2.6 Stage-B seed-family dedup (`-RDT`, `main.cc:4231-4253`)
```
doRdT3 = (rdT3 < -0.5f) ? (seedDupClean >= 0.5f) : (rdT3 >= 0.5f)     // winner: ON
```
Owners = `{t : t3Pls[t] >= 0}` sorted by (`t3Logit` desc, `t` asc). Walk in order:
`seedFamilyDup(p)` -> `t3Pls[t] = -1; plsOwned[p] = 0; ++nSeedDedupT3` else `seedFamilyKeep(p)`.
Then `ga.nT3Attached -= nSeedDedupT3`. **Against the SAME hash map stage A left behind** — that is
the cross-class guard.

### 2.7 EMITTED TC CONTRACT — delivered pT3-class row (`main.cc:4470-4488`)
```
otc.type   = 5;                  // pT3-class == LSTObjType::pT3
otc.deliv  = kDelivAttachT3;     // provenance mark (compare_types only)
otc.pt     = ev.pLS_pt[p];       // pLS ptIn  -- pixel pt is better measured
otc.eta    = ev.t3_eta[t];       // THE T3's eta
otc.phi    = ev.t3_phi[t];       // THE T3's phi
hits       = plsPixelHits(p)  [HitType::Pixel]      // FIRST, in seed order
           ++ t3OtHits         [HitType::Phase2OT]  // THEN, 6 rows
otc.nhitOT = 6;
outTCs.push_back(otc);  outTCChain.push_back(-1);   // -1 = not chain-backed
```
`t3OtHits` (built at `4425-4432`) = for each of `md0,md1,md2` in that order:
`ev.md_anchorHitIdx[md]`, `ev.md_otherHitIdx[md]` — i.e. anchor-then-other per MD, MD order
inner->outer. Six rows, no dedup.

**Provenance note for the port:** `outTCChain = -1` is what makes the `-XC` bare-chain arm skip
these rows (`main.cc:5039-5040`: "a delivered pT3-class row: pixel-anchored, not here").

### 2.8 What stage B RETIRES
Nothing directly. Stage B only writes `t3Pls`, `t3Logit`, `plsOwned`, `plsBestT3Logit`. All
retirement happens in `m16RefreshSupp()` at `main.cc:4496` — see §5.

---

## 3. (B) THE ATTACH CONTENTION MECHANISM (`-CC`, `-CCN`, `-CCR`)

Site: `main.cc:4254-4489`. Rationale block `4254-4292`. It exists because bare-T3 deliveries are
decided AFTER the K9 claim and therefore never compete for hits with anything — not with the
assembled chain TCs and not with each other. Measured consequence: 472 delivered rows/evt vs LST's
~150, 82% of them for a sim another TC already delivers, 5.5x duplicate rate.

### 3.1 TWO HARD STRUCTURAL CONSTRAINTS (maintainer requirements, not parameters)
- **Ownership-map based, NEVER pairwise.** Each candidate looks up ITS OWN units in ONE map and
  decides alone. Cost is linear in the candidate's unit count (3 MDs), never quadratic in
  candidates. This is a TIMING requirement as much as a physics one (LST's pairwise CrossClean
  loops explode in a jet core). **There is no candidate-vs-candidate comparison anywhere.**
- **NO PROXIMITY CRITERIA.** LST's own `CrossCleanpT3` kills a pT3 whose pixel direction is within
  `dR^2 < 1e-5` of a pT5's. That shape is explicitly NOT copied. Shared structure only.

### 3.2 `-CCN 1` GRANULARITY, EXACTLY
- `ccMd = (ccGran >= 0.5f)` -> winner TRUE: the **unit is the MD ROW** (`main.cc:4298`).
- `ccNeed = max(1, (int)(ccMinShared + 0.5f))` -> winner **1** (`main.cc:4299`).
- A delivery's unit set (`main.cc:4434-4443`):
  - `-CCG 1`: `ccUnits = {md0, md1, md2}` filtered to `0 <= md <= ccMaxUnit` — **exactly 3 units**.
  - `-CCG 0`: `ccUnits = t3OtHits` (6 OT hit rows).
- Verdict (`main.cc:4444-4447`):
  ```
  nShared = count of u in ccUnits with ccClaimed[u] != 0
  if (!ccUnits.empty() && nShared >= ccNeed)  -> REVOKE
  else                                        -> claim all ccUnits, then EMIT
  ```
- So `-CCN 1` at `-CCG 1` means: **a delivery dies if ANY ONE of its three MDs is already
  claimed.** The scale is: 1 = any shared MD, 2 = "2 of 3 == same track" (the earlier default and
  the maintainer's stated rule), 3 = identical MD triple only.
- Why MD and not the chain claim budget: the sibling recon reused the CHAIN claim budget
  (`<= 2 hits`, `<= 20%`), tuned for 10-14-hit objects, and killed signal with the duplicates
  (unique pT3-only sims recovered collapsed 241/265 -> 15/218). A 6-hit / 3-MD T3 shares MDs with
  other T3s by the nature of the graph, so the right unit is the MD and the right rule is
  COUNTING (`main.cc:4285-4292`).
- `ccMaxUnit` sizing (`main.cc:4302-4319`): at `-CCG 1` it is `nMDs - 1`; at `-CCG 0` it is the max
  over `md_anchorHitIdx/md_otherHitIdx`, `t5_hitIndices`, `pT3_otHitIndices` and every emitted
  `OutTC`'s Phase2OT rows.

### 3.2.1 Pre-claim (`-CCP 1`) — what writes into the map before the sweep (`main.cc:4324-4402`)
- **Assembled chain TCs** (bare AND in-place-upgraded type-7 alike): at `-CCG 1` their MD lists
  come from the chains' own dedup MD CSR, `chains.mdItems[chains.mdOffsets[c] .. offsets[c+1])`,
  with `outTCChain[j]` mapping an emitted TC back to its chain row (`4332-4338`). At `-CCG 0`, every
  `HitType::Phase2OT` row of every emitted `OutTC` (`4342-4345`).
- **Surviving carried pixel rows** (`4347-4401`): for every `it` with `!m16RowSuppressed[it]`,
  ty7 -> `t5_hitIndices[pT5_t5Idx[tc_pt5Idx[it]]]`, ty5 -> `pT3_otHitIndices[tc_pt3Idx[it]]`.
  These expose HIT rows only, so at MD granularity they are routed through a `hit2md` table built
  ONCE per event (`4352-4371`): `hit2md[md_anchorHitIdx[m]] = m`, `hit2md[md_otherHitIdx[m]] = m`.
  Still one map lookup per unit; never a pairwise loop.
- Note the `if (!ccMd || true)` at `4351` — the block always runs; the branch is vestigial.

### 3.2.2 Sweep order (`-CCK 0`, `main.cc:4403-4417`)
`t3Deliv` = `{t : t3Pls[t] >= 0}` (built at `4293-4296`, ascending T3 row), sorted:
`ccKey == 0` -> `t3Logit` desc, ties on lower T3 row; `1` -> `pLS_pt` desc; `2` -> T3 row asc
(an order-free control). Ties ALWAYS break on the lower T3 row, so the sweep is deterministic.

### 3.3 `-CCR` — REVOKE-RELEASE SEMANTICS (the documented failure mode)

On revoke (`main.cc:4448-4465`):
```
++nCCDropped;
ga.t3Pls[t] = -1;                       // the delivery is revoked (ALWAYS)
if (ccRelPls >= 0.5f) {                 // -CCR 1 or 2
  ga.plsOwned[p] = 0;                   // release ownership
  if (ccRelPls >= 1.5f)                 // -CCR 2 ONLY
    ga.plsBestT3Logit[p] = -inf;        // erase the bare-T3 EVIDENCE
}
continue;                               // no TC emitted
```

**`-CCR 0`** — delivery revoked, `plsOwned[p]` stays 1. The seed is retired by the `plsOwned` term
of the RPS predicate. No pT3 row, no bare-pLS row. Pure loss.

**`-CCR 1` (the historical default, what M9 shipped) — MEASURED NO-OP, and this is the failure
mode.** Releasing `plsOwned[p]` is supposed to hand the pLS back to the carried universe so its
type-8 row survives. It does not, for two independent reasons:
1. `m16RefreshSupp()` is **monotone** — it only ever ADDS suppressions (I6). If a previous refresh
   (`main.cc:3911`) already suppressed the row, releasing ownership cannot un-suppress it.
2. More importantly, the RPS predicate has a SECOND term:
   `ga.plsBestT3Logit[pls] >= rpsThetaT3` (`main.cc:3584`). `plsBestT3Logit` is recorded for
   **EVERY SCORED PAIR**, not only for owners (I4, `AttachDelivery.cc:163-164`). The revoked
   delivery's own winning pair had `lo >= thetaAttachT3` by construction, and
   `rpsThetaT3` defaults to `thetaAttachT3`, so the term is **necessarily true** for exactly the
   seeds `-CCR 1` is trying to save.

   Net effect: the revoked seed **disappears entirely** — no pT3-class row AND no carried type-8
   row. That is the documented failure mode: *"a refused delivery must genuinely leave its seed
   alive"* — `-CCR 1` does not.

   Recorded verbatim: `main.cc:4451-4457`, `main.cc:1023-1029`,
   `S/t3attach_ref/STATUS2.md:125-127` ("-CCR 2 is the only version of 'release' that
   releases"), and the same defect class generalized in `S/a03_ref/STATUS.md:74-78`
   ("otherwise a delivery the endcap margin refused would still have its seed retired
   (the -CCR 1 failure mode)").

**`-CCR 2` (the winner) — the release that actually releases.** It additionally erases the seed's
bare-T3 evidence (`plsBestT3Logit[p] = -inf`), so the T3 term of the RPS predicate stops firing on
this seed and the carried type-8 row survives. Cost/benefit stated at `main.cc:4461-4462`: costs
duplicate rate if the revoked delivery really was a duplicate; buys efficiency if it was the seed's
only row.

**Three things `-CCR 2` deliberately does NOT do:**
- It does not touch `plsBestChainLogit`. A seed whose CHAIN evidence reaches `-RPSA (5.5)` is still
  retired, independently. Correct: that is a different piece of evidence.
- It does not revive the pT3 delivery — `t3Pls[t]` stays `-1` (so `-XCD 2`'s "delivered set" at
  `main.cc:5330-5331` is the post-revocation set, both dedup passes writing `-1` there).
- It does not un-suppress rows already suppressed by the FIRST `m16RefreshSupp()` call at
  `main.cc:3911`. Because at that point stage B has not run, `plsBestT3Logit` is still `-inf`
  everywhere, so the only way a type-8 row could already be suppressed is the `plsOwned` (stage A)
  or `plsBestChainLogit >= -RPSA` term — and neither is the seed `-CCR 2` is releasing. **This is
  the invariant that makes the ordering `stage A refresh -> stage B -> -CC -> stage B refresh`
  correct rather than lucky.** Any port that merges the two refreshes, or runs `-CC` after the
  refresh, reintroduces the `-CCR 1` bug.

### 3.4 Post-contention bookkeeping
`ga.nT3Attached -= nCCDropped` (`main.cc:4490`), then `m16RefreshSupp()` (`main.cc:4496`).

---

## 4. DATA PER STAGE — SIZES AND GPU-PORTABILITY

### 4.1 `GeneralAttach` (`AttachDelivery.h:90-123`), sized in `gaInit` (`AttachDelivery.cc:43-56`)
| member | size | init | role |
|---|---|---|---|
| `chainPls` | `nChains` | `-1` | stage A grant |
| `chainLogit` | `nChains` | `-inf` | |
| `t3Pls` | **`nT3`** | `-1` | stage B grant; `-1` == revoked/deduped |
| `t3Logit` | `nT3` | `-inf` | |
| `plsOwned` | `nPls` | `0` | I1 authority |
| `plsBestChainLogit` | `nPls` | `-inf` | RPS chain term |
| `plsBestT3Logit` | `nPls` | `-inf` | RPS T3 term; `-CCR 2` writes here |
| `pairLog` | O(1e6)/evt | — | **DO NOT PORT** (§7) |

Per-event scratch:
- `bareMask`: `nT3` bytes (`AttachDelivery.cc:129`).
- `pairs` (`std::vector<AttachPair>`): the full stage-B prefiltered pair list, **~1.16e6/evt at
  `-T3F` OFF**. Must NEVER be materialized on device — see §7.
- `ccClaimed`: `ccMaxUnit+1` bytes = `nMDs` at `-CCG 1` (`main.cc:4319`).
- `hit2md`: `maxHit+1` ints, built once (`main.cc:4352-4371`).
- `t3Deliv` / `t3Owners` / `bidT3`: O(few hundred).
- `hit2keptPls`: hash map pixel-hit-row -> pLS list, O(4 * nOwners).

### 4.2 GPU-portability notes (hard rules)
1. **NO PAIR LOG.** `ga.pairLog` is ~1e6 records/evt. Its two consumers are the `-XC` bare-chain
   arm and `-CCS`, and both only need a per-target or per-chain REDUCTION. Port map directive
   (plan `PLAN_lst_redesign_t3_onward.md`, PORT NOTES): *"-XC4 in Alpaka = per-target bucket at
   scoring time (~250/evt, same ownership-map shape as -CC), no pair log needed."* Same for
   `-CCS`: keep one `float ccsLoserLogit[nChains]` updated with `atomicMax` at scoring time,
   conditioned on `plsOwnerChain[plsRow] != thisChain` — which requires the grant table, so it is
   a SECOND scoring pass or a deferred reduction, not a log.
2. **NO N^2 SCAN.** The contention is an ownership map (3 lookups per candidate). The `-XC`
   pixel-anchored branch is a hash set + an (eta,phi) hash grid whose cell is >= the window so the
   3x3 neighbourhood is exact and phi cells wrap (`main.cc:4919-4928`, `4942-5003`). Neither may
   become a candidate-vs-candidate loop.
3. **The greedy sweeps are order-dependent and therefore serial**, but they are only a few hundred
   entries: stage-B contention (`resolveContention`), `-RDT`, and the `-CC` sweep. The existing
   port already converts stage A's argmax to a packed `atomicMax` + rank count
   (`P/src/alpaka/LSTEvent.dev.cc:2145-2239`); stage B's contention can use the same shape. The
   `-CC` sweep and the hash-table walk stay sequential (a single-thread kernel is what
   `ChainAttachT3Contend` already does).
4. **`ccClaimed` at `-CCG 1` is indexed by MD ROW** — the natural device array. The `hit2md`
   inversion is only needed for carried pixel rows that expose hits; in the Alpaka tree the carried
   pT5/pT3 rows expose `hitIndices()[r][s][0..1]`, so the same inversion is needed, or (better) the
   K9 hit-owner map already alive at that point can supply it.
5. `t3_eta` / `t3_phi` are prototype ntuple branches. In the Alpaka tree they must come from the
   triplet's own quantities (or node features), NOT from the pixel seed — see §8.4.

---

## 5. (C) SEED RETIREMENT — THE `-RPS` PREDICATE, ITS THREE COPIES, AND THE MARGINS

### 5.0 The predicate
```
drop = ga.plsOwned[pls] != 0
if (ty == 8 && replPls >= 0.5f)
  drop = drop || ga.plsBestChainLogit[pls] >= rpsThetaChain
              || ga.plsBestT3Logit[pls]    >= rpsThetaT3
```
Semantics: "the pLS attached anywhere, OR it had a scored pair at/above its class RETIREMENT bar
but is not the owner" — i.e. it had real outer-tracker evidence and is not the owner. This one rule
replaces `CrossCleanpT5` + `CrossCleanpT3` + `CrossCleanpLS` (`main.cc:3545-3547`, `374-377`).
`ty == 7` / `ty == 5` rows use only the `plsOwned` term.

Winner bars: `rpsThetaChain = 5.5`, `rpsThetaT3 = 6.0`. **Both GLOBAL — no eta bands.** (The
DELIVERY margin `-a` IS banded; the retirement bar is not. F2 measured banded `-RPSA` as dominated,
`f3_ref/STATUS.md:18-19` / plan.)

### 5.1 COPY #1 — DECISION, carried rows (`main.cc:3569-3590`, predicate at **3584**)
`m16RefreshSupp()` lambda. Iterates all `ev.tc_type`; skips already-suppressed (idempotent, I6);
handles `ty in {7,5,8}`; `pls = rowPls(it, ty)`; applies the predicate; on drop sets
`m16RowSuppressed[it] = 1` and bumps `nM16SuppT7/T5/T8`.
Called twice: `main.cc:3911` (after stage A + `-RD`) and `main.cc:4496` (after stage B + `-RDT` +
`-CC`).

### 5.2 COPY #2 — DECISION, `-ZP8` bare-pLS additions (`main.cc:5253-5268`, predicate at **5257-5258**)
```
bool drop = p < ga.plsOwned.size() && ga.plsOwned[p] != 0;
if (!drop && replPls >= 0.5f && p < ga.plsBestChainLogit.size())
  drop = ga.plsBestChainLogit[p] >= rpsThetaChain || ga.plsBestT3Logit[p] >= rpsThetaT3;
if (!drop && p < xcRetired.size() && xcRetired[p]) { drop = true; ++totXcZp8; }
if (drop) { ++totZp8Blocked; continue; }
```
Comment at `5255`: *"Same verdict m16RefreshSupp applies to a carried row of this pLS."*
**This copy IS decision-bearing in the winner config** (`-ZP8 6` is in `POSTDELP2`): it gates the
type-8 rows this block ADDS back (the post-deletion bare-pLS universe LST's own `CheckHitspLS`
pass 2 + `CrossCleanpLS` used to remove). Emitted row at `5272-5283`: `type = 8`, `nhitOT = 0`,
`pt/eta/phi = pLS`, pixel hits only, `deliv = 4`.

### 5.3 COPY #3 — DIAGNOSTIC, `-XCD` truth partition (`main.cc:5351-5359`, predicate at **5354-5355**)
```
int fate = 3;                                       // survives as a bare type-8 row
if (ga.plsOwned[p] != 0) fate = 0;                  // consumed by a delivery
else if (replPls >= 0.5f && (ga.plsBestChainLogit[p] >= rpsThetaChain
                          || ga.plsBestT3Logit[p]    >= rpsThetaT3)) fate = 1;   // pre-existing RPS
else if (xcRetired[p]) fate = 2;                    // newly retired by the ported crossclean
```
`-XCD` is UNSET in the winner, so this copy is inert — but it must still be kept in sync, because
its whole purpose is to attribute retirements between `-RPS` and `-XC`, and a stale copy silently
mis-attributes.

### 5.4 `-RPST` — CONFIRMED DEAD END
- Only reads: the resolution at `main.cc:1550-1551`, the three predicate copies, and the summary
  printf at `5589-5594`. No other consumer.
- Measured (`S/a11_ref/STATUS.md:253-256`): `-RPST 5.0` costs **.0031 of efficiency** for a dup
  gain that `-RPSA` buys more cheaply; recommendation was `-RPST 6` which *"is provably a no-op"*
  (it equals the `-AT3` default). Re-confirmed at `S/a13_ref/STATUS.md:119-136`
  (`-RPST` variants dominated; *"stay in the code as a measured negative result, not a knob to
  ship"*).
- Plan directive (`PLAN_lst_redesign_t3_onward.md:2752`): *"-RPST and -XC4T dead ends, delete at
  port."*
- **Port action: DELETE the flag; HARDCODE the bar to `-AT3` (6.0).** The T3 TERM of the predicate
  must survive — deleting the term (rather than the flag) reintroduces the `-CCR 1` failure mode
  inverted (nothing left for `-CCR 2` to erase, so revoked seeds would never be retired at all and
  the duplicate rate returns).

### 5.5 THE SINGLE RESOLUTION SITE — `main.cc:1545-1561`
```
1548  if (rpsThetaChain >= 1e8f) rpsThetaChain = thetaAttach;      // -RPSA  <- -a
1550  if (rpsThetaT3    >= 1e8f) rpsThetaT3    = thetaAttachT3;    // -RPST  <- -AT3
1553  if (aThetaT       >= 1e8f) aThetaT       = thetaAttach;      // -a2    <- -a
1555  if (aThetaE       >= 1e8f) aThetaE       = thetaAttach;      // -a3    <- -a
1558  if (a4ThetaT      >= 1e8f) a4ThetaT      = a4Theta;          // -a42   <- -a4  (dead)
1560  if (a4ThetaE      >= 1e8f) a4ThetaE      = a4Theta;          // -a43   <- -a4  (dead)
```
Why HERE and not with the pre-scan defaults (comment `1545-1547`): `-a` comes through **getopt**,
which has only just run at `1379-1470`. Resolving earlier would capture the pre-getopt value of
`thetaAttach`.

**Two secondary resolution points that must be understood before simplifying:**
- `main.cc:1358-1362` resolves `-XCT2/-XCT3 <- -XCT` BEFORE getopt. Legal only because all three
  are pre-scanned. Do not move `-a2/-a3` here.
- `AttachDelivery.cc:77-79` re-resolves the band bins **again** inside `gaStageChains`
  (`thAT = (params.thetaAttachT < 1e8f) ? params.thetaAttachT : thA0`). Because `main.cc:1553-1556`
  already resolved them, this second check is a no-op in practice. **It is still a second
  resolution site and the port must collapse it into one.** Comment `AttachDelivery.cc:90-91`
  explains why the band lookup itself runs unconditionally: so the default command line exercises
  the exact code path and the no-op gate is a real gate.

### 5.6 PORT REQUIREMENT (the reason this section exists)
Plan PORT NOTES, verbatim: *"resolve retirement + delivery margins in ONE place post-getopt (all
THREE -RPS copies must read them — else the -CCR-1 failure mode returns)"*.

Concretely, in `P/interface/ChainConfig.h`:
```
float attachTheta      = 5.0f;   // -a   delivery, |eta| < 1.1
float attachThetaT     = 5.0f;   // -a2  delivery, 1.1 <= |eta| < 1.7
float attachThetaE     = 6.0f;   // -a3  delivery, |eta| >= 1.7
float attachThetaT3    = 6.0f;   // -AT3 delivery, bare-T3, GLOBAL
float rpsThetaChain    = 5.5f;   // -RPSA retirement, chain evidence, GLOBAL
// rpsThetaT3 == attachThetaT3 by construction (-RPST deleted)
float t3FakeMax        = 0.10f;  // -T3F
int   ccMinShared      = 1;      // -CCN  (unit = MD row; -CCG deleted, MD is the only unit)
int   ccRelease        = 2;      // -CCR  (or: delete the knob, hardcode the 2 behaviour)
```
No `1e9` sentinels survive the port — every value is resolved at construction. **The retirement
kernel must NOT reuse `attachTheta`.** `P/src/alpaka/ChainAttach.h:1072` currently does
(`thetaKey = attachOrderFloat(cfg.attachTheta)`), which was correct pre-A11 and is wrong now.

---

## 6. ADJACENT MECHANISMS THAT SHARE THE SAME STATE (do not port in isolation)

- **`-XC` seed crossclean** (`main.cc:4931-5091`): its output is `xcRetired[pLS]`, consumed by
  copy #2 (`5261`) and by the carried-type-8 channel (`5075-5086`, which sets
  `m16RowSuppressed[it] = 1` directly). Its bare-chain arm consults the ATTACH HEAD's logit for
  the `(seed, chain)` pair at a SEPARATE, LOOSER threshold (`-XCT` 3.75 / `-XCT2` 3.5), replacing
  LST's deleted pLS-embedding test. Anchors = `ga.plsOwned != 0` (`4936-4938`), so a seed a
  delivery consumed is never a retirement CANDIDATE.
- **`-XC4`** (`main.cc:3819-3834`, threshold selection `5050-5058`): score-only 4-layer chain pass.
  In Alpaka this becomes a per-target `atomicMax` bucket at scoring time (~250/evt), NOT a pair
  log. Flagged to the maintainer as scope decision (a): it extends the blessed `CrossCleanpLS`
  dR window to 4-layer chain targets.
- **`-CCS`** (`main.cc:4102-4127`, applied `4173-4180`): chain-loser suppression for the
  seedlessChain+SEEDEDchain duplicate cell. Bands on the emitted TC `|eta|` at 1.1/1.7.
  `-CCS3 = 1e9 = OFF` by design.
- **`-CCS`, `-XC`, `-XC4` all read `ga.pairLog`** — that is the single reason `recordPairs` is on
  in the winner. Porting them as scoring-time reductions is what lets the log die.

---

## 7. WHAT MUST NOT BE PORTED LITERALLY (summary)

| prototype construct | why | replacement |
|---|---|---|
| `ga.pairLog` (`AttachDelivery.h:115-122`) | ~1e6 records/evt | per-target / per-chain `atomicMax` at scoring time |
| `std::vector<AttachPair> pairs` materialized per stage | ~1.16e6/evt | fuse enumeration+scoring in the grid walk (already done: `ChainAttachT3Score`) |
| `emitTarget` full-scan fallback (`PixelAttach.cc:666-680`, `cands == nullptr`) | O(nTargets x nPls) | the K8a grid is a proven superset; keep the audit kernel as the gate |
| `-a4` / `-a42` / `-a43` stage A2 | inert in winner; plan says drop | delete |
| `-RPST`, `-XC4T` | measured dead ends; plan says delete | hardcode to `-AT3` / per-eta `-XCT` |
| `-CCG 0` (hit granularity), `-CCK 1/2`, `-CCP 0`, `-CCR 0/1` | A/B controls only | hardcode `-CCG 1`, `-CCK 0`, `-CCP 1`, `-CCR 2` |
| `-M4B/-M4T`, `-XCQ` | plan: drop | delete |
| `std::unordered_map` in `resolveContention`, `seedFamilyDup`, `hit2keptPls`, `anchorHit`, `anchorCell`, `chainPairs` | host-only | packed `atomicMax` keys / open-addressed device hash (`chainattach::kSeedHashSlots` pattern already exists) |

---

## 8. AUDIT OF THE EXISTING PARTIAL PORT — `P/src/alpaka/ChainAttachT3.h` (717 lines)

**Overall verdict: it is a MEASUREMENT PROBE, self-declared (`ChainAttachT3.h:23-34`), covering
stage B's target selection / scoring / pLS contention / `-RD` and a scaffolded emission. The `-CC`
contention does not exist at all, the `-T3F` gate exists under a different name, and the
retirement plumbing is stale relative to A11. Nothing in it is wired into a production path.**

### 8.1 Piece-by-piece

| piece | lines | vs prototype stage B | verdict |
|---|---|---|---|
| `ChainAttachT3MarkConsumed` | 119-134 | `k8BuildBareT3Mask` — marks every node item of every K9-ACCEPTED chain | **COMPLETE & FAITHFUL.** Uses `chains.nAccepted()` + `accepted[]`, i.e. the accepted set (I2 honoured) |
| `ChainAttachT3Keep` — `consumed` + `maxFake` | 152-188 | bareness + `-T3F` | **COMPLETE for `-T3F`.** `!(nodes.features()[n][12] <= maxFake)` is byte-for-byte the prototype's NaN-rejecting form (`AttachDelivery.cc:141`); feature 12 IS `triplets.fakeScore()` (`ChainEdges.h:393`). **But it is exposed as env `LST_CHAIN_T3_MAXFAKE` with default 1e9, not as `ChainConfig.t3FakeMax = 0.10`** |
| `ChainAttachT3Keep` — `maxClaimed` | 169-184 | **NOT IN THE PROTOTYPE** | **DIVERGENT / EXTRA.** A `hitOwner`-based "at most N of 6 hits already claimed" pre-filter, invented for the probe (comment `143-151`). It is a DIFFERENT mechanism from `-CC`: it filters the TARGET UNIVERSE before scoring, using the K9 claim map; `-CC` filters the DELIVERIES after scoring, using its own map pre-claimed from delivered TCs, with an MD unit and a `>= -CCN` count. **Decide explicitly: either delete `maxClaimed` (it is not in the signed-off config; default 6 = inert) or measure it as an alternative to `-CC`. Do not let it stand in for `-CC`.** |
| `ChainAttachT3Scatter` | 190-200 | CSR compaction | **COMPLETE.** Ascending dense node index == ascending triplet row, which is what makes the stage-B tie-break "lower T3 row" (§2.5) |
| `ChainAttachT3TargetPre` | 223-269 | `makeT3Pre` / `makeT3PreGeom` | **COMPLETE & carefully faithful.** `rtInner`/`zInner` from md0 anchor; `chordPhi` in FLOAT (documented deliberate match, `206-208`); `tanLambda` read off node feature 2 so it cannot drift; `fitKappa` = node feature 0; `rotSign = sign(fitKappa)`; slots 7-11 = `{fitKappa, tanLambda, feat9, 3.0, 0.0}`; centre non-finite -> `centerValid = 0`. `o.chain` carries the SPARSE TRIPLET INDEX (not a chain row) — the `kBareT3TCMarker` contract depends on this |
| `ChainAttachT3Score` | 285-404 | `gaStageT3` scoring loop | **COMPLETE for the winner's semantics.** (i) `plsBest` `atomicMax` happens for EVERY scored pair BEFORE the `plsOwned` skip and BEFORE the margin (I4, lines 330-339) — correct; (ii) input 18 overwritten with `kAttachTargetTypeT3` AFTER `attachEvalPairX` fills the frozen 19 (line 380); (iii) explicit `(lo == bestLogit && p < bestPls)` tie-break (line 340) — the correct restoration of the prototype's ascending-order "first wins" under a non-ascending cell walk; (iv) `theta` is a kernel ARGUMENT, so a per-eta or per-class margin is a one-line change. **Extras:** logit histogram + `tgtBestAny` (unthresholded per-target best) — measurement only, strip or keep behind a flag |
| `ChainAttachT3CopyOwned` | 110-115 | — | **PROBE SCAFFOLD.** Snapshots `plsOwned` so the probe never writes the live array. In a real port stage B writes the live array (I1) and this kernel disappears |
| `ChainAttachT3Contend` — pLS contention | 419-459 | `resolveContention` (`AttachDelivery.cc:17-39`) | **COMPLETE & FAITHFUL.** Sequential `plsOwnerPos` walk, higher logit wins, tie keeps the earlier position, loser -> `(-1, kAttachNoLogit)` |
| `ChainAttachT3Contend` — `-RDT` dedup | 466-546 | `main.cc:4231-4253` + `seedFamilyDup` | **COMPLETE & FAITHFUL in mechanism.** O(n^2) selection sort on (logit desc, target position asc) == (logit desc, T3 row asc); pixel hit rows from `pixelSeeds.firstHit()/nHits()` filtered on `detid() == kPixelModuleId`; `>= 2 shared` via the shared `hashKey/hashVal` table **stage A left behind** — the cross-class guard is correctly reproduced. **Gated on `chainConfig_.attachSeedDedup` (`-RD`), i.e. it implements `-RDT = -1 follow -RD`. Fine for the winner; `-RDT` as an independent knob is absent (and can stay absent)** |
| `ChainAttachT3Contend` — final `plsOwned` publish | 548-559 | `AttachDelivery.cc:196` | writes the SCRATCH copy; see `ChainAttachT3PublishOwnership` |
| **`-CC` contention (`ccClaimed` map, `-CCP` pre-claim, `-CCK` sweep, `-CCN` count, `-CCR` revoke)** | **ABSENT** | `main.cc:4254-4489` | **MISSING ENTIRELY. This is the single biggest gap.** The winner's whole barrel-dup result depends on it (`-CC 1 -CCN 1 -CCR 2`) |
| `attachUnorderFloat` | 578-581 | — | correct inverse of `attachOrderFloat` |
| `ChainAttachT3PublishOwnership` | 591-609 | `m16RefreshSupp`'s inputs | **DIVERGENT (stale, pre-A11).** It folds the T3 evidence into the SINGLE `plsBestLive` array shifted by `chainConfig_.attachTheta - theta` so the shipped `ChainSuppressCarriedTCs` test `plsBest[p] >= orderFloat(cfg.attachTheta)` reproduces a TWO-margin predicate. That trick was exact when `rpsThetaChain == attachTheta` and `rpsThetaT3 == attachThetaT3`. **Under the winner it is wrong three ways:** (a) the chain bar is now `-RPSA 5.5`, not `-a`; (b) `-a` is now BANDED (5.0/5.0/6.0) while the retirement bar is global, so there is no single `attachTheta` to shift against; (c) the shift is computed from `theta` (the env-overridable `-AT3`), so any margin sweep silently moves the retirement bar too. **Fix: drop the shift trick. Keep TWO device arrays (`plsBestChainKey`, `plsBestT3Key`) and TWO bars (`rpsThetaChain`, `attachThetaT3`) and test them separately in `ChainSuppressCarriedTCs`.** That also removes the only place where `-CCR 2`'s erasure could be lost |
| `ChainEmitBareT3TCs` | 621-713 | `main.cc:4470-4488` | **INCOMPLETE / DIVERGENT.** See §8.2 |

### 8.2 Emission audit (`ChainEmitBareT3TCs`, `ChainAttachT3.h:621-713`)
Correct: `trackCandidateType() = LSTObjType::pT3`; `pixelSeedIndex() = pixelSeeds.seedIdx()[pls]`;
pixel hits first into the `kPixelLayerSlots` slots then the three MDs' `anchorHitIndices`/
`outerHitIndices` into layer slots; `nTrackCandidatespT3` and `chains.nChainTCs()` both advanced so
`isChainTCRow` covers the rows; TC-row overflow counted (`stats[6]`).

Divergences / gaps:
1. **`directObjectIndices() = kBareT3TCMarker (0xFFFFFFFF)`** (line 656) because the row is backed
   by no `PixelTriplets` entry. `objectIndices()[tc] = {pls, t3}`. Guarded correctly in
   `parseChainTC` (`S/code/core/write_lst_ntuple.cc:3078-3090`) — but **NOT** at
   `write_lst_ntuple.cc:2475`, which does
   `pt3_idx_map[trackCandidatesExtended.directObjectIndices()[tc_idx]]` with no `chainRowTC`
   guard. `pt3_idx_map` is a by-value `std::map` (`write_lst_ntuple.cc:2016`), so `operator[]`
   silently inserts `0xFFFFFFFF -> 0` and the row reports `tc_pt3Idx = 0`. Non-crashing, wrong,
   and it will confuse every dup/fake attribution that keys on `tc_pt3Idx`. **Fix required.**
2. **eta/phi provenance diverges from the prototype.** Prototype: `eta = ev.t3_eta[t]`,
   `phi = ev.t3_phi[t]` (`main.cc:4474-4475`). Port: `parseChainTC` returns
   `pixelSeeds.eta()[ipLS], pixelSeeds.phi()[ipLS]` (`write_lst_ntuple.cc:3086-3088`) with the
   justification "exactly as LST's own pT3 rows take theirs from their pLS". `pt` agrees
   (`pixelSeeds.ptIn()` == `ev.pLS_pt`). **This is a real divergence and it matters**: eta/phi feed
   the eta-band assignment for `-CCS`, the `-XC` bands, and every per-region scoreboard number that
   the winner was tuned against. Either restore the T3's eta/phi or re-derive the banded constants.
3. `logicalLayers`/`lowerModuleIndices` slot collision fallback (`689-700`) bumps `stats[7]`,
   which `ChainAttachT3Contend` ALSO uses for seed-dedup overflow — counter collision, cosmetic.
4. `centerX/centerY/radius` of the extended SoA are never written for these rows.
5. Emission is decoupled from the contention: the owner list round-trips to the host
   (`bareT3Triplet_/bareT3Pls_/bareT3Logit_`) and back (`LSTEvent.dev.cc:2740-2774`). Self-declared
   as a scaffold artifact (`LSTEvent.dev.cc:2736-2739`). Must become device-resident, and must be
   FUSED with (or sequenced after) the `-CC` sweep, because in the prototype revocation and
   emission are one loop (`main.cc:4422-4489`).

### 8.3 Hook-up state in `P/src/alpaka/LSTEvent.dev.cc`
| grep | site | state |
|---|---|---|
| `ChainAttachT3.h` include | `9` | present |
| `chainT3AttachEnabled()` / `chainT3ReplaceEnabled()` | `96-101` | env-var gates `LST_CHAIN_T3ATTACH` / `LST_CHAIN_T3REPLACE`, **both default OFF** |
| `replacePT3` | `1402-1404` | set true (and `dropPartOfPT3 = false`) inside `arbitrateChains`, only under `LST_CHAIN_T3REPLACE`. Correct mirror of `main.cc:3196-3197` |
| `attachBareT3Probe(...)` call | `2248-2249` | **correct position**: after stage A's contention + `-RD` are final (`a3b` at `2241`) and BEFORE `ChainSuppressCarriedTCs` (`2251-2259`) — exactly I3. Comment `2243-2247` states the reference order |
| `cfgT3` | `2434-2438` | a COPY of `chainConfig_` with `attachPrefDTanL`/`attachPrefDPhi` env-overridable — measurement knobs only; cannot perturb the already-built chain grid |
| `attachThetaT3` | `2425-2427` | `theta = chainConfig_.attachThetaT3`, env-overridable via `LST_CHAIN_T3_THETA` |
| second K8a grid | `2512-2554` | `ChainAttachGridBounds/Count/Scatter` re-run over the bare-T3 target hull into ITS OWN `rMin/rMax` — the superset argument transfers because the r hull is MEASURED, not assumed (`ChainAttachT3.h:48-87`). Audit kernel re-run under `LST_CHAIN_T3_AUDIT` (`2697-2727`), `MISSING` must be 0 |
| `emitBareT3TCs` | `1894` | called after `ChainEmitTCs`, so pT3-class rows are appended AFTER the chain rows (matches `isChainTCRow`'s "last `nChainTCs` rows" contract) |
| `ChainSuppressCarriedTCs` | `2251-2259` + `ChainAttach.h:1056-1128` | the retirement kernel. Reads `plsOwned` + a SINGLE `plsBest` against `orderFloat(cfg.attachTheta)`; comment at `ChainAttach.h:1054-1055` still says the T3 term is "structurally false in the freeze". **Stale — see §8.1 `PublishOwnership`** |

### 8.4 WHAT REMAINS TO BE DONE (concrete, ordered)
1. **Promote the probe to a delivery path.** Delete `ChainAttachT3CopyOwned` and write the live
   `plsOwned`; delete the env gates; add `ChainConfig` fields (§5.6) and set `replacePT3 = true`,
   `dropPartOfPT3 = false` unconditionally.
2. **Move `-T3F` from `LST_CHAIN_T3_MAXFAKE` (default 1e9) to `ChainConfig.t3FakeMax = 0.10f`.**
   Decide the fate of `maxClaimed` (recommend: delete).
3. **Implement `-CC` (the whole of §3).** New kernels:
   - `ChainT3CCPreclaim`: mark `ccClaimed[md]` for every MD of every emitted chain TC (from
     `chains.mdItems`/`mdOffsets` via the emitted-row -> chain map) and for every surviving carried
     pT5/pT3 row's OT hits mapped to MD rows (`hit2md`, or the K9 hit-owner map).
   - `ChainT3CCSweep`: single-thread, sorted by (`t3Logit` desc, T3 row asc); for each delivery
     count `nShared` over its 3 MDs; `nShared >= ccMinShared (1)` -> revoke and apply `-CCR 2`
     (`t3Pls = -1`, `plsOwned[p] = 0`, `plsBestT3Key[p] = 0 /* orderFloat(-inf) */`); else claim
     the 3 MDs. Must run BEFORE the ownership publish and BEFORE `ChainSuppressCarriedTCs` (I7).
4. **Fix the retirement plumbing.** Two device arrays + two bars; delete the shift trick; make
   `ChainSuppressCarriedTCs` read `rpsThetaChain` (5.5) for the chain term and `attachThetaT3`
   (6.0) for the T3 term. Delete `-RPST`.
5. **Fuse emission with the sweep** (device-resident owner list; drop the host round trip).
6. **Fix the emission divergences** (§8.2 items 1-4): guard `write_lst_ntuple.cc:2475`, and settle
   the eta/phi provenance question.
7. **Port the banded delivery margin** `-a/-a2/-a3` (`ChainAttach.h:786` currently uses a single
   `cfg.attachTheta`) — coupled, because the winner moved `-a` to 5.0 and added `-a2/-a3`, and
   because `plsBestChainLogit` must stay UNBANDED (written before the threshold, I4).
8. **Port `-CCS`, `-XC` (`-XC 3`, `-XCT 3.75`, `-XCT2 3.5`), `-XC4 1`** as scoring-time reductions,
   no pair log (§7). Out of this spec's scope but on the same state.
9. **Parity gate.** Run the prototype winner line and the integrated build on the same 300 events
   and compare, in order: `nBareT3` targets/evt, scored pairs/evt, per-target picks, attached after
   contention, `-RDT` revocations, `-CC` revocations, delivered type-5 rows/evt (reference ~121.4
   rows/evt on the 977), suppressed type-8 count, and only then the scoreboard. The grid-superset
   audit (`LST_CHAIN_T3_AUDIT`) must report `MISSING = 0` on every event before any of it is
   believed.

---

## 9. REFERENCE NUMBERS (for the parity gate)

Winner CHAINFINAL2 / F3W977, full 977 events, vs LST (`plan` + `f3_ref/STATUS.md:63-72`):
```
eff  .80957  (LST -.00030; effB +.00144 ABOVE LST)
dup  .04812  (BELOW LST .05138) | dupB .02314 = 2.38x | dupT .01690 = 1.29x | dupE .07099 below
fake .04637  (LST +.00099) | fakE below LST
displaced DISP1 +639 distinct sims vs LST (DISP30 +93)
delivered pT3-class rows: 121.4 /evt
```
Standing caveat to restate with any headline: **the attach head is BORROWED (trained on chain
pairs); `-T3F`/`-RPSA`/`-a` are thresholds on its logits; a retrain re-derives the values though
the mechanisms survive.** No timing claims. Nothing tested on cube/jet samples.

Reproduce: `BIN=$S/protoFINAL2/bin/chainproto bash $S/synth_ref/syn_run.sh <TAG> -T3F 0.10 -XC4 1
-RPSA 5.5 -EXR 4.0 -a 5.0 -a2 5.0 -a3 6.0 -CCS 6.0 -CCS2 5.0 -XCT 3.75 -XCT2 3.5 -MRB -1.2 -MRT -1.2`
