// MkFitAlpakaReplayStockStages (harness lane): per-stage STOCK references for the port.
// Runs exactly mkfit::run_OneIteration (MkFitCMS/src/runFunctions.cc, the sequence MkFitProducer calls) with the stock
// MkFitCore library, and snapshots the candidates after every stage as an MkFitOutputWrapper (product instance names):
//   seeds       seeds after seed cleaning + seed_post_cleaning (+ hit sorting): the input of find_tracks_load_seeds
//   loaded      eocc front() of every CombCandidate after find_tracks_load_seeds: the seed table in stock order
//   fwd         all candidates of every CombCandidate after the forward search (findTracksCloneEngine)
//   fwdFiltered after the pre-backward-fit filter (filter_comb_cands)
//   bkfit       front() candidate of every CombCandidate after backwardFit
//   bkwsearch   all candidates after the backward search (findTracksCloneEngine(IT_BkwSearch)); hits = the backward
//               chain only (hot 0 = innermost forward hit, then the hits picked up inward), see snapshotBkw
//   postFilter  front() candidate after the post-backward-fit filter + endBkwSearch (only front() is repacked)
//   export      export_best_comb_cands (before duplicate removal)
//   (empty)     final output = what MkFitProducer puts (must equal hltInitialStepTrackCandidatesMkFit bit for bit)
// Each candidate is TrackCand::exportTrack(false): label = seed index, all hits incl. negative indices in hit-chain
// order, state/errors/chi2/score as stored at that stage. Compare a port stage with addMkFitTrackCompare.
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"

#include "RecoTracker/MkFit/interface/MkFitEventOfHits.h"
#include "RecoTracker/MkFit/interface/MkFitHitWrapper.h"
#include "RecoTracker/MkFit/interface/MkFitSeedWrapper.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitCMS/interface/MkStdSeqs.h"
#include "RecoTracker/MkFitCore/interface/IterationConfig.h"
#include "RecoTracker/MkFitCore/interface/MkBuilder.h"
#include "RecoTracker/MkFitCore/interface/MkBuilderWrapper.h"
#include "RecoTracker/MkFitCore/interface/MkJob.h"
#include "RecoTracker/MkFitCore/interface/TrackStructures.h"
#include "RecoTracker/MkFitCore/interface/TrackerInfo.h"

#include "oneapi/tbb/task_arena.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <iostream>
#include <functional>
#include <vector>

namespace {
  // copy of the file-local struct of MkFitCMS/src/runFunctions.cc
  struct StageMaskIfc : public mkfit::IterationMaskIfcBase {
    const mkfit::TrackerInfo &m_trk_info;
    const std::vector<const std::vector<bool> *> &m_mask_vector;
    StageMaskIfc(const mkfit::TrackerInfo &ti, const std::vector<const std::vector<bool> *> &maskvec)
        : m_trk_info(ti), m_mask_vector(maskvec) {}
    const std::vector<bool> *get_mask_for_layer(int layer) const override {
      return m_trk_info.layer(layer).is_pixel() ? m_mask_vector[0] : m_mask_vector[1];
    }
  };

  enum Stage { kSeeds, kLoaded, kFwd, kFwdFiltered, kBkFit, kBkwSearch, kPostFilter, kExport, kFinal, kNStages };
  constexpr const char *kStageNames[kNStages] = {
      "seeds", "loaded", "fwd", "fwdFiltered", "bkfit", "bkwsearch", "postFilter", "export", ""};

  mkfit::TrackVec snapshot(const mkfit::EventOfCombCandidates &eocc, bool allCands, const char *tag = "") {
    static const bool dbg = std::getenv("MKFA_STAGES_DEBUG") != nullptr;
    if (dbg)
      fprintf(stderr, "snapshot %s n_cc=%d\n", tag, eocc.size());
    mkfit::TrackVec out;
    for (int i = 0; i < eocc.size(); ++i) {
      const mkfit::CombCandidate &cc = eocc[i];
      const int n = allCands ? int(cc.size()) : std::min(1, int(cc.size()));
      for (int j = 0; j < n; ++j)
        out.emplace_back(cc[j].exportTrack(false));
    }
    return out;
  }

  // Backward-search representation: exportTrack() would walk past the start of the chain (hits before the backward
  // search are not linked). Export the backward chain only: hot 0 (innermost forward hit) first, then the hits picked
  // up inward, in pick-up order; state, chi2, score as stored in the candidate.
  mkfit::TrackVec snapshotBkw(const mkfit::EventOfCombCandidates &eocc) {
    mkfit::TrackVec out;
    std::vector<mkfit::HitOnTrack> chain;
    for (int i = 0; i < eocc.size(); ++i) {
      const mkfit::CombCandidate &cc = eocc[i];
      for (int j = 0; j < int(cc.size()); ++j) {
        const mkfit::TrackCand &tc = cc[j];
        chain.clear();
        int nFound = 0;
        for (int idx = tc.lastCcIndex(); idx >= 0 && chain.size() < 256; idx = cc.hot_node(idx).m_prev_idx) {
          chain.push_back(cc.hot_node(idx).m_hot);
          nFound += chain.back().index >= 0;
        }
        mkfit::Track res(static_cast<const mkfit::TrackBase &>(tc));
        res.resizeHits(chain.size(), nFound);
        for (int h = 0; h < int(chain.size()); ++h)
          res.setHitIdxAtPos(h, chain[chain.size() - 1 - h]);
        out.emplace_back(std::move(res));
      }
    }
    return out;
  }
}  // namespace

class MkFitAlpakaReplayStockStages : public edm::global::EDProducer<edm::StreamCache<mkfit::MkBuilderWrapper>> {
public:
  explicit MkFitAlpakaReplayStockStages(edm::ParameterSet const &cfg)
      : pixelHitsToken_{consumes(cfg.getParameter<edm::InputTag>("pixelHits"))},
        stripHitsToken_{consumes(cfg.getParameter<edm::InputTag>("stripHits"))},
        eventOfHitsToken_{consumes(cfg.getParameter<edm::InputTag>("eventOfHits"))},
        seedToken_{consumes(cfg.getParameter<edm::InputTag>("seeds"))},
        mkFitGeomToken_{esConsumes()},
        mkFitIterConfigToken_{esConsumes(cfg.getParameter<edm::ESInputTag>("config"))},
        seedCleaning_{cfg.getParameter<bool>("seedCleaning")},
        removeDuplicates_{cfg.getParameter<bool>("removeDuplicates")} {
    for (int s = 0; s < kNStages; ++s)
      putTokens_[s] = produces<MkFitOutputWrapper>(kStageNames[s]);
    mkfit::MkBuilderWrapper::populate();
  }

  static void fillDescriptions(edm::ConfigurationDescriptions &descriptions) {
    edm::ParameterSetDescription desc;
    desc.add("pixelHits", edm::InputTag("hltMkFitSiPixelHits"));
    desc.add("stripHits", edm::InputTag("hltMkFitSiPhase2Hits"));
    desc.add("eventOfHits", edm::InputTag("hltMkFitEventOfHits"));
    desc.add("seeds", edm::InputTag("hltInitialStepMkFitSeeds"));
    desc.add<edm::ESInputTag>("config", edm::ESInputTag("", "hltInitialStepTrackCandidatesMkFitConfig"));
    desc.add("seedCleaning", true);
    desc.add("removeDuplicates", true);
    descriptions.add("mkFitAlpakaReplayStockStages", desc);
  }

  std::unique_ptr<mkfit::MkBuilderWrapper> beginStream(edm::StreamID) const override {
    return std::make_unique<mkfit::MkBuilderWrapper>(true);
  }

private:
  void produce(edm::StreamID sid, edm::Event &ev, const edm::EventSetup &es) const override {
    const auto &pixelHits = ev.get(pixelHitsToken_);
    const auto &stripHits = ev.get(stripHitsToken_);
    const auto &eventOfHits = ev.get(eventOfHitsToken_);
    const auto &seeds = ev.get(seedToken_);
    std::array<mkfit::TrackVec, kNStages> snaps;
    if (!seeds.seeds().empty()) {
      const auto &geom = es.getData(mkFitGeomToken_);
      const auto &itconf = es.getData(mkFitIterConfigToken_);
      // the LST step has no clustersToSkip and no strip charge cut (phase 2): all-false masks as MkFitProducer
      std::vector<bool> pixelMask(pixelHits.hits().size(), false);
      std::vector<bool> stripMask(stripHits.hits().size(), false);
      std::vector<const std::vector<bool> *> masks{&pixelMask, &stripMask};
      auto seedsMutable = seeds.seeds();
      auto lambda = [&]() {
        run(geom.trackerInfo(), itconf, eventOfHits.get(), masks, streamCache(sid)->get(), seedsMutable, snaps);
      };
      tbb::this_task_arena::isolate(std::move(lambda));
    }
    nEvents_++;
    for (int s = 0; s < kNStages; ++s) {
      counts_[s] += snaps[s].size();
      ev.emplace(putTokens_[s], std::move(snaps[s]), true);
    }
  }

  void endJob() override {
    std::cout << "[stockStages] events " << nEvents_ << " candidates per stage:";
    for (int s = 0; s < kNStages; ++s)
      std::cout << " " << (kStageNames[s][0] ? kStageNames[s] : "final") << "=" << counts_[s];
    std::cout << std::endl;
  }

  // mkfit::run_OneIteration with do_backward_fit = true (backwardFitInCMSSW = false, as the menu), plus snapshots
  void run(const mkfit::TrackerInfo &trackerInfo,
           const mkfit::IterationConfig &itconf,
           const mkfit::EventOfHits &eoh,
           const std::vector<const std::vector<bool> *> &hit_masks,
           mkfit::MkBuilder &builder,
           mkfit::TrackVec &seeds,
           std::array<mkfit::TrackVec, kNStages> &snaps) const {
    using namespace mkfit;
    StageMaskIfc it_mask_ifc(trackerInfo, hit_masks);
    MkJob job({trackerInfo, itconf, eoh, eoh.refBeamSpot(), &it_mask_ifc});
    builder.begin_event(&job, nullptr, "run_OneIteration");

    const bool do_seed_clean = seedCleaning_ && itconf.m_seed_cleaner;
    if (do_seed_clean)
      itconf.m_seed_cleaner(seeds, itconf, eoh.refBeamSpot());
    builder.seed_post_cleaning(seeds);
    if (itconf.m_requires_seed_hit_sorting) {
      for (auto &s : seeds)
        s.sortHitsByLayer();
    }
    snaps[kSeeds] = seeds;

    builder.find_tracks_load_seeds(seeds, do_seed_clean);
    snaps[kLoaded] = snapshot(builder.ref_eocc(), false, "loaded");

    builder.findTracksCloneEngine();
    snaps[kFwd] = snapshot(builder.ref_eocc(), true, "fwd");

    filter_candidates_func pre_filter;
    if (itconf.m_pre_bkfit_filter)
      pre_filter = [&](const TrackCand &tc, const MkJob &jb) -> bool {
        return itconf.m_pre_bkfit_filter(tc, jb) && StdSeq::qfilter_nan_n_silly<TrackCand>(tc, jb);
      };
    else
      pre_filter = StdSeq::qfilter_nan_n_silly<TrackCand>;
    builder.filter_comb_cands(pre_filter, true);
    snaps[kFwdFiltered] = snapshot(builder.ref_eocc(), true, "fwdFiltered");

    job.switch_to_backward();

    if (itconf.m_backward_search)
      builder.compactifyHitStorageForBestCand(itconf.m_backward_drop_seed_hits, itconf.m_backward_fit_min_hits);
    builder.backwardFit();
    snaps[kBkFit] = snapshot(builder.ref_eocc(), false, "bkfit");

    if (itconf.m_backward_search) {
      builder.beginBkwSearch();
      builder.findTracksCloneEngine(SteeringParams::IT_BkwSearch);
      builder.gateBkwSearch();  // cms-sw#52015 (no-op unless backwardSearchMinPixelLayers > 0)
      snaps[kBkwSearch] = snapshotBkw(builder.ref_eocc());
    }

    filter_candidates_func post_filter;
    if (itconf.m_post_bkfit_filter)
      post_filter = [&](const TrackCand &tc, const MkJob &jb) -> bool {
        return itconf.m_post_bkfit_filter(tc, jb) && StdSeq::qfilter_nan_n_silly<TrackCand>(tc, jb);
      };
    else
      post_filter = StdSeq::qfilter_nan_n_silly<TrackCand>;
    builder.filter_comb_cands(post_filter, true);

    if (itconf.m_backward_search)
      builder.endBkwSearch();
    snaps[kPostFilter] = snapshot(builder.ref_eocc(), false, "postFilter");  // only front() is repacked

    TrackVec out_tracks;
    builder.export_best_comb_cands(out_tracks, true);
    snaps[kExport] = out_tracks;

    if (removeDuplicates_ && itconf.m_duplicate_cleaner)
      itconf.m_duplicate_cleaner(out_tracks, itconf, trackerInfo);

    builder.export_tracks(out_tracks);
    snaps[kFinal] = std::move(out_tracks);

    builder.end_event();
    builder.release_memory();
  }

  const edm::EDGetTokenT<MkFitHitWrapper> pixelHitsToken_;
  const edm::EDGetTokenT<MkFitHitWrapper> stripHitsToken_;
  const edm::EDGetTokenT<MkFitEventOfHits> eventOfHitsToken_;
  const edm::EDGetTokenT<MkFitSeedWrapper> seedToken_;
  const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
  const edm::ESGetToken<mkfit::IterationConfig, TrackerRecoGeometryRecord> mkFitIterConfigToken_;
  std::array<edm::EDPutTokenT<MkFitOutputWrapper>, kNStages> putTokens_;
  const bool seedCleaning_;
  const bool removeDuplicates_;
  mutable std::atomic<long> nEvents_{0};
  mutable std::array<std::atomic<long>, kNStages> counts_{};
};

DEFINE_FWK_MODULE(MkFitAlpakaReplayStockStages);
