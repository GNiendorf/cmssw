#ifndef RecoTracker_LSTCore_interface_ChainIncidenceSoA_h
#define RecoTracker_LSTCore_interface_ChainIncidenceSoA_h

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

#include "RecoTracker/LSTCore/interface/Common.h"

namespace lst {

  // CSR incidence bookkeeping for the chain-tracking triplet graph: which triplets meet at each
  // shared detector element. Two instances are used, one keyed by a dense MiniDoublet index (the
  // source of the E1 edge family) and one keyed by a dense Segment index (the source of E2); the
  // dense key numbering is defined by ChainPrefixKeyModules. The CSR payloads themselves are the
  // four item columns of ChainNodesSoA.
  //
  // For a key k, with the triplet read innermost -> outermost:
  //   "out" side = triplets whose FIRST  MD (resp. inner Segment) is k, i.e. edges leaving  k
  //   "in"  side = triplets whose SECOND MD (resp. outer Segment) is k, i.e. edges arriving at k
  // A chain edge joins an "in" triplet to an "out" triplet at the same key, so the number of
  // enumerable edges at key k is exactly degIn(k) * degOut(k) and the total edge count is
  // E = sum_k degIn(k) * degOut(k). That arithmetic is what lets the edge buffer be allocated
  // exactly, with no capped reservation.
  //
  // Column lengths: the collection is allocated with nKeys + 1 rows. The count columns use rows
  // [0, nKeys), the offset/prefix columns use rows [0, nKeys] so that the last entry is the total.
  GENERATE_SOA_LAYOUT(ChainIncidenceSoALayout,
                      // Triplets on each side of the key. The columns serve twice: the triplet
                      // builder tallies straight into them, and after the prefix pass has captured
                      // those tallies in the offset columns they are zeroed and reused as the
                      // per-key write cursors of the CSR fill (ChainScatterIncidence), which leaves
                      // them holding the degree again.
                      SOA_COLUMN(uint32_t, t3OutCounts),
                      SOA_COLUMN(uint32_t, t3InCounts),
                      // Exclusive prefixes of the two count columns: the CSR slice of key k is
                      // [t3OutOffsets[k], t3OutOffsets[k + 1]) in ChainNodes::*T3OutItems, and
                      // likewise for the "in" side. The degree of a key is read back as the
                      // difference of two consecutive offsets, which is how the weld and the feature
                      // builds get junction degrees without touching the counts.
                      SOA_COLUMN(uint32_t, t3OutOffsets),
                      SOA_COLUMN(uint32_t, t3InOffsets),
                      // Exclusive prefix of degIn * degOut, i.e. the first edge row belonging to
                      // key k within this family's slice of the edge list. It is what makes the edge
                      // enumeration index-decoded: an edge row finds its key by binary search here.
                      // The degrees entering the product carry the per-key degree cap
                      // (ChainConfig::degreeCap), so this is a bound on ENUMERATED edges, not on the
                      // offsets above, which stay uncapped.
                      SOA_COLUMN(uint32_t, edgeProdPrefix),
                      // Total edge count of this family, == edgeProdPrefix[nKeys]. A uint32 sum of
                      // uint32 products, so it wraps silently past 2^32 edges; the host allocation
                      // guard certifies it against an independent 64-bit recount before using it.
                      SOA_SCALAR(uint32_t, nEdgesExact))

  using ChainIncidenceSoA = ChainIncidenceSoALayout<>;
  using ChainIncidence = ChainIncidenceSoA::View;
  using ChainIncidenceConst = ChainIncidenceSoA::ConstView;

}  // namespace lst

#endif
