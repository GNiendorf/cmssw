#ifndef RecoTracker_LSTCore_src_alpaka_Kernels_h
#define RecoTracker_LSTCore_src_alpaka_Kernels_h

#include <bit>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaMath/interface/deltaPhi.h"
#include "FWCore/Utilities/interface/CMSUnrollLoop.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/ObjectRangesSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/PixelSegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void rmPixelSegmentFromMemory(PixelSegments pixelSegments,
                                                               unsigned int pixelSegmentArrayIndex,
                                                               bool secondpass = false) {
    pixelSegments.isDup()[pixelSegmentArrayIndex] |= 1 + secondpass;
  }

  struct CheckHitspLS {
    ALPAKA_FN_ACC void operator()(Acc2D const& acc,
                                  ModulesConst modules,
                                  SegmentsOccupancyConst segmentsOccupancy,
                                  PixelSeedsConst pixelSeeds,
                                  PixelSegments pixelSegments,
                                  bool secondpass) const {
      int pixelModuleIndex = modules.nLowerModules();
      unsigned int nPixelSegments = segmentsOccupancy.nSegments()[pixelModuleIndex];

      if (nPixelSegments > n_max_pixel_segments_per_module)
        nPixelSegments = n_max_pixel_segments_per_module;

      for (unsigned int ix : cms::alpakatools::uniform_elements_y(acc, nPixelSegments)) {
        if (secondpass && (!pixelSeeds.isQuad()[ix] || (pixelSegments.isDup()[ix] & 1)))
          continue;

        auto const& phits1 = pixelSegments.pLSHitsIdxs()[ix];
        float eta_pix1 = pixelSeeds.eta()[ix];
        float phi_pix1 = pixelSeeds.phi()[ix];

        for (unsigned int jx : cms::alpakatools::uniform_elements_x(acc, ix + 1, nPixelSegments)) {
          float eta_pix2 = pixelSeeds.eta()[jx];
          float phi_pix2 = pixelSeeds.phi()[jx];

          if (alpaka::math::abs(acc, eta_pix2 - eta_pix1) > 0.1f)
            continue;

          if (secondpass && (!pixelSeeds.isQuad()[jx] || (pixelSegments.isDup()[jx] & 1)))
            continue;

          int8_t quad_diff = pixelSeeds.isQuad()[ix] - pixelSeeds.isQuad()[jx];
          float score_diff = pixelSegments.score()[ix] - pixelSegments.score()[jx];
          // Always keep quads over trips. If they are the same, we want the object with better score
          int idxToRemove;
          if (quad_diff > 0)
            idxToRemove = jx;
          else if (quad_diff < 0)
            idxToRemove = ix;
          else if (score_diff < 0)
            idxToRemove = jx;
          else if (score_diff > 0)
            idxToRemove = ix;
          else
            idxToRemove = ix;

          auto const& phits2 = pixelSegments.pLSHitsIdxs()[jx];

          int npMatched = 0;
          for (int i = 0; i < Params_pLS::kHits; i++) {
            bool pmatched = false;
            for (int j = 0; j < Params_pLS::kHits; j++) {
              if (phits1[i] == phits2[j]) {
                pmatched = true;
                break;
              }
            }
            if (pmatched) {
              npMatched++;
              // Only one hit is enough
              if (secondpass)
                break;
            }
          }
          const int minNHitsForDup_pLS = 3;
          if (npMatched >= minNHitsForDup_pLS) {
            rmPixelSegmentFromMemory(pixelSegments, idxToRemove, secondpass);
          }
          if (secondpass) {
            float dEta = alpaka::math::abs(acc, eta_pix1 - eta_pix2);
            float dPhi = cms::alpakatools::deltaPhi(acc, phi_pix1, phi_pix2);

            float dR2 = dEta * dEta + dPhi * dPhi;
            if ((npMatched >= 1) || (dR2 < 1e-5f)) {
              rmPixelSegmentFromMemory(pixelSegments, idxToRemove, secondpass);
            }
          }
        }
      }
    }
  };
}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst
#endif
