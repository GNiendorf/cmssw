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
    float t4ExemptDcaMin = 0.f;

    // -G 6 three-class margin kills. mP = zPrompt - zFake, mD = zDisp - zFake,
    // mX = max(zPrompt, zDisp) - zFake.
    float m3Theta4 = 4.f;     // -M4  : T4-class IP    kill iff mX < m3Theta4
    float m3Theta4D = -1.2f;  // -M4D : T4-class exempt kill iff mD < m3Theta4D
    float m3Theta5 = 1e9f;    // -M5  : IP nLayers == 5 kill iff mP < m3Theta5 (inert at 1e9)
    float m3Theta6 = 1e9f;    // -M6  : IP nLayers >= 6 kill iff mP < m3Theta6 (inert at 1e9)
    float m3ThetaD = 1e9f;    // -MD  : exempt 5+       kill iff mD < m3ThetaD (inert at 1e9)
    float m3ThetaRI = -0.5f;  // -MRI : IP-5+     OR-rescue floor on mX
    float m3ThetaR = -1.8f;   // -MR  : exempt-5+ OR-rescue floor on mX (endcap + no-member fallback)
    // -MRB / -MRT: band split of the exempt-5+ OR-rescue floor. Band on |eta| of the innermost
    // member T3 (the K10 TC eta), boundaries zEta1 / zEta2. Winner: barrel and transition
    // tightened to -1.2, endcap stays at the global -MR. Resolved values only -- the prototype's
    // kMrUnset sentinel is resolved on the host and never reaches a kernel.
    float m3ThetaRB = -1.2f;
    float m3ThetaRT = -1.2f;
    // -C25 0.0 / -C25D -2.0 : the (nNodes == 2, nLayers == 5) cell rule; kills only when BOTH
    // margins fail, and never re-kills an already-killed chain.
    float c25Theta = 0.f;
    float c25ThetaD = -2.f;

    // Transition-band levers. The band is keyed on |eta| of the chain's INNERMOST member T3
    // (the same quantity K10 gives the TC), and the deltas are ADDITIVE to the thresholds above.
    float zEta1 = 1.1f;      // -ZE1
    float zEta2 = 1.7f;      // -ZE2
    float zdM4 = -0.5f;      // -ZM4  : added to -M4
    float zdM4D = 1.2f;      // -ZM4D : added to -M4D
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
    bool claimCountExclusive = false;

    // -W 0.50: owner-relative braid kill. -WE 0.20 / -WZ 1.5 / -WN off: the BAND-AWARE braid --
    // candidates with |eta(innermost T3)| >= braidAltEta and nNodes <= braidAltMaxNodes use the
    // tight fraction. -FB / -FBC 0: the same band's own claim tolerance (claimItemsAltMDs == -2
    // and claimFracAlt <= 0 mean "band uses the global value").
    float braidFrac = 0.5f;
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
    // -a 5.0 / -a2 5.0 / -a3 6.0 (CHAINFINAL2): the pair-head logit an (accepted chain, pLS) pair
    // must reach to attach, banded on |eta| of the SEED (pLS) at the literal 1.1 / 1.7 boundaries.
    // The head's logit calibration shifts ~4.6 units across eta, so one global margin would be a
    // different working point per band. All three are RESOLVED values (no follow-the-barrel
    // sentinel survives the port). plsBestChainLogit stays UNBANDED: it is recorded for every
    // scored pair BEFORE the threshold (invariant I4).
    float attachTheta = 5.0f;   // -a   delivery margin, |eta| < 1.1
    float attachThetaT = 5.0f;  // -a2  delivery margin, 1.1 <= |eta| < 1.7
    float attachThetaE = 6.0f;  // -a3  delivery margin, |eta| >= 1.7
    // -AT3 6.0: the bare-T3 (stage B) delivery margin, GLOBAL -- no eta bands. Also the T3-side
    // retirement bar of the -RPS predicate (-RPST was measured as a dead end and is deleted; the
    // bar is hardcoded to this value).
    float attachThetaT3 = 6.f;
    // -RPSA 5.5: the chain-side RETIREMENT bar of the -RPS predicate. Global, deliberately NOT
    // banded (banded -RPSA measured dominated). The retirement kernels must read THIS, never
    // attachTheta -- reusing the delivery margin is the pre-A11 behaviour and is wrong now that
    // delivery is banded.
    float rpsThetaChain = 5.5f;
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
    // -CCS 6.0 / -CCS2 5.0 / -CCS3 unset: chain-loser suppression of an accepted BARE chain whose
    // best scored pair toward a pLS owned by a DIFFERENT chain reaches the band bar. Band on the
    // emitted TC |eta| (the innermost member T3) at the literal 1.1 / 1.7 boundaries.
    // SENTINEL SEMANTICS DIFFER from the -a family: >= 1e8 means OFF FOR THAT BAND, with NO
    // follow-the-barrel fallback -- the endcap dup rate is already below LST and must not move by
    // accident. Do not "resolve" these.
    float ccsTheta = 6.0f;
    float ccsThetaT = 5.0f;
    float ccsThetaE = 1e9f;
    // -XC 3 -XCT 3.75 -XCT2 3.5 -XCT3 3.75 -XCR2 1e-6 -XCW2 0.02 -XC4 1: the ported CrossCleanpLS.
    // Pixel-anchored arm: retire a bare quad seed sharing >= 1 pixel hit row with, or within
    // dR^2 < xcDR2Pix of, the seed of any delivery (LST's own pT5/pT3 arms, windows verbatim).
    // Bare-chain arm: retire a bare quad seed within dR^2 < xcDR2Chain of a delivered SEEDLESS
    // chain TC whose attach-head logit for that exact (seed, chain) pair reaches the |seed eta|-
    // banded xcTheta -- the substitution for LST's deleted pLS/T5 embedding test. -XC4 is folded
    // in unconditionally (4-layer accepted chains join the scored-pair stream, score-only);
    // -XC4T / -XCD / -XCG 1 are dead ends and are not ported.
    float xcTheta = 3.75f;     // |seed eta| < 1.1
    float xcThetaT = 3.5f;     // 1.1 <= |seed eta| < 1.7
    float xcThetaE = 3.75f;    // |seed eta| >= 1.7
    float xcDR2Pix = 1e-6f;    // LST TrackCandidate.h pT5/pT3 arm window, verbatim
    float xcDR2Chain = 0.02f;  // LST TrackCandidate.h T5 arm window, verbatim
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
  static constexpr int kAttachFeatures = 19;
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
