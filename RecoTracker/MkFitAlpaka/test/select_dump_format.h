#ifndef RecoTracker_MkFitAlpaka_test_select_dump_format_h
#define RecoTracker_MkFitAlpaka_test_select_dump_format_h

// Binary record of one stock MkFinder::selectHitIndicesV2 slot (lane select, round 3). Written by a PRIVATE
// instrumented copy of stock MkFitCore (r3_select/CMSSW/src/RecoTracker/MkFitCore, env MKFA_SEL_DUMP_DIR), one file
// per event (sel_<seq>.bin, seq = 0-based mkFit begin_event count of the job), read by the select stock check.
// Plain C++ only (included by stock code and by the check).

#include <cstdint>

namespace mkfitdev::seldump {

  constexpr int kMaxSel = 6;
  constexpr uint32_t kMagic = 0x53454C32u;  // "SEL2"

  struct Record {
    int32_t layer, region, inFwd, label, seedIdx, candIdx, chg, maskSet;
    int32_t nProc, slot;            // NN chunk size and slot of this record (diagnostics)
    float parC[6], errC[21];        // candidate state fed to the inter-layer propagation (m_Par/m_Err[iC])
    int32_t failFlag;               // m_FailFlag after the propagation (input of the selection)
    float parP[6], errP[21];        // propagated state (m_Par/m_Err[iP])
    float qc, dqTrack, dphiTrack;   // Bins: q_c, dq_track, dphi_track
    int32_t q0, q1, q2, p1, p2;     // Bins: bin ranges
    int32_t wsr, inGap;             // m_XWsrResult at the end of selectHitIndicesV2 (before handle_missed_layers)
    int32_t nHits;                  // m_XHitSize
    int32_t hits[kMaxSel];          // m_XHitArr in stock order (original hit index in the layer)
    float sp1[3], sp2[3];           // Bins::sp1 / sp2 positions (prop_to_limits)
    float qmin, qmax;               // Bins::qmin / qmax
  };

}  // namespace mkfitdev::seldump

#endif
