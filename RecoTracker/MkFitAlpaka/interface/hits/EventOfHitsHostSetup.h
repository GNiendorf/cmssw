#ifndef RecoTracker_MkFitAlpaka_interface_hits_EventOfHitsHostSetup_h
#define RecoTracker_MkFitAlpaka_interface_hits_EventOfHitsHostSetup_h

// Host-only helpers: layer table from the stock geometry (TrackerInfo) and HitSoA from the stock MkFitHitWrapper hits.

#include <type_traits>
#include <vector>

#include "RecoTracker/MkFitCore/interface/HitStructures.h"
#include "RecoTracker/MkFitCore/interface/TrackerInfo.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/LayerAxes.h"

namespace mkfitdev {

  static_assert(std::is_same_v<AxisPhi, mkfit::LayerOfHits::axis_phi_t>);
  static_assert(std::is_same_v<AxisQ, mkfit::LayerOfHits::axis_eta_t>);

  inline std::vector<LayerAxisInput> layerAxisInputs(const mkfit::TrackerInfo& ti) {
    std::vector<LayerAxisInput> v;
    v.reserve(ti.n_layers());
    for (int il = 0; il < ti.n_layers(); ++il) {
      mkfit::LayerOfHits::Initializator ini(ti.layer(il));
      v.push_back({ini.m_qmin, ini.m_qmax, ini.m_nq, ti.layer(il).is_barrel(), ti.layer(il).is_pixel()});
    }
    return v;
  }

  // Raw stock PackedData bits of a hit, rebuilt from its public accessors.
  inline uint32_t packedOf(const mkfit::Hit& h) {
    const uint32_t cpcm = h.chargePerCM();
    const uint32_t chargeRaw = cpcm == 0 ? 0 : ((cpcm - mkfit::Hit::minChargePerCM()) >> 3) + 1;
    return hitpack::pack(h.detIDinLayer(), chargeRaw, h.spanRows() - 1, h.spanCols() - 1);
  }

  // Fill one wrapper's hits into rows [rowBegin, rowBegin + hits.size()). One pass over the stock AoS hits writing the
  // SoA columns through raw column pointers (the SoA element proxies cost a range check per column access).
  inline void fillHits(const mkfit::HitVec& hits, const std::vector<int>& layerOfHit, uint32_t rowBegin, HitSoA::View v) {
    auto m = v.metadata();
    float* __restrict__ x = m.addressOf_x() + rowBegin;
    float* __restrict__ y = m.addressOf_y() + rowBegin;
    float* __restrict__ z = m.addressOf_z() + rowBegin;
    float* __restrict__ e00 = m.addressOf_e00() + rowBegin;
    float* __restrict__ e10 = m.addressOf_e10() + rowBegin;
    float* __restrict__ e11 = m.addressOf_e11() + rowBegin;
    float* __restrict__ e20 = m.addressOf_e20() + rowBegin;
    float* __restrict__ e21 = m.addressOf_e21() + rowBegin;
    float* __restrict__ e22 = m.addressOf_e22() + rowBegin;
    uint32_t* __restrict__ packed = m.addressOf_packed() + rowBegin;
    int32_t* __restrict__ layer = m.addressOf_layer() + rowBegin;
    uint32_t* __restrict__ orig = m.addressOf_origIdx() + rowBegin;
    const uint32_t n = hits.size();
    const uint32_t nl = layerOfHit.size() < n ? layerOfHit.size() : n;
    const mkfit::Hit* __restrict__ h = hits.data();
    for (uint32_t i = 0; i < n; ++i) {
      const float* p = h[i].posArray();
      const float* e = h[i].errArray();
      x[i] = p[0];
      y[i] = p[1];
      z[i] = p[2];
      e00[i] = e[0];
      e10[i] = e[1];
      e11[i] = e[2];
      e20[i] = e[3];
      e21[i] = e[4];
      e22[i] = e[5];
      packed[i] = packedOf(h[i]);
      orig[i] = i;
    }
    for (uint32_t i = 0; i < nl; ++i)
      layer[i] = layerOfHit[i];
    for (uint32_t i = nl; i < n; ++i)
      layer[i] = -1;
  }

}  // namespace mkfitdev

#endif
