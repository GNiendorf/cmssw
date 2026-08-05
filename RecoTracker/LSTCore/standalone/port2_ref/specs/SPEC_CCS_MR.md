# SPEC: -CCS/-CCS2/-CCS3, -MRB/-MRT, -a/-a2/-a3, -EXR

Port source tree: `S = /mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone`,
`P = S/..`. All prototype citations are `S/protoFINAL2/<file>:<line>`.

Winner config (for value pinning): frozen prefix `S/synth_ref/syn_run.sh`
+ `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`
+ `-T3F 0.10 -XC4 1 -RPSA 5.5 -EXR 4.0 -a 5.0 -a2 5.0 -a3 6.0 -CCS 6.0 -CCS2 5.0 -XCT 3.75 -XCT2 3.5 -MRB -1.2 -MRT -1.2`.

Relevant values inherited from the frozen prefix (`syn_run.sh`): `-A 4`, `-G 6`, `-X 0.5`,
`-MR -1.800`, `-MRI -0.5`, `-MD 1e9`, `-M4 4.0`, `-M4D -1.2`, `-M5 1e9`, `-M6 1e9`,
`-ZM4D 1.2`, `-ZM4 -0.5`, `-EX 1`, `-EXW 0.25`, `-EXS 1`, `-RPS 1`, `-RD 1`, `-AT3` default 6.0,
`-ZE1 1.1`, `-ZE2 1.7`. Not set anywhere in the winner line: `-CCS3`, `-M4B`, `-M4T`, `-a4/-a42/-a43`,
`-D4`, `-ZR/-ZR5/-ZR6/-ZRI`, `-ZIL`.

---

## 1. `-CCS` / `-CCS2` / `-CCS3` — CHAIN-LOSER SUPPRESSION

Winner: `-CCS 6.0` (barrel), `-CCS2 5.0` (transition), `-CCS3` **absent = 1e9 = OFF** (endcap).

### 1.1 Flag storage, sentinels, parse order

| flag | variable | decl | default | winner | band |
|---|---|---|---|---|---|
| `-CCS`  | `ccsTheta`  | `main.cc:877` | `1e9f` | `6.0` | `\|eta\| < 1.1` |
| `-CCS2` | `ccsThetaT` | `main.cc:878` | `1e9f` | `5.0` | `1.1 <= \|eta\| < 1.7` |
| `-CCS3` | `ccsThetaE` | `main.cc:879` | `1e9f` | *unset* | `\|eta\| >= 1.7` |

Declaration block / rationale: `main.cc:866-880`.

**Sentinel semantics — DIFFERENT from every other band family in the tree.** There is **no
follow-the-barrel resolution**. Each band's sentinel `1e9` means *OFF for that band only*
(`main.cc:875-876`: the endcap dup rate is already below LST and must not move by accident).
So the winner suppresses in barrel (bar 6.0) and transition (bar 5.0) and **never** in the endcap.
Do **not** add an `if (ccsThetaE >= 1e8f) ccsThetaE = ccsTheta;` fallback — it would change physics.

Pre-scan parse (before `getopt`), longest name first: `main.cc:1286-1292`
(`-CCS2` -> `ccsThetaT`, `-CCS3` -> `ccsThetaE`, `-CCS` -> `ccsTheta`). The `-CCS*` names must be
consumed by the pre-scan and must precede `-CC`, `-CCG`, `-CCN`, `-CCP`, `-CCK`, `-CCR` **in intent**
(the scan is exact string equality, so it is order-insensitive today; the ordering is a documented
convention, `main.cc:1283-1286`). Getopt's `"-C"` is not a flag, but `-CC*` would otherwise fall
through to `args` and be misparsed.

### 1.2 Master enable

```cpp
const bool ccsOn = (ccsTheta < 1e8f || ccsThetaT < 1e8f || ccsThetaE < 1e8f);   // main.cc:4106
```

`ccsOn` has **two** effects, at two different places:

1. **It forces the pair log on** (`main.cc:3189`):
   ```cpp
   ga.recordPairs = attachCM || xcChain || (ccsTheta < 1e8f || ccsThetaT < 1e8f || ccsThetaE < 1e8f);
   ```
   i.e. `-CCS*` works even without `-XC`. (In the winner `-XC 3` already forces it.)
2. It gates the per-event map build and the suppression predicate (`main.cc:4109`, `main.cc:4173`).

The map build additionally requires `attachMode == 4` (`-A 4`) and a non-empty pair log
(`main.cc:4109`). Under any other `-A` mode `-CCS` is inert.

### 1.3 The pair log it consumes (`ga.pairLog`)

Record type: `AttachDelivery.h:116-122`
```cpp
struct ScoredPair { int8_t ttype; int tgtRow; int plsRow; float logit; };
```
`tgtRow` is the **CHAIN ROW** for `ttype == kAttachTargetChain (0)` (`AttachDelivery.h:118`), i.e.
`targetChains[pr.chainPos]`, *not* the target position. `logit = attachLogit(pr.f)` — the raw attach
head output, **pre-threshold, pre-contention**.

Three append sites, all with the SAME head / SAME prefilter windows / SAME `AttachPair` path:

| site | population | notes |
|---|---|---|
| `AttachDelivery.cc:85-86` (stage A1, `gaStageChains`) | accepted chains with `nLayers >= 5` that passed the `-D4` dca gate (`main.cc:3799-3807`) x prefiltered pLS | every scored pair, whatever its logit |
| `main.cc:3830-3832` (A15 `-XC4 1`) | accepted chains with `nLayers < 5`, score-only (`minChainLayers = 1`) | **live in the winner** — so 4-layer bare chains ARE `-CCS`-eligible |
| `AttachDelivery.cc:158-159` (stage B, `gaStageT3`) | bare-T3 targets, `ttype == 1` | filtered out by `main.cc:4116-4117`; and stage B runs *after* the emission loop anyway (`main.cc:4222`), so at build time the log holds only `ttype == 0` |

Stage A2 (`-a4`, `main.cc:3849-3876`) deliberately suppresses `recordPairs` for its duration
(`main.cc:3868-3870`) so those pairs are not double-counted. `-a4` is OFF in the winner.

Net: **with `-D4` off (winner), the chain-side pair-log universe is exactly "every accepted chain
x its prefiltered pLS neighbours".**

### 1.4 Build: invert the grant map, one pass over the log (`main.cc:4109-4127`)

```cpp
std::vector<int> plsOwnerChain(ev.pLS_pt.size(), -1);            // 4110
for (int c2 = 0; c2 < (int)chainAttachPls.size(); ++c2)          // 4111-4113
  if (chainAttachPls[c2] >= 0 && chainAttachPls[c2] < (int)plsOwnerChain.size())
    plsOwnerChain[chainAttachPls[c2]] = c2;

ccsLoserLogit.assign(chains.nLayers.size(), -1e30f);             // 4114
for (const auto& sp : ga.pairLog) {                              // 4115
  if (sp.ttype != kAttachTargetChain) continue;                  // 4116-4117
  if (sp.plsRow < 0 || sp.plsRow >= (int)plsOwnerChain.size()) continue;   // 4118-4119
  const int oc = plsOwnerChain[sp.plsRow];                       // 4120
  if (oc < 0 || oc == sp.tgtRow) continue;                       // 4121-4122
  if (sp.tgtRow >= 0 && sp.tgtRow < (int)ccsLoserLogit.size() &&
      sp.logit > ccsLoserLogit[sp.tgtRow])
    ccsLoserLogit[sp.tgtRow] = sp.logit;                         // 4123-4125
}
```

Exact semantics:

* `chainAttachPls` is `ga.chainPls` **after** the `-RD` seed-family dedup (`main.cc:3910`, dedup at
  `main.cc:3882-3909`). A chain whose grant `-RD` revoked has `chainAttachPls[c] == -1`, so it is
  **CCS-eligible**, and its ex-pLS has `plsOwnerChain == -1` (one pLS, one owner) so contributes
  nothing.
* `plsOwnerChain` is a total function pLS-row -> owning chain row or -1. It is well-defined
  (single-valued) because of the ONE-pLS-ONE-OWNER invariant (`AttachDelivery.cc:17-39`).
* `ccsLoserLogit[c]` = **max over ALL scored chain-side pairs of chain `c` whose pLS is owned by some
  OTHER chain**, of the raw head logit. Note: **not** restricted to the chain's own argmax bid, and
  **not** restricted to pairs above `-a`. Sub-threshold pairs count.
* `oc == sp.tgtRow` guard is **vacuous for every eligible chain**: `plsOwnerChain[p] == c` implies
  `chainAttachPls[c] == p >= 0`, i.e. `c` is an owner, i.e. `p >= 0` at the emission site and the
  suppression predicate cannot fire. Keep it for exactness, but a port may prove it away.
* Never-enumerated chains keep `-1e30f` -> never suppressed. `-1e30f` is the "no evidence" sentinel.
* Cost: `O(nPls) + O(|pairLog|)`. **No pairwise loop, no N^2 scan.**

### 1.5 Suppression predicate, and exactly where it sits (`main.cc:4166-4181`)

The emission loop is `main.cc:4128-4208`. The relevant sequence:

```
4130   std::size_t tcPos = 0;
4131   for (ai = 0; ai < accepted.size(); ++ai) {
4132     const int c = accepted[ai];
4133-34   if (chains.nLayers[c] < 4) continue;     // K10 dropped it; lockstep preserved
4135     ChainTC& ctc = chainTCs[tcPos++];         // <-- tcPos ADVANCES HERE
4136     OutTC otc;  ... otc.eta = ctc.eta ... (K10 fields + dbg fields)
4166     const int p = attachMode ? chainAttachPls[c] : -1;
4167-72   [comment]
4173     if (p < 0 && ccsOn && c < (int)ccsLoserLogit.size()) {
4174       const float aeCcs = std::fabs(otc.eta);
4175       const float ccsBar = (aeCcs < 1.1f) ? ccsTheta : ((aeCcs < 1.7f) ? ccsThetaT : ccsThetaE);
4176       if (ccsLoserLogit[c] >= ccsBar) { ++nCcsSuppressed; continue; }   // 4177-4178
4179     }
4181     if (p >= 0) { ... type-7 upgrade block ... }   // 4181-4201
4202-05  append ctc.hitIdxs as Phase2OT
4206     outTCs.push_back(std::move(otc));
4207     outTCChain.push_back(c);
```

Exact predicate:
```
suppress(c) ==  ccsOn
             && chainAttachPls[c] < 0                      // BARE ONLY: the chain won no pLS
             && c < nChains                                // bounds
             && ccsLoserLogit[c] >= ccsBar(|otc.eta|)
ccsBar(ae) = ae < 1.1 ? ccsTheta : (ae < 1.7 ? ccsThetaT : ccsThetaE)
```
Comparison is `>=` (inclusive). Bands are hard-coded `1.1f` / `1.7f` at `main.cc:4175` — they are
**literals, not `-ZE1`/`-ZE2`** (`main.cc:873-874` calls them "reuse the 1.1/1.7 boundaries"; the
code does not read `zEta1/zEta2`). A port must keep them literal to stay bit-exact.

**Which |eta|.** `otc.eta` at line 4174 equals `ctc.eta` (assigned before line 4166), which is the
**K10 TC eta = `ev.t3_eta[innermost member T3]`** (`Stages.h:288-289`, `K9K10.cc:425`). It is
**NOT** the seed/pLS eta. The `-a` bands use pLS eta (section 3) — the two families deliberately use
different quantities. `otc.eta` is never overwritten by anything: the type-7 block overwrites
`otc.pt` (`main.cc:4192`) and `otc.type` but not `otc.eta`; and it is unreachable anyway since the
predicate requires `p < 0`.

**Position relative to the type-7 upgrade block.** Immediately BEFORE it (4173-4179 vs 4181-4201)
and **mutually exclusive** with it by construction (`p < 0` vs `p >= 0`). Placement is therefore
free with respect to the upgrade; what is NOT free is that it must come **after** `otc.eta` is
populated and **after** `tcPos` has advanced.

**tcPos lockstep invariant (do not break).** `chainTCs` is built by
`k10AssembleChainTCs(ev, chains, accepted, chainTCs)` (`main.cc:4005-4006`), whose contract is
"accepted order, skipping `nLayers < 4`" (`main.cc:4075-4076`, `Stages.h:287-291`). `tcPos` is
incremented at `main.cc:4135`, i.e. **before** the CCS test, so the `continue` at 4178 skips the
emission but leaves `chainTCs[tcPos]` aligned with the next accepted chain. Any port that
short-circuits *earlier* than line 4135 (e.g. a pre-filter over `accepted`) must renumber the
`chainTCs` index independently, or it silently mis-pairs every subsequent TC.

`nCcsSuppressed` (`main.cc:4107`, `4177`) is incremented but **never printed** — a dead diagnostic
counter. Port it as a stat slot or drop it; it has no physics effect.

### 1.6 What `-CCS` must NOT disturb

* **`ga.plsBestChainLogit` / `-RPSA` (winner 5.5).** `plsBestChainLogit` is written at *scoring*
  time inside `gaStageChains` (`AttachDelivery.cc:87-89`, only when `writeBestLogit`). `-CCS` is
  read-only w.r.t. it (`main.cc:4170-4172`). The `-RPS` predicate
  (`drop |= plsBestChainLogit[pls] >= rpsThetaChain || plsBestT3Logit[pls] >= rpsThetaT3`,
  `main.cc:3584`, also `main.cc:5257-5258`, `main.cc:5354-5355`) fires at `m16RefreshSupp()`
  which runs at `main.cc:3910-3911` — **before** the emission loop. So the retirement of carried
  bare-pLS rows is already final when `-CCS` runs, and `-CCS` cannot un-retire anything.
  Consequence to expect (not a bug): with `-CCS 6.0` > `-RPSA 5.5`, a CCS-suppressed chain's
  contested pLS has almost always already had its bare type-8 row retired — but that pLS is owned by
  the *winner* chain, which delivers it as type-7, so no track is lost.
* **The winner chain's grant.** Final; `-CCS` never rewrites `chainAttachPls`, `ga.chainPls`,
  `ga.chainLogit`, `ga.plsOwned`, `plsSuppressed`, or `attachRef*`.
* **The K9 claim.** `-CCS` runs post-claim, post-K10. It removes a TC row only. There is no
  backfill (identical semantics to the `-Q4/-Q5` post-claim floors, `main.cc:637-641`).
* **`-CC` / `-CCR 2`** touch only `ga.t3Pls`, `ga.plsOwned[p]`, `ga.plsBestT3Logit[p]`
  (`main.cc:4448-4464`) and run *after* the emission loop, so they cannot perturb the CCS map.

### 1.7 Port dependencies (must already exist in the Alpaka pipeline)

1. Per-chain attach grant after `-RD`: the ported analogue of `chainAttachPls`
   (`src/alpaka/ChainAttach.h` K8c contention + `-RD` dedup kernel).
2. The accepted-chain list in K9 order and the K10 TC records with `eta` = innermost member-T3 eta
   (`src/alpaka/ChainArbitrate.h` / TC assembly), plus the `nLayers < 4` skip.
3. The attach head logit for (chain, pLS) pairs — `attachLogit` equivalent, already in
   `ChainAttach.h` (`attachHeadBatch`).
4. Enumeration coverage for **`nLayers < 5` chains** (the `-XC4 1` score-only extension). If the
   port's attach kernel hard-codes `minChainLayers = 5`, the 4-layer slice of `ccsLoserLogit` will be
   all `-1e30` and 4-layer bare twins will not be suppressed — a real physics delta from the winner.

### 1.8 GPU shape (recommended, avoids storing a pair log)

The prototype needs, per bare chain `c`, `max{ logit(c,p) : p scored with c, owner(p) != -1 }`.
Ownership is only known after contention, so it cannot be reduced in the scoring pass. Do **not**
materialise `pairLog` (its size is `nScored`, the full prefiltered pair count).

Faithful, cheap shape — **one inverted map + one restricted second pass**:

1. After K8c contention + `-RD`, build `plsOwnerChain[nPls]` (init -1) with one kernel over chains:
   `if (chainPls[c] >= 0) plsOwnerChain[chainPls[c]] = c;`  — `O(nChains)`, no atomics needed
   (single-valued by the invariant).
2. Second attach pass over **eligible targets only** (`chainPls[c] < 0`, all accepted chains
   including `nLayers < 5`), walking the **same prefilter cell index** as pass 1, but evaluating the
   head only for candidate pLS with `plsOwnerChain[p] >= 0` (a few hundred owned seeds out of
   ~10^4-10^5 pLS, so the head-eval count collapses). Reduce with `max` into `ccsLoserLogit[c]`.
   Cost: same cell walk as pass 1 for a subset of targets; head evals ~ `nChainAttached` scale.
3. Suppression is then a per-TC boolean at emission time; combine it with the existing TC-emission
   compaction so the "lockstep" issue disappears (the ported writer compacts with a prefix sum
   rather than a running `tcPos`).

Invariants a port must preserve: threshold-free and contention-free logits (use the raw head output,
not the post-`-a` survivors); `>=` comparison; literal 1.1/1.7 bands on the **chain** eta; per-band
1e9 = OFF with no cross-band fallback.

---

## 2. `-MRB` / `-MRT` — band-split exempt admission bars at the `-G 6` gate

Winner: `-MRB -1.2`, `-MRT -1.2`, global `-MR -1.800`. `-M4B` / `-M4T` **absent**.

### 2.1 What it is a band split OF

It is a band split of **`-MR`, the OR-rescue mX floor of the DISPLACED-EXEMPT 5+ branch of the
`-G 6` three-class chain gate** — i.e. of the "exempt OR-rescue floor", not of a plain admission
margin. Sibling `-M4B`/`-M4T` split `-M4D`, the exempt **T4-class** mD bar. Declarations and
rationale: `main.cc:617-629`.

| flag | variable | decl | default | splits | winner |
|---|---|---|---|---|---|
| `-MRB` | `m3ThetaRB`  | `main.cc:626` | `kMrUnset = 1e30f` | `-MR`  (`m3ThetaR`)  | `-1.2` |
| `-MRT` | `m3ThetaRT`  | `main.cc:627` | `kMrUnset` | `-MR`  | `-1.2` |
| `-M4B` | `m3Theta4DB` | `main.cc:628` | `kMrUnset` | `-M4D` (`m3Theta4D`) | *unset* |
| `-M4T` | `m3Theta4DT` | `main.cc:629` | `kMrUnset` | `-M4D` | *unset* |

`kMrUnset = 1e30f` declared at `main.cc:615`.

### 2.2 Sentinel resolution (single point, after the pre-scan, before use)

`main.cc:1366-1374`:
```cpp
if (m3ThetaRB  >= kMrUnset) m3ThetaRB  = m3ThetaR;    // 1367-1368
if (m3ThetaRT  >= kMrUnset) m3ThetaRT  = m3ThetaR;    // 1369-1370
if (m3Theta4DB >= kMrUnset) m3Theta4DB = m3Theta4D;   // 1371-1372
if (m3Theta4DT >= kMrUnset) m3Theta4DT = m3Theta4D;   // 1373-1374
```
(`-MRI` resolves to `-MR` immediately above, `main.cc:1364-1365`.) So each band bar defaults to its
own global flag — "one number stays a global threshold" — and an unset flag is bit-exact.

**Winner consequence for `-M4B`/`-M4T`:** they resolve to `-M4D = -1.2`, so the T4 band split is a
**no-op in the winner**. They are absent from the winner line because the tuning round found no gain
over the already-set `-M4D -1.2` (the T4 exempt branch was tuned globally in the frozen `STACK`);
`-M4D -1.2` + `-ZM4D 1.2` already band-shifts the T4 exempt bar through the additive `-Z` lever, so
a second band mechanism on the same bar is redundant. **Confirmed: `-M4B` and `-M4T` are NOT in the
winner line and must be ported as inert-by-default (defaulting to `-M4D`), not omitted** — omitting
them silently is fine only if `-M4D` remains a single global constant.

### 2.3 Ordering constraint (pre-scan vs getopt)

All four are consumed in the **pre-scan loop** (`main.cc:1040-1346`; the loop's flag table ends at
`main.cc:1336`, value consumption at `main.cc:1345`), at `main.cc:1097-1104`:
```
-MRB -> m3ThetaRB ; -MRT -> m3ThetaRT ; -M4B -> m3Theta4DB ; -M4T -> m3Theta4DT ; then -MR -> m3ThetaR
```
Constraints, in order of hardness:
1. **`-MRB`/`-MRT`/`-M4B`/`-M4T` must be tested BEFORE `-MR`** (`main.cc:1095-1106`). The scan is
   exact string equality today, so this is a documented convention (`main.cc:1095-1096`: "Longer
   names first ... keeps a future prefix match honest") — but if the port ever moves to prefix
   matching it becomes load-bearing.
2. **All of them must be consumed by the pre-scan, not getopt.** `-M4B` etc. are multi-char flags;
   getopt's option string (`main.cc:1379`) has no `M`, so an unconsumed `-MRB` would be pushed to
   `args` and rejected/ignored. Same reason `-T4/-T5/-T6/-U4/-U5/-U6` are pre-scanned
   (`main.cc:1034-1037`).
3. **The sentinel resolution (2.2) must run AFTER the pre-scan** so that `-MR` given after `-MRB`
   on the command line still supplies the fallback. It does (`main.cc:1348` onward, after
   `nArgs` is computed at `main.cc:1347`).

### 2.4 The gate site (`main.cc:2946-2951`, used at `main.cc:2987`)

Inside the `-G 6` three-class kill loop (`main.cc:2886-3005`), per chain `c`:

```cpp
// band eta: innermost member T3, hoisted at 2953-2959
float aEtaC = -1.f;
if (chains.offsets[c + 1] > chains.offsets[c]) {
  const int t3In = chains.items[chains.offsets[c]];
  if (t3In >= 0 && t3In < (int)ev.t3_eta.size())
    aEtaC = std::fabs(ev.t3_eta[t3In]);
}
const bool inZ = zEta2 > zEta1 && aEtaC >= zEta1 && aEtaC < zEta2;     // 2944
const bool inB = aEtaC >= 0.f && aEtaC < zEta1;                        // 2949
const float barR  = inB ? m3ThetaRB  : (inZ ? m3ThetaRT  : m3ThetaR);   // 2950
const float bar4D = inB ? m3Theta4DB : (inZ ? m3Theta4DT : m3Theta4D);  // 2951
```

Band definition, exactly:
* `inB` (barrel, `-MRB`/`-M4B`): `aEtaC >= 0 && aEtaC < zEta1` — `zEta1 = -ZE1 = 1.1` (`main.cc:679`).
* `inZ` (transition, `-MRT`/`-M4T`): `zEta2 > zEta1 && aEtaC >= zEta1 && aEtaC < zEta2` —
  `zEta2 = -ZE2 = 1.7`.
* Everything else (endcap `aEtaC >= 1.7`, **and the `aEtaC < 0` "no member T3" fallback**) uses the
  **global** `-MR` / `-M4D`. `main.cc:2937-2938`: "`aEtaC < 0` means 'no member T3', which falls back
  to the global bar."
* Note the asymmetry: `inB` does **not** require `zEta2 > zEta1`, while `inZ` does. With
  `-ZE2 <= -ZE1` the barrel band still applies and the transition band collapses into the global bar.
* `|eta|` quantity: `std::fabs(ev.t3_eta[chains.items[chains.offsets[c]]])` — the chain's
  **innermost member T3 eta**, which is by construction the K10 TC eta
  (`K9K10.cc:425`, `main.cc:658-661`), "so a chain is cut in the band its TC is counted in".
  **Same quantity `-CCS` uses; different quantity from `-a`/`-a2`/`-a3`.**

Consumption sites:
```cpp
} else {   // exempt (large-DCA) 5+ : dcaAll[c] >= dcaSplit          main.cc:2984-2989
  if (mD < m3ThetaD && mX < barR + dR)
    chains.score[c] -= kGateKill;
  exemptMask[c] = 1;
}
```
and, for T4-class:
```cpp
if (nL <= 4) {                                                        // main.cc:2967
  if (dcaAll[c] >= std::max(dcaSplit, t4ExemptDcaMin)) {               // 2968
    if (mD < bar4D + d4D) chains.score[c] -= kGateKill;                // 2970-2971
    exemptMask[c] = 1;                                                 // 2972
  } else if (mX < m3Theta4 + d4) { chains.score[c] -= kGateKill; }     // 2973-2974
}
```
where (`main.cc:2924-2932`, `2961-2966`):
`mP = z[1]-z[0]`, `mD = z[2]-z[0]`, `mX = max(z[1],z[2]) - z[0]` from `runChainInference3`;
`dcaSplit = -X = 0.5`; `kGateKill = 1e9f` (`main.cc:691`).

Additive `-Z` deltas (`main.cc:2960-2966`): `dR = zz ? (zdR + (nL == 5 ? zdR5 : zdR6)) : 0`,
`d4D = zz ? zdM4D : 0`, with `zz = zOn && inZ && ilOk`. In the winner `zdR = zdR5 = zdR6 = 0`, so
`dR = 0` and the transition bar is exactly `-MRT`. `-ZM4D 1.2` DOES apply to `bar4D` in the
transition band (`bar4D + d4D = -1.2 + 1.2 = 0.0`). `zOn` is true in the winner (`zdM4 = -0.5`,
`zdM4D = 1.2`, `main.cc:2918-2921`). `-ZIL` unset -> `ilOk = true` (`main.cc:2953-2959`).

**Winner reduction.** With `-MD 1e9` (`m3ThetaD = 1e9`), `mD < m3ThetaD` is always true, so the
exempt-5+ kill degenerates to a **pure mX floor**:
```
kill(exempt 5+)  <=>  mX < barR
barR = -1.2   for |etaInnerT3| in [0, 1.1)      (-MRB)
barR = -1.2   for |etaInnerT3| in [1.1, 1.7)    (-MRT, dR = 0)
barR = -1.800 for |etaInnerT3| >= 1.7 or no member T3   (-MR)
```
i.e. the winner tightens the exempt-5+ floor by +0.6 in barrel+transition and leaves the endcap
loose. Positive/higher bar = kills more (`main.cc:672`).

### 2.5 Port dependencies + notes

Consumes: the three-class chain gate logits `z3` (`runChainInference3`), `chainDca`, `chains.nLayers`,
`chains.items/offsets` (member-T3 list), `ev.t3_eta`. All already present in
`src/alpaka/ChainGate.h` — the existing kill is at `src/alpaka/ChainGate.h:614`
(`if (mD < cfg.m3ThetaD && mX < cfg.m3ThetaR + dR)`), which currently reads the **global**
`cfg.m3ThetaR`. The port is: add `cfg.m3ThetaRB`, `cfg.m3ThetaRT` (and, for completeness,
`cfg.m3Theta4DB/DT` next to `src/alpaka/ChainGate.h:601`-region T4 branch), compute `inB`/`inZ` from
the innermost-member-T3 `|eta|` already needed for `inZ`, and select `barR` before the comparison.

GPU notes: purely per-chain, no cross-chain state, no map, no scan. Fully parallel, one extra
compare-select per chain. Resolve the `kMrUnset -> global` fallback **on the host** when filling
`ChainConfig` so the device never sees the 1e30 sentinel.

---

## 3. `-a` / `-a2` / `-a3` — attach margin eta bands

Winner: `-a 5.0`, `-a2 5.0`, `-a3 6.0` (so barrel == transition, endcap tighter by 1.0).

### 3.1 Storage and sentinels

| flag | variable (main) | field (params) | decl | default | band |
|---|---|---|---|---|---|
| `-a`  | `thetaAttach` | `AttachParams::thetaAttach` via `gap.pref.thetaAttach` | `main.cc:584` | `0.0f` | `\|eta\| < 1.1` |
| `-a2` | `aThetaT` | `GeneralAttachParams::thetaAttachT` | `main.cc:864` / `AttachDelivery.h:67` | `1e9f` | `1.1 <= \|eta\| < 1.7` |
| `-a3` | `aThetaE` | `GeneralAttachParams::thetaAttachE` | `main.cc:865` / `AttachDelivery.h:68` | `1e9f` | `\|eta\| >= 1.7` |

Design note: `main.cc:859-865`, `AttachDelivery.h:61-69`. Rationale: the attach head's logit
calibration shifts ~4.6 units across eta, so one global margin is a different working point per band.

**Sentinel `1e9` = unset = follow `-a`** (opposite of `-CCS`'s per-band OFF). Resolved TWICE,
harmlessly:
* Host, `main.cc:1553-1556`: `if (aThetaT >= 1e8f) aThetaT = thetaAttach; if (aThetaE >= 1e8f) aThetaE = thetaAttach;`
* Again inside the stage, `AttachDelivery.cc:78-79`.

Plumbing: `main.cc:3149-3152`
```cpp
gap.pref.thetaAttach = thetaAttach;
gap.thetaAttachT = aThetaT;   // -a2 (1.1-1.7)
gap.thetaAttachE = aThetaE;   // -a3 (1.7+)
```

### 3.2 The band lookup site (`AttachDelivery.cc:75-98`)

```cpp
const float thA0 = params.pref.thetaAttach;                                   // 77
const float thAT = (params.thetaAttachT < 1e8f) ? params.thetaAttachT : thA0;  // 78
const float thAE = (params.thetaAttachE < 1e8f) ? params.thetaAttachE : thA0;  // 79
const int nPlsEta = (int)ev.pLS_eta.size();                                   // 80
for (const AttachPair& pr : pairs) {
  const float lo = attachLogit(pr.f);                                         // 82
  ++io.nScored;
  if (io.recordPairs) io.pairLog.push_back({kAttachTargetChain, targetChains[pr.chainPos], pr.plsRow, lo});  // 85-86
  if (params.writeBestLogit && pr.plsRow >= 0 && pr.plsRow < (int)io.plsBestChainLogit.size() &&
      lo > io.plsBestChainLogit[pr.plsRow])
    io.plsBestChainLogit[pr.plsRow] = lo;                                     // 87-89
  float thr = thA0;                                                           // 92
  if (pr.plsRow >= 0 && pr.plsRow < nPlsEta) {
    const float ae = std::fabs(ev.pLS_eta[pr.plsRow]);                        // 94
    thr = (ae < 1.1f) ? thA0 : ((ae < 1.7f) ? thAT : thAE);                   // 95
  }
  if (lo < thr) continue;                                                     // 97-98
  ...
}
```

Exact semantics:
* **Which eta: `std::fabs(ev.pLS_eta[pr.plsRow])` — the pLS (SEED) eta of the pair**, not the chain
  eta, not the emitted TC eta. Reason (`main.cc:862-865`): "Band is taken from the pLS eta of the
  pair (the same quantity `xcThetaOf` uses), so one seed is priced in one bin no matter which chain
  bids for it." **Contrast `-CCS` and `-MRB/-MRT`, which use the chain/TC eta.**
* Bands are the literals `1.1f` / `1.7f` (`AttachDelivery.cc:95`), **not** `-ZE1`/`-ZE2`.
* Out-of-range `plsRow` falls back to the barrel bin `thA0` (`AttachDelivery.cc:92`).
* Acceptance is `lo >= thr` (the `continue` is on `lo < thr`).
* The band lookup runs unconditionally, even when all three bins are equal
  (`AttachDelivery.cc:90-91`) — deliberate, so the default command line exercises the same code path.
* **Ordering inside the loop matters:** `pairLog` append (85-86) and `plsBestChainLogit` update
  (87-89) happen **BEFORE** the band threshold (97-98). So `-a*` does **not** affect the `-RPS/-RPSA`
  evidence, and does **not** affect the `-CCS` pair log. Any port that hoists the threshold earlier
  changes `-RPSA` and `-CCS` behaviour.
* Stage B (`gaStageT3`) uses the single global `params.thetaAttachT3` (`-AT3`) with **no** band split
  (`AttachDelivery.cc:167`).
* Stage A2 (`-a4/-a42/-a43`) reuses the identical code with its own three constants
  (`main.cc:3863-3865`); the `-a4` bins follow `-a4`, **not** `-a` (`main.cc:1557-1561`).

### 3.3 getopt pre-scan constraint

`-a2`/`-a3` (and `-a4`/`-a42`/`-a43`) **must** be consumed by the pre-scan, at
`main.cc:1228-1239`, longest-first (`-a42`, `-a43`, `-a4`, `-a2`, `-a3`):

> `main.cc:1228-1229`: "B02 `-a2`/`-a3`: MUST be consumed here. getopt's `"a:"` would parse `-a2` as
> `-a` with the value `2` and silently move the barrel margin instead."

`-a` itself is a genuine getopt option (`"...A:a:B:..."`, `main.cc:1379`; `case 'a'` at
`main.cc:1428-1429`). Hence the resolution of `aThetaT/aThetaE -> thetaAttach` **cannot** live with
the pre-scan defaults and is done at `main.cc:1552-1556`, immediately after getopt returns
(`main.cc:1545-1547` states this explicitly for the sibling `-RPSA/-RPST`).

Silent-failure mode to guard in the port: if `-a2` reaches getopt, the barrel margin becomes `2` and
the run still "succeeds" with completely different physics.

### 3.4 Port dependencies + GPU notes

Consumes: `ev.pLS_eta` (per-pLS eta, must be available in the attach candidate struct), the attach
head logit, and the stage-A chain-target list. In the port, `src/alpaka/ChainAttach.h:785`
currently thresholds on a single `cfg.attachTheta`; the change is to carry the pLS `|eta|` in the
staged pair record (`AttachPlsPre` already carries per-pLS quantities: `tanLambda`, `phiMask`, ...)
and select `thr` per pair. `attachOrderFloat`/`plsBest` atomicMax at
`src/alpaka/ChainAttach.h:782-783` must stay **before** the threshold, matching
`AttachDelivery.cc:87-89` vs `97`.

GPU notes: per-pair scalar select, no extra memory, no scan. Resolve the `1e9` fallbacks on the host
so the device sees three finite floats.

---

## 4. `-EXR` — chain-extension `|rz|` window

Winner: `-EXR 4.0` (was `2.0` in the frozen `M19` block of `syn_run.sh`). Pure numeric retune.

### 4.1 Storage / plumbing

| flag | variable | decl | default | winner |
|---|---|---|---|---|
| `-EXR` | `extendRz` -> `ExtendParams::rzWindow` | `main.cc:831` / `Extend.h:45` | `0.f` (= combined test) | `4.0` |
| `-EXW` | `extendWindow` -> `ExtendParams::window` | `main.cc:830` / `Extend.h:43` | `0.5f` | `0.25` |

Pre-scan parse: `main.cc:1160-1165` (`-EXW`, `-EXR`, longest-first, "consumed HERE so getopt's `-e`
cannot swallow them", `main.cc:1159-1160`). Plumbed at `main.cc:3989-3992`:
`exp.window = extendWindow; exp.rzWindow = extendRz;`.

Sentinel: `rzWindow <= 0` = **combined** test (single sqrt window); `rzWindow > 0` = **split** test.
`0` is the only "off" value; the winner is firmly in the split regime, as was the frozen `-EXR 2.0`.

### 4.2 The one comparison that uses it (`Extend.cc:319-329`)

`win = (double)p.window`, `win2 = win*win` at `Extend.cc:217-218`. Inside the shared candidate test
`testCand` (`Extend.cc:290-342`), after the residuals are formed
(`Extend.cc:312-317`: `rxy = |candidateAnchor - fit.center| - fit.R`;
`rrz = mz - (fit.a + fit.b * sC)` with `sC = sT +/- chord`):

```cpp
double res;
if (p.rzWindow > 0.f) {                                          // 319
  // Split test: two residuals, two windows. RANKING key stays the xy residual.
  if (std::fabs(rxy) > win || std::fabs(rrz) > (double)p.rzWindow)   // 323
    return;                                                       // reject candidate
  res = std::fabs(rxy);                                            // 325
} else {
  res = std::sqrt(rxy*rxy + rrz*rrz);                              // 327
  if (res > win) return;                                           // 328
}
```

**Exactly one comparison uses `-EXR`: `std::fabs(rrz) > (double)p.rzWindow` at `Extend.cc:323`**
(rejection when true). It is the rz half of the split acceptance
`|rxy| <= -EXW && |rrz| <= -EXR`. `p.rzWindow` is compared as a `double`-promoted `float`.

**No structural change, confirmed.** `-EXR 2.0 -> 4.0` changes only that numeric bound. Specifically:
* The **ranking key is unaffected**: `res = |rxy|` in the split branch regardless of `-EXR`
  (`Extend.cc:320-322`, `325`).
* The **tie-break is unaffected**: `bestHitKey = (anchorHit << 32) | otherHit`, smaller wins
  (`Extend.cc:283-287`, `331-338`).
* The **`-EXU` ambiguity guard is unaffected** in form: it compares `secondRes` to `bestRes`, both
  `|rxy|` (guard at `Extend.cc:376-379`; `secondRes` maintained at `Extend.cc:334-341`).
* The **`-EXF` chi2 guard is unaffected**: it uses `win2 = window^2` only
  (`Extend.cc:218` definition, `Extend.cc:397` use:
  `chi2New > chi2Factor * max(fit.chi2, win2)`) — `rzWindow` never enters it.
* Every other prefilter (`-EXD` `maxDist`, `-EXJ` `maxJump`, layer-mask, `claimedHit`, `-EXS`
  segment-linked neighbour walk) is untouched (`Extend.cc:291-311`, `344-352`).
* Widening `-EXR` can only **admit more** candidates -> more extensions -> longer chains. It cannot
  change which chains are examined (`-EXL` `minLayers`, `-EXC` `maxChi2` guards, `Extend.h:59-60`,
  `62-68`).

### 4.3 Port dependencies + GPU notes

Consumes: the chain's own circle+rz fit (`fit.cx/cy/R/a/b`), the candidate MD anchor position, the
arc-length `sC`, and the hit-level claim map — all already required by the extension stage itself. If
`src/alpaka/` has no extension stage yet, `-EXR` is a `ChainConfig` float on that stage's port; if it
does, `-EXR` is a **one-constant change** and nothing else.

GPU notes: per-(chain-end, candidate MD) scalar compare inside the existing candidate loop. No map,
no pair log, no cross-candidate state beyond the existing best/second-best registers.

---

## 5. Cross-mechanism summary

### 5.1 The three different `|eta|` quantities — do not mix them

| mechanism | `|eta|` source | citation | bands |
|---|---|---|---|
| `-CCS/-CCS2/-CCS3` | emitted TC eta = `ctc.eta` = `ev.t3_eta[innermost member T3]` | `main.cc:4174`, `K9K10.cc:425` | literal `1.1` / `1.7` |
| `-MRB/-MRT` (`-M4B/-M4T`) | chain innermost member-T3 eta (same quantity) | `main.cc:2953-2959`, `2944`, `2949` | `-ZE1` / `-ZE2` (= 1.1 / 1.7) |
| `-a2/-a3` (and `-a42/-a43`) | **pLS (seed) eta of the pair** | `AttachDelivery.cc:94` | literal `1.1` / `1.7` |

`-CCS` and `-MRB/-MRT` use the same physical eta but reach it differently (`otc.eta` vs a re-derived
`ev.t3_eta[...]`) and use different band constants (literals vs `-ZE1/-ZE2`). Both coincide at the
default `-ZE1 1.1 -ZE2 1.7`.

### 5.2 Sentinel semantics, side by side

| family | sentinel | meaning | resolution site |
|---|---|---|---|
| `-CCS/-CCS2/-CCS3` | `1e9f` | **OFF for that band** (no fallback) | none — tested in place, `main.cc:4106`, `4175` |
| `-MRB/-MRT/-M4B/-M4T` | `kMrUnset = 1e30f` | follow `-MR` / `-M4D` | `main.cc:1367-1374` (post-pre-scan) |
| `-a2/-a3` | `1e9f` | follow `-a` | `main.cc:1553-1556` (post-getopt) + `AttachDelivery.cc:78-79` |
| `-a42/-a43` | `1e9f` | follow `-a4` (NOT `-a`) | `main.cc:1557-1561` |
| `-EXR` | `0.f` | combined (unsplit) window | none — tested in place, `Extend.cc:319` |

### 5.3 Ordering constraints, consolidated

1. Pre-scan (before getopt) must consume: `-CCS2`, `-CCS3`, `-CCS` (before `-CC*`), `-MRB`, `-MRT`,
   `-M4B`, `-M4T` (before `-MR`, and before `-M4`/`-M4D`), `-a42`, `-a43`, `-a4`, `-a2`, `-a3`
   (because getopt owns `a:`), `-EXW`, `-EXR` (because getopt owns `e`).
2. Sentinel resolution order: `-MR*` family right after the pre-scan (`main.cc:1364-1374`);
   `-a2/-a3` and `-RPSA/-RPST` **after getopt** (`main.cc:1548-1561`) because `-a` arrives via getopt.
3. Pipeline order that `-CCS` depends on: stage A1 scoring (writes `plsBestChainLogit`, pair log)
   -> `-XC4` score-only append -> `-RD` dedup -> `chainAttachPls = ga.chainPls` -> `m16RefreshSupp()`
   (the `-RPS` retirement, `main.cc:3910-3911`) -> `-EX` extension -> K10 -> **CCS map build**
   (`main.cc:4109`) -> emission loop -> stage B / `-CC`. `-CCS` must run after the grants are final
   and before/at emission; it must NOT run before `m16RefreshSupp()` (would not matter, since it
   writes nothing that `-RPS` reads — this is exactly the invariant being protected).
4. Inside `gaStageChains`: pair-log append and `plsBestChainLogit` update must stay **before** the
   `-a` band threshold (`AttachDelivery.cc:85-89` vs `97-98`).

### 5.4 GPU-portability bottom line

* `-MRB/-MRT`, `-a2/-a3`, `-EXR`: embarrassingly parallel scalar selects/compares inside kernels
  that already exist. No new memory, no scans, no pair logs.
* `-CCS`: the only one with a data-structure requirement. It is **one inverted map**
  (`plsOwnerChain[nPls]`, `O(nChains)` to build) **plus one restricted pass** to obtain
  `ccsLoserLogit[nChains]` (bare targets x owned pLS only), then a per-TC boolean at emission.
  It must **never** be a pairwise chain-vs-chain comparison and must **never** materialise the full
  `nScored`-sized pair log on device. Suppression must be folded into the TC compaction (prefix sum)
  so the prototype's `tcPos` running-index lockstep hazard does not exist in the port.
