#ifndef RecoTracker_LSTCore_interface_ChainConfig_h
#define RecoTracker_LSTCore_interface_ChainConfig_h

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace lst {

  // Configuration for the chain-tracking pipeline, which replaces LST's post-triplet
  // reconstruction (T4/T5/pT3/pT5 building) with a graph over triplets: edges between triplets
  // that share detector elements, a weld that links them into chains, a gate that judges each
  // chain, and a claim that resolves chains competing for the same hits.
  //
  // This is a plain struct with defaults rather than bare constexpr so that LSTProducer can fill
  // it from its parameter set without a kernel change. It is passed to the kernels by value.
  struct ChainConfig {
    // Weld eligibility on the edge logit: an edge is a weld candidate only at or above this bar.
    float thetaEdge = 0.f;
    // Per-edge-family weld eligibility. The two edge families produce different chain lengths --
    // an E1 edge (shared mini-doublet) always yields a 5-layer chain, an E2 edge (shared line
    // segment) always yields a 4-layer one -- and the gate downstream already judges those two
    // classes with separate bars. A single shared bar couples them, and not neutrally: the
    // families sit on very different logit scales (E1 median -3.79, 19.95% at or above 0; E2
    // median +0.22, 53.49%), so a global loosening admits roughly four times more E1 than E2.
    // Because the weld argmax runs over all incident eligible edges regardless of type, with one
    // out-slot and one in-slot per node, those extra E1 edges take the slots the E2 pairs wanted
    // and loosening E1 alone destroys 4-layer chains. Splitting the bar keeps the weld consistent
    // with the gate.
    //
    // A value >= 1e29 means "inherit thetaEdge for this family", resolved on the host, so leaving
    // both at the sentinel reproduces single-bar behaviour exactly.
    float thetaEdgeE1 = 1e30f;
    float thetaEdgeE2 = -2.0f;
    // Take the weld bar from a table instead of the two scalars above. The table is indexed on the
    // T5-DNN working-point binning (kPtBins x kEtaBins, separately per edge family) and keyed on
    // the inner node's cell; it ships alongside the edge head in EdgeNetworkWeights.h. It is part
    // of the head's calibration: each cell reproduces that head's measured true-edge acceptance,
    // so retraining the head leaves the true-edge rate fixed by construction and moves only the
    // false-edge rate. False falls back to the per-family scalars byte for byte.
    bool edgeWpTable = true;
    // Weight on chain length in the chain score.
    float lambdaLen = 3.f;

    // Terminal trim: drop an end node of a chain when doing so yields a more credible chain.
    bool terminalTrim = true;
    // Chi2/hit improvement ratio a drop must beat when the chi2 rule takes the decision.
    float trimFactor = 1.2f;
    // Concentrating guard: a chain whose combined fit is already this good is never trimmed, on
    // the grounds that a chain that fits well has no parasitic arm to remove.
    float trimAbsChi2 = 1.f;
    // Layers the shortened chain must still have; see trimMode for which rules enforce it.
    int trimMinLayersAfter = 5;
    // How many times the trim runs; each pass can drop at most one terminal node per chain.
    int trimPasses = 1;
    // Which rule takes the terminal-trim decision. The candidate variants (full chain, inner
    // terminal dropped, outer terminal dropped) are constructed the same way in every mode; this
    // selects only who chooses between them.
    //   0 the fit chi2 improvement ratio
    //   1 the chain head's argmax over the variant margin mX, unguarded
    //   2 as 1, but a dropped variant must still keep trimMinLayersAfter layers
    //   3 as 1, but only on chains the concentrating guard admits (chi2Full > trimAbsChi2)
    //   4 the unguarded 3-way argmax of mode 1 decided by the chi2 proxy rather than by the head;
    //     a control that separates "the head chooses better" from "trimming more is better"
    //   5 as 1, but the winning variant must beat the full chain by trimMarginGap logit units
    //
    // Mode 5 is the default: the decision belongs to the chain head rather than to a chi2
    // threshold, but the head must be decisive rather than merely ahead. Trimming on a bare argmax
    // (mode 1) buys more jet-core efficiency at the cost of 46% more outer-tracker hits for 20%
    // more realness -- the marginal trims shorten tracks without buying anything.
    int trimMode = 5;
    // How decisively the head must prefer a drop, in logit units. At 1.0 the shortened chain must
    // be e^1 = 2.7x more favourable in real-vs-fake odds before a terminal node is dropped.
    float trimMarginGap = 1.f;

    // IP-compatibility boundary on the chain dcaXY, cm: below this a chain is treated as prompt.
    float dcaSplit = 0.5f;
    // Extra dca floor for the T4-class exempt branch (exempt iff dca >= max(dcaSplit, this)).
    float t4ExemptDcaMin = 2.0f;
    // Second breakpoint on the same reconstructed-dcaXY axis, applied to the T4-class exempt
    // branch only. The exempt bar m3Theta4D is a single number covering every chain with
    // dcaXY >= dcaSplit, and that population is overwhelmingly concentrated just above the
    // boundary -- so the bar a 20 cm displaced chain must clear is one fitted on 0.5-2 cm objects.
    // Above dcaSplit2 the chain gets its own bar and the eta-band delta is not applied.
    //
    // That far cell additionally requires a good fit (maxXyResid), so it selects "displaced and
    // well measured" rather than merely "badly fitted": far-displaced true chains sit at
    // maxXyResid p50 0.015 against p50 0.245 for fakes. dcaSplit2 = 1e9 leaves the cell empty and
    // t4FarMaxResid = 1e9 disables the guard. Both are per-chain reconstructed quantities.
    float dcaSplit2 = 12.f;
    float m3Theta4D2 = -1e9f;
    float t4FarMaxResid = 0.02f;

    // Gate bars. The chain head is 3-class (fake / prompt / displaced) and every bar below is on a
    // logit MARGIN, never on a probability:
    //   mP = zPrompt - zFake, mD = zDisp - zFake, mX = max(zPrompt, zDisp) - zFake.
    // A chain that fails its bar is killed by subtracting gateKill from its score. Which bar
    // applies is decided by the (nLayers, dcaXY) branch in ChainGate.h. The values are fitted
    // working points with no closed form.
    float m3Theta4 = 2.256949f;    // 4-layer, IP branch:     kill iff mX < this
    float m3Theta4D = -2.381371f;  // 4-layer, exempt branch: kill iff mD < this
    // The 5+ branches kill only when BOTH the per-class bar and the mX rescue floor fail, so a
    // chain that is unambiguously real on one class survives whatever the other class says. 1e9
    // disables the per-class half, leaving the rescue floor as the entire rule -- which is how all
    // three are configured, so the floors are the numbers that were actually fitted.
    float m3Theta5 = 1e9f;          // IP, nLayers == 5: per-class bar on mP
    float m3Theta6 = 1e9f;          // IP, nLayers >= 6: per-class bar on mP
    float m3ThetaD = 1e9f;          // exempt 5+:        per-class bar on mD
    float m3ThetaRI = -0.4409661f;  // IP 5+ rescue floor on mX
    float m3ThetaR = -1.44432f;     // exempt 5+ rescue floor on mX, outside both eta bands
    // Band split of the exempt-5+ rescue floor on |eta| of the innermost member T3: m3ThetaRB in
    // the barrel (|eta| < zEta1), m3ThetaRT in the transition band [zEta1, zEta2), m3ThetaR
    // elsewhere and for a chain with no member T3. Raising either band bar buys fake rate at the
    // cost of displaced efficiency, because the exempt (large-dcaXY) branch is the branch
    // displaced tracks come through: it is the expensive place to tighten.
    float m3ThetaRB = -1.214288f;
    float m3ThetaRT = -1.011139f;
    // Extra bars for the (nNodes == 2, nLayers == 5) cell, applied on top of whichever branch rule
    // already ran. Kills only when BOTH margins fail, and never re-kills an already-killed chain.
    float c25Theta = 2.268653f;
    float c25ThetaD = -1.208447f;

    // Transition-band levers: the deltas are ADDITIVE to the bars above, and reach only chains
    // whose innermost member T3 has |eta| in [zEta1, zEta2). A delta of 0 is a no-op, and the gate
    // skips the whole band computation when every delta is 0 (etaBandActive below).
    float zEta1 = 1.1f;
    float zEta2 = 1.7f;
    float zdM4 = -0.5098293f;  // added to m3Theta4
    float zdM4D = 1.473291f;   // added to m3Theta4D
    float zdRI = 0.f;          // added to m3ThetaRI
    float zdR = 0.f;           // added to the exempt-5+ rescue floor
    float zdR5 = 0.f;          // added to zdR for exempt nLayers == 5
    float zdR6 = 0.f;          // added to zdR for exempt nLayers >= 6
    float zdCP = 0.f;          // added to c25Theta
    float zdCD = 0.f;          // added to c25ThetaD
    bool zInLayer1 = false;    // restrict every band lever to chains starting in layer 1

    // Score subtraction that marks a killed chain. It only has to be large enough that a killed
    // chain can never outrank a live one downstream; "still alive" is tested as
    // score > -0.5 * gateKill.
    float gateKill = 1e9f;

    // Hit-claim arbitration.
    // Base per-length acceptance threshold. The gate is a KILL rather than a threshold, so this
    // only has to pass every live chain while still rejecting a killed one; any value between the
    // two does.
    float noCutTheta = -1e5f;
    // Per-length acceptance thresholds of the EXEMPT (large-dcaXY) branch, which replace
    // noCutTheta for those chains. They live on chains.score -- the summed edge-logit scale, not
    // the margin scale -- which is why 0 and not noCutTheta is the pass-everything value here.
    float thetaExempt4 = 0.f;
    float thetaExempt5 = 0.f;
    float thetaExempt6 = 0.f;

    // Best-first ORDER key of the greedy claim walk. It is a RANK, never a threshold:
    //   orderKey = score - alphaEff * max(0, orderHinge - marginX)
    // that is, a chain is pushed down the queue in proportion to how far its realness margin falls
    // short of orderHinge, and not at all once it clears it.
    float orderAlpha = 10.f;
    float orderHinge = 5.f;
    // Eta-conditioned weight on that same hinge, on |eta| of the innermost member T3 -- the band
    // the braid tolerances below already compute, so this costs no new column and no new kernel:
    //   alphaEff = orderAlpha + (orderAlphaCentral - orderAlpha) * (1 - ramp(|eta|))
    //   ramp(x)  = clip((x - orderEtaRampLo) / (orderEtaRampHi - orderEtaRampLo), 0, 1)
    //
    // Why eta and nothing else: the key is a GLOBAL total order, so conditioning its weight on a
    // per-chain quantity scrambles comparisons between chains in different regimes. The same boost
    // conditioned on the chain's own pt estimate or on its junction degree product measures
    // strictly worse than applying it unconditionally. Eta is the exception because the chains
    // that CONTEND for a hit sit in one detector neighbourhood and therefore share the
    // conditioner, so the key stays a consistent order inside any one contention. A central boost
    // is also where the payoff is: the gain from a stronger hinge is central barrel, while the
    // duplicate cost of applying it everywhere is entirely endcap.
    // orderAlphaCentral <= 0 means "inherit orderAlpha", i.e. no ramp.
    float orderAlphaCentral = 50.f;
    float orderEtaRampLo = 1.0f;
    float orderEtaRampHi = 1.3f;

    // Candidate-relative claim tolerance: a candidate is rejected once too much of it is already
    // claimed. maxClaimedMDs is expressed in MINI-DOUBLETS for readability, but the claim universe
    // is HIT ROWS and every MD contributes two of them, so the effective budget is twice this.
    // claimCountExclusive makes the count the whole test; false ORs it with the fraction, i.e.
    // admits a candidate that passes either.
    float maxClaimedFrac = 0.20f;
    int maxClaimedMDs = 1;
    bool claimCountExclusive = true;

    // Owner-relative braid kill: drop a candidate that overlaps an already-accepted chain by more
    // than this fraction OF THE OWNER, which is the test that catches a short candidate braided
    // through a long accepted one. The Alt fields are a band-specific tightening applied to
    // candidates with |eta(innermost T3)| >= braidAltEta and nNodes <= braidAltMaxNodes; that band
    // may also take its own claim tolerance, where claimItemsAltMDs == -2 and claimFracAlt <= 0
    // mean "use the global value".
    float braidFrac = 0.2f;
    float braidFracAlt = 0.20f;
    float braidAltEta = 1.5f;
    float braidAltMaxNodes = 1e9f;
    float claimFracAlt = 0.f;
    int claimItemsAltMDs = 0;

    // Claim-universe unification: the outer-tracker hits of the kept carried pixel rows are
    // pre-claimed before the greedy walk, so a chain cannot claim hits a surviving pixel track
    // already owns. Pixel owners only supply claimed slots to the maxClaimedFrac test; they do not
    // take part in the braid.
    bool preClaim = true;

    // Wholesale class replacement: replacePT5 drops every carried pT5 (type-7) row and replacePT3
    // every carried pT3 (type-5) row, because the chains deliver those two classes themselves --
    // pT5 as bare or pixel-attached chain TCs, pT3 through the bare-T3 attach (ChainAttachT3.h).
    bool replacePT5 = true;
    bool replacePT3 = true;

    // Pixel attach: the pLS -> outer-tracker attach, which is the delivery path for the classes
    // above.
    // The pair-head logit an (accepted chain, pLS) pair must reach to attach, banded on |eta| of
    // the SEED at 1.1 / 1.7. The head's logit calibration shifts by ~4.6 units across eta, so a
    // single global margin would be a different working point in each band.
    //
    // These are NOT independent of AttachNetworkWeights.h. They are set at fixed per-band TRUE-PAIR
    // acceptance: each is the quantile of the current head's true-pair logits, in that bar's own
    // universe x |seed eta| band, that reproduces the previous head's true-pair acceptance in the
    // same cell. Retraining the attach head therefore means re-deriving every bar on this scale;
    // carrying one across a retrain silently moves the operating point. The direction that hurts
    // is LOOSENING: a bare chain converted with the wrong seed loses its match outright, so a
    // looser attach costs displaced efficiency.
    float attachTheta = 6.785090f;   // |seed eta| < 1.1
    float attachThetaT = 5.986921f;  // 1.1 <= |seed eta| < 1.7
    float attachThetaE = 6.056449f;  // |seed eta| >= 1.7
    // Delivery margin of the bare-T3 attach (ChainAttachT3.h), global -- no eta bands. Doubles as
    // the T3-side retirement bar of the carried-pLS retirement predicate.
    float attachThetaT3 = 5.811417f;
    // Chain-side retirement bar of that same predicate. Deliberately NOT banded; a banded version
    // measured dominated. The retirement kernels must read THIS and never attachTheta -- reusing
    // the delivery margin is wrong now that delivery is banded.
    float rpsThetaChain = 5.386236f;
    // Bare-T3 target admission on the production T3 fake score (node feature 12). Written in the
    // NaN-rejecting form !(fakeScore <= t3FakeMax), and applied to the target mask, so a rejected
    // T3 is never scored and never becomes a seed's best T3.
    float t3FakeMax = 0.10f;
    // Hit-overlap contention on the bare-T3 deliveries: two deliveries conflict once they share
    // this many mini-doublet rows. The unit, the sweep order (logit descending, then T3 row
    // ascending), the pre-claim set (every emitted chain TC's deduped MD set) and the revoke
    // semantics are all fixed in the kernel; only the count is a parameter.
    int ccMinShared = 1;
    // Cross-clean of the carried bare pixel seeds against the deliveries, in two arms:
    //   - pixel-anchored: retire a bare quad seed sharing at least one pixel hit row with the seed
    //     of any delivery;
    //   - bare-chain: retire a bare quad seed whose attach-head logit toward SOME delivered
    //     seedless chain TC reaches the |seed eta|-banded bar below.
    // The second arm stands in for LST's pLS/T5 embedding test. Both drop a geometric window LST
    // additionally required; see ChainCrossClean.h and ChainAttach.h for which, and why each was
    // measured inert. Set by the same fixed-acceptance rule as the attach bars above, so likewise
    // tied to AttachNetworkWeights.h.
    float xcTheta = 1.922617f;   // |seed eta| < 1.1
    float xcThetaT = 2.080702f;  // 1.1 <= |seed eta| < 1.7
    float xcThetaE = 3.541193f;  // |seed eta| >= 1.7
    // Cross-clean of the delivered SEEDLESS chain rows against the delivered SEEDED ones: drop a
    // seedless row sharing this many outer-tracker hits (2 = one full mini-doublet) with a
    // delivered pT5- or pT3-class row. 0 disables it. Effectively binary, because a seedless row
    // never shares more than 2. Measured cost is two displaced sims, neither of them in the
    // efficiency denominator, against a barrel fake-rate gain.
    int cc9MinShared = 1;
    // MUTUAL-BEST bare-seed retirement, off at any negative value: a bare pLS row is retired when
    // it is the pre-threshold argmax pair of a delivered 5+-layer accepted chain AND that chain is
    // the seed's own best-scoring chain, provided the pair's logit is within this much of that
    // pair's banded delivery margin.
    // The mutual test exists because a BAR cannot distinguish "this seed's track is already
    // delivered" from "this seed is the only thing covering its track" -- it admits a seed whose
    // own track lies elsewhere, while an argmax taken from both sides cannot. (delivered chain TC,
    // un-retired bare pLS row) is the great majority of the duplicate rate, and the three
    // bar-based retirement channels above cannot reach it.
    float dupMutualDelta = -1.f;
    // Region-conditioned relaxation of the cross-clean bars (see ChainAttachPlsPre): the bar is
    // lowered by up to this much, ramped in pt so that it reaches full depth only as pt -> 0 and
    // vanishes at dupXcPtMax, and never applied in the barrel. <= 0 is off.
    float dupXcDelta = 1.5f;
    float dupXcPtMax = 3.f;
    // Also retire a carried bare-pLS row whose seed had a scored pair above its class retirement
    // bar but lost the contention.
    bool attachSuppressBarePLS = true;
    // Seed-family dedup of the attach owners: two pLS are the same seed when they share at least
    // two pixel hit rows. Also gates the bare-T3 stage's own dedup, which follows this one.
    bool attachSeedDedup = true;
    // Attach-eligibility dcaXY ceiling, deliberately OFF: a displaced chain with a pixel seed is an
    // upside to claim, not a dilution to guard against.
    float attachDcaMax = 1e9f;
    // Analytic prefilter windows: a (chain, pLS) pair is scored only when the propagated seed
    // agrees with the target to within these. They are ALSO the attach grid's design inputs --
    // ChainAttach.h derives its cell widths from them -- so widening one widens the grid.
    float attachPrefDPhi = 0.4f;
    float attachPrefDTanL = 0.6f;

    // 4-layer class policy.
    // The 4-layer class is the sole cover of a large slice of jet-core tracks, concentrated in
    // exactly the sim-pt band where the pixel-seeded classes collapse, and the 4-layer IP gate bar
    // -- not the weld and not the claim -- is what rejects most of them: three quarters of the
    // uncovered jet-core sims already have a gate-KILLED 4-layer IP chain built entirely from
    // their own triplets.
    //
    // t4DensDelta lowers the 4-layer IP bar by up to this much, ramped in LOCAL graph occupancy
    // (chain feature 14: the maximum over the chain's own weld junctions of degIn * degOut at the
    // junction element),
    //     w = min(1, max(0, log10(1 + deg) - log10(1 + t4DensRho0)) / t4DensDecades)
    // which is identically ZERO at or below t4DensRho0 and continuous above it. Every region
    // sparser than rho0 therefore keeps the unrelaxed bar BY CONSTRUCTION rather than by
    // measurement, which is what makes the relaxation safe for isolated displaced tracks -- they
    // live decades of occupancy below a jet core. Occupancy is a CONDITIONING variable here, never
    // a rank term, and the unconditioned form (rho0 = 0, decades = 0) was measured and rejected:
    // it buys more displaced efficiency everywhere but takes the fake rate from below LST's to
    // above it.
    float t4DensDelta = 2.f;
    float t4DensRho0 = 30.f;
    float t4DensDecades = 1.f;
    // The same lever in the opposite direction, unconditioned, for pricing the suppression side.
    // Added to the 4-layer IP bar and to the NON-far exempt bar; the far-dca cell (dcaSplit2 with
    // t4FarMaxResid) keeps its free pass.
    float t4GateTighten = 0.f;
    // Overrides of the two 4-layer participation limits, 0 meaning "use the constant".
    // t4EmitMinLayers overrides kChainTCMinLayers at the TC emission flag ONLY, so the claim runs
    // identically and the knob separates what the class DELIVERS from what its hits do to everyone
    // else. t4AttachMinLayers overrides kAttachMinLayers in the attach target filter, which
    // promotes a 4-layer chain from a score-only auxiliary target to a full one (contention, best
    // pair, delivery); the auxiliary append de-duplicates itself against the widened list.
    int t4EmitMinLayers = 0;
    int t4AttachMinLayers = 0;

    // Per-shared-key incidence degree cap. A memory bound, NOT a physics working point.
    // The edge count is the sum over shared elements of degIn * degOut (ChainEdges.h). On a
    // collimated (jet) event the number of distinct shared MDs saturates while the triplet count
    // keeps growing, so the mean per-element degree rises from ~2 to 62-272 and that ratio SQUARES
    // into the edge count: 5500x more edges at the tail, 8.4 GB of edge rows on a single event,
    // past two hard allocation ceilings. Meanwhile the weld consumes at most kChainWeldSweeps edges
    // per node slot and welds 0.018% of a jet event's edges.
    //
    // So an element keeps at most this many in- and out-triplets and enumerates
    // min(degIn, C) * min(degOut, C) edges. Measured cost at 256: 0.074% of the E1 rows on minimum
    // bias, whose largest per-element degree ever observed is 554 against 2303 on jets, in exchange
    // for 3-6x fewer edges on jets. WHICH triplets are kept is CSR arrival order -- the first C to
    // land -- because a score-ranked rule needs either a per-slice ranking, which costs the very
    // O(sum deg^2) this removes, or a sort with two triplet-sized buffers.
    //
    // Two things this deliberately does NOT touch. The CSR offsets stay UNCAPPED, so the incidence
    // fill and its bounds are unchanged and the cap is purely an enumeration bound. And so do the
    // degIn / degOut values that reach the edge head (features 12/13) and the chain head: those are
    // TRAINED inputs, and capping them would move scores for no memory saving at all.
    uint32_t degreeCap = 256u;

    // True iff any eta-band delta is live. The gate short-circuits its whole band computation on
    // this, so leaving every delta at 0 costs nothing.
    constexpr bool etaBandActive() const {
      return zEta2 > zEta1 && (zdRI != 0.f || zdR != 0.f || zdR5 != 0.f || zdR6 != 0.f || zdM4 != 0.f || zdM4D != 0.f ||
                               zdCP != 0.f || zdCD != 0.f);
    }
  };

  // Environment overrides of the BARE-SEED RETIREMENT bars, and nothing else. They exist so that
  // one binary supplies a whole scan, which is what makes it possible to prove an arm inert by
  // BIT-IDENTITY instead of asserting it: with nothing set this writes nothing and prints nothing.
  //
  // Only these fields, because the only evidence any of the retirement channels has is the attach
  // head's pair logit against these bars, and the (delivered chain TC, un-retired bare pLS row)
  // pair is the great majority of the duplicate rate.
  //
  //   LST_D_RPS_DELTA     subtract from rpsThetaChain (the chain-side retirement bar)
  //   LST_D_XC_DELTA      subtract from xcTheta / xcThetaT / xcThetaE together
  //   LST_D_T3_DELTA      subtract from attachThetaT3 (the T3-side retirement bar)
  //   LST_D_XCLO_DELTA    set dupXcDelta, with LST_D_XCLO_PTMAX setting dupXcPtMax
  //   LST_D_MUTUAL_DELTA  set dupMutualDelta, i.e. turn on the mutual-best retirement
  //
  // The first three are deltas rather than absolute values because the three eta bands were fitted
  // at a common fixed acceptance: moving them together preserves that structure.
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
      std::printf("[chainenv] dupXcDelta: OFF -> %g (ramped, |eta| >= 1.1, pt < %g)\n", cfg.dupXcDelta, cfg.dupXcPtMax);
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
    std::printf(
        "[chainenv] D retirement resolved: rpsThetaChain=%g xcTheta=%g/%g/%g attachThetaT3=%g"
        " dupMutualDelta=%g\n",
        cfg.rpsThetaChain,
        cfg.xcTheta,
        cfg.xcThetaT,
        cfg.xcThetaE,
        cfg.attachThetaT3,
        cfg.dupMutualDelta);
  }

  // Environment overrides of the TERMINAL TRIM group, so the trim can be A/B'd from one binary.
  // With nothing set this writes nothing and prints nothing.
  //   LST_TRIM_ON    0 disables the terminal trim entirely
  //   LST_TRIM_ABS   the absolute chi2 floor below which a chain is never trimmed
  //   LST_TRIM_MODE  which rule takes the drop decision (see ChainConfig::trimMode)
  //   LST_TRIM_GAP   how decisively the head must prefer a drop, in logit units
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

  // Environment overrides of the 4-layer class policy group, so one binary supplies every arm of
  // an A/B. With nothing set this writes nothing and prints nothing.
  //
  //   LST_T4_DENS_DELTA    lower the 4-layer IP gate bar by up to this much, ramped in local graph
  //                        occupancy (0 = off)
  //   LST_T4_DENS_RHO0     occupancy at which the ramp STARTS; below it the bar is unchanged
  //   LST_T4_DENS_DECADES  ramp width in decades of log10(1 + occupancy)
  //   LST_T4_GATE_TIGHTEN  add to the 4-layer IP bar and the non-far exempt bar
  //   LST_T4_EMIT_MIN      override kChainTCMinLayers at the TC emission flag
  //   LST_T4_ATTACH_MIN    override kAttachMinLayers in the attach target filter
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
      std::printf(
          "[chainenv] T4 class policy resolved: densDelta=%g rho0=%g decades=%g"
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

  // How many argmax / apply sweeps the weld runs. This is not a completeness knob, it is where a
  // TRUNCATION lands: an edge competes only while BOTH of its slots are free (ChainWeld.h), so a
  // node's argmax cannot advance past its own rank-1 edge until some other weld frees the blocker.
  // The sweep pair is therefore an iterated mutual-best matching whose fixed point is the greedy
  // matching by weld key, and reaching that fixed point takes 12-20 sweeps in a jet core.
  //
  // Running it to the fixed point is monotonically HARMFUL, because the marginal weld's quality
  // collapses: the same-sim purity of the welds a sweep produces is .55, .31, .12 and .05 for
  // sweeps 1 to 4 on jet cores, and 16 sweeps costs jet-core efficiency outright. Truncating is
  // self-limiting rather than region-dependent -- where the iteration had already converged there
  // was nothing left for a further sweep to do -- so no density conditioning is needed. Caveat
  // that rides with the value: the jet-core duplicate rate is worse at 2 than at 3.
  static constexpr int kChainWeldSweeps = 2;
  // Junction occupancy above which the weld argmax orders the two edge families explicitly (see
  // chainWeldKeyDense in ChainWeld.h); 0 disables the family term entirely.
  //
  // The weld puts both families into ONE atomicMax on the edge logit, but the head's eligibility
  // bar is fitted PER FAMILY and per (pt, |eta|) cell at equal per-cell SIGNAL efficiency, so it
  // admits 92-97% of E2 rows against 23-45% of E1 rows. The two eligible pools it hands the argmax
  // are therefore not of equal purity, and E2 wins slots on a scale it was never calibrated
  // against E1 on.
  //
  // What actually carries the miscalibration is LOCAL OCCUPANCY, not family. Measured over 7.1M
  // minimum-bias and 347M jet eligible edge rows, the same-sim rate of an eligible edge falls from
  // .84 at junction degree product 1 to .0001 above 16384, and INSIDE every occupancy decade of
  // both samples E1 is the better family (likelihood ratio E2/E1 from .72 down to .013, never
  // above 1). The pooled "E2 is 6.8x more likely" is a composition artefact: 99.2% of the eligible
  // E1 rows on jets sit in the highest occupancy bin. Ordering E2 first everywhere therefore
  // demotes E1 exactly where E1 is unambiguous, which is the sparse regime a displaced track lives
  // in, and that is measurably what it costs.
  //
  // Conditioning on the junction's incidence degree product confines the family term to the regime
  // it was measured in. The knee is not a softening dial: the weld is a global matching, so a
  // lower knee yields a DIFFERENT matching rather than a partial one, and half this value measured
  // worse on minimum bias than no family term at all.
  static constexpr long long kChainWeldFamilyDegKnee = 128;
  // A chain shorter than this emits no TrackCandidate.
  static constexpr int kChainTCMinLayers = 4;
  // The claim universe is hit rows, not mini-doublets.
  static constexpr bool kChainHitLevelClaim = true;
  // Width of the per-chain layer bitmask: md_layer runs 1-6 in the barrel and 7-11 in the endcap.
  static constexpr int kChainMaxMdLayer = 12;
  // Cycle guard for the weld path walk. Edges point strictly inward -> outward, so a chain cannot
  // exceed the detector layer count; this is the device-safe stand-in for a `visited` array and
  // has never been reached.
  static constexpr uint32_t kChainMaxNodes = 64;

  // TrackCandidates allocation headroom for the bare-T3 (pT3-class) deliveries, measured at ~120
  // per event. The emission sweep guards the bound and counts any overflow.
  static constexpr unsigned int kChainBareT3TCHeadroom = 4096u;

  // Width of the attach head's pair-feature row; must match AttachNetworkWeights.h.
  static constexpr int kAttachFeatures = 23;
  // Only chain targets with at least this many layers bid for a pixel seed.
  //
  // It is 4, not 5, because a 4-layer chain would otherwise be unreachable by ANY seeded route: it
  // is excluded here, and its triplets are marked consumed so the bare-triplet stage will not take
  // them either. Admitting it is worth +.0010 overall efficiency with the barrel fake rate DOWN
  // .0011 and duplicates unchanged. The mechanism is the 75% hit-match bar: an 8-hit seedless
  // object misses it and the same object with the seed's four pixel hits clears it, which is why
  // the fake rate falls while the candidate count rises.
  static constexpr int kAttachMinLayers = 4;

  // ---- Attach grid geometry (derivation at the top of src/alpaka/ChainAttach.h) ----------------
  // The three axes are keyed on the quantities the two prefilter windows are written in:
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
