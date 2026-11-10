#ifndef RecoTracker_LSTCore_interface_PixelObjectOptions_h
#define RecoTracker_LSTCore_interface_PixelObjectOptions_h

namespace lst {
  // Options of the pT5 selection and ranking (plain data, passed to the kernels by value). The defaults are the
  // standard selection.
  struct PixelObjectOptions {
    // pT5 rz and outward rPhi chi2 cuts at every pLS pT, not only below the high-pT threshold
    bool pT5Chi2CutsAllPt = false;
    // pT5 ranking score, lower is better: 0 the rPhi chi2, 1 the log of rz x rPhi x inward rPhi chi2,
    // 2 the log of rz x rPhi chi2
    int pT5ScoreMode = 0;
    // > 0: the inward rPhi cut is applied at every T5 radius, with its threshold scaled by this factor above the
    // high-pT threshold; 0: below the high-pT threshold only
    float pT5RPhiInHiScale = 0.f;
    // >= 0: pT5 DNN working point above the high-pT threshold; < 0: the built-in one
    float pT5WpHigh = -1.f;
    // outward rPhi cut for pLS above the high-pT threshold: 0 unchanged, > 0 threshold x (pT / threshold)^p,
    // < 0 no cut
    float pT5RPhiOutHiPow = 0.f;
    // 3-hit pLSs become track candidates last, after every other candidate (the OT-seeded ones included), and only if
    // none of their hits is in a track candidate
    bool tripletpLSFillHoles = false;
  };
}  // namespace lst

#endif
