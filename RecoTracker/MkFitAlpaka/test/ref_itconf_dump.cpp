// lane ref (round 6), NOT built by scram: g++ -std=c++20 -O0 -I$CMSSW_BASE/src -I$CMSSW_RELEASE_BASE/src -I<json> ref_itconf_dump.cpp
//   -L<lib dir> -Wl,--no-as-needed -lRecoTrackerMkFitCMS -lRecoTrackerMkFitCore; run with LD_LIBRARY_PATH=<MkFitCore build>: doc/ref.txt.
// itconf_dump <json> <minPt> <maxClusterSize>: parse the LST-step IterationConfig exactly as MkFitIterationConfigESProducer
// does (ConfigJson::load_File, minPtCut/maxClusterSize override, setupStandardFunctionsFromNames) and print every field;
// floats as hexfloat (bit-exact). Compiled once; run against two libRecoTrackerMkFitCore.so builds (LD_LIBRARY_PATH).
#include "RecoTracker/MkFitCore/interface/IterationConfig.h"
#include "RecoTracker/MkFitCore/interface/SteeringParams.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace mkfit;
static void F(const char* n, float v) { std::printf("%s %a\n", n, (double)v); }
static void I(const char* n, long v) { std::printf("%s %ld\n", n, v); }
static void S(const char* n, const std::string& v) { std::printf("%s '%s'\n", n, v.c_str()); }
static void P(const char* pre, const IterationParams& p) {
  char b[128];
#define PI(x) std::snprintf(b, sizeof b, "%s.%s", pre, #x), I(b, (long)p.x)
#define PF(x) std::snprintf(b, sizeof b, "%s.%s", pre, #x), F(b, p.x)
  PI(nlayers_per_seed); PI(maxCandsPerSeed); PI(maxHolesPerCand); PI(maxConsecHoles); PF(chi2Cut_min); PF(chi2CutOverlap);
  PF(pTCutOverlap); PI(recheckOverlap); PI(useHitSelectionV2); PI(minHitsQF); PF(minPtCut); PI(maxClusterSize);
  // raw bytes too (catches fields added later)
  const unsigned char* c = reinterpret_cast<const unsigned char*>(&p);
  std::printf("%s.bytes ", pre); for (size_t i = 0; i < sizeof p; ++i) std::printf("%02x", c[i]); std::printf("\n");
}
int main(int argc, char** argv) {
  if (argc < 4) { std::fprintf(stderr, "usage: itconf_dump <json> <minPt> <maxClusterSize>\n"); return 1; }
  ConfigJson cj;
  auto it = cj.load_File(argv[1]);
  it->m_params.minPtCut = (float)std::atof(argv[2]);   // the ES producer stores a double PSet value into float
  it->m_backward_params.minPtCut = (float)std::atof(argv[2]);
  it->m_params.maxClusterSize = std::atoi(argv[3]);
  it->m_backward_params.maxClusterSize = std::atoi(argv[3]);
  it->setupStandardFunctionsFromNames();
  I("m_iteration_index", it->m_iteration_index); I("m_track_algorithm", it->m_track_algorithm);
  I("m_requires_seed_hit_sorting", it->m_requires_seed_hit_sorting); I("m_backward_search", it->m_backward_search);
  I("m_backward_drop_seed_hits", it->m_backward_drop_seed_hits);
  F("sc_ptthr_hpt", it->sc_ptthr_hpt); F("sc_drmax_bh", it->sc_drmax_bh); F("sc_dzmax_bh", it->sc_dzmax_bh);
  F("sc_drmax_eh", it->sc_drmax_eh); F("sc_dzmax_eh", it->sc_dzmax_eh); F("sc_drmax_bl", it->sc_drmax_bl);
  F("sc_dzmax_bl", it->sc_dzmax_bl); F("sc_drmax_el", it->sc_drmax_el); F("sc_dzmax_el", it->sc_dzmax_el);
  F("dc_fracSharedHits", it->dc_fracSharedHits); F("dc_drth_central", it->dc_drth_central);
  F("dc_drth_obarrel", it->dc_drth_obarrel); F("dc_drth_forward", it->dc_drth_forward);
  P("m_params", it->m_params); P("m_backward_params", it->m_backward_params);
  I("m_n_regions", it->m_n_regions);
  for (size_t i = 0; i < it->m_region_order.size(); ++i) std::printf("m_region_order[%zu] %d\n", i, it->m_region_order[i]);
  for (size_t r = 0; r < it->m_steering_params.size(); ++r) {
    const auto& sp = it->m_steering_params[r];
    std::printf("steer[%zu] region %d fwd_pickup %d bkw_fit_last %d bkw_search_pickup %d scorer '%s' scorer_set %d plan",
                r, sp.m_region, sp.m_fwd_search_pickup, sp.m_bkw_fit_last, sp.m_bkw_search_pickup,
                sp.m_track_scorer_name.c_str(), (int)(bool)sp.m_track_scorer);
    for (auto& lc : sp.m_layer_plan) std::printf(" %d", lc.m_layer);
    std::printf("\n");
  }
  for (size_t l = 0; l < it->m_layer_configs.size(); ++l) {
    const auto& lc = it->m_layer_configs[l];
    std::printf("layer[%zu] m_layer %d dphi %a %a dq %a %a fwd", l, lc.m_layer, (double)lc.m_select_min_dphi,
                (double)lc.m_select_max_dphi, (double)lc.m_select_min_dq, (double)lc.m_select_max_dq);
    for (float v : lc.m_winpars_fwd) std::printf(" %a", (double)v);
    std::printf(" bkw");
    for (float v : lc.m_winpars_bkw) std::printf(" %a", (double)v);
    std::printf("\n");
  }
  S("m_seed_cleaner_name", it->m_seed_cleaner_name); S("m_seed_partitioner_name", it->m_seed_partitioner_name);
  S("m_pre_bkfit_filter_name", it->m_pre_bkfit_filter_name); S("m_post_bkfit_filter_name", it->m_post_bkfit_filter_name);
  S("m_duplicate_cleaner_name", it->m_duplicate_cleaner_name); S("m_default_track_scorer_name", it->m_default_track_scorer_name);
  I("funcs_set", ((bool)it->m_seed_cleaner) | ((bool)it->m_seed_partitioner << 1) | ((bool)it->m_pre_bkfit_filter << 2) |
                     ((bool)it->m_post_bkfit_filter << 3) | ((bool)it->m_duplicate_cleaner << 4) | ((bool)it->m_default_track_scorer << 5));
  // the library's own JSON serialisation (nlohmann, shortest round-trip floats) as a second, independent view
  IterationsInfo ii; ii.m_iterations.push_back(*it);
  std::fflush(stdout);
  cj.dump(ii);
  return 0;
}
