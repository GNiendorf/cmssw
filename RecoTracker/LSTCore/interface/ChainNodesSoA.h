#ifndef RecoTracker_LSTCore_interface_ChainNodesSoA_h
#define RecoTracker_LSTCore_interface_ChainNodesSoA_h

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  // Dense triplet-node view used by the chain-tracking graph (port map phase P2.0, stage K0/K1c).
  //
  // LST stores triplets per-lower-module with gaps (ObjectRanges::tripletModuleIndices gives the
  // start of each module slice, TripletsOccupancy::nTriplets gives how many of that slice were
  // actually filled). The chain graph needs a dense global node index instead, so K0 compacts the
  // module-segmented store: node n in [0, nNodes) maps to the sparse triplet tripletIndex()[n].
  //
  // The four item columns are the CSR payloads of the two ChainIncidence instances. Each of them
  // is a permutation of [0, nNodes): every node appears exactly once in every item array, because
  // every triplet has exactly one first MD, one last MD, one inner Segment and one outer Segment.
  //
  // The collection is allocated with exactly nNodes rows, so metadata().size() is the node count.
  GENERATE_SOA_LAYOUT(ChainNodesSoALayout,
                      SOA_COLUMN(uint32_t, tripletIndex),   // dense node index -> sparse triplet index
                      SOA_COLUMN(uint32_t, mdT3OutItems),   // payload of ChainIncidence(MD).t3OutOffsets
                      SOA_COLUMN(uint32_t, mdT3InItems),    // payload of ChainIncidence(MD).t3InOffsets
                      SOA_COLUMN(uint32_t, lsT3OutItems),   // payload of ChainIncidence(LS).t3OutOffsets
                      SOA_COLUMN(uint32_t, lsT3InItems))    // payload of ChainIncidence(LS).t3InOffsets

  using ChainNodesSoA = ChainNodesSoALayout<>;
  using ChainNodes = ChainNodesSoA::View;
  using ChainNodesConst = ChainNodesSoA::ConstView;

}  // namespace lst

#endif
