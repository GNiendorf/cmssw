#ifndef RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitESDataTestKernels_h
#define RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitESDataTestKernels_h

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESView.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev_estest {

  // Device-side use of the ES data, for MkFitESDataTester. All pointers are device memory.

  // outModule[i] = view.findModule(detid[i]); outShortId[i] = view.shortId(layer[i], detid[i])
  void launchDetIdLookups(Queue& queue,
                          mkfitdev::ESView view,
                          int n,
                          uint32_t const* detid,
                          int const* layer,
                          int* outModule,
                          int* outShortId);

  // view.material.materialChecked(z[i], r[i])
  void launchMaterialLookups(
      Queue& queue, mkfitdev::ESView view, int n, float const* z, float const* r, float* outBbxi, float* outRadl);

  // per probe i: out[4*i + 0] = wsr_z * 2 + in_gap_z, out[4*i + 1] = wsr_r * 2 + in_gap_r,
  // out[4*i + 2] = isWithinQLimits, out[4*i + 3] = isInRHole
  void launchLayerPredicates(
      Queue& queue, mkfitdev::ESView view, int n, int const* layer, float const* q, float const* dq, int* out);

  // Field-by-field reads of an ESConfig (device offsets vs host offsets): fills kNConfigFields floats.
  constexpr int kNConfigFields = 24;
  ALPAKA_FN_HOST_ACC inline void extractConfigFields(mkfitdev::ESConfig const& c, float* out) {
    int k = 0;
    out[k++] = c.params.chi2Cut_min;
    out[k++] = c.params.chi2CutOverlap;
    out[k++] = c.params.minPtCut;
    out[k++] = c.params.maxCandsPerSeed;
    out[k++] = c.params.maxConsecHoles;
    out[k++] = c.params.useHitSelectionV2 ? 1.f : 0.f;
    out[k++] = c.backward_params.maxClusterSize;
    out[k++] = c.backward_params.minHitsQF;
    out[k++] = c.dc_fracSharedHits;
    out[k++] = c.dc_drth_forward;
    out[k++] = c.sc_dzmax_el;
    out[k++] = c.n_regions;
    out[k++] = c.region_order[c.n_regions - 1];
    out[k++] = c.steering_params[c.n_regions - 1].n_plan;
    out[k++] = c.steering_params[c.n_regions - 1].layer[c.steering_params[c.n_regions - 1].n_plan - 1];
    out[k++] = c.steering_params[1].bkw_search_pickup;
    out[k++] = static_cast<int>(c.steering_params[2].track_scorer);
    out[k++] = static_cast<int>(c.seed_partitioner);
    out[k++] = static_cast<int>(c.duplicate_cleaner);
    out[k++] = c.prop_config.finding_intra_layer_pflags.apply_material ? 1.f : 0.f;
    out[k++] = c.usePtMultScat ? 1.f : 0.f;
    out[k++] = c.maxcth_fw;
    out[k++] = c.outer_barrel_layer;
    out[k++] = c.backward_fit_min_hits;
  }
  void launchConfigFields(Queue& queue, mkfitdev::ESView view, float* out);

  // copies *view.config byte by byte into out (sizeof(ESConfig) bytes)
  void launchConfigRead(Queue& queue, mkfitdev::ESView view, unsigned char* out);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev_estest

#endif
