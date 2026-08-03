#ifndef RecoTracker_LSTCore_interface_ChainsSoA_h
#define RecoTracker_LSTCore_interface_ChainsSoA_h

#include <cstdint>

#include "DataFormats/Common/interface/StdArray.h"
#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  struct Params_ChainFeat {
    // FROZEN chain-feature contract (prototype/ChainFeatures.h, kChainFeat = 25). Slots 0-15 are
    // the M6 contract, 16-24 the a2 additions. The 3-class gate head gathers a SUBSET of these
    // columns through its own kSrcCol table (column -1 = the chain dcaXY), so the contract must
    // stay exactly 25 wide and in this order:
    //   0 nNodes  1 nLayers  2 sumEdgeLogit  3 minEdgeLogit  4 meanEdgeLogit
    //   5 fullFitChi2PerHit  6 rzLineChi2PerHit  7 fitKappa  8 dKappaFitVsMedianT3  9 ptEst
    //  10 innermostLayer  11 layerSpan  12 nPS  13 nBarrel  14 maxJunctionDegProduct
    //  15 chargeConsistency  16 maxXyResid  17 maxRzResid  18 stdEdgeLogit  19 maxBridgeChi2
    //  20 minT3FakeScore  21 maxT3FakeScore  22 meanT3PromptScore  23 minT3DisplacedScore
    //  24 meanT3DisplacedScore
    static constexpr int kFeatures = 25;
    using ArrayFxFeat = edm::StdArray<float, kFeatures>;
  };

  // Welded chains (port map phase P2.2, stages K6c-K6f and K7a-K7c).
  //
  // Storage contract for the CSR payloads, all in ChainItemsSoA and all keyed off ONE per-chain
  // number, nodeOffset:
  //   nodeItems[nodeOffset,             nodeOffset + nNodes)         member nodes, innermost first
  //   edgeItems[nodeOffset,             nodeOffset + nNodes - 1)     welded edges, innermost first
  //   mdItems  [3 * nodeOffset,         3 * nodeOffset + nMDs)       deduped MD union, first
  //                                                                  appearance order
  //   mdScratch[3 * nodeOffset,         3 * nodeOffset + 3 * nNodes) K6f working space
  // The edge list is one shorter than the node list, so it fits in the node region with one slot
  // to spare; the MD union of n nodes can never exceed 3n entries, so the 3x-strided MD region is
  // an exact bound. ChainItems is therefore allocated with 3 * (total welded nodes) rows and needs
  // no second prefix pass. The terminal trim (K6f) moves nodeOffset by +1 (inner drop) or shortens
  // nNodes (outer drop); both keep every region above inside the chain's original allocation.
  GENERATE_SOA_LAYOUT(ChainsSoALayout,
                      SOA_COLUMN(uint32_t, nodeOffset),
                      SOA_COLUMN(uint16_t, nNodes),
                      SOA_COLUMN(uint16_t, nMDs),
                      SOA_COLUMN(uint8_t, nLayers),
                      // -G 6 branch code: 0 = T4 IP, 1 = T4 exempt, 2 = 5+ IP, 3 = 5+ exempt.
                      SOA_COLUMN(int8_t, branch),
                      // 0 = untrimmed, 1 = innermost node dropped, 2 = outermost node dropped.
                      SOA_COLUMN(int8_t, trimAction),
                      // bit0 killed by the gate, bit1 exempt (large-DCA) branch, bit2 |eta| band,
                      // bit3 killed by the C1 (nNodes == 2, nLayers == 5) cell rule.
                      SOA_COLUMN(uint8_t, flags),
                      // K6e/K6f: sum of member weld-edge logits + lambdaLen * nLayers. K7c
                      // subtracts kChainGateKill from it when the gate kills the chain, exactly as
                      // the reference implementation does, so K9 can threshold on this one number.
                      SOA_COLUMN(float, score),
                      SOA_COLUMN(float, dcaXY),  // K7a, the -X split axis and gate input 24
                      SOA_COLUMN(float, zFake),
                      SOA_COLUMN(float, zPrompt),
                      SOA_COLUMN(float, zDisp),
                      SOA_COLUMN(float, marginP),  // zPrompt - zFake
                      SOA_COLUMN(float, marginD),  // zDisp   - zFake
                      SOA_COLUMN(float, marginX),  // max(zPrompt, zDisp) - zFake
                      SOA_COLUMN(Params_ChainFeat::ArrayFxFeat, features),
                      // ---- P2.3 (K9 claim + K10 assembly) --------------------------------------
                      // K9 best-first ordering key, score - alpha * hinge(marginX). Never a
                      // threshold: acceptance always cuts on score.
                      SOA_COLUMN(float, orderKey),
                      // Deduped claim-universe hit rows of this chain live at
                      // ChainClaimHits[6 * nodeOffset, 6 * nodeOffset + nClaimHits); 2 hits per MD
                      // and nMDs <= 3 * nNodes make 6 * nNodes an exact bound, so the region
                      // inherits the nodeOffset addressing of ChainItemsSoA with no second prefix.
                      SOA_COLUMN(uint16_t, nClaimHits),
                      // bit0 K9 candidate (passed theta + the pixel-consumed drop), bit1 accepted.
                      SOA_COLUMN(uint8_t, claimFlags),
                      // K10: TC kinematics of an accepted chain. pt = LOWER median of the member
                      // t3_pt, eta/phi = the innermost member T3's (prototype k10AssembleChainTCs).
                      SOA_COLUMN(float, tcPt),
                      SOA_COLUMN(float, tcEta),
                      SOA_COLUMN(float, tcPhi),
                      // Row this chain occupies in TrackCandidatesBase, or -1 if it emits no TC.
                      SOA_COLUMN(int32_t, tcRow),
                      // ---- P2.4 (K8 pixel attach) ---------------------------------------------
                      // The a2 2-class chain head's logit. NOT a gate and NOT in the order key; it
                      // exists solely as attach pair feature 11 (prototype/PixelAttach.cc af_11).
                      SOA_COLUMN(float, gateLogit2),
                      // Attach decision: the pLS row this chain owns after the head threshold, the
                      // one-pLS-one-owner contention and the -RD seed-family dedup, or -1.
                      SOA_COLUMN(int32_t, attachPls),
                      SOA_COLUMN(float, attachLogit),
                      // ---- P2.5 (determinism) --------------------------------------------------
                      // K6e. The run- and backend-invariant identity of the chain: the stableId
                      // (ChainNodesSoA.h) of its PRE-trim head node. Heads are unique across chains
                      // because the welded graph has in/out degree <= 1, so this names the chain.
                      // Consumers are the K9 claim-order tie-break and the attach -RD dedup order,
                      // both of which used to fall back to the chain index -- a numbering that
                      // permutes between two identical runs.
                      // It is appended LAST on purpose: inserting it mid-layout shifted every later
                      // column's base address and cost the single-threaded CUDA contend kernel about
                      // 1 ms/event, with no change in the work it did. Kept here, every pre-existing
                      // column keeps its P2.4 offset and that regression disappears.
                      SOA_COLUMN(uint32_t, stableKey),
                      SOA_SCALAR(uint32_t, nChains),
                      SOA_SCALAR(uint32_t, nAccepted),
                      SOA_SCALAR(uint32_t, nChainTCs),
                      SOA_SCALAR(uint32_t, nAttached))

  using ChainsSoA = ChainsSoALayout<>;
  using Chains = ChainsSoA::View;
  using ChainsConst = ChainsSoA::ConstView;

  GENERATE_SOA_LAYOUT(ChainItemsSoALayout,
                      SOA_COLUMN(uint32_t, nodeItems),  // dense chain-node index
                      SOA_COLUMN(uint32_t, edgeItems),  // row in ChainEdges
                      SOA_COLUMN(uint32_t, mdItems),    // MiniDoublet index
                      SOA_COLUMN(uint32_t, mdScratch))  // K6f candidate MD union

  using ChainItemsSoA = ChainItemsSoALayout<>;
  using ChainItems = ChainItemsSoA::View;
  using ChainItemsConst = ChainItemsSoA::ConstView;

  // K9 claim-flag bits (ChainsSoA::claimFlags).
  static constexpr uint8_t kChainClaimCandidate = 0x1;
  static constexpr uint8_t kChainClaimAccepted = 0x2;

  // Chain flag bits.
  static constexpr uint8_t kChainFlagKilled = 0x1;
  static constexpr uint8_t kChainFlagExempt = 0x2;
  static constexpr uint8_t kChainFlagEtaBand = 0x4;
  static constexpr uint8_t kChainFlagCellKill = 0x8;

}  // namespace lst

#endif
