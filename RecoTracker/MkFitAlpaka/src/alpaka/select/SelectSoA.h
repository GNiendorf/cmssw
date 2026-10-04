#ifndef RecoTracker_MkFitAlpaka_src_alpaka_select_SelectSoA_h
#define RecoTracker_MkFitAlpaka_src_alpaka_select_SelectSoA_h

// K2 (propagate to layer + selectHitIndicesV2 + find_tracks_handle_missed_layers) input and output SoAs.
// One row per listed candidate (the engine's dense (seed, ic) list, stock seed_cand_idx order).
//   SelListSoA   in : candidate slot row (CandSlotsSoA), layer, eta region
//   PropStateSoA out: state propagated to the layer (stock m_Par/m_Err[iP]) and the propagation fail flag
//   SelHitsSoA   out: selected hits (stock m_XHitArr order, <= 6), WSR after handle_missed_layers, in_gap, extras

#include <cstdint>

#include "DataFormats/SoATemplate/interface/SoALayout.h"

namespace mkfitdev {

  constexpr int kMaxSelHits = 6;  // stock NEW_MAX_HIT of selectHitIndicesV2

  // extras flags written by K2 for find_tracks_handle_missed_layers (MkBuilder.cc:706); consumed by the engine (K4)
  enum SelExtraFlags : uint8_t {
    kSelExtraNone = 0,
    kSelExtraHeldBack = 0x1,  // push a copy of the input candidate into the seed's extras (WSR_Outside or barrel Failed)
    kSelExtraStopNode = 0x2   // that copy gets a -2 (stopped) hit node on this layer (barrel Failed in Reg_Barrel)
  };

  struct PropState {
    float par[6];
    float err[21];  // packed lower triangle, MatriplexSym / CandState order
    int32_t fail;   // stock m_FailFlag after the inter-layer propagation
  };

  struct SelHits {
    int32_t hits[kMaxSelHits];  // original hit index within the layer (stock m_XHitArr), best ddphi first
    int8_t nHits;               // stock m_XHitSize
    int8_t wsr;                 // debug (diag): m_wsr AFTER handle_missed_layers (WSR_Failed -> WSR_Outside); else = wsrRaw
    int8_t wsrRaw;              // m_wsr as selectHitIndicesV2 left it (WSR_Failed kept)
    int8_t inGap;               // m_XWsrResult.m_in_gap
    uint8_t extra;              // SelExtraFlags, debug cross-check only (diag); the engine computes its own (H5)
  };

  // K2a -> K2b hand-off (GPU group scan, round 5): the stock Bins of the candidate and its preselection cuts
  struct SelScan {
    uint16_t q0, q1, q2, p1, p2;  // Bins q0/q1/q2/p1/p2 (bin_index_t)
    int8_t scan;                  // 1: the hit scan runs (not failed, not WSR_Outside)
    int8_t charge;
    float dqTrack;  // Bins dq_track
    float dphiCut;  // Bins dphi_track + DDPHI_PRESEL_FAC * 0.0123f
  };

  GENERATE_SOA_LAYOUT(SelListSoALayout,
                      SOA_COLUMN(int32_t, row),     // CandSlotsSoA row of the candidate
                      SOA_COLUMN(int16_t, layer),   // layer of the current plan step
                      SOA_COLUMN(int16_t, region),  // TrackerInfo::EtaRegion of the seed
                      SOA_SCALAR(int32_t, n))       // number of listed candidates

  GENERATE_SOA_LAYOUT(PropStateSoALayout, SOA_COLUMN(PropState, ps))
  GENERATE_SOA_LAYOUT(SelHitsSoALayout, SOA_COLUMN(SelHits, sel), SOA_COLUMN(SelScan, scan))

  using SelListSoA = SelListSoALayout<>;
  using PropStateSoA = PropStateSoALayout<>;
  using SelHitsSoA = SelHitsSoALayout<>;

}  // namespace mkfitdev

#endif
