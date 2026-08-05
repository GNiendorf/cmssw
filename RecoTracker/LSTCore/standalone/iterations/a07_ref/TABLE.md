# A07 -- everything tried, with full metrics (frozen 300, ASSEMBLED BASELINE)

Denominators, so any delta reads as tracks: eff 22633 | barrel 8787 | transition 3928 |
endcap 9026 | vxy[1,5) 1368 | vxy[5,10) 593 | vxy[10,30) 1249 | dxy[1,5) 897 |
fake/dup 480309 in-cut TCs.

## A. REFERENCE ROWS (measured, real runs)
```
tag                     eff      dup     fake    effB    effT    effE    dupB    dupT    dupE    fakB    fakT    fakE     v15    v510   v1030     d15    nhB    nhT    nhE
FINBASE (baseline)  0.80992  0.06230  0.05551 .92660 .88213 .74496 .04344 .03431 .08106 .06680 .06903 .04525 .79459 .73356 .74139 .61650  9.803  9.879  3.560
LST (target)        0.80988  0.05179  0.04476 .92557 .88187 .74596 .00989 .01264 .08556 .04249 .04454 .04603 .77193 .65430 .66453 .55407 10.148 10.015 3.562
GATEA07 (my no-op)  = FINBASE, all branches identical
```

## A2. THE DECOMPOSITION ITSELF (measured, no new runs -- the ntuple carries provenance)
In-cut rows per event, their fake counts and rates, ours vs LST's own TC collection:
```
OUR CLASS              N/evt  fake/evt   rate   |  LST CLASS      N/evt  fake/evt   rate
chain T5   (bare 5+)  328.42     30.26  .0921   |  T5 (bare 5)   111.92      7.43  .0664
carried pLS           600.58     28.53  .0475   |  pLS           624.66     30.16  .0483
attachT3 pT3           93.32     16.23  .1739   |  pT3           104.48      3.84  .0368
chain T4   (bare 4)    26.00      8.62  .3314   |  T4             31.53     18.63  .5908
attachT5 pT5          531.20      4.04  .0076   |  pT5           706.58     10.62  .0150
zp8 pLS (-ZP8 rows)    21.52      1.21  .0564   |  --
TOTAL                1601.02     88.88  .05551  |  TOTAL        1579.17     70.68  .04476
```
Per region (in-cut, per event), the same split:
```
region     ours N/evt  ours fake/evt  ours rate | LST N/evt  LST fake/evt  LST rate | excess
barrel        477.9        31.92       .06680   |   462.9       19.67       .04249  | +12.25
transition    258.0        17.81       .06903   |   251.0       11.18       .04454  |  +6.63
endcap        865.2        39.15       .04525   |   865.2       39.83       .04603  |  -0.68
```
Largest single region cells (rate_contr = that cell's fakes / all in-cut TCs in the region):
```
barrel      chain T5 155.39/evt .10275 (contr .0334) | attachT3 53.68 .13202 (.0148)
            chain T4  10.11/evt .67480 (contr .0143) | attachT5 226.89 .00485 (.0023)
transition  chain T5 108.65/evt .09987 (contr .0421) | attachT3 17.71 .20934 (.0144)
endcap      carried pLS 567.43  .04656 (contr .0305) | attachT3 21.93 .24776 (.0063)
```
nhitOT profile inside the two big cells (fake/N):
```
chain T5   10 hits: 7242/51788 (.140)   12 hits: 1794/44619 (.040)  14: 42/2118 (.020)
attachT5   10 hits:  650/70917 (.009)   12 hits:  531/83478 (.006)  14: 30/4965 (.006)
LST T5     10 hits: 1873/13472 (.139)   12 hits:  349/19322 (.018)
LST pT5    10 hits: 1151/51746 (.022)   12 hits: 2004/156849 (.013)
attachT3    6 hits: 4868/27995 (.174)   LST pT3   6 hits: 1152/31344 (.037)
```

Same decomposition on the FULL 977 (r_W_X4.root vs fin_base977.root) -- it transfers:
```
OUR CLASS              N/evt  fake/evt   rate   |  LST CLASS      N/evt  fake/evt   rate
chain T5   (bare 5+)  331.00     30.80  .0931   |  T5 (bare 5)   112.79      7.66  .0679
carried pLS           605.95     29.11  .0480   |  pLS           630.11     30.80  .0489
attachT3 pT3           93.97     16.69  .1776   |  pT3           104.88      4.00  .0381
chain T4   (bare 4)    26.13      8.72  .3336   |  T4             32.06     19.37  .6040
attachT5 pT5          537.03      3.98  .0074   |  pT5           713.32     10.47  .0147
zp8 pLS                21.54      1.29  .0600   |  --
TOTAL                1615.62     90.59  .05607  |  TOTAL        1593.16     72.30  .04538
population ledger, 977: 5+ layer +16.65 | pT3-class +12.69 | bare pixel -0.40 |
                        T4 -10.65  => +18.29 fakes/evt   (300 evts gave +18.20)
```

## A3. EFFICIENCY ATTRIBUTION (sims whose BEST-matching TC is in that class)
Both ntuples reconstruct exactly the same 61.1 of 75.4 in-cut sims per event.
```
                       tracks/evt   fakes/evt   tracks per fake
our attachT5 pT5          24.05        4.04          5.95
our chain T5              17.54       30.26          0.58
our carried pLS           14.81       28.53          0.52
our attachT3 pT3           2.86       16.23          0.18
our zp8 pLS                1.60        1.21          1.32
our chain T4               0.25        8.62          0.03
LST pT5                   39.47       10.62          3.72
LST pLS                   15.28       30.16          0.51
LST pT3                    4.17        3.84          1.09
LST T5                     2.03        7.43          0.27
LST T4                     0.15       18.63          0.01
```

## B. SIMULATED CLASS ABLATIONS -- which cells may even be touched
Removal simulator, validated to reproduce every FINBASE number exactly with an empty set.
Absolute values (baseline row first), all finish-line metrics including track length:
```
label                          eff     dup    fake |  effB   effT   effE |  dupB   dupT   dupE |  fakB   fakT   fakE |   v15   v510  v1030    d15 |   nhB    nhT    nhE
BASELINE                   0.80992 0.06230 0.05551 |.92660 .88213 .74496 |.04344 .03431 .08106 |.06680 .06903 .04525 |.79459 .73356 .74139 .61650 | 9.803  9.879  3.560
drop attachT3 pT3          0.77224 0.06420 0.04819 |.85979 .84674 .73089 |.04752 .03496 .08092 |.05855 .05868 .03998 |.77193 .73019 .74139 .61650 |10.284 10.165  3.496
drop chain T4              0.80754 0.05647 0.05096 |.92660 .87780 .74086 |.04293 .02885 .07218 |.05367 .06784 .04442 |.78436 .71332 .71337 .56856 | 9.841  9.899  3.491
drop chain T4 br1 (exempt) 0.80953 0.06049 0.05074 |.92660 .88187 .74407 |.04308 .03367 .07800 |.05366 .06749 .04414 |.78728 .71838 .71337 .56856 | 9.841  9.887  3.525
drop chain T4 br0 (IP)     0.80794 0.05834 0.05576 |.92660 .87805 .74175 |.04330 .02952 .07531 |.06681 .06938 .04554 |.79167 .72850 .74139 .61650 | 9.803  9.891  3.526
drop chain T5 br3 (exempt) 0.77201 0.05819 0.04446 |.90338 .76680 .72269 |.03561 .02262 .07815 |.04512 .04530 .04391 |.67471 .46374 .20897 .05797 | 9.700  9.587  3.177
drop chain T5 br2 (IP)     0.65051 0.05500 0.05822 |.65904 .64053 .71084 |.01608 .02731 .07935 |.07659 .07440 .04592 |.46418 .33221 .56125 .60647 | 9.380  9.654  3.368
drop zp8 pLS rows          0.80316 0.04509 0.05550 |.91590 .87296 .74241 |.01017 .01539 .07285 |.06805 .06924 .04462 |.78436 .73187 .74139 .61650 |10.041 10.024  3.586
```
Same rows as deltas, with the rows/evt removed and that cell's fake fraction:
```
change                      d_eff    d_dup   d_fake    d_v15   d_v510  d_v1030    d_d15   rows/evt  fake% of cell
drop attachT3 pT3        -0.03769 +0.00190 -0.00733 -0.02266 -0.00337 +0.00000 +0.00000     135.4        17.4
drop chain T5 br3 exempt -0.03791 -0.00411 -0.01106 -0.11988 -0.26981 -0.53243 -0.55853     211.8        15.4
drop chain T5 br2 IP     -0.15941 -0.00730 +0.00270 -0.33041 -0.40135 -0.18014 -0.01003     213.8         3.2
drop chain T4 br1 exempt -0.00040 -0.00181 -0.00477 -0.00731 -0.01518 -0.02802 -0.04794      23.5        48.0
drop chain T4 br0 IP     -0.00199 -0.00396 +0.00024 -0.00292 -0.00506 +0.00000 +0.00000      10.7         0.9
drop chain T4 (both)     -0.00239 -0.00583 -0.00455 -0.01023 -0.02024 -0.02802 -0.04794      34.2        33.1
drop zp8 pLS rows        -0.00676 -0.01721 -0.00001 -0.01023 -0.00169 +0.00000 +0.00000      29.4         5.6
```

### B2. THE SAME ABLATIONS ON THE FULL 977 -- every one transfers
977 denominators: eff 73782 | barrel 28507 | transition 13146 | endcap 29248 |
vxy[1,5) 4560 | vxy[5,10) 1990 | vxy[10,30) 4098 | dxy[1,5) 3023 | fake/dup 1578457.
```
change                      d_eff    d_dup   d_fake    d_v15   d_v510  d_v1030    d_d15   (300-evt d_fake)
drop attachT3 pT3        -0.03852 +0.00190 -0.00751 -0.02412 -0.00352 +0.00000 +0.00000     (-0.00733)
drop chain T5 br3 exempt -0.03663 -0.00420 -0.01109 -0.11031 -0.29296 -0.49585 -0.51671     (-0.01106)
drop chain T5 br2 IP     -0.16065 -0.00725 +0.00266 -0.33684 -0.37337 -0.18814 -0.00959     (+0.00270)
drop chain T4 br1 exempt -0.00053 -0.00171 -0.00478 -0.00636 -0.01357 -0.02684 -0.05524     (-0.00477)
drop chain T4 br0 IP     -0.00192 -0.00396 +0.00024 -0.00241 -0.00352 +0.00000 +0.00000     (+0.00024)
drop zp8 pLS rows        -0.00717 -0.01697 -0.00005 -0.01031 -0.00050 +0.00000 +0.00000     (-0.00001)
```
The two load-bearing facts hold exactly on the full sample: the pT3-class cell moves
vxy[10,30) and dxy[1,5) by **0.00000**, and the 5+ exempt branch holds ~50% of both.

## C. SIMULATED GATE-CONSTANT MOVES (all five constants already exist; no new code)
Reconstructed exactly from the dumped branch code, mP, mD, nLayers, nNodes.
```
change             d_eff    d_dup   d_fake    d_v15   d_v510  d_v1030    d_d15  kill/evt   verdict
-M4D -1        +0.00000 -0.00016 -0.00087 +0.00000 +0.00000 -0.00320 -0.00334      3.00  cheapest; 7 displaced trk
-MR -1.5       -0.00013 +0.00001 -0.00142 -0.00073 -0.00337 -0.00080 -0.00223      3.40  3 prompt + 5 displ trk
-C25 0.5 -2    -0.00027 -0.00014 -0.00075 +0.00000 -0.00169 +0.00000 +0.00000      3.30  6 prompt trk, 1 displ
-C25 1 -2      -0.00049 -0.00029 -0.00113 +0.00000 -0.00169 -0.00080 +0.00000      6.13  11 prompt trk
-M4D -0.5      -0.00018 -0.00050 -0.00259 -0.00292 -0.00337 -0.00801 -0.00892      9.06  costs displaced
-C25 0 -1      -0.00013 +0.00006 -0.00303 -0.00073 -0.00169 -0.00480 -0.00446      7.99  costs displaced
-M4D 0         -0.00022 -0.00079 -0.00365 -0.00292 -0.00675 -0.01121 -0.01672     13.32  costs displaced
-C25 0 0       -0.00040 +0.00010 -0.00438 -0.00219 -0.00169 -0.01601 -0.02118     12.32  costs displaced
-MR -0.5       -0.00110 -0.00001 -0.00589 -0.00292 -0.00675 -0.01761 -0.02007     16.15  costs displaced
-MR 0          -0.00221 -0.00021 -0.00771 -0.00731 -0.01180 -0.02962 -0.03233     23.45  costs displaced
-MR 1          -0.00641 -0.00168 -0.01137 -0.01462 -0.02698 -0.07286 -0.07358     50.58  costs displaced
-M4 4.5        -0.00097 -0.00213 +0.00013 -0.00073 -0.00337 +0.00000 +0.00000      6.62  DUP lever, fake worse
-M4 6          -0.00199 -0.00405 +0.00025 -0.00292 -0.00506 +0.00000 +0.00000     11.28  DUP lever, fake worse
-MRI 1         -0.00358 -0.00040 -0.00147 -0.00877 -0.01180 -0.00881 -0.00111      9.71  bad ratio
-MRI 3         -0.04326 -0.00150 -0.00149 -0.08772 -0.10961 -0.05685 -0.00780     64.07  bad ratio
```

### C2. THE SAME GATE MOVES ON THE FULL 977 -- the whole frontier transfers
```
change             d_eff    d_dup   d_fake    d_v15   d_v510  d_v1030    d_d15   (300-evt d_fake)
-M4D -1        -0.00005 -0.00012 -0.00089 -0.00044 +0.00000 -0.00293 -0.00331     (-0.00087)
-M4D -0.5      -0.00014 -0.00047 -0.00258 -0.00132 -0.00201 -0.00805 -0.01290     (-0.00259)
-M4D 0         -0.00020 -0.00068 -0.00365 -0.00197 -0.00603 -0.01391 -0.02183     (-0.00365)
-M4D 2         -0.00053 -0.00147 -0.00478 -0.00592 -0.01106 -0.02635 -0.05326     (-0.00477)
-MR -1.5       -0.00012 +0.00004 -0.00144 -0.00022 -0.00151 -0.00293 -0.00265     (-0.00142)
-MR -1         -0.00037 +0.00007 -0.00383 -0.00175 -0.00302 -0.00903 -0.00893     (-0.00378)
-MR -0.5       -0.00081 +0.00000 -0.00592 -0.00351 -0.00603 -0.01781 -0.01886     (-0.00589)
-MR 0          -0.00155 -0.00021 -0.00771 -0.00658 -0.01307 -0.02953 -0.03209     (-0.00771)
-MR 1          -0.00622 -0.00167 -0.01136 -0.01776 -0.04020 -0.07077 -0.07145     (-0.01137)
-MRI 0         -0.00039 -0.00007 -0.00036 -0.00066 -0.00101 +0.00000 +0.00000     (-0.00037)
-MRI 1         -0.00332 -0.00038 -0.00149 -0.00592 -0.00955 -0.00610 -0.00132     (-0.00147)
-M4 4.5        -0.00117 -0.00222 +0.00013 -0.00132 -0.00251 +0.00000 +0.00000     (+0.00013)
-M4 6          -0.00202 -0.00405 +0.00024 -0.00285 -0.00352 +0.00000 +0.00000     (+0.00025)
-M4 8          -0.00202 -0.00405 +0.00024 -0.00285 -0.00352 +0.00000 +0.00000     (+0.00025)
-C25 0.5 -2    -0.00035 -0.00014 -0.00075 -0.00044 -0.00151 -0.00024 -0.00033     (-0.00075)
-C25 1 -2      -0.00077 -0.00027 -0.00112 -0.00066 -0.00251 -0.00073 -0.00066     (-0.00113)
-C25 0 -1      -0.00020 +0.00007 -0.00302 -0.00110 -0.00101 -0.00659 -0.00662     (-0.00303)
-C25 0 0       -0.00043 +0.00011 -0.00442 -0.00219 -0.00251 -0.01635 -0.01886     (-0.00438)
-C25 2 0       -0.00374 -0.00067 -0.00684 -0.00768 -0.01156 -0.02367 -0.02448     (-0.00679)
```
Every fake delta reproduces to <= .00006. The conclusion is not a small-sample effect.

## D. SIMULATED STRUCTURAL FILTERS (new-code candidates; the best ~15 of 41 tried)
```
change                       d_eff    d_dup   d_fake   d_v510    d_d15  kill/evt
T4 br1 & nB==4            +0.00000 -0.00002 -0.00396 -0.00675 -0.04571     12.30
T4 nB>=4                  +0.00000 -0.00009 -0.00396 -0.00675 -0.04571     12.48
T4 inLay in {2,3}         +0.00000 -0.00002 -0.00275 -0.00169 -0.02899      8.76
T4 nPS<=2                 -0.00004 -0.00019 -0.00300 -0.00337 -0.03010     10.09
T4 nPS<=1                 -0.00004 -0.00001 -0.00184 -0.00169 -0.02787      6.55
T4 inLay==3               +0.00000 +0.00002 -0.00172 -0.00169 -0.02676      5.91
T4 inLay==2               +0.00000 -0.00004 -0.00103 +0.00000 -0.00223      2.86
T4 pt>3                   -0.00022 -0.00013 -0.00066 +0.00000 -0.01449      1.82
T5 nhit10 & nPS<=2        -0.01264 -0.00182 -0.00473 -0.07926 -0.24192     56.34
T5 br3 & nPS<=2           -0.00437 -0.00081 -0.00440 -0.05565 -0.23746     43.37
T5 nPS<=1                 -0.00035 -0.00007 -0.00010 -0.00169 +0.00000      2.34
bare pLS pt>5             -0.02134 -0.00519 -0.00155 -0.00169 +0.00000     12.80
bare pLS pt>10            -0.00893 -0.00323 -0.00085 -0.00169 +0.00000      5.45
attachT3 |eta|>2.0        -0.00415 -0.00026 -0.00045 -0.00337 +0.00000     20.40
attachT3 pt>3             -0.00526 -0.00001 -0.00050 +0.00000 +0.00000      3.05
attachT3 |eta|>2.4        -0.00128 +0.00006 +0.00004 +0.00000 +0.00000      4.00
```
The T4 sub-cuts on the FULL 977 -- the "exactly zero efficiency" ones stop being exactly
zero, and all of them still pay in dxy[1,5):
```
change                       d_eff    d_dup   d_fake   d_v510    d_d15  rows/evt
T4 br1 & nB==4            -0.00011 -0.00001 -0.00395 -0.00704 -0.05293     12.49
T4 nPS<=2                 -0.00014 -0.00014 -0.00295 -0.00201 -0.03837     10.23
T4 inLay in {2,3}         -0.00005 +0.00001 -0.00275 -0.00101 -0.03804      8.97
T4 nPS<=1                 -0.00004 +0.00001 -0.00182 -0.00050 -0.03573      6.62
T4 inLay==2               -0.00003 -0.00003 -0.00102 -0.00050 -0.00265      2.92
T4 br==0 (the dup lever)  -0.00192 -0.00396 +0.00024 -0.00352 +0.00000     10.66
```
The attachT3 sub-cuts on the FULL 977 -- same verdict, all cost ~10x more efficiency than
the fake they buy:
```
change                       d_eff    d_dup   d_fake   d_v510    d_d15  rows/evt
X: drop all attachT3      -0.03852 +0.00190 -0.00751 -0.00352 +0.00000    133.6
attachT3 pt>3             -0.00586 -0.00000 -0.00051 -0.00050 +0.00000      3.1
attachT3 |eta|>2.0        -0.00450 -0.00022 -0.00050 -0.00101 +0.00000     20.1
attachT3 pt>5             -0.00287 -0.00002 -0.00011 +0.00000 +0.00000      0.7
attachT3 |eta|>2.4        -0.00171 +0.00004 +0.00004 +0.00000 +0.00000      4.0
```

## Cf. FINE SWEEP -- hunting a setting that is EXACTLY zero on every efficiency number
All four vxy bands and all four dxy bands printed, not just the headline ones.
```
change             d_eff    d_dup   d_fake  d_v01  d_v15 d_v510 d_v1030  d_d01  d_d15
-MR -1.75       +0.00000 -0.00000 -0.00021   0      0      0      0        0      0    <-- FREE
-MR -1.7        +0.00000 -0.00001 -0.00048   0      0      0      0     -1trk  -1trk
-MR -1.65       +0.00000 -0.00000 -0.00072   0      0      0      0     -1trk  -1trk
-MR -1.6        +0.00000 -0.00000 -0.00096   0      0   -1trk     0     -2trk  -1trk
-M4D -1.15      +0.00000 -0.00004 -0.00025   0      0      0   -1trk       0   -1trk
-M4D -1.1       +0.00000 -0.00007 -0.00048   0      0      0   -1trk       0   -1trk
-M4D -1.05      +0.00000 -0.00010 -0.00071   0      0      0   -3trk       0   -3trk
-M4D -1.0       +0.00000 -0.00016 -0.00087   0      0      0   -4trk   -1trk  -3trk
-C25 0.1 -2     -0.00009 -0.00002 -0.00019 -2trk    0      0      0     -2trk    0
-C25 0.5 -2     -0.00027 -0.00014 -0.00075 -6trk    0   -1trk     0     -7trk    0
```
`-MR -1.75` (from the frozen -1.800) is the ONLY setting in ~90 simulated variants that is
exactly zero on efficiency, on all four vxy bands and on all four dxy bands while gaining
fake rate on the frozen 300. It is worth -0.00021, i.e. 2% of the +.01075 gap.

THE SAME SWEEP ON THE FULL 977 -- the freeness does not survive:
```
change             d_eff    d_dup   d_fake  d_v01  d_v15 d_v510 d_v1030  d_d01  d_d15
-MR -1.75       +0.00000 +0.00001 -0.00024   0      0      0   -3trk   -tiny     0    <-- NOT free
-MR -1.7        -0.00001 +0.00001 -0.00049   0      0      0   -3trk   -tiny  -1trk
-MR -1.65       -0.00005 +0.00002 -0.00074 -tiny    0   -1trk  -5trk   -tiny  -3trk
-MR -1.6        -0.00005 +0.00003 -0.00097 -tiny    0   -2trk  -9trk   -tiny  -5trk
-M4D -1.15      -0.00001 -0.00003 -0.00025 -tiny    0      0   -1trk   -tiny  -1trk
-M4D -1.0       -0.00005 -0.00012 -0.00089 -tiny  -2trk    0  -12trk   -tiny -10trk
-C25 0.1 -2     -0.00008 -0.00002 -0.00020 -tiny    0   -1trk     0    -tiny     0
-C25 0.5 -2     -0.00035 -0.00014 -0.00075 -tiny  -2trk  -3trk  -1trk  -tiny  -1trk
```
NOT ADOPTED. The 300-event freeness was an artifact; this is the confirmation.

The T5 sub-cuts on the FULL 977 -- every one destroys dxy[1,5) efficiency, as on the 300:
```
change                       d_eff    d_dup   d_fake   d_v510    d_d15  rows/evt
T5 nhit10 & nPS<=2        -0.01293 -0.00175 -0.00478 -0.08844 -0.20973     57.29
T5 br3 & nPS<=2           -0.00436 -0.00083 -0.00446 -0.05779 -0.20774     44.15
T5 inLay>=2               -0.01435 -0.00295 -0.00477 -0.08945 -0.21303     72.66
T5 nN==2 & nPS<=2         -0.01004 -0.00105 -0.00333 -0.06935 -0.19054     43.04
T5 nPS<=1                 -0.00043 -0.00007 -0.00008 -0.00452 -0.00132      2.24
```
EVERY family in sections C and D was re-measured on the full 977 and reproduces. There is
no candidate anywhere in the search that is free on the confirmation sample.

## E. THE ZERO-COST CELL SEARCH
All 7-dimensional structural cells (deliv x type x branch x nPS x innermost layer x pt bin
x eta bin) with N >= 150, filtered to those containing ZERO rows that are the sole cover of
any counted sim. Exactly one usable cell exists:
```
cell                                          rows/evt  fake%  essential  d_fake
chain T4, br1, nPS 2, inLay 2, pt<1.5, barrel     0.89   76.3          0  -0.0004
```
A seven-way cut fitted on 300 events for four ten-thousandths of fake rate. NOT PROPOSED.
THE SAME SEARCH ON 977. At the identical threshold N >= 150 the 300-event cell is GONE
(it now contains essential rows) and only two zero-essential cells survive, together
0.74 rows/evt at 14.4% fake purity -- worth about **-0.00004** of fake rate:
```
cell                                          rows/evt  fake%  essential
('chain','T4',1,1,7,'p1','E1')                    0.16   43.0          0
('zp8pLS','pLS',-1,0,0,'p1','E2')                 0.58    6.4          0
```
At N >= 400 only the second survives, and at 6.4% fake it is DIRTIER than the 5.6%
average -- deleting it makes the fake rate worse. There is no usable zero-cost structural
cell on the confirmation sample at all.

## G. THE PERFECT-FILTER CEILING PER CELL (full 977; costs nothing anywhere by construction)
```
label                        eff      dup     fake |  fakB   fakT   fakE |   v15   v510  v1030    d15 |  nhB    nhT   nhE
BASELINE (W_X4)          0.80905  0.06184  0.05607 |.06754 .06932 .04578 |.80110 .72161 .71474 .58320 | 9.801 9.884 3.557
perfect pT3 filter       0.80905  0.06249  0.04622 |.05318 .05578 .03957 |.80110 .72161 .71474 .58320 | 9.859 9.940 3.541
perfect 5+ chain filter  0.80905  0.06305  0.03773 |.03497 .02861 .04183 |.80110 .72161 .71474 .58320 | 9.780 9.861 3.528
perfect T4 filter        0.80905  0.06218  0.05095 |.05395 .06760 .04436 |.80110 .72161 .71474 .58320 | 9.827 9.887 3.551
pT3 head at LST's rate   0.80905  0.06235  0.04835 |.05625 .05884 .04090 |.80110 .72161 .71474 .58320 | 9.847 9.927 3.545
LST                      0.80987  0.05138  0.04538 |.04365 .04542 .04630 |.77719 .64422 .62567 .51042 |10.150 10.009 3.557
```
"pT3 head at LST's rate" = keep every non-fake pT3-class row and 3498 of the 16308 fake
ones, i.e. exactly LST's own 3.81% pT3 fake rate at our own 93.97 rows/evt. It lands at
fake **.04835 (+.0030 vs LST, from +.0107)** with d_eff +0.00000 and every displacement
band +0.00000; the only cost is dup +.00051, which is pure denominator shrink.
Ceilings: pT3 -0.00985, bare 5+ -0.01834, T4 -0.00512, all at d_eff and d_(every band)
exactly 0.00000. Only the pT3 ceiling is ATTAINABLE -- LST's own pT3 is 3.81% fake against
our 17.76%, whereas LST's bare T5 (6.79%) and T4 (60.4%) are no better or much worse than
ours (9.31%, 33.4%).

## F. WHAT I CHANGED IN THE END
Nothing at all. `diff -r protoFIN protoA07` is empty and both binaries are md5
519b0abc34a28cd1e803d6b9407ef224. bestFlags = the assembled baseline, unmodified:

    -XC 3 -XCT 4 -T3E 1 -CC 1 -CCN 1 -CCR 2

No-op gate: tag GATEA07 reproduces fin_ref/r_FINBASE.root on 33/33 branches
(cmp_gate.txt) and on every aggregate.

## H. INDEPENDENT RE-DERIVATION (resumed session, second simulator written from scratch)
`a07_ref/sim_cut.py` + `a07_ref/a07b_run.py`, full 977 (fin_ref/r_W_X4.root).
Absolute rows (the same harness conventions, implemented independently of a07_sim.py):
```
tag                  eff      v01      v15     v510    v1030      d15     d510    d1030      dup     fake      nhB      nhT      nhE      nTC
BASELINE         0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06184  0.05607  9.80064  9.88373  3.55719  2033868
PERFECT_pT3      0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06249  0.04622  9.85914  9.94025  3.54130  2014916
PERFECT_T4       0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06218  0.05095  9.82688  9.88723  3.55057  2023933
PERFECT_BARE5    0.80905  0.84144  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.06305  0.03773  9.78037  9.86113  3.52792  1996529
PERFECT_ZP8_DUP  0.80903  0.84142  0.80110  0.72161  0.71474  0.58320  0.24521  0.03151  0.04467  0.05658  9.96625  9.98037  3.57300  2016327
DROP_pT3_ALL     0.77053  0.80080  0.77697  0.71809  0.71474  0.58320  0.24521  0.03151  0.06375  0.04857 10.27969 10.16800  3.49352  1900461
DROP_T4_BARREL   0.80892  0.84135  0.79868  0.71457  0.69204  0.53391  0.19374  0.01543  0.06183  0.05211  9.83994  9.88373  3.55719  2021840
DROP_T4_EXEMPT   0.80852  0.84100  0.79474  0.70804  0.68790  0.52795  0.19374  0.01543  0.06013  0.05130  9.83977  9.89110  3.52337  2010582
DROP_T4_ALL      0.80659  0.83902  0.79232  0.70452  0.68790  0.52795  0.19374  0.01543  0.05612  0.05151  9.83994  9.90306  3.48909  2000170
LST (target)     0.80987  0.84285  0.77719  0.64422  0.62567  0.51042  0.22906  0.05402  0.05138  0.04538 10.14984 10.00937  3.55665  1998494
```
per region: BASELINE effB .92454 effT .87981 effE .74436 | fakB .06754 fakT .06932
fakE .04578; PERFECT_pT3 .05318/.05578/.03957 (efficiency identical by construction);
DROP_T4_BARREL fakB .05433 with effB .92423.
Deltas:
```
change               d_eff    d_dup   d_fake    d_v15   d_v510  d_v1030    d_d15
PERFECT_pT3       +0.00000 +0.00065 -0.00985 +0.00000 +0.00000 +0.00000 +0.00000
PERFECT_T4        +0.00000 +0.00034 -0.00512 +0.00000 +0.00000 +0.00000 +0.00000
PERFECT_BARE5     +0.00000 +0.00120 -0.01834 +0.00000 +0.00000 +0.00000 +0.00000
PERFECT_ZP8_DUP   -0.00001 -0.01717 +0.00050 +0.00000 +0.00000 +0.00000 +0.00000
DROP_pT3_ALL      -0.03852 +0.00190 -0.00751 -0.02412 -0.00352 +0.00000 +0.00000
DROP_T4_BARREL    -0.00012 -0.00002 -0.00396 -0.00241 -0.00704 -0.02269 -0.04929
DROP_T4_EXEMPT    -0.00053 -0.00171 -0.00478 -0.00636 -0.01357 -0.02684 -0.05524
DROP_T4_ALL       -0.00245 -0.00573 -0.00456 -0.00877 -0.01709 -0.02684 -0.05524
```
Every one of these reproduces the corresponding row of sections B2/G to five decimals,
from a second implementation. `DROP_T4_BARREL` is the trap named in STATUS M10: on the
prompt denominator alone it is exactly free (zero sole-cover rows) and worth -.0040 of
fake rate; with the displacement bands carried it costs d15 -.0493 and v1030 -.0227.
