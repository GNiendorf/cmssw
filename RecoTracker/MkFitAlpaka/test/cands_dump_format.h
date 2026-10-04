#ifndef RecoTracker_MkFitAlpaka_test_cands_dump_format_h
#define RecoTracker_MkFitAlpaka_test_cands_dump_format_h

// Binary record format written by the instrumented PRIVATE copy of stock MkFitCore
// (CandCloner::processSeedRange, lane_cands/stockdump) and read by testCandsSelection.
// One record per seed and layer step. Host-only POD, little endian, no padding surprises
// (all members are 4-byte types).

#include <cstdint>

namespace mkfitdump {

  constexpr uint32_t kRecordMagic = 0x31444343u;  // "CCD1"

  struct Cand {  // the TrackCand fields the per-seed selection reads or writes
    float score;
    float chi2;
    float pt;
    int32_t lastHitIdx;
    int32_t nFound;
    int32_t nMissing;
    int32_t nOverlap;
    int32_t nInsideMinusOne;
    int32_t nTailMinusOne;
    int32_t originIndex;
    int32_t label;
    int32_t hasCombCand;  // TrackCand::combCandidate() != nullptr (best-short validity)
    int32_t ovHit[2];     // HitMatchPair M[0..1]
    int32_t ovModule[2];
    float ovChi2[2];
  };

  struct Option {  // stock IdxChi2List, same member order
    uint32_t module;
    int32_t hitIdx;
    int32_t trkIdx;
    int32_t nhits;
    int32_t ntailholes;
    int32_t noverlaps;
    int32_t nholes;
    float pt;
    float chi2;
    float chi2_hit;
    float score;
  };

  struct HoT {
    int32_t index;
    int32_t layer;
    float chi2;
    int32_t prev;
  };

  struct Update {
    int32_t cand_idx;
    int32_t hit_idx;
    int32_t ovlp_idx;
  };

  struct Header {
    uint32_t magic;
    int32_t layer;
    int32_t seedIdx;  // absolute seed index in EventOfCombCandidates
    int32_t state;    // CombCandidate::SeedState_e before processing
    int32_t bkwRep;   // EventOfCombCandidates::cands_in_backward_rep() (1 = backward search)
    int32_t maxCandsPerSeed;
    float pTCutOverlap;
    int32_t recheckOverlap;
    int32_t nHotsIn;  // CombCandidate::hotsSize() before processing
  };
  // Record layout:
  //   Header
  //   int32 nCandsIn,  Cand[nCandsIn]
  //   int32 nExtras,   Cand[nExtras]
  //   Cand bestShortIn
  //   int32 nOpts,     Option[nOpts]      (stock input order, before std::sort)
  //   int32 nSorted,   Option[nSorted]    (after std::sort, nSorted == nOpts)
  //   int32 nCandsOut, Cand[nCandsOut]
  //   Cand bestShortOut
  //   int32 nUpd,      Update[nUpd]       (entries this seed appended to the kalman update list)
  //   int32 nOvl,      Update[nOvl]       (entries appended to the overlap re-check list)
  //   int32 nHotsNew,  HoT[nHotsNew]      (nodes appended to the seed's HoT vector, indices nHotsIn..)

}  // namespace mkfitdump

#endif
