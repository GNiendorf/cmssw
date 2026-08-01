#ifndef PROTOTYPE_MATCHING_H
#define PROTOTYPE_MATCHING_H

// Verbatim port of the production sim-matching (trkCore.cc matchedSimTrkIdxsAndFracs,
// ~200 self-contained lines) so the prototype's TC labels reproduce production
// definitions bit-for-bit (plan 10.4). Do NOT "improve" the algorithm here; any
// intentional deviation breaks comparability with the baseline harness.

#include <tuple>
#include <vector>

namespace proto {

// Mirror of lst::HitType (interface/Common.h). Values must match the production enum
// because see_hitType stores them numerically.
enum class HitType : short { Pixel = 0, Phase2OT = 4 };  // implementer: verify values against LSTCore and fix if they differ

// Returns (matched full-sim rows sorted by fraction desc, fractions), keeping matches with
// fraction strictly > matchfrac. pmatched (optional) receives the max fraction regardless
// of threshold.
std::tuple<std::vector<int>, std::vector<float>> matchedSimTrkIdxsAndFracs(
    std::vector<unsigned int> const& hitidxs,
    std::vector<HitType> const& hittypes,
    std::vector<int> const& trk_simhit_simTrkIdx,
    std::vector<std::vector<int>> const& trk_ph2_simHitIdx,
    std::vector<std::vector<int>> const& trk_pix_simHitIdx,
    bool verbose = false,
    float matchfrac = 0.75,
    float* pmatched = nullptr);

}  // namespace proto

#endif
