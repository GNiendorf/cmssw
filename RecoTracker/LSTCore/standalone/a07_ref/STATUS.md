# A07 -- FAKE RATE explorer -- STATUS

Workspace: `standalone/protoA07` (copy of protoFIN, binary md5 519b0abc34a28cd1e803d6b9407ef224).
Artifacts: `standalone/a07_ref/`. Runner: `a07_ref/a07_run.sh <TAG> [overrides]` -- the frozen
fin_run.sh line with the ASSEMBLED BASELINE tail `-XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2`
built in, overrides appended after it.

## M0 SETUP (done)
protoA07 = byte copy of protoFIN. No rebuild needed.

## M1 NO-OP GATE -- PASSED BIT-EXACTLY
tag GATEA07 = a07_run.sh with no overrides, run on the copied (pre-edit) binary
519b0abc34a28cd1e803d6b9407ef224. `rebase_ref/cmp_branches.py fin_ref/r_FINBASE.root
a07_ref/r_GATEA07.root` -> **33 IDENTICAL, 0 DIFFER, 0 MISSING**. Aggregates reproduce
FINBASE on every metric: eff .8099 dup .0623 fake .0555, nTC 618793 = 618793, all three
regions, all four vxy and all four dxy bands, nhitOT 9.803/9.879/3.560. Wall 2260 s
(the machine carried ~120 concurrent sibling jobs; no timing claim is made from it).
Artifacts: a07_ref/r_GATEA07.{root,json,log,cmd}, a07_ref/cmp_gate.txt.

## M2 THE DECOMPOSITION (free -- no new runs; the ntuple already carries provenance)
`a07_ref/a07_decomp.py` reproduces the HEADLINE fake rate exactly
(`compare_ab.fake_overall_incut` = |eta|<4.5 AND pt>0.9) and splits it by the
`tc_isChain` delivery class the writer already stores (0 carried / 1 chain / 2 attach-pT5 /
3 attach-pT3 / 4 -ZP8 bare-pLS), by tc_type, region and nhitOT.
Output: `a07_ref/decomp_base.txt`.

## M3 THE REMOVAL SIMULATOR (`a07_ref/a07_sim.py`) -- VALIDATED EXACTLY
Given the FINBASE ntuple it recomputes eff / dup / fake / per-region / displaced bands for
any "delete this set of delivered TCs" change, with the harness's own conventions
(duplicates recomputed after the deletion; a sim is reconstructed iff a surviving TC
matches it). With an EMPTY deletion set it reproduces all 16 published FINBASE numbers to
5 decimals: eff .80992 dup .06230 fake .05551 | effB .92660 effT .88213 effE .74496 |
dupB .04344 dupT .03431 dupE .08106 | fakB .06680 fakT .06903 fakE .04525 |
v15 .79459 v510 .73356 v1030 .74139 d15 .61650.
It also reproduces the published FULL-977 scoreboard row exactly from w_x4.pkl:
eff .80905 dup .06184 fake .05607 | effB .92454 effT .87981 effE .74436 | dupB .04294
dupT .03375 dupE .08069 | fakB .06754 fakT .06932 fakE .04578 | v15 .80110 v510 .72161
v1030 .71474 d15 .58320 | nhitOT 9.801/9.884/3.557 -- every published digit.
CAVEAT: it models deletion only. A real GATE change also frees the chain's MDs for
competitors and lets the pixel attach re-target, so simulated eff loss is an UPPER bound
and simulated dup gain a LOWER bound. Chosen points get confirmed with real runs.

## M4 SOURCE EDIT -- WRITTEN, THEN REVERTED (no code is delivered)
pT3-class rows carry NO provenance, so the decomposition cannot see inside the cell with
the biggest quality gap. I wrote a write-only diagnostic that fills the existing OutTC dbg
fields for those rows (dbgBranch=4, dbgMP = the accepting attach logit, dbgNPS / dbgNB /
dbgInLay / dbgNL / dbgNMD = the target T3's module and layer composition; nothing but the
ntuple dumper reads dbg*, so it changes no decision) and launched the run that would have
proved it inert. The machine was carrying ~120 concurrent sibling jobs, the run took over
an hour and its shell was killed at 285/300 events, leaving an unclosed ROOT file that
cannot be opened.

Rather than ship an edit whose no-op I had not proved, I REVERTED it: protoA07/main.cc was
restored from protoFIN and the pristine objects and binary put back.
`diff -r protoFIN protoA07` is now EMPTY and both binaries are md5
519b0abc34a28cd1e803d6b9407ef224. **No code is delivered from this angle.**
The 30-line diagnostic is described above for whoever does the retrain; it is trivially
re-addable and is genuinely useful for characterising the head's operating point, but it
is not needed for any conclusion in this report.

## M4b -- 977-EVENT CONFIRMATION (no new runs needed; fin_ref already had the files)
Decomposition, efficiency attribution, class ablations and the fine constant sweep were all
re-run on fin_ref/r_W_X4.root (= FINBASE at 977) vs fin_ref/fin_base977.root. Every
per-class fake rate transfers to within .0005 and the population ledger reproduces
(+18.29 vs +18.20 fakes/evt). The two load-bearing facts hold exactly: dropping the
pT3-class cell moves vxy[10,30) and dxy[1,5) by 0.00000, and the 5+ exempt branch holds
~50% of both. The single move that looked exactly free on the 300 (`-MR -1.75`) is NOT
free on the 977 (v1030 -3 tracks) -- the 300-event freeness was an artifact.
Artifacts: decomp_977.txt, classes977.txt, fine977.txt, w_x4.pkl.

## M5 -- RESULTS (see RESULTS.md and TABLE.md)
The fake excess (+18.20 fakes/evt) decomposes into exactly two populations: 5+ layer OT
objects (+16.25) and pT3-class deliveries (+12.39), repaid by T4 (-10.01) and
pixel-attached 5+ (-6.58). 82% of the chain-derived fakes sit on the two EXEMPT (large-DCA)
admission branches, which are the same branches that hold 72% of the vxy[10,30) and 91% of
the dxy[1,5) efficiency. 41 structural predicates + 27 gate-constant settings + an
exhaustive zero-cost cell search found NO free win of meaningful size.
DELIVERED CONFIG = the assembled baseline, unchanged.
THE ONE RECOMMENDATION: a perfect pT3-class fake filter, measured on the FULL 977, takes
fake .05607 -> .04622 (LST .04538) with efficiency and all eight displacement bands
EXACTLY unchanged and track length slightly up. Merely matching LST's own pT3 fake rate
(3.81% vs our 17.76%) at equal volume gives .04835, i.e. +.0030 instead of +.0107. That is
the permitted attach-head retrain, and it is the only thing in this angle worth doing.
The other two big cells have LARGER perfect-filter ceilings (bare 5+ -.01834, T4 -.00512)
but no evidence they are attainable: LST's own bare T5 is 6.79% fake against our 9.31% and
its T4 is 60.4% against our 33.4%, so nobody has demonstrated the quality there.

## M7 -- THE CLOSING ORACLE (full 977; truth-based, NOT an achieved result)
Two localised fixes, neither of which touches the large-DCA chain branches:
```
                                eff      dup     fake |   v15    v510   v1030    d15
ASSEMBLED BASELINE          0.80905  0.06184  0.05607 |.80110 .72161 .71474 .58320
+ pT3 head at LST's rate    0.80905  0.06235  0.04835 |.80110 .72161 .71474 .58320
+ working -ZP8 seed dedup   0.80903  0.04504  0.04879 |.80110 .72161 .71474 .58320
LST                         0.80987  0.05138  0.04538 |.77719 .64422 .62567 .51042
```
dup BELOW LST, fake within +.0034, length at parity (10.02/10.03/3.56 vs 10.15/10.01/3.56),
displaced completely untouched. The residual would be the -.0008 of efficiency already
flagged by the Baseline agent on the 977.

## ARTIFACT INDEX (all under standalone/a07_ref/)
```
RESULTS.md            the report: decomposition, addressability, verdict, retrain target
TABLE.md              everything tried x full metrics, 300 and 977
STATUS.md             this file

a07_run.sh            runner (frozen line + assembled-baseline tail built in)
setup_ws.sh build.sh  workspace copy and rebuild
pyrun.sh              runs a python tool under the standalone env

a07_decomp.py         reproduces the headline fake rate and splits it by delivery class
a07_extract.py        ntuple -> pickle for the simulator
a07_sim.py            THE REMOVAL SIMULATOR (validated exactly against FINBASE)
a07_profile.py        fake%/dup% for every structural cell
a07_scan.py           41 structural predicates
a07_gate.py           the five existing -G 6 gate constants, reconstructed exactly
a07_fine.py           fine sweep with all EIGHT displacement bands (the free-move hunt)
a07_oracle.py         per-cell zero-cost ceiling (essential-row marking)
a07_freecells.py      exhaustive zero-essential structural cell search
a07_bestclass.py      efficiency attribution by delivery class (ours vs LST)
a07_denoms.py         denominators, so a delta can be read as tracks
a07_classes.py        class ablations with all finish-line metrics

r_GATEA07.*           the no-op gate run;  cmp_gate.txt = 33/33 branches identical
r_DIAG.log/.cmd       the reverted diagnostics run (killed at 285/300; .root deleted)
decomp_base.txt decomp_977.txt profile.txt profile977.txt oracle.txt oracle977.txt
scan1.txt scan977_at3.txt gatescan.txt gate977_m4.txt gate977_mr.txt gate977_m4d.txt
fine.txt fine977.txt freecells.txt freecells977.txt classes.txt classes2.txt
classes977.txt bestclass.txt
finbase.pkl w_x4.pkl   (frozen 300 and full 977, ready for the simulator)
```

## M6 -- SIDE-FINDINGS HANDED ON (duplicate angle, measured on the full 977)
THE BIG ONE: the -ZP8 bare-pLS rows are 21.5 in-cut rows/evt and **67.2% duplicate**.
Deleting exactly the duplicate ones (17.95 rows/evt) gives d_dup **-0.01717** at
d_eff -0.00001, d_fake +0.00050 and every displacement band +0.00000 -- that is
dup .06184 -> .04467, BELOW LST's .05138, for one hundred-thousandth of efficiency.
It is an oracle cut, but it localises the ENTIRE +.0105 duplicate gap into one cell of
bare pixel seeds, which is precisely what the ported CrossCleanpLS is meant to dedup.
MECHANISM PINNED: of those 17.95 duplicate -ZP8 rows/evt, 17.35 (97%) duplicate a BARE
CHAIN TC; only 0.39 duplicate a pT5-class delivery, 0.09 a carried pixel row, 0.05 a
pT3-class delivery. It is seed-vs-chain dedup in the sub-threshold band, not seed-vs-seed.
Smaller instance of the same shape: T4 IP-branch duplicate rows, 4.49/evt, dup -.00413 at
eff +0.00000. Flag-only version: `-M4 6` gives dup -.00405 at eff -.00199, fake +.00025,
displaced EXACTLY unmoved (saturates at 6; -M4 8 is identical).

## ===== RESUMED SESSION (after a transient interruption) =====

## M8 -- STATE RE-VERIFIED
`diff -r protoFIN protoA07` EMPTY, both binaries md5 519b0abc34a28cd1e803d6b9407ef224.
(An accidental `cp -a protoFIN protoA07` at resume created a nested `protoA07/protoFIN`
copy; removed, diff re-checked empty.) The M1 no-op gate (GATEA07, 33/33 branches
identical to fin_ref/r_FINBASE.root) therefore still holds for the delivered binary --
nothing was rebuilt. A second confirmation run was launched at resume, then cancelled and
its partial output deleted: the machine was carrying 108 concurrent sibling chainproto
jobs and the gate was already proven bit-exactly.

## M9 -- INDEPENDENT RE-DERIVATION OF THE HEADLINE (new tooling, written from scratch)
Before re-reading any earlier artifact I rebuilt the decomposition from the ntuple with
two fresh scripts, `a07_ref/decomp.py` (fake by tc_type x region) and `a07_ref/sim_cut.py`
(a second, independently written removal simulator). sim_cut.py reproduces ALL FOURTEEN
published FINBASE numbers exactly with an empty removal set -- eff .80992, v01 .84282,
v15 .79459, v510 .73356, v1030 .74139, d15 .61650, d510 .20161, d1030 .03073, dup .06230,
fake .05551, nhitOT 9.80254/9.87906/3.55956, nTC 618793, effB .92660 effT .88213
effE .74496, dupB .04344 dupT .03431 dupE .08106, fakB .06680 fakT .06903 fakE .04525.
Two independent implementations of the harness conventions now agree.

The fresh by-type table (300 evts, in-cut) reproduces the earlier by-delivery-class one:
```
type   ours N/evt  fake%  |  LST N/evt  fake%      (type 4 = 5+ chain / LST T5,
  4      328.4    .0921   |    111.9   .0664       5 = our pT3-class / LST pT3,
  5       93.3    .1739   |    104.5   .0368       7 = attached / LST pT5,
  7      531.2    .0076   |    706.6   .0150       8 = bare pixel rows,
  8      622.1    .0478   |    624.7   .0483       9 = 4-layer chain / LST T4)
  9       26.0    .3314   |     31.5   .5908
```

## M10 -- ONE TRAP RE-CONFIRMED (worth recording, it is the shape of this whole angle)
A naive "load-bearing" pass that protects only the PROMPT efficiency denominator
(sim pt>0.9, |vz|<30, vxy<2.5) reports the barrel 4-layer-chain cell as perfectly free:
3032 in-cut rows on the 300, 2046 of them fake (67.5%), and ZERO of them the sole cover of
any prompt sim -- apparently -.00393 of fake rate for nothing. It is not free. Those rows
sit on the large-DCA (exempt) admission branch and their cover is DISPLACED: the full
simulator, which protects all four vxy and all four dxy bands, prices the same cut at
d_d15 -0.04571 on the 300 and -0.05293 on the 977 (41 and ~160 displaced tracks).
ANY fake-angle screen on this line MUST carry the displacement bands, or it will
"discover" free wins that are paid for out of the displaced result.

## M11 -- CONCLUSION UNCHANGED, NOTHING DELIVERED
Sections 0-6 of RESULTS.md and A-G of TABLE.md stand as written. bestFlags = the assembled
baseline verbatim; codeChanged = false.
