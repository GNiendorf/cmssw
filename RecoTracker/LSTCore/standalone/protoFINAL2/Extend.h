#ifndef PROTOTYPE_EXTEND_H
#define PROTOTYPE_EXTEND_H

// EXPLOIT -- CHAIN EXTENSION AT ASSEMBLY (length lever 3, structural).
//
// The inverse of the terminal trim. After K9 has arbitrated the hit claim, the accepted
// chains own their hits and every other hit row in the event is UNCLAIMED. RECON-1 F5
// showed that 21.5% / 24.1% / 15.8% (barrel / transition / endcap) of the sims both LST
// and the prototype deliver are short by EXACTLY one layer, and that the missing layer is
// the OUTERMOST terminal in 67-79% of those cases -- the trim signature. This pass gives
// the layer back where the detector actually has a free MiniDoublet on the chain's own
// trajectory.
//
// CONTRACT (deliberately conservative):
//   * runs POST-K9-claim, POST-attach-stage, PRE-K10 assembly. It therefore changes only
//     the emitted hit list / nhitOT / nLayers of chains that were going to be TCs anyway.
//     No chain is created, none is removed, no claim is revisited, no arbitration re-runs.
//   * candidates are MDs whose BOTH hits are unclaimed in the final K9 owner map (and not
//     owned by a surviving carried pixel TC). Extension can therefore never take a hit
//     that any delivered object already holds -- it cannot manufacture an overlap.
//   * a candidate must live on a layer the chain does NOT already occupy, so an accepted
//     extension always adds exactly one layer (nLayers += 1, nhitOT += 2).
//   * the compatibility window is CHAIN-TO-HIT ONLY: the candidate anchor hit's residual
//     to the chain's own combined fit (Kasa xy circle + rz line vs cumulative chord,
//     the VERBATIM ChainFeatures/Trim arithmetic) must be within `window` cm, and the
//     REFIT combined chi2/hit over the enlarged MD list must stay within `chi2Factor` of
//     the original. No dR / dphi / deta to any OTHER track candidate, no proximity or
//     embedding test of any kind enters -- jet safety is structural.
//   * chains are visited in K9 accepted (best-first) order and each accepted extension
//     marks its hits claimed, so two chains can never absorb the same free MD.
//
// mode 0 = off. With mode 0 the Chains object is untouched and the whole binary is
// bit-identical to the golden tree (the caller does not even build the free-MD index).

#include <cstdint>
#include <vector>

#include "EventData.h"
#include "Stages.h"

struct ExtendParams {
  int mode = 0;             // -EX  : 0 off, 1 outer end only, 2 inner only, 3 both
  float window = 0.5f;      // -EXW : xy (circle) residual window, cm. With rzWindow <= 0
                            //        this is the COMBINED sqrt(rxy^2 + rrz^2) window.
  float rzWindow = 0.f;     // -EXR : separate |rz| window, cm. > 0 splits the test into
                            //        |rxy| <= window AND |rrz| <= rzWindow. The two
                            //        residuals have very different intrinsic scales (a 2S
                            //        strip is ~5 cm long in z but ~100 um in r-phi), so one
                            //        combined window is forced to the WORSE of the two.
  float chi2Factor = 2.0f;  // -EXF : refit chi2/hit <= factor * max(chi2Full, window^2)
  float uniqMargin = 0.f;   // -EXU : ambiguity guard, cm. > 0 requires the runner-up free
                            //        MD to be at least this much worse than the winner --
                            //        when two free MDs both fit the trajectory the choice
                            //        is a coin flip, and a coin flip is exactly what breaks
                            //        a marginal harness match. Purely chain-to-hit.
  float maxDist = 60.f;     // -EXD : max 3D distance terminal MD -> candidate MD, cm
  int maxJump = 1;          // -EXJ : max md_layer index jump from the terminal layer
  int maxPerEnd = 1;        // -EXN : max MDs appended per chain end
  int minLayers = 4;        // -EXL : chains below this many layers are never touched
  float maxChi2 = 0.f;      // -EXC : only extrapolate chains whose OWN combined fit chi2/hit
                            //        is below this (cm^2). The mirror image of the trim's
                            //        -TA guard: -TA trims chains that mis-fit, -EXC refuses
                            //        to extrapolate them. A chain that does not sit on one
                            //        trajectory has no trustworthy trajectory to extend
                            //        along, and its marginal harness match is exactly the
                            //        one an extra hit tips below threshold. 0 = off.
  int segLinked = 0;        // -EXS : 1 = the candidate MD must be joined to the terminal MD
                            //        by an EXISTING LineSegment. LST only builds an LS
                            //        between two MDs that already passed its module map and
                            //        its segment geometry cuts, so this replaces a free-form
                            //        window with the detector's own compatibility statement.
                            //        Still purely chain-to-hit: an LS is a two-MD object, it
                            //        knows nothing about any other track candidate.
};

struct ExtendStats {
  long long nChains = 0;      // chains examined (accepted, >= minLayers, fit usable)
  long long nNoFit = 0;       // chains skipped: degenerate circle / rz fit
  long long nCand = 0;        // candidate MDs that passed the cheap geometric prefilter
  long long nExtOuter = 0;    // outer-end MDs appended
  long long nExtInner = 0;    // inner-end MDs prepended
  long long nRejChi2 = 0;     // best candidate rejected by the refit chi2 factor
  long long nRejUniq = 0;     // best candidate rejected by the -EXU ambiguity guard
  long long nRejFit = 0;      // chains refused by the -EXC own-fit-quality guard
  long long nExtChains = 0;   // chains that gained at least one MD
};

// Mutates `chains` in place: mdItems / mdOffsets / nLayers of extended chains grow; the
// T3 node list (items/offsets), the weld-edge list and the K6 score are left VERBATIM
// (an extension adds a hit, not a graph node -- nothing downstream re-reads the node
// list for physics). `claimedHit` is the hit-level claim map (1 = owned by a delivered
// object) and is UPDATED as extensions consume rows.
void extendChains(const LSTEventData& ev,
                  const std::vector<int>& acceptedChains,
                  const ExtendParams& p,
                  std::vector<char>& claimedHit,
                  Chains& chains,
                  ExtendStats& st);

#endif
