# LST ladder, phase 1: displaced efficiency at the hit-level ceiling (reference branch)

Reference only. This branch is the head of "phase 1" of a systematic campaign that walked the LST chain rung by
rung (mini-doublets, segments + module map, triplets, T4/T5 builders, track-candidate selection) and, at each
rung, made >= 99% of true displaced objects survive before moving on. Cost (timing, memory, fake rate,
duplicate rate) was deliberately ignored; buying it back is phase 2. Do not merge this branch.

It sits on the pre-rebase head of cms-sw/cmssw#51866 (`a2d88dd2240b`) plus the segment counting bound and the
"round 2" displaced changes. One commit per rung on top of that.

## Result (PU200 ttbar, 2000 events, sim pT > 0.9, |eta| < 2.4, |vz| < 30 cm, paired track by track)

| production radius (cm) | round 2 | this branch | hit-level ceiling (no layer skips) |
|---|---|---|---|
| prompt < 0.1 | 0.912 | 0.920 | - |
| 2.5 - 30 | 0.778 | 0.860 | 0.876 |
| 25 - 37.2 | 0.719 | 0.857 | 0.871 |
| 37.2 - 52.4 | 0.563 | 0.734 | 0.724 |

The bill: 202 ms/event against 41.7 (CPU, 32 streams), 827 MB/event allocated against 102, about 75,000 track
candidates per event (72,000 of them T4s), fake rate 79%, duplicate rate 21%.

## What each rung changed

1. Mini-doublets: pointing allowance `d0 / rt` with `kMdDispD0 = 16 cm` in every module. A circle with impact
   parameter d0 crosses radius rt at `sin(alpha) = rt/2R - d0/rt`: the displacement term is a length over a radius.
2. Segments and module map: a geometric module map (module A connects to B when a helix with pT >= 0.8 GeV,
   |d0| <= 16 cm, |z0| <= 30 cm can cross both; no layer-skipping steps), selected at run time with the
   environment variable `LST_MODULE_MAP`; and the same 16 cm as a displacement term in the segment r-z window.
3. Triplets: the mini-doublet direction test no longer rejects (the flag is still written); the T3 DNN displaced
   working point is multiplied by 0.02.
4. T4 builder: the T4 DNN accepts on `fakeScore < 0.99`; the direction-flag veto is removed; the r-z cut is
   `max(stock eta-keyed cut, per-layer-combination cut)`.
5. Track candidates: a T4 is removed as a duplicate only if a KEPT T4 of the same lower module holds >= 7 of its
   8 hits; the all-module T4 pass and CrossCleanT4 are deleted; the T5 DNN working point x0.01 and the endcap
   2S-2S-2S regions are enabled behind a `heldBack` flag (a held-back object never wins against a regular one).

The three loosened DNN working points (T3, T4, T5) are placeholders for a retrain. Loud guards were added for
the 4 GiB per-buffer limit and the track-candidate buffer. The instrumentation (truth-filtered ntuple writer,
reject probes, caps census) is default-off.

## Running it

The module map is NOT the one shipped with CMSSW. It is in this directory and must be selected explicitly;
without it the binary silently falls back to the stock map and rung 2 is lost:

    export LST_MODULE_MAP=$PWD/RecoTracker/LSTCore/standalone/ladder_phase1/module_connection_tracing_merged.bin
    lst_cpu -i <trackingNtuple> -n 1000 -s 12 -p 0.8 -v 1 -o out.root      # prints one [MAP] line when the variable is read

1000 events with the standard ntuple writer need ~120 GB of RAM at this candidate multiplicity. For EFFICIENCY
studies set `LST_RUNG_FILTER=1 LST_RUNG_TCFILTER=1 LST_RUNG_VXY_MIN=0`: the writer then keeps only candidates that
touch a hit of a kept sim track (~24 GB). That is exact for efficiency and WRONG for fake and duplicate rates;
take those from a standard run on a few hundred events.

## Regenerating the map

`module_connection_tracing_merged.bin`: 10,036 lists, 345,564 connections (stock 132,596), longest list 162
(loader cap raised from 40 to 1024; a longer list is now a load-time error instead of a silent truncation).
It is the union of the stock file and the geometric family, so no stock connection is lost.

    gunzip -k sensor_corners.txt.gz          # exact CMSSW sensor corners, 26,400 sensors
    python3 mapgen.py sensor_corners.txt <stock module_connection_tracing_merged.bin> out.bin SKIP=0 ESKIP=0 BE2=0

`GENERATION.log` is the log of the run that made the file. Note: the live C++ generator
(`RecoTracker/LSTGeometry/src/ModuleMap.cc`), run unmodified on identical geometry, does not reproduce the
shipped stock map (123,902 connections; 8,834 of the shipped 132,596 are absent). Porting the geometric d0 rule
into it is owed.
