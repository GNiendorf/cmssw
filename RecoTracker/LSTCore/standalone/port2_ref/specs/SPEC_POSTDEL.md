# SPEC_POSTDEL -- translation table for the prototype's post-deletion emulation flags

Scope: the two frozen flag groups of the winner command line

    CFF="-CF 1 -CFC 1"
    POSTDELP2="-ZPF 3 -ZP5 1 -RT3 1 -ZP8 6"

Reference binary: `standalone/protoFINAL2/main.cc` (5995 lines; `protoFINAL/main.cc` is the
5798-line predecessor that `synth_ref/syn_run.sh` defaults to -- every flag cited below has the
same semantics in both, but all line numbers in this file are protoFINAL2).

Measured baseline this group defines (`fin_ref/fin_scoreboard.txt:5`, `fin_ref/STATUS.md:155`):

    POSTDELP2   eff 0.80626  dup 0.20553  fake 0.04630  nTC 655866   (300 evt, no BASE overrides)

The full command line the row was measured on is `fin_ref/STATUS.md:118-126`; POSTDELP2 is that
line with the assembled BASE (`-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`) removed.

Target: `P/src/alpaka` with the master switch `useChainTracking_`
(`P/src/alpaka/LSTEvent.h:66`), no LST code deleted.

---

## 0. Summary table

| flag | prototype meaning | tree ON-state must | status today |
|---|---|---|---|
| `-CF 1` | binned candidate prefilter instead of the full analytic scan, for BARE-T3 targets | use the K8a grid on the bare-T3 target array | ALREADY DONE (structurally; the grid is the only form that exists) |
| `-CFC 1` | same prefilter extended to CHAIN targets | use the K8a grid on the chain target array | ALREADY DONE |
| `-ZPF 3` | zero `t3_partOfPT5` AND `t3_partOfPT3` for every T3 | not consult `triplets.partOfPT5()/partOfPT3()` when selecting claim candidates | PARTIAL -- bit0 done by default, bit1 only under an env var |
| `-ZP5 1` | clear the pT5/T5 routing containers | carry no LST type-7 row and let no type-7 row pre-claim | ALREADY DONE (and a provable no-op given `-RT5 1`) |
| `-RT3 1` | drop every carried type-5 row, switch `dropPartOfPT3` off, and (via `-T3E` default) turn bare-T3 stage B ON as the pT3-class delivery | `replacePT3=true`, `dropPartOfPT3=false`, stage B running and emitting type-5 rows | PARTIAL -- fully implemented but gated on `LST_CHAIN_T3REPLACE`, not on `useChainTracking_` |
| `-ZP8 6` | replace the carried bare-pLS universe with `pLS_isDupAlgPass2 == 0`, i.e. undo CrossCleanpLS entirely | skip `CrossCleanpLS` so `AddpLSasTrackCandidate` admits the post-pass-2 set | MISSING |

---

## 1. `-CF 1` and `-CFC 1` -- candidate finding

### 1.1 What the prototype does

Declarations `protoFINAL2/main.cc:995` (`candMode`, `-CF`) and `:1001` (`candChainToo`, `-CFC`);
pre-scan consumption `:1274-1276`. Modes (`protoFINAL2/PixelAttachCand.h:17-27`):

* `kCandAnalytic (0)` -- the frozen full scan `for each target { for p in 0..nPls }`, DEFAULT.
* `kCandBinned (1)` -- scalar binned prefilter keyed on `(target rt bin) x (seed tanLambda bin) x
  (phi bin)`. **Provable superset** of the analytic candidate set: the tanLambda axis is
  non-expansive under clamping, and `phiDir(P,R)` is monotone in `R` so the per-`r`-bin arc is
  computed exactly from its endpoints and padded (`PixelAttachCand.h:29-64`). Because it is a
  superset and the predicate is then evaluated exactly on the candidates it returns, the emitted
  pair list is IDENTICAL to mode 0's -- `-CFA 1` counts the analytic-accepted pairs the candidate
  set missed and that counter must be 0 (`PixelAttachCand.h:66-69`).
* `kCandMap (2)` -- a production-dumped candidate-pair list read from `-CFM <path>`, used as the
  prefilter (`PixelAttachCand.h:72-88`).

`-CFC` (`candChainToo`) extends the prefilter from the bare-T3 targets to the chain targets:
`gap.pref.candChainToo = (candChainToo >= 0.5f)` at `main.cc:3159`, the index being built at
`main.cc:3160-3167` (`k8BuildPlsCandIndex`). The pairdump path sets the same three fields at
`main.cc:2171-2178`.

**Physics: neutral.** `-CF 1 -CFC 1` is a pure speedup that exists because the bare-T3 universe
(~35k targets x ~21k pLS) is unaffordable to full-scan (`PixelAttachCand.h:8-12`). It is in the
frozen line only because the frozen line runs stage B (`-RT3 1`, see section 4).

### 1.2 Corresponding tree behaviour

The tree has NO analytic full scan -- the grid IS the implementation:

* chain targets: `P/src/alpaka/ChainAttach.h:38` (`K8a ChainAttachGridBounds / GridCount /
  GridPrefix / GridScatter`), superset proof `ChainAttach.h:44-70`, launched from
  `P/src/alpaka/LSTEvent.dev.cc` inside `attachChains` (grid stages up to `:2239`).
* bare-T3 targets: `P/src/alpaka/ChainAttachT3.h:47-80` -- the bounds kernel is re-run over the
  bare-T3 target array into its own `rMin/rMax` buffers, giving a second independent grid; superset
  proof transfers verbatim (`ChainAttachT3.h:53-79`).
* geometry constants `P/interface/ChainConfig.h` `kAttachRBins/kAttachRBinWidth/kAttachTanLBins/
  kAttachTanLLo/kAttachPhiBins/kAttachPhiPad`.
* the superset audits that correspond to `-CFA 1`: `ChainAttachAudit` under
  `LST_CHAIN_T3_AUDIT` (`LSTEvent.dev.cc:2694-2721`, `MISSING=` counter must be 0).

### 1.3 Status

**ALREADY DONE**, both flags, for the chain arm unconditionally and for the bare-T3 arm whenever
that arm runs at all. No work item.

### 1.4 Note

There is nothing to gate: the tree cannot express `-CF 0`. Any port note claiming "`-CF 1` needs
porting" is wrong; what needs porting is the *thing the prefilter is for*, i.e. stage B (section 4).

---

## 2. `-ZPF 3` -- zero the pixel-consumed flags

### 2.1 What the prototype does

`main.cc:853` declares `auZpf` (`-ZPF <mask>`: bit0 zero `t3_partOfPT5`, bit1 zero
`t3_partOfPT3`); parsed at `:1330-1331`. Implementation, per LST entry, right after
`reader.loadEntry` and BEFORE `k1BuildIncidence`:

```
main.cc:2786-2792
  if (auZpf >= 0.5f) {
    const int zm = static_cast<int>(auZpf + 0.5f);
    if (zm & 1) std::fill(ev.t3_partOfPT5.begin(), ev.t3_partOfPT5.end(), false);
    if (zm & 2) std::fill(ev.t3_partOfPT3.begin(), ev.t3_partOfPT3.end(), false);
  }
```

Vector SIZES are preserved so the `havePixFlags` guard still passes (`main.cc:2785`;
`protoFINAL2/K9K10.cc:66-68`). Rationale recorded at `main.cc:2782-2785`: both flags are written
only by the pT5 and pT3 builders, both of which are in the P2.7 deletion set, so post-deletion both
are false for every T3. That is verified in this tree -- the only writers are
`P/src/alpaka/PixelQuintuplet.h:732-733` and `P/src/alpaka/PixelTriplet.h:830`.

Readers of the zeroed vectors, all of which therefore see `false`:

* the K9 claim-candidate predicate `protoFINAL2/K9K10.cc:82-91` (the decision that matters);
* the K9K10 funnel mirror `K9K10.cc:335-359`, the main.cc funnel mirror `main.cc:4027-4050`, the
  per-chain diagnostic `main.cc:1683-1691`, `DumpWriter.cc:253-263` -- all diagnostics.

**Redundancy note.** Under the frozen line `-ZPF 3` is functionally redundant with `-RT5 1 -RT3 1`,
because `main.cc:3194-3197` already forces both halves off:

```
if (replT5 >= 0.5f) ap.dropPartOfPT5 = false;
if (replT3 >= 0.5f) ap.dropPartOfPT3 = false;
```

so the K9 predicate `(partOfPT5 && dropPartOfPT5) || (partOfPT3 && dropPartOfPT3)` is false either
way. `-ZPF 3` additionally silences the diagnostics. The reasoning is stated at `main.cc:3191-3193`
and mirrored in `P/interface/ChainConfig.h` ("`-RT5 1` removes every carried type-7 row, so the
partOfPT5 half would be killing chains for colliding with rows that no longer exist").

### 2.2 Corresponding tree behaviour

Single consumer: `P/src/alpaka/ChainArbitrate.h:152-160`, inside `ChainOrderAndSelect`:

```
if (cfg.dropPixelConsumed) {
  ...
  consumed = (triplets.partOfPT5()[t3] && cfg.dropPartOfPT5) ||
             (triplets.partOfPT3()[t3] && cfg.dropPartOfPT3);
}
```

The ON-state requirement is therefore exactly: `cfg.dropPartOfPT5 == false` AND
`cfg.dropPartOfPT3 == false` (equivalently `dropPixelConsumed == false`). The flags themselves stay
set in the tree (the builders still run) -- they must simply not be consulted.

### 2.3 Status

* bit0 (`partOfPT5`): **ALREADY DONE**. `P/interface/ChainConfig.h` default
  `bool dropPartOfPT5 = false;  // == !replacePT5`.
* bit1 (`partOfPT3`): **PARTIAL**. Default is `bool dropPartOfPT3 = true;  // == !replacePT3`
  in `ChainConfig.h`; it is flipped to `false` only inside
  `LSTEvent.dev.cc:1402-1406`, guarded by `chainT3ReplaceEnabled()`, i.e. by the environment
  variable `LST_CHAIN_T3REPLACE` (`LSTEvent.dev.cc:2624-2628`), not by `useChainTracking_`.

### 2.4 Recommended implementation

Fold into the `-RT3` item (section 4): make `replacePT3 = true` / `dropPartOfPT3 = false` the
`useChainTracking_` ON-state rather than the env-var state. Concretely, replace the guard at
`LSTEvent.dev.cc:1402` with the master switch (the function is only ever reached from
`arbitrateChains`, which is itself only called under `useChainTracking_` at
`LSTEvent.dev.cc:3243-3244`) -- so the change is a strict no-op with the flag off by construction.
Do NOT change the `ChainConfig.h` defaults: `ChainConfig` is also the CMSSW PSet surface, and the
defaults document the *frozen prototype* values per-field. Set them in `arbitrateChains` where the
existing precedent already lives.

---

## 3. `-ZP5 1` -- kill the pT5-side routing

### 3.1 What the prototype does

`main.cc:854` declares `auZp5`; parsed `:1332-1333`. Implementation, per entry (`main.cc:2793-2801`):

```
if (auZp5 >= 0.5f) {
  ev.pT5_t5Idx.clear();
  ev.t5_hitIndices.clear();
  ev.pT5_plsIdx.clear();
  std::fill(ev.tc_pt5Idx.begin(), ev.tc_pt5Idx.end(), -999);
}
```

Rationale `main.cc:2793-2796`: the pT5/T5 SoAs are deleted, so every route into them is absent;
clearing the containers makes each consumer take its already-present bounds-check branch, which is
exactly "this row owns nothing / has no outer-tracker hit list". Note it does NOT remove the
`tc_type == 7` rows themselves, and it does NOT undo LST's `CrossCleanT5` / `CrossCleanpLS` T5 arm
(the latter is what `-ZP8 6` handles, section 5).

Complete reader list for the four cleared containers, and their guards:

| site | what it resolves | guarded by |
|---|---|---|
| `main.cc:3491-3496` `rowPls(it,7)` | type-7 row -> pLS | callers skip suppressed rows (`:3571`, `:5116`, `:5077`) |
| `main.cc:3602-3609` pre-claim owner hits, type 7 | `tc_pt5Idx -> pT5_t5Idx -> t5_hitIndices` | `if (m16RowSuppressed[it]) continue;` at `:3602` |
| `main.cc:4381-4392` `-XC` carried-hit marking, type 7 | same route | `if (m16RowSuppressed[it]) continue;` at `:4381` |
| `main.cc:2030-2069` legacy (`-A != 4`) pre-claim | same route | not reachable at `-A 4` |
| `OutputWriter.cc:247-250, 275-283` | type-7 row -> pLS / OT hits | suppressed rows are not written (`main.cc:5367`) |

Under `-RT5 1` EVERY `tc_type == 7` row is unconditionally in `m16RowSuppressed`
(`main.cc:3557-3565`):

```
if ((ty == 7 && replT5 >= 0.5f) || (ty == 5 && replT3 >= 0.5f)) m16RowSuppressed[it] = 1;
```

so every reader above short-circuits before touching the cleared containers.

**Conclusion (from the code, not measurement): with `-RT5 1` also set, `-ZP5 1` is a no-op on
physics inside protoFINAL2.** It is belt-and-braces: it guarantees that any *future* unguarded
reader also sees the post-deletion value.

### 3.2 Corresponding tree behaviour

Two requirements, both about type-7 rows never being usable:

1. no carried type-7 row survives into the chain stages -- `ChainCompactCarriedTCs`
   (`P/src/alpaka/ChainArbitrate.h:862-906`) sets `keep = !cfg.replacePT5` for `LSTObjType::pT5`
   (`:878-879`) and drops `T5` / `T4` outright (`:880`, by falling through with `keep == false`), with the parallel-backend transcription
   in `P/src/alpaka/ChainParallel.h:122-140`; `replacePT5 = true` is the `ChainConfig.h` default.
2. no type-7 row pre-claims -- the `-PU` pre-claim walk explicitly accepts only pT3 rows:
   `ChainArbitrate.h:266-267` (`if (candsBase.trackCandidateType()[row] != LSTObjType::pT3)
   continue;`), and it runs after the compaction (`:261-265`).

### 3.3 Status

**ALREADY DONE.** Nothing to implement. The one thing to record is *why* it is a no-op, so that a
future reader does not go looking for a missing suppression: the type-7 rows are gone before any
consumer, exactly as `m16RowSuppressed` makes them unreachable in the reference.

### 3.4 Caveat that `-ZP5` does NOT cover (and which the port inherits)

`-ZP5` does not model the deletion's effect on `CrossCleanpT3` (`P/src/alpaka/TrackCandidate.h:172`)
or `CrossCleanT4` (`:423`), both of which today kill pT3 / T4 rows against pT5 and T5 rows that
would not exist post-deletion. In the frozen line this is moot for two reasons only:
`-RT5 1` drops every type-7 row and `-RT3 1` drops every type-5 row, so no surviving carried row's
existence depends on those crosscleans. **If `-RT3` is ever turned off in the tree ON-state, this
becomes a real un-emulated dependence** -- the tree would keep pT3 rows that the post-deletion
universe would have kept MORE of. Flagging, not fixing.

---

## 4. `-RT3 1` -- wholesale pT3-class replacement

### 4.1 What the prototype does

`main.cc:849` declares `replT3` (`-RT3`: drop all carried type-5 rows, full pT3 replacement);
parsed `:1217-1218`. Four distinct consequences:

1. **Carried-row drop.** `main.cc:3561-3563`: every `tc_type == 5` row goes into
   `m16RowSuppressed`, counted as `nM16SuppT5`. Those rows are then excluded from pre-claim
   (`:3602`), from the `-XC` carried-hit marking (`:4381`), from `plsDelivered` in the `-ZP8`
   block (`:5116`) and from the output (`:5367`).
2. **`dropPartOfPT3` off.** `main.cc:3196-3197`: `if (replT3 >= 0.5f) ap.dropPartOfPT3 = false;`
   (redundant with `-ZPF 3`, see 2.1).
3. **Bare-T3 stage B ON.** `main.cc:2655`:
   `const bool doT3Stage = (t3StageEnable < -0.5f) ? (replT3 >= 0.5f) : (t3StageEnable >= 0.5f);`
   with `t3StageEnable` defaulting to `-1.f` (`main.cc:1002`, "`-1` = follow `-RT3` (frozen
   coupling)"). POSTDELP2 passes no `-T3E`, so **stage B IS ACTIVE in the POSTDELP2 baseline** and
   delivers the pT3 class as attach owners. (The assembled BASE's `-T3E 1` is therefore a no-op
   restatement of the coupling.) Rationale `main.cc:2652-2654`.
4. `main.cc:4670-4685` / `:5592-5593` / `:5711`: reporting and the `-XC` type-8 channel read
   `replT3` for their own accounting only.

### 4.2 Corresponding tree behaviour

All four pieces already exist:

1. `ChainCompactCarriedTCs` `ChainArbitrate.h:874-875` (`if (ty == pT3) keep = !cfg.replacePT3;`)
   plus the counter resets `:899-903`; parallel form `ChainParallel.h:133-134`, `:273`.
2. `ChainArbitrate.h:157-158` with `cfg.dropPartOfPT3 == false`.
3. Stage B: `P/src/alpaka/ChainAttachT3.h` in full; driver `LSTEvent::attachBareT3Probe`
   (`LSTEvent.dev.cc:2400`), called at `:2248` -- after stage A's contention and `-RD` dedup are
   final and BEFORE the carried-row retirement, which is the reference order
   (`LSTEvent.dev.cc:2241-2247`). Ownership publication onto the LIVE arrays:
   `ChainAttachT3PublishOwnership`, launched at `LSTEvent.dev.cc:2613-2622`, kernel
   `ChainAttachT3.h:591-603` (the margin shift `attachTheta - attachThetaT3` is an order
   isomorphism, so the shipped `-RPS` test is reused unchanged).
4. Delivery: `LSTEvent::emitBareT3TCs` (`LSTEvent.dev.cc:2730-2777`) appends one type-5 row per
   owner via `ChainEmitBareT3TCs`, called from `arbitrateChains` at `LSTEvent.dev.cc:1894`.

### 4.3 Status

**PARTIAL.** Everything is implemented and none of it runs in the ON-state:

* `LSTEvent.dev.cc:1402` `if (chainT3ReplaceEnabled())` -- sets `replacePT3` / `dropPartOfPT3`.
* `LSTEvent.dev.cc:2400`+ `attachBareT3Probe` -- first statement returns unless
  `chainT3AttachEnabled()`.
* `LSTEvent.dev.cc:2613` -- publish gated on `chainT3ReplaceEnabled()`.
* `LSTEvent.dev.cc:2740-2741` -- `emitBareT3TCs` returns unless `chainT3ReplaceEnabled()`.
* predicates `LSTEvent.dev.cc:2616-2628` -- both read `std::getenv` via `chainT3EnvOn`.

`ChainConfig.h` defaults `replacePT3 = false`, `dropPartOfPT3 = true`, `replacePT5 = true`,
`dropPartOfPT5 = false` -- i.e. the `-RT5 1 -RT3 0` freeze, one generation behind POSTDELP2.

### 4.4 Recommended implementation

Replace the three env predicates with the master switch, keeping the code paths byte-identical:

1. In `arbitrateChains`, change `LSTEvent.dev.cc:1402` from `if (chainT3ReplaceEnabled())` to
   unconditional (the function body already only runs under `useChainTracking_`; see 2.4). Keep the
   two assignments and the comment.
2. Change `chainT3AttachEnabled()` / `chainT3ReplaceEnabled()` (`LSTEvent.dev.cc:2620-2628`) so
   they return `true` by default and can be forced OFF by the env var (`LST_CHAIN_T3REPLACE=0`),
   preserving the A/B lever the measurement round used. A cleaner equivalent: add two `bool` fields
   to `ChainConfig` (`enableBareT3Stage`, defaulting to the frozen `true`) and have the three call
   sites read `chainConfig_`, with the env var kept as an override. Either form is a strict no-op
   with `useChainTracking_ == false`, because all four call sites sit inside
   `arbitrateChains` / `attachChains`, both reached only from `LSTEvent.dev.cc:3243-3244` (`if (useChainTracking_) arbitrateChains(nTotal);`).
3. **Not a physics item but a required cleanup**: the stage-B delivery currently makes a host round
   trip through `bareT3Triplet_` / `bareT3Pls_` / `bareT3Logit_` (`LSTEvent.dev.cc:2645-2652`,
   consumed at `:2741-2752`), explicitly described as "a scaffold artifact of the measurement
   config and NOT part of the design" (`LSTEvent.dev.cc:2733-2739`). It must become a device path
   before the ON-state is timed; it does not change any number.

---

## 5. `-ZP8 6` -- the post-deletion bare-pLS TC universe

### 5.1 What the prototype does

`main.cc:855` declares `auZp8`; parsed `:1334-1335`. The block runs at `main.cc:5093-5271` and is
active only when `auZp8 >= 0.5f && attachMode == 4`.

Framing (`main.cc:5081-5092`): today's carried type-8 rows are LST's set AFTER `CheckHitspLS`
pass 2 AND after `CrossCleanpLS`; both of those are in the P2.7 deletion set, only pass 1 survives
for certain. `pixelSegments.isDup()` is monotone (`|=1`, `|=2`, `=true` all leave it nonzero), so
today's admitted set is a strict SUBSET of the post-deletion one. The block adds the difference back
as synthetic type-8 deliveries.

Modes (`main.cc:5093-5101` and `:5148-5155`):

| mode | universe added back | mechanism |
|---|---|---|
| 0 | off | -- |
| 1 | UPPER bracket: every `isQuad` pLS not already delivered | no `pass1Removable` array is built (`:5163` requires `>= 2`), so `:5253` never skips |
| 2 | LOWER bracket: only pLS with no pass-1-qualifying quad partner | union-find graph, edge iff `>= 3` of 4 shared pixel hit rows and `\|dEta\| <= 0.1` (`:5165-5215`); `pass1Removable[p] = 1` if p has any such partner (`:5213`) |
| 3 | FAMILY MODEL: one representative per connected component; components that already have a delivered member add nothing, others elect the highest-pt member | `:5220-5240` |
| 4 | as 2/3 but the edge relation also admits the pass-2 predicate (`>= 1` shared hit OR `dR2 < 1e-5`) | `:5211` `const bool edge = (kv.second >= 3) \|\| (zp8Mode >= 4);` |
| 5 | MEASURED, after `CheckHitspLS` pass 1 only | `pass1Removable[p] = (ev.pLS_isDupAlgSelf[p] != 0)` |
| **6** | **MEASURED, after BOTH `CheckHitspLS` passes** | `pass1Removable[p] = (ev.pLS_isDupAlgPass2[p] != 0)` |

Mode 5/6 code, `main.cc:5142-5162`:

```
if (zp8Mode >= 5) {
  const std::vector<int>& alg = (zp8Mode == 5) ? ev.pLS_isDupAlgSelf : ev.pLS_isDupAlgPass2;
  if (alg.size() < nPls) { fatal "needs the pLS_isDupAlg* branches"; }
  pass1Removable.assign(nPls, 0);
  for (p) pass1Removable[p] = (alg[p] != 0) ? 1 : 0;
}
```

So **mode 6 selects exactly `{p : pLS_isQuad[p] && pLS_isDupAlgPass2[p] == 0}` as the bare-pLS TC
universe**, replacing the model-based union-find of modes 2-4. Comment `:5136-5141`: "Both passes
are pure seed self-cleaning (they read only pixelSeeds and pixelSegments), so mode 6 is the universe
that survives the P2.7 deletion if the second pass is kept and mode 5 is the universe if it is
dropped with the rest."

The add-back loop (`main.cc:5241-5271`) then applies OUR machinery to each candidate, so the
synthetic rows are subject to the same retirements as a carried row:

* skip non-`isQuad` (`:5243`);
* skip if `plsDelivered[p]` -- built at `:5113-5124` from all live type-7/5/8 rows, honouring
  `m16RowSuppressed`;
* skip if `pass1Removable[p]` (`:5253-5254`), i.e. for mode 6, if `isDupAlgPass2 != 0`;
* drop if the attach owns the seed (`ga.plsOwned[p] != 0`) or, under `-RPS`, if
  `ga.plsBestChainLogit[p] >= rpsThetaChain || ga.plsBestT3Logit[p] >= rpsThetaT3`
  (`:5256-5259`) -- the same predicate as `m16RefreshSupp` (`main.cc:3583-3586`);
* drop if `xcRetired[p]` (`:5262-5265`) -- the ported seed crossclean's channel (b). Inert in
  POSTDELP2 (no `-XC`), live in the assembled BASE.
* otherwise emit an `OutTC` with `type = 8`, `nhitOT = 0`, the pLS pt/eta/phi, and the pLS pixel
  hit rows (`:5266-5271` -> `:5222`ff of `OutTC` fill, `deliv = 4` provenance mark).

### 5.2 Where `pLS_isDupAlgSelf` / `pLS_isDupAlgPass2` come from

Branch creation: `standalone/code/core/write_lst_ntuple.cc:616-618`
(`pLS_isDupAlgSelf`, `pLS_isDupAlgPass2`, `pLS_isDupAlgFinal`), listed again at `:1894-1901`;
filled at `write_lst_ntuple.cc:1890-1902`:

```
ana.tx->pushbackToBranch<int>("pLS_isDupAlgSelf",
    ipLS < event->plsIsDupSelf_.size()  ? (int)event->plsIsDupSelf_[ipLS]  : -999);
ana.tx->pushbackToBranch<int>("pLS_isDupAlgPass2",
    ipLS < event->plsIsDupPass2_.size() ? (int)event->plsIsDupPass2_[ipLS] : -999);
ana.tx->pushbackToBranch<int>("pLS_isDupAlgFinal",
    ipLS < event->plsIsDupFinal_.size() ? (int)event->plsIsDupFinal_[ipLS] : -999);
```

`-999` marks "not recorded", so a stale ntuple cannot be mistaken for a measured zero
(`write_lst_ntuple.cc:1888-1890`).

Host vectors: `P/src/alpaka/LSTEvent.h:268-272`

```
std::vector<char> plsIsDupSelf_;   // end of pixelLineSegmentCleaning (CheckHitspLS pass 1)
std::vector<char> plsIsDupPass2_;  // after the second CheckHitspLS (both self-clean passes)
std::vector<char> plsIsDupFinal_;  // after CrossCleanpLS, before AddpLSasTrackCandidate
```

Snapshot sites, all gated on `dupSnapshotsEnabled()` (`LSTEvent.dev.cc:2592-2595`, env
`LST_DUP_SNAPSHOTS`, set by the standalone driver for `--allobj`); the copy helper is
`snapshotByteColumn` (`LSTEvent.dev.cc:2599-2611`), and the `alpaka::wait` is required because the
very next kernel overwrites the column:

* `LSTEvent.dev.cc:3560-3563` (inside `pixelLineSegmentCleaning`) -- after `CheckHitspLS`
  `secondpass = false` (launched `:3546-3556`, kernel named `:3551`) -> `plsIsDupSelf_`.
* `LSTEvent.dev.cc:3059-3062` -- after `CheckHitspLS` `secondpass = true` (launched `:3046-3056`,
  kernel named `:3050`) and before anything else touches the column -> `plsIsDupPass2_`.
* `LSTEvent.dev.cc:3222-3224` -- after `CrossCleanpLS` (launched `:3203-3219`, kernel named `:3205`)
  -> `plsIsDupFinal_`.

The kernel that computes the values: `P/src/alpaka/Kernels.h:786-864` `CheckHitspLS`. Pass
structure:
* pass 1 (`secondpass = false`): removes on `npMatched >= 3` of 4 shared pixel hit rows with
  `|dEta| <= 0.1`, quads beating trips then better score (`Kernels.h:799-852`);
* pass 2 (`secondpass = true`): additionally skips non-quads and anything already carrying bit 1
  (`:800`, `:814`), and removes on `npMatched >= 1 || dR2 < 1e-5` (`:854-862`);
* the write is `rmPixelSegmentFromMemory` -> `pixelSegments.isDup()[i] |= 1 + secondpass`
  (`Kernels.h:40-42`), which is where the monotonicity of the column comes from.

**Between the `plsIsDupPass2_` snapshot and `AddpLSasTrackCandidate`, the ONLY kernel that writes
`pixelSegments.isDup()` is `CrossCleanpLS`** (`P/src/alpaka/TrackCandidate.h:327-419`); it sets
`isDup() = true` (not `|=`) from the T5 embedding arm (`:369-379`), the pT3 hit-overlap / dR arm
(`:381-398`) and the pT5 hit-overlap / dR arm (`:399-416`). It writes nothing else -- its entire
observable effect is that column.

### 5.3 Corresponding tree behaviour

`AddpLSasTrackCandidate` (`TrackCandidate.h:706-741`, launched `LSTEvent.dev.cc:3228-3240`) admits exactly

```
if ((tc_pls_triplets ? 0 : !pixelSeeds.isQuad()[i]) || pixelSegments.isDup()[i]) continue;
```

(`TrackCandidate.h:718-719`; `tc_pls_triplets` is false by default -- `standalone/bin/lst.cc:74`,
`:256`). So with `CrossCleanpLS` skipped, `isDup()` still holds exactly the value that was
snapshotted as `plsIsDupPass2_`, and the admitted set becomes
`{isQuad && isDupAlgPass2 == 0}` -- **bit-for-bit the `-ZP8 6` universe**, with no add-back
machinery needed.

Allocation is already sized for it: `CountSurvivingTCs` counts pLS with the comment "upper bound -
before CrossCleanpLS" and the predicate `(tc_pls_triplets || isQuad[i]) && !isDup()[i]`
(`TrackCandidate.h:594-598`), launched at `LSTEvent.dev.cc:3070-3086`, i.e. after the pass-2
snapshot and before `CrossCleanpLS`. No headroom change.

The prototype's four post-selection drops map onto existing tree machinery, all of which already
runs after `AddpLSasTrackCandidate`:
* `plsDelivered` / `plsOwned` and the `-RPS` predicate -> `ChainSuppressCarriedTCs`
  (`P/src/alpaka/ChainAttach.h:1056`, kernel body: `drop = plsOwned[p] != 0u;` then
  `if (ty == pLS && cfg.attachSuppressBarePLS) drop = drop || (plsBest[p] >= thetaKey);`),
  launched `LSTEvent.dev.cc:2250-2265`;
* `xcRetired` -> the `-XC` seed-crossclean port, which is a SEPARATE work item (A11/A15) and is
  NOT part of the POSTDELP2 baseline.

### 5.4 Status

**MISSING.** `CrossCleanpLS` is launched unconditionally at `LSTEvent.dev.cc:3203-3219` (kernel named `:3205`), with no
reference to `useChainTracking_`. The ON-state today therefore delivers LST's `isDupAlgFinal`
universe, which is a strict subset of POSTDELP2's -- fewer bare-pLS rows than the measurement, so
both dup and eff will sit below the POSTDELP2 row.

### 5.5 Recommended implementation

Guard the single launch:

```
// P2.7 emulation: CrossCleanpLS is in the deletion set (it reads T5 / pT5 / pT3 rows the chain
// pipeline replaces). Both CheckHitspLS self-clean passes survive, so with the chain flag ON the
// bare-pLS universe is {isQuad && isDup == 0} evaluated after pass 2 -- the reference's -ZP8 6.
if (!useChainTracking_) {
  alpaka::exec<Acc2D>(queue_, crossCleanpLS_workDiv, CrossCleanpLS{}, ...);
}
```

at `LSTEvent.dev.cc:3203` (workdiv `:3201`). Strict no-op with the flag off (single `if` around one launch). Notes:

* leave the `plsIsDupFinal_` snapshot at `:3222-3224` in place -- with the kernel skipped it simply
  records the same value as `plsIsDupPass2_`, which is a useful ON-state assertion.
* do NOT touch `pixelLineSegmentCleaning` (`:3545-3557`, pass 1) or the pass-2 launch (`:3046-3056`).
  `-ZP8 6`, not 5, is the frozen choice: pass 2 is KEPT.
* do NOT touch `CrossCleanpT3` / `CrossCleanT5` / `CrossCleanT4`. `-RT3 1` / `-RT5 1` drop every row
  those crosscleans could have affected, and the prototype's own baseline was measured with them
  applied to the ntuple (section 3.4).
* `CrossCleanpLS` is currently `nAllocatedTCs`-independent and has no other consumers; grep confirms
  the only writes are to `pixelSegments.isDup()`.

---

## 6. Is POSTDELP2 reachable with the master switch ON and no code deleted?

**Yes.** Every one of the four POSTDELP2 flags is either already reproduced by the tree's ON-state
or is reproduced by a suppression the tree can express with existing kernels. Nothing in POSTDELP2
requires an LST kernel to be *absent* -- only that its output not be *consulted*:

* `-ZPF 3` suppresses a READ (`ChainArbitrate.h:157-158`), not a write. The builders can keep
  setting `partOfPT5` / `partOfPT3`.
* `-ZP5 1` and `-RT3 1` suppress carried TC ROWS, which is what `ChainCompactCarriedTCs` already
  does class by class.
* `-ZP8 6` suppresses one kernel launch (`CrossCleanpLS`) whose entire effect is one column, and
  the column's pre-`CrossCleanpLS` value is exactly the branch the prototype measures against.
* `-CF 1 -CFC 1` are physics-neutral and structurally already in force.

Two things that the prototype did NOT emulate and the ON-state therefore also need not emulate,
because `-RT5 1 -RT3 1` make them unobservable: the deletion's effect on `CrossCleanpT3` and on
`CrossCleanT4` (section 3.4). If `-RT3` is later switched off in the tree, this stops being true.

### Minimal set of ON-mode suppressions, in dependency order

| # | suppression | site | today | depends on |
|---|---|---|---|---|
| 1 | do not run `CrossCleanpLS` | `LSTEvent.dev.cc:3201-3219` wrap in `if (!useChainTracking_)` | MISSING | none |
| 2 | do not carry LST `T5` / `T4` / `pT5` TC rows (`replacePT5 = true`) | `ChainArbitrate.h:876-880`, `ChainParallel.h:133-140`; default in `ChainConfig.h` | ALREADY DONE | 1 (the compaction runs after the pLS admission) |
| 3 | do not carry LST `pT3` TC rows: `replacePT3 = true` | `LSTEvent.dev.cc:1402-1403` -- degate from `chainT3ReplaceEnabled()` | PARTIAL | 2 |
| 4 | do not consult `partOfPT5` when selecting claim candidates (`dropPartOfPT5 = false`) | `ChainConfig.h` default | ALREADY DONE | 2 |
| 5 | do not consult `partOfPT3` (`dropPartOfPT3 = false`) | `LSTEvent.dev.cc:1406` -- degate | PARTIAL | 3 |
| 6 | run bare-T3 stage B and let it publish ownership | `LSTEvent.dev.cc:2248` + `:2612-2613` -- degate `chainT3AttachEnabled()` / `chainT3ReplaceEnabled()` | PARTIAL | 3, 5 |
| 7 | emit one type-5 row per stage-B owner | `LSTEvent.dev.cc:1894` / `:2740-2741` -- degate | PARTIAL | 6 |
| 8 | retire carried pixel rows the attach owns / `-RPS` retires | `ChainSuppressCarriedTCs` (`ChainAttach.h:1056`), `LSTEvent.dev.cc:2250` | ALREADY DONE | 6, 7 |
| 9 | keep the grid as the only pair enumeration | `ChainAttach.h:38`, `ChainAttachT3.h:47-80` | ALREADY DONE | 6 |

Items 3, 5, 6, 7 are ONE change: replace the `LST_CHAIN_T3REPLACE` / `LST_CHAIN_T3ATTACH` env gate
with the master switch (or with a `ChainConfig` field defaulting to on, env kept as an override).
Item 1 is a second, independent change. Items 2, 4, 8, 9 need no work.

Expected direction of the two changes relative to today's ON-state, from the mechanisms alone
(NOT a prediction of magnitude -- the POSTDELP2 numbers are the only measurement):
item 1 admits strictly MORE bare-pLS rows (dup up, eff up); items 3/5/6/7 remove LST's pT3 class
and replace it with the attach's own, which is the change the `-RT3` A/B was built to measure.

### Two residual risks to check when the ON-state is first scored against the 0.80626/0.20553 row

1. **`-RPS` margin split.** POSTDELP2 leaves `-RPSA` / `-RPST` at their `1e9` "unset = follow
   `-a` / `-AT3`" default (declarations `main.cc:896-897`), and the tree's
   `ChainSuppressCarriedTCs` tests one key derived from `cfg.attachTheta` with the stage-B value
   folded in shifted by `attachTheta - attachThetaT3` (`ChainAttachT3.h:585-590`). Those agree only
   while `rpsThetaChain == attachTheta` and `rpsThetaT3 == attachThetaT3`. They do in POSTDELP2.
   They do NOT if A11's split margins are later ported -- that is A11's problem, but the parity
   check must be run BEFORE A11 lands or the two changes will not be separable.
2. **`plsDelivered` bookkeeping.** In the reference, a pLS already delivered by a live type-7/5/8
   row is never added back (`main.cc:5119-5124`). In the tree that is automatic: the row simply
   still exists. But it is automatic only because `AddpLSasTrackCandidate` runs BEFORE
   `arbitrateChains` (`LSTEvent.dev.cc:3228-3244`), i.e. the admission and the retirement are
   ordered exactly as the reference orders them. Do not move `arbitrateChains` above
   `AddpLSasTrackCandidate`.

---

## 7. Things that could NOT be determined from the code

* Whether `-ZP8 6` and `-ZP5 1` are *numerically* separable in the POSTDELP2 row. The code shows
  `-ZP5 1` is a no-op given `-RT5 1` (section 3.1), but no `-ZP5 0` control run exists in
  `fin_ref/fin_scoreboard.txt` or `synth_ref/`, so this is a code-reading conclusion, not a
  measured one.
* The exact residue mode 4 under-counts (`main.cc:5205-5210` notes it, does not quantify it).
  Irrelevant to the port: the freeze uses mode 6, which is measured, not modelled.
* Whether `pixelSegments.score()` ordering inside `CheckHitspLS` is bit-reproducible across
  backends; the ON-state parity harness (`dumpChainTCs`, `LSTEvent.dev.cc:2777`, called `:3249`) covers the TC
  collection but the `isDup` column itself is not in the sidecar. If a CPU/GPU ON-state discrepancy
  appears in the bare-pLS count after change 1, that column is the first place to look.
