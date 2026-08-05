#ifndef PROTOTYPE_FEATURES_H
#define PROTOTYPE_FEATURES_H

// Origin-free node (T3) and edge features (plan section 3 NN inventory). HARD RULE
// (plan 5, displaced feature hygiene): NO raw global position enters any feature —
// no |eta|, |phi|, |z|, r of a HIT POSITION, no betaIn. Only displacement-derived
// quantities (chords, curvatures, deltas) and detector-category integers are allowed.
// Circle center/radius are allowed (origin-free: translation of hits shifts the center
// identically). tanLambda uses dz/ds of displacements, never z/r of positions.
//
// Anchor-hit convention: T3 hits here = the 3 MD ANCHOR hits h0,h1,h2 (md_anchor_x/y/z of
// t3_md0/md1/md2). Chord c01 = h1-h0, c12 = h2-h1, c02 = h2-h0.

#include <vector>

#include "EventData.h"
#include "Stages.h"

// Node features, kNodeFeat floats per T3, row-major [t3 * kNodeFeat + i]:
//  0 kappaSigned   : rotSign / t3_radius, rotSign = sign of z of cross(c01, c12)
//  1 log10R        : log10(t3_radius)
//  2 tanLambda     : (h2.z - h0.z) / |c02_xy|  (xy chord length)
//  3 chordEta      : eta of the displacement vector c02 (NOT of a position)
//  4 dphi01        : deltaPhi(phi(c01), phi(c12))  (bending between chords)
//  5 dz01, 6 dz12  : z deltas of the chords
//  7 drt01, 8 drt12: rt(h1)-rt(h0), rt(h2)-rt(h1)  (radial progression deltas — these are
//                    differences of positions' rt, allowed as they are translation-
//                    sensitive only radially; keep, they encode layer spacing)
//  9 innermostLayer: md_layer of t3_md0 (1-11, detector category integer)
// 10 nBarrel       : how many of the 3 MDs are barrel (layer <= 6)
// 11 nPS           : how many of the 3 MDs are PS (md_type == 1)
// 12 fakeScoreT3   : t3_fakeScore (the existing t3dnn output, cheap context; may be ablated)
constexpr int kNodeFeat = 13;

// Edge features, kEdgeFeat floats per edge, row-major:
//  0 etype         : 1 = E1 (shared MD), 2 = E2 (shared LS)
//  1 dKappa        : kappaSigned(inner) - kappaSigned(outer)
//  2 dKappaRel     : |dKappa| / (|kappa_in| + |kappa_out| + 1e-9)
//  3 chargeAgree   : 1 if rotSign(inner) == rotSign(outer) else 0
//  4 dTanLambda    : tanLambda(inner) - tanLambda(outer)
//  5 kinkPhi       : deltaPhi(phi(inner c12), phi(outer c01)) — junction bending
//  6 kinkTheta     : atan2(|c12_xy|, dz12)(inner) - atan2(|c01_xy|, dz01)(outer) style
//                    rz junction kink (implementer: define once, document, be consistent)
//  7 centerDist    : |(t3_centerX,Y)(inner) - (t3_centerX,Y)(outer)|
//  8 centerDistRel : centerDist / (0.5*(R_in + R_out))
//  9 sharedLayer   : md_layer of the shared MD (E1) / of LS's first MD (E2)
// 10 sharedIsPS    : md_type of that MD
// 11 sharedIsBarrel: layer <= 6
// 12 degIn         : incidence in-degree at the shared key (E1: mdT3In count at shared MD;
//                    E2: lsT3In count at shared LS) — the density-context aggregate
// 13 degOut        : same, out-degree
constexpr int kEdgeFeat = 14;

struct NodeFeatures {
  std::vector<float> f;  // nT3 * kNodeFeat
};
struct EdgeFeatures {
  std::vector<float> f;  // nEdges * kEdgeFeat
};

void computeNodeFeatures(const LSTEventData& ev, NodeFeatures& out);
void computeEdgeFeatures(const LSTEventData& ev, const ChainGraph& g, const NodeFeatures& nf, EdgeFeatures& out);

#endif
