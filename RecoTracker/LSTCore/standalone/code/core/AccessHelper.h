#ifndef AccessHelper_h
#define AccessHelper_h

// Navigation helpers for the standalone harness: given an index into one of LSTEvent's collections,
// walk down the composition hierarchy (TC -> ... -> LS -> MD -> hit) and return the constituents.
//
// The getHitIdxs* functions return GLOBAL tracking-ntuple hit rows, not the event-local hit indices
// the SoAs store, because that is the space the truth matching in trkCore works in. The paired
// HitType says which ntuple branch family a row indexes (pix_* or ph2_*).

#include <vector>
#include <tuple>
#include "LSTEvent.h"

using LSTEvent = ALPAKA_ACCELERATOR_NAMESPACE::lst::LSTEvent;

// ----* Hit *----
std::tuple<std::vector<unsigned int>, std::vector<lst::HitType>> convertHitsToHitIdxsAndHitTypes(
    LSTEvent* event, std::vector<unsigned int> hits);

// ----* pLS *----
std::vector<unsigned int> getHitsFrompLS(LSTEvent* event, unsigned int pLS);
std::vector<unsigned int> getHitIdxsFrompLS(LSTEvent* event, unsigned int pLS);
std::vector<lst::HitType> getHitTypesFrompLS(LSTEvent* event, unsigned int pLS);
std::tuple<std::vector<unsigned int>, std::vector<lst::HitType>> getHitIdxsAndHitTypesFrompLS(LSTEvent* event,
                                                                                              unsigned pLS);

// ----* MD *----
std::vector<unsigned int> getHitsFromMD(LSTEvent* event, unsigned int MD);
std::tuple<std::vector<unsigned int>, std::vector<lst::HitType>> getHitIdxsAndHitTypesFromMD(LSTEvent* event,
                                                                                             unsigned MD);

// ----* LS *----
std::vector<unsigned int> getMDsFromLS(LSTEvent* event, unsigned int LS);
std::vector<unsigned int> getHitsFromLS(LSTEvent* event, unsigned int LS);
std::tuple<std::vector<unsigned int>, std::vector<lst::HitType>> getHitIdxsAndHitTypesFromLS(LSTEvent* event,
                                                                                             unsigned LS);

// ----* T3 *----
std::vector<unsigned int> getLSsFromT3(LSTEvent* event, unsigned int T3);
std::vector<unsigned int> getMDsFromT3(LSTEvent* event, unsigned int T3);
std::vector<unsigned int> getHitsFromT3(LSTEvent* event, unsigned int T3);
std::vector<lst::HitType> getHitTypesFromT3(LSTEvent* event, unsigned int T3);
std::vector<unsigned int> getModuleIdxsFromT3(LSTEvent* event, unsigned int T3);
std::tuple<std::vector<unsigned int>, std::vector<lst::HitType>> getHitIdxsAndHitTypesFromT3(LSTEvent* event,
                                                                                             unsigned T3);

// ----* TC *----
std::pair<std::vector<unsigned int>, std::vector<lst::HitType>> getHitIdxsAndHitTypesFromTC(LSTEvent* event,
                                                                                            unsigned int tc_idx);

#endif
