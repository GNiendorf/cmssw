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
    // pixel stub for pixel-less TCs: bit 0 T5 TCs, bit 1 T4 TCs, bit 2 the new OT-seeded TCs carry their cell, bit 3 a TC
    // whose stub is two hits of one unused pLS takes that pLS (becomes pT5-typed); 0 = off
    int attach;
    float attachChi2Cut;    // chi2 cut of the TC lookup (same variables and sigma table)
    float attachHiPt;       // GeV (anchor-circle pT): above it the TC lookup uses attachChi2CutHi
    float attachChi2CutHi;  // chi2 cut of the TC lookup above attachHiPt
    int attachPLSVeto;      // 1: cells sharing a hit with the pLS of a pLS/pT3/pT5 TC are not offered to TCs
  };
}  // namespace lst

#endif
