#ifndef RecoTracker_LSTCore_interface_OTPull_h
#define RecoTracker_LSTCore_interface_OTPull_h

namespace lst {
  // Decision constants of the OT-seeded cell lookup (plain data, passed to the kernels by value).
  struct OTPullParams {
    static constexpr int kNVars =
        5;  // outer-hit circle residual, outer-hit rz residual, direction, rz slope, joint rms
    static constexpr int kNPtBins = 5;
    float sigma[kNVars][kNPtBins];
    float ptEdges[kNPtBins - 1];
    float chi2Cut;
    float phiWindow;
    int nPhiBins;
    int nShells;       // radial shells of the cell outer hit (bin = shell * nPhiBins + phi bin)
    float shellWidth;  // cm, shells start at r = 0
    int plsMatch;  // cells sharing a pixel hit with a pLS are not offered: 0 none, 1 pLS last hit, 2 all stored pLS hits
    unsigned int capacity;  // extra track candidate slots
  };
}  // namespace lst

#endif
