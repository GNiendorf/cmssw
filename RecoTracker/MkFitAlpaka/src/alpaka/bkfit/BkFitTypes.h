#ifndef RecoTracker_MkFitAlpaka_src_alpaka_bkfit_BkFitTypes_h
#define RecoTracker_MkFitAlpaka_src_alpaka_bkfit_BkFitTypes_h

// Flat (self-contained) backward-fit input/output records: one per best candidate, the compacted HoT chain with the
// hit and module data already resolved. Used by the stock-replay test (stock dumps of MkBuilder::fit_cands) and by
// anyone who wants to run the backward fit without the device EventOfHits / ES. The production path reads the
// clone-engine SoAs directly (BkFitLaunch.h: backwardFit).

#include <cstdint>

namespace mkfitdev::bkfit {

  // cms-sw#52015 backward-fit outlier rejection (IterationConfig m_backward_fit_outlier_chi2 / max_outliers /
  // outlier_min_pt): chi2 <= 0 = off.
  struct OutlierParams {
    float chi2 = 0.f;
    int maxOutliers = 0;
    float minPt = 0.f;
  };

  // Best candidate (eoccs[i][0]) at the entry of the backward fit.
  struct FlatCand {
    float par[6];
    float err[21];  // SMatrixSym66 / MatriplexSym packed order
    int32_t charge;
    int32_t lastNode;   // TrackCand::lastCcIndex(), index into this cand's node range
    int32_t nodeBegin;  // first node row of this cand in the node array
    int32_t nNodes;
    // TrackCand counters used by getScoreCand (phase1:default scorer)
    int16_t nFound;
    int16_t nMissing;
    int16_t nOverlap;
    int16_t nInsideMinusOne;
    int16_t nTailMinusOne;
    int16_t pad;
  };

  // One HoT node (stock HoTNode: m_hot.index, m_hot.layer, m_prev_idx) with its hit (Hit::posArray/errArray) and
  // module (ModuleInfo pos/zdir/xdir) resolved. Hit/module data are only meaningful for index >= 0.
  struct FlatNode {
    int32_t index;
    int32_t layer;
    int32_t prev;  // relative to the cand's nodeBegin, -1 = none
    float msPar[3];
    float msErr[6];
    float pnt[3];
    float nrm[3];
    float dir[3];
  };

  // What the backward fit writes back into the TrackCand (MkFinder::bkFitFitTracksProp2Plane copy-out).
  struct FlatOut {
    float par[6];
    float err[21];
    int32_t charge;
    float chi2;
    float score;
  };

}  // namespace mkfitdev::bkfit

#endif
