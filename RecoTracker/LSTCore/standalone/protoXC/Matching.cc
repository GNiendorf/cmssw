#include "Matching.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <numeric>
#include <utility>

namespace proto {

  namespace {

    // Verbatim helper from trkCore.h: indices that sort vec descending.
    template <typename T>
    std::vector<size_t> sort_indices(const std::vector<T>& vec) {
      std::vector<size_t> indices(vec.size());
      std::iota(indices.begin(), indices.end(), 0);
      std::sort(indices.begin(), indices.end(), [&vec](size_t i1, size_t i2) { return vec[i1] > vec[i2]; });
      return indices;
    }

  }  // namespace

  std::tuple<std::vector<int>, std::vector<float>> matchedSimTrkIdxsAndFracs(
      std::vector<unsigned int> const& hitidxs,
      std::vector<HitType> const& hittypes,
      std::vector<int> const& trk_simhit_simTrkIdx,
      std::vector<std::vector<int>> const& trk_ph2_simHitIdx,
      std::vector<std::vector<int>> const& trk_pix_simHitIdx,
      bool verbose,
      float matchfrac,
      float* pmatched) {
    if (hitidxs.size() != hittypes.size()) {
      std::cout << "Error: matched_sim_trk_idxs()   hitidxs and hittypes have different lengths" << std::endl;
      std::cout << "hitidxs.size(): " << hitidxs.size() << std::endl;
      std::cout << "hittypes.size(): " << hittypes.size() << std::endl;
    }

    std::vector<std::pair<unsigned int, HitType>> to_check_duplicate;
    for (size_t i = 0; i < hitidxs.size(); ++i) {
      auto hitidx = hitidxs[i];
      auto hittype = hittypes[i];
      auto item = std::make_pair(hitidx, hittype);
      if (std::find(to_check_duplicate.begin(), to_check_duplicate.end(), item) == to_check_duplicate.end()) {
        to_check_duplicate.push_back(item);
      }
    }

    int nhits_input = to_check_duplicate.size();

    std::vector<std::vector<int>> simtrk_idxs;
    std::vector<int> unique_idxs;  // to aggregate which ones to count and test

    if (verbose) {
      std::cout << " '------------------------': "
                << "------------------------" << std::endl;
    }

    for (size_t ihit = 0; ihit < to_check_duplicate.size(); ++ihit) {
      auto ihitdata = to_check_duplicate[ihit];
      auto&& [hitidx, hittype] = ihitdata;

      if (verbose) {
        std::cout << " hitidx: " << hitidx << " hittype: " << static_cast<int>(hittype) << std::endl;
      }

      std::vector<int> simtrk_idxs_per_hit;

      const std::vector<std::vector<int>>* simHitIdxs =
          hittype == HitType::Phase2OT ? &trk_ph2_simHitIdx : &trk_pix_simHitIdx;

      if (verbose) {
        std::cout << " trk_ph2_simHitIdx.size(): " << trk_ph2_simHitIdx.size() << std::endl;
        std::cout << " trk_pix_simHitIdx.size(): " << trk_pix_simHitIdx.size() << std::endl;
      }

      if (static_cast<const unsigned int>((*simHitIdxs).size()) <= hitidx) {
        std::cout << "ERROR" << std::endl;
        std::cout << " hittype: " << static_cast<int>(hittype) << std::endl;
        std::cout << " trk_pix_simHitIdx.size(): " << trk_pix_simHitIdx.size() << std::endl;
        std::cout << " trk_ph2_simHitIdx.size(): " << trk_ph2_simHitIdx.size() << std::endl;
        std::cout << (*simHitIdxs).size() << " " << static_cast<int>(hittype) << std::endl;
        std::cout << hitidx << " " << static_cast<int>(hittype) << std::endl;
      }

      for (auto& simhit_idx : (*simHitIdxs).at(hitidx)) {
        if (static_cast<const int>(trk_simhit_simTrkIdx.size()) <= simhit_idx) {
          std::cout << (*simHitIdxs).size() << " " << static_cast<int>(hittype) << std::endl;
          std::cout << hitidx << " " << static_cast<int>(hittype) << std::endl;
          std::cout << trk_simhit_simTrkIdx.size() << " " << simhit_idx << std::endl;
        }
        int simtrk_idx = trk_simhit_simTrkIdx[simhit_idx];
        if (verbose) {
          std::cout << " hitidx: " << hitidx << " simhit_idx: " << simhit_idx << " simtrk_idx: " << simtrk_idx
                    << std::endl;
        }
        simtrk_idxs_per_hit.push_back(simtrk_idx);
        if (std::find(unique_idxs.begin(), unique_idxs.end(), simtrk_idx) == unique_idxs.end())
          unique_idxs.push_back(simtrk_idx);
      }

      if (simtrk_idxs_per_hit.size() == 0) {
        if (verbose) {
          std::cout << " hitidx: " << hitidx << " -1: " << -1 << std::endl;
        }
        simtrk_idxs_per_hit.push_back(-1);
        if (std::find(unique_idxs.begin(), unique_idxs.end(), -1) == unique_idxs.end())
          unique_idxs.push_back(-1);
      }

      simtrk_idxs.push_back(simtrk_idxs_per_hit);
    }

    if (verbose) {
      std::cout << " unique_idxs.size(): " << unique_idxs.size() << std::endl;
      for (auto& unique_idx : unique_idxs) {
        std::cout << " unique_idx: " << unique_idx << std::endl;
      }
    }

    // print
    if (verbose) {
      std::cout << "va print" << std::endl;
      for (auto& vec : simtrk_idxs) {
        for (auto& idx : vec) {
          std::cout << idx << " ";
        }
        std::cout << std::endl;
      }
      std::cout << "va print end" << std::endl;
    }

    // Compute all permutations
    std::function<void(std::vector<std::vector<int>>&, std::vector<int>, size_t, std::vector<std::vector<int>>&)> perm =
        [&](std::vector<std::vector<int>>& result,
            std::vector<int> intermediate,
            size_t n,
            std::vector<std::vector<int>>& va) {
          if (va.size() > n) {
            for (auto x : va[n]) {
              std::vector<int> copy_intermediate(intermediate);
              copy_intermediate.push_back(x);
              perm(result, copy_intermediate, n + 1, va);
            }
          } else {
            result.push_back(intermediate);
          }
        };

    std::vector<std::vector<int>> allperms;
    perm(allperms, std::vector<int>(), 0, simtrk_idxs);

    if (verbose) {
      std::cout << " allperms.size(): " << allperms.size() << std::endl;
      for (unsigned iperm = 0; iperm < allperms.size(); ++iperm) {
        std::cout << " allperms[iperm].size(): " << allperms[iperm].size() << std::endl;
        for (unsigned ielem = 0; ielem < allperms[iperm].size(); ++ielem) {
          std::cout << " allperms[iperm][ielem]: " << allperms[iperm][ielem] << std::endl;
        }
      }
    }
    int maxHitMatchCount = 0;  // ultimate maximum of the number of matched hits
    std::vector<int> matched_sim_trk_idxs;
    std::vector<float> matched_sim_trk_idxs_frac;
    float max_percent_matched = 0.0f;
    for (auto& trkidx_perm : allperms) {
      std::vector<int> counts;
      for (auto& unique_idx : unique_idxs) {
        int cnt = std::count(trkidx_perm.begin(), trkidx_perm.end(), unique_idx);
        counts.push_back(cnt);
      }
      auto result = std::max_element(counts.begin(), counts.end());
      int rawidx = std::distance(counts.begin(), result);
      int trkidx = unique_idxs[rawidx];
      if (trkidx < 0)
        continue;
      float percent_matched = static_cast<float>(counts[rawidx]) / nhits_input;
      if (verbose) {
        std::cout << " fr: " << percent_matched << std::endl;
      }
      if (percent_matched > matchfrac) {
        matched_sim_trk_idxs.push_back(trkidx);
        matched_sim_trk_idxs_frac.push_back(percent_matched);
      }
      maxHitMatchCount = std::max(maxHitMatchCount, *std::max_element(counts.begin(), counts.end()));
      max_percent_matched = std::max(max_percent_matched, percent_matched);
    }

    // If pmatched is provided, set its value
    if (pmatched != nullptr) {
      *pmatched = max_percent_matched;
    }

    std::map<int, float> pairs;
    unsigned size = matched_sim_trk_idxs.size();
    for (unsigned i = 0; i < size; ++i) {
      int idx = matched_sim_trk_idxs[i];
      float frac = matched_sim_trk_idxs_frac[i];
      if (pairs.find(idx) != pairs.end()) {
        if (pairs[idx] < frac)
          pairs[idx] = frac;
      } else {
        pairs[idx] = frac;
      }
    }
    std::vector<int> result;
    std::vector<float> result_frac;
    // Loop over the map using range-based for loop
    for (const auto& pair : pairs) {
      result.push_back(pair.first);
      result_frac.push_back(pair.second);
    }
    // Sort indices based on 'values'
    auto indices = sort_indices(result_frac);
    // Reorder 'vec1' and 'vec2' based on the sorted indices
    std::vector<int> sorted_result(result.size());
    std::vector<float> sorted_result_frac(result_frac.size());
    for (size_t i = 0; i < indices.size(); ++i) {
      sorted_result[i] = result[indices[i]];
      sorted_result_frac[i] = result_frac[indices[i]];
    }
    return std::make_tuple(sorted_result, sorted_result_frac);
  }

}  // namespace proto
