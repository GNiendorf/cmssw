#ifndef RecoTracker_LSTCore_interface_ChainsSoA_h
#define RecoTracker_LSTCore_interface_ChainsSoA_h

#include <cstdint>

#include "DataFormats/Common/interface/StdArray.h"
#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  struct Params_ChainFeat {
    // Frozen per-chain feature row, built by ChainFeaturesKernel and consumed by the 3-class chain
    // head (the gate). The head gathers a SUBSET of these columns through its own kSrcCol table, in
    // which column -1 means the chain's dcaXY rather than a feature column, so the row must stay
    // exactly this wide and in this order:
    //   0 nNodes  1 nLayers  2 sumEdgeLogit  3 minEdgeLogit  4 meanEdgeLogit
    //   5 fullFitChi2PerHit  6 rzLineChi2PerHit  7 fitKappa  8 dKappaFitVsMedianT3  9 ptEst
    //  10 innermostLayer  11 layerSpan  12 nPS  13 nBarrel  14 maxJunctionDegProduct
    //  15 chargeConsistency  16 maxXyResid  17 maxRzResid  18 stdEdgeLogit  19 maxBridgeChi2
    //  20 minT3FakeScore  21 maxT3FakeScore  22 meanT3PromptScore  23 minT3DisplacedScore
    //  24 meanT3DisplacedScore
    // ChainGate.h defines each column where it builds it.
    static constexpr int kFeatures = 25;
    using ArrayFxFeat = edm::StdArray<float, kFeatures>;
  };

  // One row per WELDED CHAIN: a path of triplets linked by the weld, and the object the gate judges,
  // the claim arbitrates and the emission turns into a TrackCandidate. Rows are created by
  // ChainEmitChains and then written in place by the terminal trim, the gate, the claim, the pixel
  // attach and the row assignment; each column below names the stage that owns it.
  //
  // Storage contract for the CSR payloads, all in ChainItemsSoA and all keyed off ONE per-chain
  // number, nodeOffset:
  //   nodeItems[nodeOffset,             nodeOffset + nNodes)         member nodes, innermost first
  //   edgeItems[nodeOffset,             nodeOffset + nNodes - 1)     welded edges, innermost first
  //   mdItems  [3 * nodeOffset,         3 * nodeOffset + nMDs)       deduped MD union, first
  //                                                                  appearance order
  //   mdScratch[3 * nodeOffset,         3 * nodeOffset + 3 * nNodes) terminal-trim working space
  // The edge list is one shorter than the node list, so it fits in the node region with one slot
  // to spare; the MD union of n nodes can never exceed 3n entries, so the 3x-strided MD region is
  // an exact bound. ChainItems is therefore allocated with 3 * (total welded nodes) rows and needs
  // no second prefix pass. The terminal trim moves nodeOffset by +1 (inner drop) or shortens nNodes
  // (outer drop); both keep every region above inside the chain's original allocation.
  GENERATE_SOA_LAYOUT(ChainsSoALayout,
                      // Base of this chain's CSR payloads, per the storage contract above.
                      SOA_COLUMN(uint32_t, nodeOffset),
                      // Member triplets. At least 2, since a chain is a welded path, and bounded by
                      // the weld's cycle guard kChainMaxNodes.
                      SOA_COLUMN(uint16_t, nNodes),
                      // Entries in the deduped MD union; bounded by 3 * nNodes.
                      SOA_COLUMN(uint16_t, nMDs),
                      // Distinct detector layers the chain touches: the popcount of its md_layer
                      // bitmask, so it is <= nMDs and never counts a layer twice.
                      SOA_COLUMN(uint8_t, nLayers),
                      // Which gate branch judged this chain: 0 = 4-layer IP, 1 = 4-layer exempt,
                      // 2 = 5+ IP, 3 = 5+ exempt. -1 until the gate runs.
                      SOA_COLUMN(int8_t, branch),
                      // 0 = untrimmed, 1 = innermost node dropped, 2 = outermost node dropped.
                      SOA_COLUMN(int8_t, trimAction),
                      // 1 = the learned terminal trim already wrote features[] and dcaXY for the
                      // variant it kept, so ChainFeaturesKernel must not rebuild them; 0 = the
                      // feature build owns this row. Zeroed for every chain before the trim runs.
                      SOA_COLUMN(uint8_t, featValid),
                      // Gate outcome bits, see kChainFlag* below. Written by the gate; zeroed at
                      // emission.
                      SOA_COLUMN(uint8_t, flags),
                      // Chain quality on the summed-edge-logit scale: sum of the member weld-edge
                      // logits + lambdaLen * nLayers, written at emission and rewritten by the
                      // terminal trim. The gate subtracts gateKill from it to kill a chain, so
                      // downstream "still alive" is the single test score > -0.5 * gateKill and the
                      // claim can threshold on this one number.
                      SOA_COLUMN(float, score),
                      // Transverse distance of closest approach of the chain's circle fit to the
                      // beam line, cm. It is the prompt/displaced split axis (ChainConfig::dcaSplit)
                      // and an input of the gate head. 1e9 marks a row whose feature build was
                      // skipped.
                      SOA_COLUMN(float, dcaXY),
                      // The gate head's three raw logits (fake / prompt / displaced) and the three
                      // margins every gate bar, the claim order key and the attach head are written
                      // on. Probabilities are never formed.
                      SOA_COLUMN(float, zFake),
                      SOA_COLUMN(float, zPrompt),
                      SOA_COLUMN(float, zDisp),
                      SOA_COLUMN(float, marginP),  // zPrompt - zFake
                      SOA_COLUMN(float, marginD),  // zDisp   - zFake
                      SOA_COLUMN(float, marginX),  // max(zPrompt, zDisp) - zFake
                      // The gate head's input row, see Params_ChainFeat above.
                      SOA_COLUMN(Params_ChainFeat::ArrayFxFeat, features),
                      // Best-first ordering key of the greedy claim walk,
                      // score - alphaEff * max(0, orderHinge - marginX). Never a threshold:
                      // acceptance always cuts on score.
                      SOA_COLUMN(float, orderKey),
                      // Deduped claim-universe hit rows of this chain live at
                      // ChainClaimHits[6 * nodeOffset, 6 * nodeOffset + nClaimHits); 2 hits per MD
                      // and nMDs <= 3 * nNodes make 6 * nNodes an exact bound, so the region
                      // inherits the nodeOffset addressing of ChainItemsSoA with no second prefix.
                      SOA_COLUMN(uint16_t, nClaimHits),
                      // NOT WRITTEN and NOT READ anywhere in the tree; the claim carries its own
                      // per-candidate state instead. The column is retained because it sits
                      // mid-layout and removing it would move every later column's offset -- see
                      // the stableKey note below for why that is not free.
                      SOA_COLUMN(uint8_t, claimFlags),
                      // Kinematics of the emitted track candidate. pt GeV = the LOWER median of the
                      // member triplet pt, replaced by the seed's pt when a pixel seed attaches;
                      // eta from the outermost anchor hit of the innermost member triplet, phi from
                      // its innermost anchor hit.
                      SOA_COLUMN(float, tcPt),
                      SOA_COLUMN(float, tcEta),
                      SOA_COLUMN(float, tcPhi),
                      // Row this chain occupies in TrackCandidatesBase, or -1 if it emits no TC.
                      SOA_COLUMN(int32_t, tcRow),
                      // Attach decision: the pLS row this chain owns after the pair-head threshold,
                      // the one-pLS-one-owner contention and the seed-family dedup, or -1 for a
                      // seedless chain. attachLogit is that pair's head logit, -1e30 when there is
                      // no pair. A chain with a pLS is emitted as a pT5-class row instead of a bare
                      // one; the attach never creates a row of its own.
                      SOA_COLUMN(int32_t, attachPls),
                      SOA_COLUMN(float, attachLogit),
                      // Run- and backend-invariant identity of the chain: the stableId
                      // (ChainNodesSoA.h) of its PRE-trim head node. Heads are unique across chains
                      // because the welded graph has in/out degree <= 1, so this names the chain.
                      // Its consumer is the claim's order tie-break, which may not fall back to the
                      // chain index -- that numbering permutes between two identical runs.
                      // It is appended LAST on purpose: inserting it mid-layout shifted every later
                      // column's base address and cost the single-threaded CUDA contend kernel about
                      // 1 ms/event with no change in the work it did.
                      SOA_COLUMN(uint32_t, stableKey),
                      // nChains is unused; the chain count is metadata().size(). nAccepted is the
                      // length of the claim's accepted list, nChainTCs the number of TC rows the
                      // chains appended after the carried ones.
                      SOA_SCALAR(uint32_t, nChains),
                      SOA_SCALAR(uint32_t, nAccepted),
                      SOA_SCALAR(uint32_t, nChainTCs))

  using ChainsSoA = ChainsSoALayout<>;
  using Chains = ChainsSoA::View;
  using ChainsConst = ChainsSoA::ConstView;

  // The CSR payloads of every chain, addressed off ChainsSoA::nodeOffset as described above.
  GENERATE_SOA_LAYOUT(ChainItemsSoALayout,
                      SOA_COLUMN(uint32_t, nodeItems),  // dense chain-node index
                      SOA_COLUMN(uint32_t, edgeItems),  // row in ChainEdges
                      SOA_COLUMN(uint32_t, mdItems),    // MiniDoublet index
                      SOA_COLUMN(uint32_t, mdScratch))  // terminal-trim candidate MD union

  using ChainItemsSoA = ChainItemsSoALayout<>;
  using ChainItems = ChainItemsSoA::View;
  using ChainItemsConst = ChainItemsSoA::ConstView;

  // Bit names of the claimFlags column above. Like the column itself, neither has a writer or a
  // reader: the claim keeps its per-candidate state in its own scratch arrays.
  static constexpr uint8_t kChainClaimCandidate = 0x1;
  static constexpr uint8_t kChainClaimAccepted = 0x2;

  // Chain flag bits, written by the gate.
  static constexpr uint8_t kChainFlagKilled = 0x1;    // failed its branch's bar
  static constexpr uint8_t kChainFlagExempt = 0x2;    // took the exempt (large-dcaXY) branch
  static constexpr uint8_t kChainFlagEtaBand = 0x4;   // the transition-band deltas were applied
  static constexpr uint8_t kChainFlagCellKill = 0x8;  // killed by the (nNodes == 2, nLayers == 5) rule

}  // namespace lst

#endif
