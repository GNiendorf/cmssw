#ifndef RecoTracker_LSTCore_interface_ChainNodesSoA_h
#define RecoTracker_LSTCore_interface_ChainNodesSoA_h

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  struct Params_ChainNode {
    // Frozen per-node feature row of the edge head, built by ChainNodeFeatures:
    //  0 kappaSigned  1 log10R  2 tanLambda  3 chordEta  4 dphi01  5 dz01  6 dz12
    //  7 drt01  8 drt12  9 innermostLayer  10 nBarrel  11 nPS  12 fakeScoreT3
    // The head's input row is the concatenation (inner node's 13, outer node's 13, the edge's own
    // 14) == edgemlp::kInput == 40. Do not reorder.
    static constexpr int kFeatures = 13;
    using ArrayFxFeat = edm::StdArray<float, kFeatures>;
  };

  // One row per NODE of the chain-tracking graph, i.e. per LST triplet, under a dense index.
  //
  // LST stores triplets per-lower-module with gaps (ObjectRanges::tripletModuleIndices gives the
  // start of each module slice, TripletsOccupancy::nTriplets gives how many of that slice were
  // actually filled). The chain graph needs a dense global node index instead, so
  // ChainScatterTripletModules compacts the module-segmented store: node n in [0, nNodes) maps to
  // the sparse triplet tripletIndex()[n]. The collection is allocated with exactly nNodes rows, so
  // metadata().size() is the node count.
  //
  // The feature block is stored rather than recomputed per edge because every node is read by ~4
  // edges on average (E ~ 2 * nNodes, each edge touching two nodes), so recomputing would triple the
  // transcendental work to save 2.9 MB.
  GENERATE_SOA_LAYOUT(ChainNodesSoALayout,
                      // Dense node index -> sparse LST triplet index. Written by
                      // ChainScatterTripletModules; every other column is keyed on the dense index.
                      SOA_COLUMN(uint32_t, tripletIndex),
                      // CSR payloads of the two ChainIncidence tables, filled by
                      // ChainScatterIncidence. Each is a permutation of [0, nNodes): every node
                      // appears exactly once in every item array, because every triplet has exactly
                      // one first MD, one last MD, one inner Segment and one outer Segment.
                      SOA_COLUMN(uint32_t, mdT3OutItems),  // payload of ChainIncidence(MD).t3OutOffsets
                      SOA_COLUMN(uint32_t, mdT3InItems),   // payload of ChainIncidence(MD).t3InOffsets
                      SOA_COLUMN(uint32_t, lsT3OutItems),  // payload of ChainIncidence(LS).t3OutOffsets
                      SOA_COLUMN(uint32_t, lsT3InItems),   // payload of ChainIncidence(LS).t3InOffsets
                      // Run- and backend-invariant identity of the node. The dense node index, the
                      // sparse triplet index it maps to, and every CSR slot derived from them are
                      // handed out by atomicAdd, so they permute between two identical runs and
                      // between the CPU and CUDA backends. stableId is instead a mix of the six hit
                      // ROWS of the node's three MDs (anchor and outer hit of md0, md1, md2), and
                      // hit rows are fixed by the event data alone. Its only consumer is the weld
                      // tie-break, through ChainEdges::tie.
                      SOA_COLUMN(uint32_t, stableId),
                      // The edge head's node row, see Params_ChainNode above.
                      SOA_COLUMN(Params_ChainNode::ArrayFxFeat, features),
                      // Direction of the triplet's two anchor-hit chords c01 = md1 - md0 and
                      // c12 = md2 - md1, in rad. These are the per-node quantities the EDGE feature
                      // build hoists out of its edge loop, so they are computed once per node here.
                      // theta is atan2(|c_xy|, c_z) in [0, pi], one fixed rz-angle definition, so
                      // that an edge's kink theta12(inner) - theta01(outer) needs no wrap.
                      SOA_COLUMN(float, phiC01),    // atan2(c01y, c01x)
                      SOA_COLUMN(float, phiC12),    // atan2(c12y, c12x)
                      SOA_COLUMN(float, thetaC01),  // atan2(|c01_xy|, c01z)
                      SOA_COLUMN(float, thetaC12),  // atan2(|c12_xy|, c12z)
                      // The two DENSE incidence keys of this node's "in" side: the dense index of
                      // its last MD and of its outer Segment (ChainPrefixKeyModules in ChainGraph.h
                      // defines the dense numbering). These are exactly the keys the E1 and E2
                      // junction-degree lookups need, and both of those lookups sit in per-EDGE
                      // loops, where recovering the key from the raw index would cost two dependent
                      // SoA loads plus a module lookup on every edge. Stored once per node instead.
                      // Appended after the feature block so no pre-existing column's offset moves.
                      SOA_COLUMN(uint32_t, mdKeyIn),
                      SOA_COLUMN(uint32_t, lsKeyIn),
                      // This node's cell in LST's T3-DNN working-point binning, packed as
                      // ptbin * dnn::kEtaBins + etabin, so 0..19:
                      //   ptbin  = (radius * k2Rinv1GeVf * 2 > 5)              (dnn::kPtBins  == 2)
                      //   etabin = (|eta| > 2.5) ? 9 : |eta| / dnn::kEtaSize   (dnn::kEtaBins == 10)
                      // with eta = mds.anchorEta()[md0] -- EXACTLY the two quantities
                      // t3dnn::runInference bins its own working points on (NeuralNetwork.h). It is
                      // the cell the edge head's weld eligibility bar is keyed on, and it is computed
                      // in the node feature build, where the radius and md0 are already in registers,
                      // so the per-edge cost is one byte load instead of a
                      // triplet -> segment -> md index chase.
                      SOA_COLUMN(uint8_t, wpBin))

  using ChainNodesSoA = ChainNodesSoALayout<>;
  using ChainNodes = ChainNodesSoA::View;
  using ChainNodesConst = ChainNodesSoA::ConstView;

}  // namespace lst

#endif
