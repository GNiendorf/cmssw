# B00 -- DIAGNOSIS AGENT (per-eta-band duplicate + fake decomposition on CHAINFINAL)

Artifact dir: `standalone/b00_ref`   Code: `standalone/protoB00` (copy of protoFINAL +
one new INERT diagnostic value `-XCD 4`).  Reference run reused, not regenerated:
`synth_ref/r_W_D1.root` = CHAINFINAL on the full 977.

## M0 -- tooling (done)
* `b00_ref/b00_census.py` -- per-eta-band x per-class TC/dup/fake table and the DUPLICATE
  PATTERN CENSUS (dup TC class x partner classes) + multi-TC sim profiles, CF vs LST on
  the same events. Coarse (B/T/E) and `--fine` (8 bins).
  Outputs: `census_coarse.txt`, `census_fine.txt`.
* `b00_ref/b00_cells.py` -- prices ORACLE deletions of each cell with the A07 removal
  simulator on the CHAINFINAL 977 pickle (`cf977.pkl`, made with `a07_ref/a07_extract.py`).
  Output: `cells977.txt`.
* `protoB00` `-XCD 4` -- the existing `-XCD 2` truth partition resolved by |eta| band
  (5 bins), by cover kind (seedless chain / other / none / no-truth), by fate, and by the
  FOUR logits the retirement knobs key on (plsBestChainLogit = -RPSA, plsBestT3Logit =
  -RPST, best logit over a delivered SEEDLESS chain inside the -XCT dR window = -XCT,
  and the same ignoring the window). Counters only; no decision reads them.
  Output: `run300.out` / `synth_ref/r_B00D300.log` (B00ROW lines), decoded by
  `b00_ref/b00_frontier.py`.

## M1 -- THE BARREL DUPLICATE IS TWO PATTERNS, AND ONE OF THEM IS NEW
(see census_coarse.txt; per event, full 977; dup TCs, both members of a pair counted)

barrel total CF 17.878 vs LST 5.475 -> excess 12.403
  seedless-chain + bare-pLS   CF 14.443  LST 2.998  excess +11.445  (92% of the excess)
  seedless-chain + SEEDED(t7) CF  1.899  LST 0.006  excess  +1.893  (15%)  <-- NEW CELL
  chain + chain               CF  0.595  LST 0.988  CREDIT  -0.393
  pT3-class + seeded          CF  0.006  LST 0.672  CREDIT  -0.666
transition total CF 9.014 vs LST 3.837 -> excess 5.177
  seedless-chain + bare-pLS   CF  6.048  LST 1.580  excess  +4.468  (86%)
  seedless-chain + SEEDED(t7) CF  1.634  LST 0.000  excess  +1.634  (32%)
endcap total CF 79.145 vs LST 92.671 -> CF is 13.5 BELOW (protected), but the SAME two
  cells are +3.28 and +2.00 there; the endcap credit comes from pLS+pLS parity and from
  LST's own barePLS+seeded / T4+barePLS excess.


## M2 -- ORACLE PRICE OF EACH CELL (a07 removal simulator, CHAINFINAL 977)
`b00_ref/cells977.txt`. The simulator reproduces CHAINFINAL exactly on the empty set
(eff .80978 dup .05246 fake .04939 dupB .03056 dupT .02793 dupE .07184).

| cell (ORACLE deletion) | d_dupB | d_dupT | d_dupE | d_eff | d_fakB | d_nhB |
|---|---|---|---|---|---|---|
| C1 B  bare pLS of a (chain+pLS) pair, barrel | **-.02502 -> .00554** | -.00015 | 0 | .00000 | +.00071 | +0.128 |
| C1 T  same, transition | -.00026 | **-.01750 -> .01043** | -.00008 | .00000 | 0 | 0 |
| C2 B  seedless chain of a (chain+seeded) pair | -.00348 | 0 | 0 | .00000 | +.00009 | -0.001 |
| C2 T  same | 0 | -.00620 | 0 | .00000 | 0 | 0 |
| C9 BT C1+C2 | -.02888 | -.02400 | -.00008 | .00000 | +.00081 | +0.128 |
| C4 B  every FAKE seedless chain, barrel | +.00150 | 0 | 0 | .00000 | **-.04653** | +0.015 |
| C4 T  same, transition | 0 | +.00120 | 0 | .00000 | -.04053 (fakT) | 0 |

LST targets: dupB .00971, dupT .01308, fakB .04365, fakT .04542.
=> C1 alone OVERSHOOTS the barrel duplicate target (.00554 < .00971): only ~72% of the
cell has to be reached. Displaced bands v15/v510/v1030/d15 are BIT-IDENTICAL for every
duplicate cell -- the duplicate work does not touch the displaced win at all.

## M3 -- THE FAKE IS THE DISPLACED-EXEMPT ADMISSION BRANCH (`b00_ref/branch977.txt`)
Barrel seedless chains by `tc_dbgBr` (per event): br2 (5-layer IP) 53.20 rows at 5.3%
fake; br3 (5-layer DISPLACED-EXEMPT) 48.44 rows at **26.0%** fake; br1 (4-layer exempt)
10.29 rows at **67.4%** fake; br0 (4-layer IP) 0.04 rows.
Of the barrel fake seedless chains, br3 carries 12.58/evt and br1 6.94/evt = 78% of the
barrel fake excess, and those two branches are exactly where the displaced win lives
(br3 barrel: 1.69 SOLE covers of |dxy|>=1 sims per event).
=> the barrel/transition fake cell and the protected displaced lead are THE SAME BRANCH.

## M4 -- THE EX-ANTE FRONTIER (-XCD 4 on protoB00, frozen 300; gate PASSED 33/33)
`b00_ref/frontier300.txt` (+ `frontier300_fine.txt`, `reach.txt`).

Bare-seed universe of CHAINFINAL, per event, by band x truth class x fate:
```
group      class              N   consumed  RPSblock  XCretire   SURVIVE
BARREL     A_seedlessCover  37.65    0.97     15.27     13.74      7.68
BARREL     A_otherCover    256.81  244.86     11.85      0.04      0.06
BARREL     B_noCover        45.50    2.59      0.90      1.67     40.33
TRANS      A_seedlessCover  21.09    0.72      8.55      8.58      3.24
TRANS      B_noCover        22.75    1.43      1.37      0.79     19.16
ENDCAP     A_seedlessCover  30.35    0.46     15.44     10.12      4.33
ENDCAP     B_noCover       774.34    0.93      5.93      1.64    765.83
```
The 7.68 surviving barrel class-A seeds ARE the C1 cell (oracle count 7.34). EVERY one of
them has a scored chain pair -- enumeration is NOT the barrel problem; the logit is low.

CALIBRATION (`bprice977.txt`, measured with the a07 simulator): retiring one class-B-like
bare seed costs **0.0355 sims (barrel) / 0.0390 (transition) / 0.0171 (endcap)**, i.e.
one class-B barrel seed per event = **-.00047 of overall efficiency**. Validated against a
real run: -RPSA 5.5 -> 4.5 predicts -.00036, measured (D1 -> E4) -.00040.

PREDICTED dup rate and efficiency cost of a BAND-tightened threshold (`reach.txt`):
```
BARREL (now .03056, LST .00971)        TRANSITION (now .02793, LST .01308)
-XCT band  3.0 -> .02146  d_eff -.00050    -XCT2 3.0 -> .01774  -.00025
-XCT band  2.5 -> .01846  d_eff -.00079    -XCT2 2.5 -> .01482  -.00042
-XCT band  2.0 -> .01588  d_eff -.00107    -XCT2 2.0 -> .01226  -.00065  (BELOW LST)
-RPSA band 3.5 -> .02016  d_eff -.00062    -RPSA2 2.5 -> .01424  -.00101
-RPSA band 2.0 -> .01026  d_eff -.00254
```
* the TRANSITION can be bought outright with `-XCT2` (an EXISTING flag, no new code) for
  ~.0004-.0007 of efficiency;
* the BARREL cannot: closing it to LST costs -.0025, five times the 1-sigma floor. The
  best exchange rate anywhere on the curve is ~19 duplicate-rate units per 1e-4 of
  efficiency and it degrades fast.
* the calibration-free `bidMargin` (rank among the bidders for ONE target, which the
  4.6-unit eta shift cancels out of) is NOT better than the raw logit at equal yield --
  measured, not assumed. The barrel A/B separation is intrinsically weak on this pair log.

## M5 -- REAL (non-oracle) FAKE CELLS (`b00_ref/fakecells977.txt`)
```
cell (real criterion, final-pass deletion)   d_eff    d_fakB    d_dupB   d_v510    d_d15   kill/evt  fake%
B br1 (4-layer exempt) WHOLESALE           -.00020  -.01368   -.00010  -.00704  -.04929     12.4     64
B br1 & dca>2                              -.00005  -.00719   +.00011  -.00101  -.03341      7.1     62
B br1 & dca>5                              +.00000  -.00296   +.00011  +.00000  -.00132      3.2     60
B br1 & dca>10                             +.00000  -.00091   +.00005  +.00000  -.00066      1.1     60
B br3 (5-layer exempt) WHOLESALE           -.00626  -.02316   -.00923  -.13568  -.43566     61.3     25
B br3 & dca>5                              -.00016  -.00257   -.00033  -.00201  -.00562      5.9     32
T br3 WHOLESALE                            -.01302  (fakT -.02726)               -.09296     69.2     16
T br1 WHOLESALE                            -.00011  (fakT -.00150)               -.00397      1.4     36
```
The whole barrel fake gap (+.01142 vs LST) IS the br1 cell (-.01368 wholesale) and it costs
only -.00020 of efficiency -- but it spends **d15 -.0493 of a +.0711 lead**, which the
protection forbids. The affordable slice is `dca>5` (fakB -.00296 for d15 -.0013).
The transition fake gap lives in br3, which is 84% true and carries the displaced win;
no cheap criterion was found.

## M6 -- GATE
`B00D300` (protoB00, CHAINFINAL line + `-XCD 4`) vs `synth_ref/r_D1` (CHAINFINAL, same
300): **33 PRE-EXISTING branches IDENTICAL, 0 DIFFER, 0 MISSING, 0 ADDED** under
`rebase_ref/cmp_branches.py`. `-XCD 4` is a bit-exact no-op.
