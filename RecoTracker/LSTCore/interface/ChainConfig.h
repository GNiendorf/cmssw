#ifndef RecoTracker_LSTCore_interface_ChainConfig_h
#define RecoTracker_LSTCore_interface_ChainConfig_h

#include <cstdint>

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
    float m3Theta4 = 2.352515f; // -M4  : T4-class IP    kill iff mX < m3Theta4
    float m3Theta4D = -2.255362f; // -M4D : T4-class exempt kill iff mD < m3Theta4D
    float m3Theta5 = 1e9f;    // -M5  : IP nLayers == 5 kill iff mP < m3Theta5 (inert at 1e9)
    float m3Theta6 = 1e9f;    // -M6  : IP nLayers >= 6 kill iff mP < m3Theta6 (inert at 1e9)
    float m3ThetaD = 1e9f;    // -MD  : exempt 5+       kill iff mD < m3ThetaD (inert at 1e9)
    float m3ThetaRI = -0.2518739f; // -MRI : IP-5+     OR-rescue floor on mX
    float m3ThetaR = -1.263387f; // -MR  : exempt-5+ OR-rescue floor on mX (endcap + no-member fallback)
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
    float m3ThetaRB = -1.108733f;
    float m3ThetaRT = -0.8797723f;
    // -C25 0.0 / -C25D -2.0 : the (nNodes == 2, nLayers == 5) cell rule; kills only when BOTH
    // margins fail, and never re-kills an already-killed chain.
    float c25Theta = 1.999641f;
    float c25ThetaD = -0.9891577f;

    // Transition-band levers. The band is keyed on |eta| of the chain's INNERMOST member T3
    // (the same quantity K10 gives the TC), and the deltas are ADDITIVE to the thresholds above.
    float zEta1 = 1.1f;      // -ZE1
    float zEta2 = 1.7f;      // -ZE2
    float zdM4 = -0.4565438f; // -ZM4  : added to -M4
    float zdM4D = 1.451099f; // -ZM4D : added to -M4D
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
    float attachTheta = 6.626049f;   // -a   delivery margin, |eta| < 1.1
    float attachThetaT = 5.753662f;  // -a2  delivery margin, 1.1 <= |eta| < 1.7
    float attachThetaE = 5.907102f;  // -a3  delivery margin, |eta| >= 1.7
    // -AT3 6.0: the bare-T3 (stage B) delivery margin, GLOBAL -- no eta bands. Also the T3-side
    // retirement bar of the -RPS predicate (-RPST was measured as a dead end and is deleted; the
    // bar is hardcoded to this value).
    float attachThetaT3 = 5.515511f;  // S3: re-fitted for the 22-input head
    // -RPSA 5.5: the chain-side RETIREMENT bar of the -RPS predicate. Global, deliberately NOT
    // banded (banded -RPSA measured dominated). The retirement kernels must read THIS, never
    // attachTheta -- reusing the delivery margin is the pre-A11 behaviour and is wrong now that
    // delivery is banded.
    float rpsThetaChain = 5.480793f;  // S3: re-fitted for the 22-input head
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
    float xcTheta = 3.430463f;   // |seed eta| < 1.1
    float xcThetaT = 2.82865f;   // 1.1 <= |seed eta| < 1.7
    float xcThetaE = 3.641699f;  // |seed eta| >= 1.7
    // -CC9 2: the T4-class crossclean against the delivered SEEDED rows -- drop a delivered type-9
    // bare chain sharing this many outer-tracker hits (2 = one full mini-doublet) with a delivered
    // type-7 or type-5 row. 0 disables it. See ChainCrossClean.h; measured fake barrel -.0037 with
    // efficiency unchanged to 6 decimals and an exact cost of two displaced sims, neither in the
    // efficiency denominator (977 evt). type-9 rows never share 3+, so the value is effectively
    // binary.
    int cc9MinShared = 1;
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

    // True iff any band delta is live; reproduces the reference's `zOn` short-circuit exactly.
    constexpr bool etaBandActive() const {
      return zEta2 > zEta1 && (zdRI != 0.f || zdR != 0.f || zdR5 != 0.f || zdR6 != 0.f || zdM4 != 0.f || zdM4D != 0.f ||
                               zdCP != 0.f || zdCD != 0.f);
    }
  };

  // prototype/Stages.h kWeldSweeps.
  static constexpr int kChainWeldSweeps = 3;
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
