#ifndef RecoTracker_LSTCore_interface_ChainConfig_h
#define RecoTracker_LSTCore_interface_ChainConfig_h

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace lst {

  // FROZEN chain-tracking configuration (standalone/fanout5/final/FREEZE_RECORD.txt, resolved
  // ANCHOR + CTL + FLAGSHIP + M19 with later flags winning). Fields are grouped by the port-map
  // phase that introduced them: P2.2 = weld / trim / gate, P2.3 = claim + extension + assembly.
  // The attach knobs (-a -AT3 -RPS -RD -D4) belong to P2.4 and are still deliberately absent;
  // P2.3 runs with attach INERT, which is what makes -RPS / -RD irrelevant here.
  //
  // Kept as a plain struct with frozen defaults rather than as bare constexpr so that P2.3 can
  // fill it from the LSTProducer parameter set (port map section 3.3) with no kernel change; the
  // struct is passed to the kernels by value.
  struct ChainConfig {
    // -e 0 : K6 weld eligibility on the edge logit.
    float thetaEdge = 0.f;
    // PER-EDGE-FAMILY weld eligibility. A type-1 (E1, shared-MD) weld ALWAYS yields a 5-layer
    // chain and a type-2 (E2, shared-LS) weld ALWAYS yields a 4-layer one, and the gate downstream
    // already judges those two classes with SEPARATE bars (m3Theta4 / m3Theta4D for the 4-layer
    // class, m3ThetaRI / m3ThetaR / m3ThetaRB / m3ThetaRT for 5+). The weld carried ONE bar for
    // both, even though the edge type DETERMINES which class the weld produces. That coupling is
    // not neutral: the two families sit on very different logit scales (PU200RelVal, shipped edge
    // head, 3.29M E1 + 1.33M E2 edges: E1 median logit -3.79 with 19.95% at-or-above thetaEdge = 0,
    // E2 median +0.22 with 53.49%), so a GLOBAL loosening admits about four times more E1 than E2 --
    // and because the weld argmax is taken over all incident eligible edges REGARDLESS OF TYPE with
    // one out-slot and one in-slot per node (ChainWeld.h), the extra E1 edges take the slots the E2
    // pairs wanted. Loosening E1 alone therefore DESTROYS 4-layer chains. Splitting the bar makes
    // the weld consistent with the gate rather than adding a new kind of knob.
    //
    // A value >= 1e29 means "inherit thetaEdge for this family", resolved on the HOST, so leaving
    // both at the sentinel reproduces the single-bar behaviour exactly.
    float thetaEdgeE1 = 1e30f;
    float thetaEdgeE2 = -2.0f;
    // S1 (on-policy edge retrain). The weld bar is a TABLE on LST's T3-DNN working-point binning --
    // dnn::kPtBins (2, split at pT 5) x dnn::kEtaBins (10 x 0.25 in |eta|, last bin absorbing
    // > 2.5), SEPARATELY for E1 and E2 -- shipped in EdgeNetworkWeights.h as dnn::edgemlp::kWpBar
    // and keyed on the INNER node's cell (ChainNodesSoA::wpBin). The table is part of the head's own
    // calibration artefact: every cell reproduces the SHIPPED head's measured true-edge acceptance
    // in that cell, which is what makes swapping the head physics-neutral by construction and leaves
    // only the false-edge rate free to move.
    // false falls back to the two per-family SCALARS above, byte for byte: that is the A/B and the
    // inertness proof for the plumbing, not a tuning knob.
    bool edgeWpTable = true;
    // -L 3.0 : chain score length weight (ANCHOR said 0.5, the M19 block overrides).
    float lambdaLen = 3.f;

    // -TR 1 / -TT 1.2 / -TA 1.0 / -TL 5 / -TP 1 : terminal trim.
    bool terminalTrim = true;
    float trimFactor = 1.2f;
    float trimAbsChi2 = 1.f;
    int trimMinLayersAfter = 5;
    int trimPasses = 1;
    // TRIM-NN: WHICH RULE takes the terminal-trim decision. The variants are constructed either
    // way -- that half is mechanical -- so this selects only who chooses between them.
    //   0 the K6f chi2 improvement ratio (the shipped rule)
    //   1 the chain head's argmax over the variant margin mX, no guard at all
    //   2 as 1, but a dropped variant must still keep trimMinLayersAfter layers
    //   3 as 1, but only on chains the K6f concentrating guard admits (chi2Full > trimAbsChi2)
    //   4 REFEREE CONTROL, not a candidate: the same unguarded 3-way argmax as 1, decided by the
    //     chi2 PROXY instead of by the head, so "the head chooses better" can be separated from
    //     "trimming more is better".
    //   5 as 1, but the winning variant must beat the full chain by trimMarginGap logit units
    // SHIPPED (trim-NN round, arm G10): the terminal-trim DECISION is the chain head's, not a chi2
    // threshold -- but the head must be DECISIVE, not merely ahead. A terminal node is dropped only
    // when the best variant's realness margin beats the FULL chain's by trimMarginGap logit units,
    // i.e. when the shortened chain is e^gap = 2.7x more favourable in real-vs-fake odds. mode 1 is
    // the same argmax with NO gap (measured: more jet-core efficiency, but it spends 46% more
    // outer-tracker hits to buy 20% more realness -- the marginal trims shorten tracks without
    // buying anything). mode 0 restores the chi2 rule for A/B and is scheduled for deletion once it
    // has gone a round unused. See standalone/FINDINGS_TRIMNN.md.
    int trimMode = 5;
    // The one constant the learned rule can need: how decisively the head must prefer a drop.
    float trimMarginGap = 1.f;

    // -X 0.5 : IP-compatibility boundary on the chain dcaXY, cm.
    float dcaSplit = 0.5f;
    // -Z 0 : extra dca floor for the T4-class exempt branch (exempt iff dca >= max(-X, -Z)).
    float t4ExemptDcaMin = 2.0f;
    // -X2 / -M4D2 / -FR : SECOND breakpoint on the same reconstructed-dcaXY axis dcaSplit already
    // branches on, applied to the T4-class EXEMPT branch only. The exempt bar m3Theta4D is ONE
    // number covering every chain with dcaXY >= 0.5 cm, and that population is overwhelmingly
    // concentrated just above 0.5 cm, so the bar a 20 cm displaced chain must clear was fitted on
    // 0.5-2 cm objects. Above dcaSplit2 the cell gets its own bar and the eta-band delta zdM4D is
    // NOT applied. The cell also requires a GOOD FIT (feature 16 = maxXyResid) so it selects
    // "displaced AND well measured" rather than merely "badly fitted": in the far cell PU200
    // far-displaced positives sit at maxXyResid p50 0.015 while PU200 fakes sit at p50 0.245.
    // dcaSplit2 = 1e9 leaves the cell EMPTY and t4FarMaxResid = 1e9 disables the guard, so the
    // defaults reproduce the shipped configuration exactly. Both are per-chain RECO quantities.
    float dcaSplit2 = 12.f;
    float m3Theta4D2 = -1e9f;
    float t4FarMaxResid = 0.02f;

    // -G 6 three-class margin kills. mP = zPrompt - zFake, mD = zDisp - zFake,
    // mX = max(zPrompt, zDisp) - zFake.
    float m3Theta4 = 2.256949f; // -M4  : T4-class IP    kill iff mX < m3Theta4
    float m3Theta4D = -2.381371f; // -M4D : T4-class exempt kill iff mD < m3Theta4D
    float m3Theta5 = 1e9f;    // -M5  : IP nLayers == 5 kill iff mP < m3Theta5 (inert at 1e9)
    float m3Theta6 = 1e9f;    // -M6  : IP nLayers >= 6 kill iff mP < m3Theta6 (inert at 1e9)
    float m3ThetaD = 1e9f;    // -MD  : exempt 5+       kill iff mD < m3ThetaD (inert at 1e9)
    float m3ThetaRI = -0.4409661f; // -MRI : IP-5+     OR-rescue floor on mX
    float m3ThetaR = -1.44432f;  // -MR  : exempt-5+ OR-rescue floor on mX (endcap + no-member fallback)
    // -MRB / -MRT: band split of the exempt-5+ OR-rescue floor. Band on |eta| of the innermost
    // member T3 (the K10 TC eta), boundaries zEta1 / zEta2. Resolved values only -- the prototype's
    // kMrUnset sentinel is resolved on the host and never reaches a kernel.
    //
    // BOTH BANDS ARE BACK AT THE GLOBAL -MR (maintainer, 2026-08-06). The tightened -1.2 bars were
    // the largest single consumer of the displaced advantage: ~45 of the 77 distinct DISP1 sims the
    // barrel-dup round spent (displaced ledger in PLAN_lst_redesign_t3_onward.md), because the
    // exempt (large-DCA) branch they cut is exactly the branch that carries displaced tracks. What
    // they bought was FAKE rate only (fakB -.0057, fakT -.0055), which is the lowest-priority
    // metric, so the trade is being unwound. The band machinery stays in place -- setting these two
    // fields is all it takes to put the bars back.
    float m3ThetaRB = -1.214288f;
    float m3ThetaRT = -1.011139f;
    // -C25 0.0 / -C25D -2.0 : the (nNodes == 2, nLayers == 5) cell rule; kills only when BOTH
    // margins fail, and never re-kills an already-killed chain.
    float c25Theta = 2.268653f;
    float c25ThetaD = -1.208447f;

    // Transition-band levers. The band is keyed on |eta| of the chain's INNERMOST member T3
    // (the same quantity K10 gives the TC), and the deltas are ADDITIVE to the thresholds above.
    float zEta1 = 1.1f;      // -ZE1
    float zEta2 = 1.7f;      // -ZE2
    float zdM4 = -0.5098293f; // -ZM4  : added to -M4
    float zdM4D = 1.473291f; // -ZM4D : added to -M4D
    float zdRI = 0.f;        // -ZRI  : added to -MRI
    float zdR = 0.f;         // -ZR   : added to -MR
    float zdR5 = 0.f;        // -ZR5  : added to -ZR for exempt nLayers == 5
    float zdR6 = 0.f;        // -ZR6  : added to -ZR for exempt nLayers >= 6
    float zdCP = 0.f;        // -ZCP  : added to -C25
    float zdCD = 0.f;        // -ZCD  : added to -C25D
    bool zInLayer1 = false;  // -ZIL : restrict every band lever to chains starting in layer 1

    // Reference-implementation constant, not a flag: the score subtraction that marks a killed
    // chain (prototype main.cc kGateKill).
    float gateKill = 1e9f;

    // ------------------------------------------------------------------------------------------
    // P2.3 -- K9 hit-claim arbitration.
    // ------------------------------------------------------------------------------------------
    // prototype/main.cc kNoCutTheta: with -G 6 the gate is a KILL (score -= gateKill), so K9's
    // base per-length threshold must pass every live chain while still rejecting a killed one.
    float noCutTheta = -1e5f;
    // -U4 / -U5 / -U6: the per-length acceptance thresholds of the EXEMPT (large-dcaXY) branch,
    // which K9 applies through ArbitrationParams::altThreshold. Live on chains.score, i.e. on the
    // legacy sum-logit scale, which is why they are 0 rather than noCutTheta.
    float thetaExempt4 = 0.f;
    float thetaExempt5 = 0.f;
    float thetaExempt6 = 0.f;

    // -B 10 / -BK 1 / -BT 5: the K9 best-first ORDER key. Never a threshold (M9 lesson):
    //   orderKey = score - orderAlpha * max(0, orderHinge - marginX)
    // -BK is compile-time 1 (the 3-class mX hinge); modes 0 and 2..11 are dead experiments.
    float orderAlpha = 10.f;
    float orderHinge = 5.f;
    // JET ROUND 3 (KEY): an ETA-CONDITIONED weight on the SAME hinge, no new column and no new
    // kernel -- the band |eta(innermost T3)| this ramps on is already computed a few lines below
    // for the -WZ braid band, and is simply hoisted above the key.
    //
    //   alphaEff = orderAlpha + (orderAlphaCentral - orderAlpha) * (1 - ramp(aEta))
    //   ramp(x)  = clip((x - orderEtaRampLo) / (orderEtaRampHi - orderEtaRampLo), 0, 1)
    //
    // WHY ETA AND NOT pT OR OCCUPANCY. A rank key is a GLOBAL total order, so conditioning its
    // WEIGHT on a per-chain quantity scrambles comparisons between chains in different regimes
    // (measured, 200 jet tune events, offline claim replay: the identical boost conditioned on the
    // chain's own ptEst is core -.0747 and on its junction-degree product -.0360, against +.0054
    // unconditioned). Eta is the exception because the chains that CONTEND for a hit are in one
    // detector neighbourhood and therefore share the conditioner. The measured payoff is that the
    // deployed PU200 duplicate cost of the unconditioned boost is ENTIRELY ENDCAP (+239 dup TCs at
    // |eta| >= 1.7, against -54 barrel and -32 transition, 1000 events), while the jet-core gain is
    // central barrel (JPR2: the residual master gap is |eta| < 0.6).
    // orderAlphaCentral <= 0 means "inherit orderAlpha", i.e. the ramp is OFF, which is the
    // shipped behaviour bit for bit.
    float orderAlphaCentral = 50.f;
    float orderEtaRampLo = 1.0f;
    float orderEtaRampHi = 1.3f;

    // -F 0.20 / -FC 1 / -FCX 0: the candidate-relative claim tolerance. -FC is expressed in MD
    // units for physics readability; the claim universe is HITS (-H 1 is compile-time true) and
    // every MD contributes 2 of them, so the budget doubles.
    float maxClaimedFrac = 0.20f;
    int maxClaimedMDs = 1;
    bool claimCountExclusive = true;

    // -W 0.50: owner-relative braid kill. -WE 0.20 / -WZ 1.5 / -WN off: the BAND-AWARE braid --
    // candidates with |eta(innermost T3)| >= braidAltEta and nNodes <= braidAltMaxNodes use the
    // tight fraction. -FB / -FBC 0: the same band's own claim tolerance (claimItemsAltMDs == -2
    // and claimFracAlt <= 0 mean "band uses the global value").
    float braidFrac = 0.2f;
    float braidFracAlt = 0.20f;
    float braidAltEta = 1.5f;
    float braidAltMaxNodes = 1e9f;
    float claimFracAlt = 0.f;
    int claimItemsAltMDs = 0;

    // -PU 1: claim-universe unification. The kept carried pixel rows' outer-tracker hits are
    // pre-claimed before the greedy walk. Mode 2 (pixel owners also join the braid) is not in the
    // freeze, so pixel owners only supply claimed slots to the maxClaimedFrac test.
    bool preClaim = true;

    // -RT5 1 / -RT3 1: wholesale class replacement. replacePT5 drops every carried type-7 row and
    // replacePT3 every carried type-5 row; the chains deliver the pT5 class as bare/attached chain
    // TCs and the pT3 class through the bare-T3 attach (stage B, ChainAttachT3.h).
    bool replacePT5 = true;
    bool replacePT3 = true;

    // ------------------------------------------------------------------------------------------
    // P2.4 -- pixel attach (-A 4, the general pLS -> outer-tracker attach as the DELIVERY path).
    // ------------------------------------------------------------------------------------------
    // The pair-head logit an (accepted chain, pLS) pair must reach to attach, banded on |eta| of the
    // SEED (pLS) at the literal 1.1 / 1.7 boundaries. The head's logit calibration shifts ~4.6 units
    // across eta, so one global margin would be a different working point per band. All three are
    // RESOLVED values (no follow-the-barrel sentinel survives the port). plsBestChainLogit stays
    // UNBANDED: it is recorded for every scored pair BEFORE the threshold (invariant I4).
    //
    // ALL THREE BANDS ARE BACK AT 6.0 (maintainer, 2026-08-06). The barrel-dup round lowered the
    // barrel and transition margins to 5.0 to convert failed attaches into single longer tracks; it
    // bought barrel duplicate rate but every one of its displaced losses was a bare chain converted
    // with a WRONG seed (the T5 -> type-7 match is lost), and together with -CCS it accounted for
    // ~34 of the 77 distinct DISP1 sims the round spent (displaced ledger in the plan). Displaced
    // efficiency is the headline advantage, so the trade is unwound.
    // RE-FITTED for the 20-input head, whose logit scale differs: -a 6.0 on it would be a LOOSENED
    // attach (conversion 0.6799 -> 0.7470), and loosening conversion is exactly what costs displaced
    // efficiency. These values hold conversion at the 19-input head's operating point and are not
    // independent of AttachNetworkWeights.h.
    // S3 (2026-08-12) RE-FIT FOR THE 22-INPUT HEAD. Every bar on the attach logit scale below is
    // set at FIXED per-band TRUE-PAIR acceptance: each value is the quantile of the new head's
    // true-pair logits, in that bar's own universe x |seed eta| band, that reproduces the SHIPPED
    // bar's true-pair acceptance in the same cell -- measured with the shipped head's own logit on
    // the SAME on-policy rows, so the reference is in the data and the fit costs ZERO parameters.
    // Still NOT independent of AttachNetworkWeights.h.
    float attachTheta = 6.64275f;    // -a   delivery margin, |eta| < 1.1
    float attachThetaT = 6.23461f;   // -a2  delivery margin, 1.1 <= |eta| < 1.7
    float attachThetaE = 6.209162f;  // -a3  delivery margin, |eta| >= 1.7
    // -AT3 6.0: the bare-T3 (stage B) delivery margin, GLOBAL -- no eta bands. Also the T3-side
    // retirement bar of the -RPS predicate (-RPST was measured as a dead end and is deleted; the
    // bar is hardcoded to this value).
    float attachThetaT3 = 5.558521f;  // S3: re-fitted for the 22-input head
    // -RPSA 5.5: the chain-side RETIREMENT bar of the -RPS predicate. Global, deliberately NOT
    // banded (banded -RPSA measured dominated). The retirement kernels must read THIS, never
    // attachTheta -- reusing the delivery margin is the pre-A11 behaviour and is wrong now that
    // delivery is banded.
    float rpsThetaChain = 5.32247f;   // S3: re-fitted for the 22-input head
    // -T3F 0.10: stage-B target admission on the production T3 fake score (node feature 12 ==
    // triplets.fakeScore()). NaN-rejecting form !(fakeScore <= t3FakeMax). Applied to the bare
    // mask so cut targets are never scored and never write plsBestT3.
    float t3FakeMax = 0.10f;
    // -CC 1 -CCG 1 -CCN 1 -CCK 0 -CCP 1 -CCR 2: hit-overlap contention on the stage-B deliveries.
    // The unit is the MD row (-CCG 1 hardcoded), the sweep is (logit desc, T3 row asc) (-CCK 0
    // hardcoded), the pre-claim is every emitted chain TC's deduped MD set (-CCP 1; the carried
    // pixel rows contribute nothing because replacePT5/replacePT3 dropped them all), and a revoke
    // releases the seed AND erases its bare-T3 evidence (-CCR 2 hardcoded -- the only release that
    // releases; see the -CCR 1 failure mode in the port spec). Only the count is a parameter.
    int ccMinShared = 1;
    // -CCS (chain-loser suppression of an accepted BARE chain whose best scored pair toward a pLS
    // owned by a DIFFERENT chain reached a banded bar) IS DELETED, not switched off. It was part of
    // the same barrel-dup trade as the -a lowering above, was unwound with it (all three bands
    // 1e9 = OFF, maintainer 2026-08-06) and then cost 1.1 ms/event of GPU time computing a loser
    // score that no configuration consumed: it needed its own post-contend grid pass plus the
    // inverted grant map that fed it. Restoring it means restoring that pass, so it lives in git
    // history rather than as a dead field. The -XC4 half of that pass -- which IS load-bearing --
    // now rides in ChainAttachScore.
    // -XC 3 -XCT 3.665 -XCT2 3.15 -XCT3 3.75 -XCR2 1e-6 -XC4 1: the ported CrossCleanpLS.
    // Pixel-anchored arm: retire a bare quad seed sharing >= 1 pixel hit row with the seed of any
    // delivery. LST's second test there (dR^2 < 1e-6 between the two seeds) is DELETED as measured
    // inert -- see ChainCrossClean.h.
    // Bare-chain arm: retire a bare quad seed whose attach-head logit for SOME delivered SEEDLESS
    // chain TC reaches the |seed eta|-banded xcTheta -- the substitution for LST's deleted pLS/T5
    // embedding test. LST's T5 arm also required dR^2 < 0.02 against the TC centroid direction;
    // that window is DROPPED (see ChainAttach.h pass 1 for why, and for the measurement).
    // -XC4 is folded in unconditionally (4-layer accepted chains join the scored-pair stream,
    // score-only); -XC4T / -XCD / -XCG 1 are dead ends and are not ported.
    // Bars re-fitted for the 20-input head (its logit scale differs from the 19-input one; see
    // AttachNetworkWeights.h). These are NOT independent of the weights header.
    // S3: re-fitted for the 22-input head by the same fixed-acceptance rule as the -a bars.
    float xcTheta = 2.114034f;   // |seed eta| < 1.1
    float xcThetaT = 2.120019f;  // 1.1 <= |seed eta| < 1.7
    float xcThetaE = 3.401128f;  // |seed eta| >= 1.7
    // -CC9 2: the T4-class crossclean against the delivered SEEDED rows -- drop a delivered type-9
    // bare chain sharing this many outer-tracker hits (2 = one full mini-doublet) with a delivered
    // type-7 or type-5 row. 0 disables it. See ChainCrossClean.h; measured fake barrel -.0037 with
    // efficiency unchanged to 6 decimals and an exact cost of two displaced sims, neither in the
    // efficiency denominator (977 evt). type-9 rows never share 3+, so the value is effectively
    // binary.
    int cc9MinShared = 1;
    // JET ROUND 2 (D) -- MUTUAL-BEST bare-seed retirement. A bare pLS row is retired when it is the
    // PRE-THRESHOLD argmax pair of a delivered 5+-layer accepted chain and that chain is the seed's
    // OWN best-scoring chain, provided the pair's logit is within dupMutualDelta of that pair's
    // banded DELIVERY margin. NEGATIVE = OFF, which is the shipped value: nothing is written and
    // nothing is read, so the arm is bit-identical to the shipped configuration.
    // Rationale (d2_ref): 97% of jet-core duplicate pairs and 91% of the gate retrain's PU200
    // duplicate regression are (delivered chain TC, un-retired bare pLS row), and the three existing
    // retirement bars cannot separate "this seed's track is already delivered" from "this seed is the
    // only thing covering its track" -- a BAR admits a seed whose own track lies elsewhere, while a
    // MUTUAL argmax cannot. Measured pair precision on EA's stage-A corpus: .87-.93 for the new
    // decisions at delta 0.5 against .46-.53 for the marginal decisions of a bar.
    float dupMutualDelta = -1.f;
    // JET ROUND 2 (D) -- the REGION-CONDITIONED -XC delta (see ChainAttachPlsPre). dupXcDelta <= 0
    // is OFF; the ramp reaches its full depth only as pt -> 0 and vanishes at dupXcPtMax, and the
    // barrel never sees it. SHIPPED at 1.5: it removes the gun-enriched gate's whole PU200
    // duplicate excess (dup_overall and dup_transition both land BELOW the pre-round value) at
    // PU200 efficiency equal to it, jet-core efficiency exactly unchanged, zero discordant sims in
    // every displaced band, and both cube guns bit-identical.
    float dupXcDelta = 1.5f;
    float dupXcPtMax = 3.f;
    // -RPS 1: also retire a carried bare-pLS (type 8) row whose seed had a scored pair above its
    // class RETIREMENT bar but lost the contention. -RD 1: seed-family dedup of the attach owners,
    // two pLS being the same seed when they share >= 2 pixel hit rows (also gates the stage-B
    // -RDT dedup, which follows -RD).
    bool attachSuppressBarePLS = true;
    bool attachSeedDedup = true;
    // -D4 1e9: attach-eligibility dcaXY gate, deliberately OFF (the displaced-with-pixel-seed
    // population is an upside to claim, not a dilution to guard against).
    float attachDcaMax = 1e9f;
    // prototype/PixelAttach.h AttachParams: the analytic prefilter windows. No flag; frozen. They
    // are ALSO the grid's design inputs (ChainAttach.h derives its cell widths from them).
    float attachPrefDPhi = 0.4f;
    float attachPrefDTanL = 0.6f;

    // ------------------------------------------------------------------------------------------
    // JET ROUND 3 (T4) -- the 4-LAYER CLASS POLICY. Every default below reproduces the shipped
    // configuration EXACTLY, so an unset environment is bit-identical (proven, not asserted: the
    // zero-knob arm is the control).
    //
    // The measurement that motivates the group (t4_ref/{price,stage}.py on JPR2's 450-event jet
    // funnel corpus): the 4-layer class is the SOLE cover of 585 core sims per 450 jet tune events
    // (+.067 of dR<.05 efficiency, +.047 of pooled jet-core efficiency) and it concentrates in
    // exactly the sim-pt band where LST master collapses (best-match is 4-layer for .22 / .31 /
    // .37 of matched sims at 50-100 / 100-300 / >300 GeV). At the same time 677 of 873 stage-e
    // core sims have a gate-KILLED chain made entirely of their own T3s that is 4 layers long and
    // sits in the IP branch -- i.e. the 4-layer IP bar, not the weld and not the claim, is what
    // stands between us and three quarters of the gate stage's budget on jets.
    //
    // (1) t4DensDelta LOWERS the branch-0 (4-layer, IP) gate bar by up to t4DensDelta, ramped in
    //     LOCAL graph occupancy: ChainFeatures column 14, the maximum over the chain's OWN weld
    //     junctions of degIn * degOut at the junction element. The ramp is
    //         w = min(1, max(0, log10(1+deg) - log10(1+t4DensRho0)) / t4DensDecades)
    //     which is identically ZERO at or below t4DensRho0 and continuous above it, so every
    //     region below that occupancy is bit-identical to the ship BY CONSTRUCTION rather than by
    //     measurement (PU200 branch-0 chains and both displaced cube guns live decades below the
    //     jet-core values). It is a CONDITIONING variable, never a rank term.
    // (2) t4GateTighten is the opposite sign as a single unconditioned number, for PRICING the
    //     suppression direction. It is added to the 4-layer IP bar and to the NON-far exempt bar;
    //     the E1-B2 far-dca cell (dcaSplit2 + t4FarMaxResid) keeps its free pass untouched.
    // (3) t4EmitMinLayers overrides kChainTCMinLayers at the K10 emission flag ONLY, so the claim
    //     runs identically and the arm isolates "what does the class DELIVER" from "what do its
    //     hits do to everyone else". 0 = the frozen kChainTCMinLayers.
    // (4) t4AttachMinLayers overrides kAttachMinLayers in the stage-A target filter, which makes a
    //     4-layer chain a real attach target (contention, plsBest, delivery) instead of the
    //     score-only -XC4 aux target it is today. 0 = the frozen kAttachMinLayers. The aux append
    //     de-duplicates itself against the widened stage-A list (see ChainAttachSelectAux).
    // SHIPPED (jet round 3, arm T4C1): the 4-layer IP gate bar is relaxed by up to 2 units, ramped
    // over one decade of local junction density starting at 30. Below rho0 the relaxation is
    // IDENTICALLY zero, so sparse regions take the frozen bar bit for bit -- which is why both cube
    // guns are bit-identical under this arm and why the sparse-region invariance is a property of
    // the code rather than of a measurement. The unconditioned form (rho0 = 0, decades = 0) was
    // measured and REJECTED: it buys more displaced efficiency everywhere (PU200 dxy[1,5) +.0130 vs
    // +.0057, cube50_highPt overall +.0345 with zero losses) but takes PU200 fake from .04402 to
    // .04619, i.e. from cleaner than LST master (.0454) to worse than it.
    float t4DensDelta = 2.f;
    float t4DensRho0 = 30.f;
    float t4DensDecades = 1.f;
    float t4GateTighten = 0.f;
    int t4EmitMinLayers = 0;
    int t4AttachMinLayers = 0;

    // ------------------------------------------------------------------------------------------
    // JET ROUND (M1) -- the per-shared-key incidence degree cap. NOT a physics working point.
    // ------------------------------------------------------------------------------------------
    // The edge families are the full cross product at each shared key, so the enumerated count is
    // exactly sum_key degIn(key) * degOut(key). On a collimated (jet) event the number of distinct
    // shared MDs SATURATES near 1000 while the triplet count grows unbounded, so the mean per-key
    // degree goes from PU200's 2.08 to 62-272 and that ratio SQUARES into the edge count: measured
    // 5500x more edges than PU200 at the tail, 8.4 GB of edge rows on one event of 1000, and past
    // two hard allocation ceilings (FINDINGS_JET.md). Meanwhile the weld consumes at most
    // kChainWeldSweeps edges per node-slot and only 0.018% of a jet event's edges are ever welded.
    //
    // So a key keeps at most this many in- and out-triplets: the enumerated count at that key
    // becomes min(degIn, C) * min(degOut, C). Measured cost on PU200: 0.074% of E1 pooled over 150
    // events at C = 256 (max per-key degree ever seen on PU200 is 554, on jets 2303), against 3.0x
    // fewer edges pooled on jets and 6.0x on the big events.
    //
    // kChainDegreeCapOff reproduces today's behaviour BIT FOR BIT rather than approximately: the
    // implementation is a min() against this value and min(deg, 1e9) == deg for every reachable
    // degree, so the capped and the uncapped path are the same arithmetic. That is what makes the
    // cap-off bit-identity gate a real test of the plumbing.
    //
    // WHICH triplets a key keeps is CSR arrival order (the first C to land), which on a host backend
    // is ascending node index and on a device backend is atomicAdd race order. A deterministic or
    // score-ranked keep-rule needs a per-slice ranking (O(sum_key deg^2), i.e. the very cost this
    // removes) or a sort with two nT3-sized buffers; the cheap high-quality selection is a per-NODE
    // top-C at enumeration time, which is a separate change.
    //
    // NOTE the two things this deliberately does NOT touch: the CSR offsets stay UNCAPPED (so the
    // K1c fill and its bounds are unchanged), and so do the degIn / degOut values that reach the
    // edge head (features 12/13) and the chain head (ChainGate's shared-key degrees) -- those are
    // trained inputs, and capping them would move PU200 scores for no memory saving at all.
    uint32_t degreeCap = 256u;

    // True iff any band delta is live; reproduces the reference's `zOn` short-circuit exactly.
    constexpr bool etaBandActive() const {
      return zEta2 > zEta1 && (zdRI != 0.f || zdR != 0.f || zdR5 != 0.f || zdR6 != 0.f || zdM4 != 0.f || zdM4D != 0.f ||
                               zdCP != 0.f || zdCD != 0.f);
    }
  };

  // ------------------------------------------------------------------------------------------
  // JET ROUND 2 (D). Env overrides of the BARE-SEED RETIREMENT bars, and nothing else.
  //
  // Why only these five fields: the (delivered chain TC, un-retired bare pLS row) pair is 87% of
  // the PU200 duplicate excess of the gate-retrain arm and 97% of its jet-core duplicate excess
  // (d2_ref/anat_*), and the only evidence any of the three retirement channels has is the attach
  // head's pair logit against these bars. The knobs move the bars DOWN by a delta so one binary
  // supplies a whole scan; every default is untouched, so an unset environment is bit-identical to
  // the shipped configuration (proven, not asserted: the zero-delta arm is the control).
  //
  //   LST_D_RPS_DELTA   subtract from rpsThetaChain  (-RPSA, the chain-side retirement bar)
  //   LST_D_XC_DELTA    subtract from xcTheta / xcThetaT / xcThetaE together (-XC, all three bands)
  //   LST_D_T3_DELTA    subtract from attachThetaT3  (-AT3, the T3-side retirement bar)
  //
  // A delta rather than three absolute values because the bands were fitted at a common fixed
  // acceptance: moving them together keeps that structure, and a per-band scan is a later refinement
  // (which is why the deltas are separate knobs and not one).
  inline void chainConfigRetirementEnv(ChainConfig& cfg) {
    auto readf = [](char const* name) {
      char const* s = std::getenv(name);
      if (s == nullptr || *s == '\0')
        return 0.f;
      return static_cast<float>(std::atof(s));
    };
    float const dRps = readf("LST_D_RPS_DELTA");
    float const dXc = readf("LST_D_XC_DELTA");
    float const dT3 = readf("LST_D_T3_DELTA");
    char const* xl = std::getenv("LST_D_XCLO_DELTA");
    if (xl != nullptr && *xl != '\0') {
      cfg.dupXcDelta = static_cast<float>(std::atof(xl));
      char const* pm = std::getenv("LST_D_XCLO_PTMAX");
      if (pm != nullptr && *pm != '\0')
        cfg.dupXcPtMax = static_cast<float>(std::atof(pm));
      std::printf("[chainenv] dupXcDelta: OFF -> %g (ramped, |eta| >= 1.1, pt < %g)\n",
                  cfg.dupXcDelta,
                  cfg.dupXcPtMax);
    }
    char const* mu = std::getenv("LST_D_MUTUAL_DELTA");
    if (mu != nullptr && *mu != '\0') {
      cfg.dupMutualDelta = static_cast<float>(std::atof(mu));
      std::printf("[chainenv] dupMutualDelta: OFF -> %g\n", cfg.dupMutualDelta);
    }
    if (dRps != 0.f) {
      cfg.rpsThetaChain -= dRps;
      std::printf("[chainenv] rpsThetaChain: -%g -> %g\n", dRps, cfg.rpsThetaChain);
    }
    if (dXc != 0.f) {
      cfg.xcTheta -= dXc;
      cfg.xcThetaT -= dXc;
      cfg.xcThetaE -= dXc;
      std::printf("[chainenv] xcTheta/T/E: -%g -> %g %g %g\n", dXc, cfg.xcTheta, cfg.xcThetaT, cfg.xcThetaE);
    }
    if (dT3 != 0.f) {
      cfg.attachThetaT3 -= dT3;
      std::printf("[chainenv] attachThetaT3: -%g -> %g\n", dT3, cfg.attachThetaT3);
    }
    std::printf("[chainenv] D retirement resolved: rpsThetaChain=%g xcTheta=%g/%g/%g attachThetaT3=%g"
                " dupMutualDelta=%g\n",
                cfg.rpsThetaChain,
                cfg.xcTheta,
                cfg.xcThetaT,
                cfg.xcThetaE,
                cfg.attachThetaT3,
                cfg.dupMutualDelta);
  }

  // COORDINATOR PROBE (not a shipped knob): env override of the TERMINAL TRIM group, so the
  // trim can be A/B'd from ONE binary. With nothing set this writes nothing and prints nothing.
  //   LST_TRIM_ON     0 disables ChainTrimTerminals entirely
  //   LST_TRIM_ABS    the absolute chi2 floor below which a chain is never trimmed
  inline void chainConfigTrimEnv(ChainConfig& cfg) {
    bool any = false;
    if (char const* s = std::getenv("LST_TRIM_ON")) {
      if (*s != '\0') {
        cfg.terminalTrim = (std::atoi(s) != 0);
        any = true;
      }
    }
    if (char const* s = std::getenv("LST_TRIM_ABS")) {
      if (*s != '\0') {
        cfg.trimAbsChi2 = static_cast<float>(std::atof(s));
        any = true;
      }
    }
    if (char const* s = std::getenv("LST_TRIM_MODE")) {
      if (*s != '\0') {
        cfg.trimMode = std::atoi(s);
        any = true;
      }
    }
    if (char const* s = std::getenv("LST_TRIM_GAP")) {
      if (*s != '\0') {
        cfg.trimMarginGap = static_cast<float>(std::atof(s));
        any = true;
      }
    }
    if (any)
      std::printf("[chainenv] terminal trim resolved: on=%d absChi2=%g mode=%d gap=%g\n",
                  static_cast<int>(cfg.terminalTrim),
                  cfg.trimAbsChi2,
                  cfg.trimMode,
                  cfg.trimMarginGap);
  }

  // JET ROUND 3 (T4): env overrides of the 4-layer class policy group. One binary supplies every
  // arm of the A/B, which is the only way to prove inertness by BIT-IDENTITY rather than assert it.
  // With nothing set this function writes nothing and prints nothing.
  //
  //   LST_T4_DENS_DELTA    lower the branch-0 (4-layer IP) gate bar by up to this much, ramped in
  //                        local graph occupancy (0 = off, the shipped value)
  //   LST_T4_DENS_RHO0     occupancy at which the ramp STARTS (below it: bit-identical to ship)
  //   LST_T4_DENS_DECADES  ramp width in decades of log10(1 + occupancy)
  //   LST_T4_GATE_TIGHTEN  add to the 4-layer IP bar and the non-far exempt bar (suppression arm)
  //   LST_T4_EMIT_MIN      override kChainTCMinLayers at the K10 emission flag
  //   LST_T4_ATTACH_MIN    override kAttachMinLayers in the stage-A attach target filter
  inline void chainConfigT4Env(ChainConfig& cfg) {
    auto rdf = [](char const* name, float* dst) {
      char const* s = std::getenv(name);
      if (s == nullptr || *s == '\0')
        return false;
      *dst = static_cast<float>(std::atof(s));
      return true;
    };
    auto rdi = [](char const* name, int* dst) {
      char const* s = std::getenv(name);
      if (s == nullptr || *s == '\0')
        return false;
      *dst = std::atoi(s);
      return true;
    };
    bool any = false;
    any |= rdf("LST_T4_DENS_DELTA", &cfg.t4DensDelta);
    any |= rdf("LST_T4_DENS_RHO0", &cfg.t4DensRho0);
    any |= rdf("LST_T4_DENS_DECADES", &cfg.t4DensDecades);
    any |= rdf("LST_T4_GATE_TIGHTEN", &cfg.t4GateTighten);
    any |= rdi("LST_T4_EMIT_MIN", &cfg.t4EmitMinLayers);
    any |= rdi("LST_T4_ATTACH_MIN", &cfg.t4AttachMinLayers);
    if (any)
      std::printf("[chainenv] T4 class policy resolved: densDelta=%g rho0=%g decades=%g"
                  " gateTighten=%g emitMin=%d attachMin=%d\n",
                  cfg.t4DensDelta,
                  cfg.t4DensRho0,
                  cfg.t4DensDecades,
                  cfg.t4GateTighten,
                  cfg.t4EmitMinLayers,
                  cfg.t4AttachMinLayers);
  }

  // The value of ChainConfig::degreeCap that means "no cap". It is not a sentinel the code tests
  // for: it is simply larger than any degree a min() can ever see (a degree is bounded by the
  // triplet count, and 2^32 / 21 B = 204 M edge rows is the hard allocation wall long before that),
  // so the capped expressions collapse to the uncapped ones with no branch.
  static constexpr uint32_t kChainDegreeCapOff = 1000000000u;

  // prototype/Stages.h kWeldSweeps was 3. WELD ROUND (L4): this is not a completeness knob, it is
  // where a TRUNCATION lands. K6a lets an edge compete only while BOTH of its slots are free
  // (ChainWeld.h), so a node's argmax cannot advance past its own rank-1 edge until some OTHER weld
  // removes the blocker; K6a/K6b is therefore an iterated mutual-best whose fixed point is the
  // greedy matching by weld key, and on a jet core it needs 12-20 sweeps to get there (measured:
  // 32.8% of the slots a true core edge loses are still EMPTY after 3 sweeps, 0.3% after 20).
  // Running it to that fixed point is monotonically HARMFUL, because the marginal weld's quality
  // collapses: the measured same-sim purity of the welds a sweep produces is .55 (sweep 1), .31
  // (sweep 2), .12 (sweep 3), .05 (sweep 4) on jet cores. Deployed on 500 jet tune events, 16
  // sweeps costs .0164 of core efficiency and adds .0108 of fake, while dropping the THIRD sweep is
  // a Pareto move: core efficiency .7659 -> .7695 (p = 9.9e-04, paired McNemar), dR < .02
  // .5074 -> .5169, fake .1317 -> .1271, all eight dR aggregates >= 0, and PU200's seven displaced
  // bands all neutral-or-better. It needs no density conditioning because it is self-limiting:
  // where the iteration had already converged the third sweep had nothing to do (PU200 overall
  // efficiency +.0002, cube50 exactly .00000 on every band). Caveat that rides with the constant:
  // jet-core (dR < .05) duplicate goes .0313 -> .0345 (p = .017), in a cell we already owe master.
  static constexpr int kChainWeldSweeps = 2;
  // JET ROUND 5 (W5). THE WELD ARGMAX'S DENSITY-CONDITIONED FAMILY ORDER.
  //
  // K6a puts both adjacency families into ONE atomicMax on `logOdds`, but the head's eligibility
  // bar is fitted PER FAMILY and per (pT,|eta|) cell (the 80 weld WPs) at equal per-cell SIGNAL
  // efficiency -- so it admits 92-97% of E2 rows against 23-45% of E1 rows, and the two eligible
  // pools it hands the argmax are NOT of equal purity. E2 (shared line segment) therefore wins
  // slots on a scale it was never calibrated against E1 (shared middle MD) on.
  //
  // The variable that actually carries the miscalibration is LOCAL OCCUPANCY, not family. Measured
  // on 7.1M PU200 and 347M jets eligible edge rows, the same-sim rate of an eligible edge falls
  // from .84 at junction degree product 1 to .0001 above 16384, and INSIDE every occupancy decade
  // of both samples E1 is the better family, LR(E2/E1) = .72 -> .013, never above 1. The pooled
  // "E2 is 6.8x more likely" is a composition artefact: 99.2% of jets' eligible E1 rows sit in the
  // >=16384 bin. Ordering E2 first EVERYWHERE therefore demotes E1 exactly where E1 is
  // unambiguous, which is the sparse regime a displaced track lives in -- and that is what cost
  // PU200 `dxy[1,5)` -.021 and `vxy[10,30)` -.021 when the family order was applied globally.
  //
  // So the family term is admitted ONLY at junctions with at least this many competing T3 pairs.
  // The junction's incidence degree product is ChainGate feature 14's per-edge summand, read from
  // the same two CSRs (ChainGate.h:393-412), so the two agree by construction. 0 disables the
  // family term entirely and reproduces the shipped weld bit for bit.
  //
  // Deployed at 128 (jets TUNE 500 events, PU200 event_1000 1000 events, paired McNemar):
  // jet core .8197 -> .8413 (+.0216, p 1.1e-30), deep core [0,.0025) +.0591, ALL FIFTEEN fine dR
  // bins >= 0, jets fake -.0002 (n.s.) and jets duplicate -.0008; PU200 has NO resolved cell in
  // either direction -- worst is `vxy[10,30)` -.0007 (4 lost / 1 gained of 4178, p .375), overall
  // -.00005, and the whole PU200 TC count moves by 24 of 1,582,757. The knee is NOT a softening:
  // at 8 the arm is WORSE on PU200 than at 0, because the weld is a global matching and a partial
  // family term produces a different matching, not a partial one.
  static constexpr long long kChainWeldFamilyDegKnee = 128;
  // prototype/K9K10.cc k10AssembleChainTCs: a chain shorter than this emits no TC.
  static constexpr int kChainTCMinLayers = 4;
  // -H 1 is FROZEN: the claim universe is hit rows, not MiniDoublets.
  static constexpr bool kChainHitLevelClaim = true;
  // prototype/Extend.cc kMaxLayer: md_layer is 1-6 barrel, 7-11 endcap.
  static constexpr int kChainMaxMdLayer = 12;
  // Cycle guard for the weld path walk. Edges point strictly inward -> outward so a chain cannot
  // exceed the detector layer count; this is a device-safe stand-in for the reference's `visited`
  // array and has never been reached.
  static constexpr uint32_t kChainMaxNodes = 64;

  // TrackCandidates allocation headroom for the stage-B pT3-class deliveries (~120/event measured
  // at PU200; the emission sweep guards the bound and counts any overflow).
  static constexpr unsigned int kChainBareT3TCHeadroom = 4096u;

  // prototype/PixelAttach.h kAttachFeat: the frozen pair-feature contract.
  static constexpr int kAttachFeatures = 22;
  // prototype/PixelAttach.cc: only chain targets with at least this many layers bid for a pLS
  // (v1 scope decision -- chain + pLS is a pT5-class object).
  static constexpr int kAttachMinLayers = 5;

  // ---- K8a grid geometry (see the derivation block at the top of src/alpaka/ChainAttach.h) -----
  // The three axes are keyed on the quantities the two frozen windows are written in:
  //   r    the TARGET's innermost anchor radius, the radius the pLS helix is propagated to
  //   tanL the tanLambda difference axis, cell width == attachPrefDTanL
  //   phi  the propagated-direction axis,   cell width chosen against attachPrefDPhi
  // The r axis uses fixed bins ONLY as a bucketing device: the phi interval a pLS occupies in bin
  // j is computed against the MEASURED [min, max] innermost radius of the targets that landed in
  // bin j, so the bin edges never enter the superset argument.
  static constexpr int kAttachRBins = 9;  // 8 x 16 cm over [0, 128) plus one overflow bin
  static constexpr float kAttachRBinWidth = 16.f;
  static constexpr int kAttachTanLBins = 100;  // 0.6-wide, covering [-30, 30]
  static constexpr float kAttachTanLLo = -30.f;
  static constexpr int kAttachPhiBins = 16;  // 2 pi / 16 = 0.3927 rad
  // Absolute pad added to every grid phi interval. It covers the fast-atan2 approximation used in
  // the grid build (measured max error < 3e-6 rad) plus float rounding, with >4 decades of margin.
  static constexpr float kAttachPhiPad = 1e-3f;

}  // namespace lst

#endif
