#ifndef RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaHpMlpKernels_h
#define RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaHpMlpKernels_h
// Stage C, HP option (b): the initial step's HP classifier (RecoTracker/FinalTrackSelectors model.pt) as a hand-written
// Alpaka MLP; weights folded from model.pt at construction (MkFitAlpakaHpClassifier.cc). See doc/stagec.txt.
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/FinalTrackSelectors/interface/TrackTorchClassifierFeaturesSoA.h"

namespace mkfitdev::hpsel {
  // the folded program, offsets into one float array on the device:
  //   [min(15)][den(15)] then per layer W^T[nIn][nOut] and b[nOut]
  struct MlpProgram {
    static constexpr int kNIn = 15;
    static constexpr int kMaxLayers = 16;
    static constexpr int kDense = 0, kResFirst = 2, kResSecond = 3, kOutput = 4;
    struct Layer {
      int nOut, nIn, kind, wOffset, bOffset;
    };
    int nLayers = 0;
    int minOffset = 0, denOffset = 15;
    Layer layers[kMaxLayers];
  };
  // TrackTorchClassifierFromSoA working point
  struct HpRule {
    float minScore, dxyThreshold, highDxyMinScore;
  };
  // kernel shape limits (checked by the producer against the folded program)
  constexpr int kMlpMaxWidth = 256;
  constexpr int kMlpSkipWidth = 32;
}  // namespace mkfitdev::hpsel

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel {
  // scores (always) and the HP decision as 0/1 floats (hpMask, optional: nullptr = scores only)
  void launchMlp(Queue& queue,
                 TrackTorchClassifierFeaturesSoA::ConstView in,
                 int n,
                 ::mkfitdev::hpsel::MlpProgram const& prog,
                 float const* w,
                 TrackTorchClassifierScoresSoA::View out,
                 ::mkfitdev::hpsel::HpRule const& rule,
                 float* hpMask);
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::hpsel

#endif
