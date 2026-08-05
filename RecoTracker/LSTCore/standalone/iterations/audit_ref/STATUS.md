# P1 DEPENDENCY AUDIT -- STATUS

Work dir: standalone/protoAUDIT (verified clean copy of prototype, all .cc/.h/Makefile identical).
Artifacts: standalone/audit_ref/
Gate: P25BASE eff .81303 | vxy01 .84610 | v15 .80283 | v510 .72713 | v1030 .71732 |
      d15 .56009 | d510 .24912 | dup .05188 | fake .04636 | nTC 614277 (300 evts).

## A1 -- FROZEN LINE DECODED (reading only)
P25BASE = `-m hybrid` + ANCHOR + CTL(`-A 4 -a 999 -D 5 -RT5 1`) + STACK + FLAGSHIP + M19.
Effective: -A 4 (M16 general attach), -RT5 1, -RT3 0 (default), -RPS 1, -RD 1,
-PU 1 (STACK overrides ANCHOR's -PU 2), -EX 1, -CC 0 (default), -H 1 (hit-level claim).

Consequences established by code reading:
 * -RT5 1 => replT5=1 => (main.cc:2859) ap.dropPartOfPT5=false  => the partOfPT5 half of
   the K9 pixel-consumed drop is ALREADY OFF in P25BASE.  t3_partOfPT5 is read only by
   diagnostics/funnel counters and by the -A 1/-A 2 paths (not used at -A 4).
 * -RT3 0 => ap.dropPartOfPT3 = TRUE => t3_partOfPT3 IS LIVE in K9 (K9K10.cc:91).
 * -RT5 1 => (main.cc:3226) EVERY carried type-7 (pT5) row is wholesale-suppressed, so
   tc_pt5Idx / pT5_t5Idx / t5_hitIndices / pT5_plsIdx are all skipped at every consumer
   (preclaim main.cc:3271, extend main.cc:3572, -CC main.cc:3944, OutputWriter.cc:280).
 * carried type-5 (pT3) rows ARE live: emitted to output, pre-claim owners via
   pT3_otHitIndices (main.cc:3284), extend claim map (main.cc:3583), rowPls via
   pT3_plsIdx (main.cc:3163).
 * carried type-8 (bare pLS) rows ARE live (tc_plsIdx, main.cc:3165).

## A2 -- NO-OP GATE (300 evts, protoAUDIT clean build, tag AUBASE)
eff .81303 vxy01 .84610 v15 .80283 v510 .72713 v1030 .71732 d15 .56009 d510 .24912
dup .05188 fake .04636 nTC 614277 == P25BASE EXACTLY. Instrument is free. wall 101.4 s.

## A3 -- LST-SIDE SETTER MAP (reading)
 * t3_partOfPT5  set ONLY by PixelQuintuplet.h:732/733 (CreatePixelQuintuplets). DIES.
 * t3_partOfPT3  set ONLY by PixelTriplet.h:830 (CreatePixelTriplets). DIES.
 * pLS block in the ntuple is written for EVERY pixel segment (write_lst_ntuple.cc:1833
   loops 0..n_pls with NO isDup filter) -> pLS_* SURVIVES unchanged.
 * pixelSegments.isDup() write order:
     LST.cc:106 pixelLineSegmentCleaning -> CheckHitspLS(secondpass=false)  |= 1  KEPT
     LSTEvent.dev.cc:3020 CheckHitspLS(secondpass=true)                     |= 2  DELETED
     TrackCandidate.h:383/390/400/405/415 CrossCleanpLS                     = true DELETED
   isDup is MONOTONE (never returns to 0), so today's admitted type-8 set is a strict
   SUBSET of the post-deletion one.
 * AddpLSasTrackCandidate gate (TrackCandidate.h:718): !isQuad || isDup.
 * CrossCleanpLS `= true` CLOBBERS the 1/2 bitmask -> a single end-of-run snapshot cannot
   separate pass-1 from pass-2 from crossclean. TWO snapshots are genuinely required.
 * IN-LST PORT (src/alpaka/Chain*.h) hard deps on deleted SoAs:
     ChainArbitrate.h:157-158  triplets.partOfPT5()/partOfPT3()
     ChainAttach.h:1081/1084 + ChainParallel.h:176/179
        pixelTriplets.pixelSegmentIndices() / pixelQuintuplets.pixelSegmentIndices()
   The port reads carried-row OT hits from candsBase.hitIndices() (generic), NOT from
   t5_hitIndices -- so the prototype's t5_hitIndices/pT3_otHitIndices routing has no
   port-side equivalent to break.

## A4 -- COUNTS FROM THE NTUPLE (20 evts)
 nPLS 18403/evt | quad pLS 14303/evt | type-8 TCs 743.5/evt | type-5 (pT3) 129.4/evt
 | type-7 (pT5) 690.5 | type-4 (T5) 119.0 | type-9 (T4) 28.9
 => isDup removes 13,560 of the 14,303 quad pLS per event today.

## A5 -- MEASURED SCOREBOARD (300 evts, all runs `-CF 1 -CFC 1`)
tag      flags                                        eff     dup     fake    nTC
AUBASE   (frozen)                                   .81303  .05188  .04636  614277
ZPF1     -ZPF 1  (t3_partOfPT5 -> false)            .81303  .05188  .04636  614277  IDENTICAL
ZP5      -ZP5 1  (all pT5-side routing dead)        .81303  .05188  .04636  614277  IDENTICAL
ZPF2     -ZPF 2  (t3_partOfPT3 -> false)            .81303  .05334  .04652  614879
ZPF3     -ZPF 3  (both)                             .81303  .05334  .04652  614879
ROFF     -RT3 1 -T3E 0 (pT3 class deleted)          .77432  .05254  .04905  579968
ZP8L     -ZP8 2  (bare-pLS lower bracket)           .81790  .13622  .04631  646271
ZP8P2    -ZP8 4  (pass 2 KEPT, crossclean dead)     .82163  .18956  .04703  665941
ZP8F     -ZP8 3  (family model, pass 1 only)        .82216  .19732  .04872  674079
ZP8U     -ZP8 1  (upper bracket, unphysical)        .82501  .91193  .02535 4626808
FULL     ZPF3+ZP5+RT3/T3E0+ZP8 3                    .81074  .21093  .05024  666875
FULLP2   same but -ZP8 4                            .81009  .20329  .04856  658675

## A6 -- CONCLUSIONS
1. t3_partOfPT5 and the whole pT5-side routing are ALREADY INERT under the frozen -RT5 1
   (measured zero, twice). Deleting them costs NOTHING.
2. t3_partOfPT3 is LIVE but SMALL: dup +.00146, fake +.00016, nTC +2.0/evt, eff 0.
3. Carried LST type-5 (pT3) rows are worth eff +.03871 (reproduces gen_c r_off .77432).
4. THE UNMEASURED REGRESSION IS THE BARE-pLS TC UNIVERSE. CrossCleanpLS + CheckHitspLS
   pass 2 remove 13,560 of 14,303 quad pLS per event today. Post-deletion only pass 1
   survives; the family model puts ~200 extra type-8 TCs/evt back and dup goes
   .05188 -> .19732 (x3.8). Keeping pass 2 recovers almost none of it (.18956): the
   damage is CrossCleanpLS, which our M16 contention is supposed to replace but cannot,
   because it only ever inspects the carried rows LST already let through.
5. NOT MEASURABLE FROM THIS SAMPLE: the carried type-5 (pT3) row SET ITSELF changes when
   pT5 is deleted. PixelTriplet.h:721/:770 skip partOfPT5-flagged pLS/T3 and CrossCleanpT3
   kills pT3 within dR2<1e-5 of a pT5. Today 482.6 pT3/evt are built but only 129.4 become
   type-5 TCs. Post-deletion all three filters lose their pT5 input.
