// Portable seed import of the mkFit LST step: stock MkFitSeedWrapper (+ host hit wrappers for the last-hit position)
// -> device seed table -> seed_post_cleaning, phase2:1 partition, binnor rank, region-major import order ->
// initial CombCandidate rows (cands SoA). Then the best-candidate export of those rows -> TrackSoA.
//
// Round 3: the device products are not put into the Event yet (product dictionaries are integ's). With validate =
// True the module copies everything back and checks it per event against STOCK: MkBuilder::seed_post_cleaning +
// find_tracks_load_seeds run on the same seeds in the same job (stock MkFitCore, the production code path of
// run_OneIteration), and TrackCand::exportTrack(true) of every imported candidate. One summary line per event.
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoTracker/MkFit/interface/MkFitEventOfHits.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFit/interface/MkFitHitWrapper.h"
#include "RecoTracker/MkFit/interface/MkFitSeedWrapper.h"
#include "RecoTracker/MkFitCMS/interface/MkStdSeqs.h"
#include "RecoTracker/MkFitCore/interface/HitStructures.h"
#include "RecoTracker/MkFitCore/interface/IterationConfig.h"
#include "RecoTracker/MkFitCore/interface/MkBuilder.h"
#include "RecoTracker/MkFitCore/interface/MkJob.h"
#include "RecoTracker/MkFitCore/interface/TrackStructures.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitAlpaka/interface/CandStoreProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/CandStoreProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/EventOfHitsProduct.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/TrackProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/SeedsHostPack.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsAlgo.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsPackDevice.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaSeedImportProducer : public global::EDProducer<> {
  public:
    explicit MkFitAlpakaSeedImportProducer(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          seedsToken_{consumes(iConfig.getParameter<edm::InputTag>("seeds"))},
          pixelHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
          stripHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("stripHits"))},
          mkFitGeomToken_{esConsumes()},
          candStoreToken_{produces()},
          tracksToken_{produces()},
          validate_{iConfig.getParameter<bool>("validate")},
          hotsPerSeed_{iConfig.getParameter<int>("hotsPerSeed")},
          injectSillyEvery_{iConfig.getParameter<int>("injectSillyEvery")},
          runTail_{iConfig.getParameter<bool>("runTail")} {
      iterConfigToken_ = esConsumes(iConfig.getParameter<edm::ESInputTag>("config"));
      if (validate_)
        eohToken_ = consumes(iConfig.getParameter<edm::InputTag>("eventOfHits"));
      const auto devEoh = iConfig.getParameter<edm::InputTag>("deviceEventOfHits");
      if (!devEoh.label().empty()) {
        devEohToken_ = consumes(devEoh);
        useDeviceHits_ = true;
      }
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add("seeds", edm::InputTag{"hltInitialStepMkFitSeeds"});
      desc.add("pixelHits", edm::InputTag{"hltMkFitSiPixelHits"});
      desc.add("stripHits", edm::InputTag{"hltMkFitSiPhase2Hits"});
      desc.add("eventOfHits", edm::InputTag{"hltMkFitEventOfHits"})->setComment("stock EventOfHits (validation only)");
      desc.add("deviceEventOfHits", edm::InputTag{""})
          ->setComment("device EventOfHits product for the last-hit positions (empty: host hit wrappers)");
      desc.add("config", edm::ESInputTag{"", "hltInitialStepTrackCandidatesMkFitConfig"});
      desc.add("validate", true);
      desc.add("hotsPerSeed", 64);
      desc.add("runTail", true)
          ->setComment(
              "test only: also run the chain tail on the imported rows (validated with validate = True); "
              "False puts the plain export as the TrackSoA product");
      desc.add("injectSillyEvery", 0)
          ->setComment(
              "test only: k > 0 makes every k-th seed silly (NaN or negative diagonal error) on a copy that "
              "both the device and the stock reference import");
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      auto& queue = iEvent.queue();
      const auto& seedsOrig = iEvent.get(seedsToken_).seeds();
      mkfit::TrackVec seedsMod;
      if (injectSillyEvery_ > 0) {
        seedsMod = seedsOrig;
        for (size_t i = 0; i < seedsMod.size(); i += injectSillyEvery_) {
          const int j = (i / injectSillyEvery_) % 3;
          if (j == 0)
            seedsMod[i].errors_nc().At(2, 1) = std::numeric_limits<float>::quiet_NaN();
          else if (j == 1)
            seedsMod[i].errors_nc().At(4, 4) = -1e-6f;
          else
            seedsMod[i].errors_nc().At(5, 0) = std::numeric_limits<float>::infinity();
        }
      }
      const auto& seedsIn = injectSillyEvery_ > 0 ? seedsMod : seedsOrig;
      const auto& pixelHits = iEvent.get(pixelHitsToken_).hits();
      const auto& stripHits = iEvent.get(stripHitsToken_).hits();
      const auto& mkFitGeom = iSetup.getData(mkFitGeomToken_);
      const auto& ti = mkFitGeom.trackerInfo();
      const int n = seedsIn.size();
      const int hps = hotsPerSeed_;
      if (n == 0) {
        iEvent.emplace(candStoreToken_, queue, ::mkfitdev::candStoreSizes(0, hps));
        mkfitdev::TrackSoADeviceCollection empty(queue, 0);
        iEvent.emplace(tracksToken_, std::move(empty));
        return;
      }

      // seed table. Device hit positions: packSeedsToDevice (no host staging on CPU backends). Otherwise the host
      // packer looks up the last-hit position (= stock eoh[layer].refHit(index): pixel wrapper for pixel layers).
      auto packWithHostPositions = [&]() {
        ::mkfitdev::SeedsHostCollection seedsH(queue, n);
        ::mkfitdev::packSeeds(
            seedsIn,
            [&](int layer, int index, float* xyz) {
              const auto& h = ti[layer].is_pixel() ? pixelHits[index] : stripHits[index];
              xyz[0] = h.x();
              xyz[1] = h.y();
              xyz[2] = h.z();
            },
            seedsH.view());
        mkfitdev::SeedsDeviceCollection d(queue, n);
        alpaka::memcpy(queue, d.buffer(), seedsH.buffer());
        return d;
      };
      mkfitdev::SeedsDeviceCollection seedsD =
          useDeviceHits_ ? mkfitdev::seeds::packSeedsToDevice(queue, seedsIn) : packWithHostPositions();

      // candidate storage product (all blocks; the import writes the seed, slot and HoT blocks)
      mkfitdev::CandStoreDeviceCollection store(queue, ::mkfitdev::candStoreSizes(n, hps));
      auto scV = store.view().seeds();
      auto slV = store.view().slots();
      auto hoV = store.view().hots();
      mkfitdev::seeds::DeviceHitPositions hitPos;
      if (useDeviceHits_) {
        const auto& eohD = iEvent.get(devEohToken_);
        const auto ev = eohD.const_view();
        hitPos.x = ev.hits().metadata().addressOf_x();
        hitPos.y = ev.hits().metadata().addressOf_y();
        hitPos.z = ev.hits().metadata().addressOf_z();
        hitPos.layerHitBase = ev.layers().metadata().addressOf_hitBase();
      }
      mkfitdev::seeds::importSeeds(
          queue, seedsD.view(), n, ::mkfitdev::seedPartitionLimits(ti), scV, slV, hoV, hps, hitPos);

      mkfitdev::TrackSoADeviceCollection tracksD(queue, n);
      mkfitdev::seeds::exportBestCands(queue,
                                       store.const_view().seeds(),
                                       store.const_view().slots(),
                                       store.const_view().hots(),
                                       hps,
                                       &seedsD.view().nKept(),
                                       n,
                                       nullptr,
                                       tracksD.view());
      // host copies of the import results before the chain tail changes the candidate rows
      std::unique_ptr<HostCopies> hc;
      if (validate_) {
        hc = std::make_unique<HostCopies>(queue, n, hps);
        alpaka::memcpy(queue, hc->sH.buffer(), seedsD.buffer());
        alpaka::memcpy(queue, hc->storeH.buffer(), store.buffer());
        alpaka::memcpy(queue, hc->tH.buffer(), tracksD.buffer());
      }

      if (!runTail_ && !validate_) {
        iEvent.emplace(candStoreToken_, std::move(store));
        iEvent.emplace(tracksToken_, std::move(tracksD));
        return;
      }
      // chain tail on the imported rows: post filter -> export -> duplicate cleaner (stock run_OneIteration order)
      const auto& itconf = iSetup.getData(iterConfigToken_);
      const float dc[4] = {
          itconf.dc_fracSharedHits, itconf.dc_drth_central, itconf.dc_drth_obarrel, itconf.dc_drth_forward};
      // cms-sw#52015: phase2:clean_duplicates_sharedhits_pixelpriority needs the pixel layers of the geometry
      uint64_t pixMask[4] = {0, 0, 0, 0};
      const bool pixPriority = itconf.m_duplicate_cleaner_name == "phase2:clean_duplicates_sharedhits_pixelpriority";
      if (pixPriority) {
        const auto& ti = iSetup.getData(mkFitGeomToken_).trackerInfo();
        for (int l = 0; l < ti.n_layers() && l < 256; ++l)
          if (ti.layer(l).is_pixel())
            pixMask[l >> 6] |= uint64_t(1) << (l & 63);
      }
      mkfitdev::TrackSoADeviceCollection exportedD(queue, n);
      mkfitdev::TrackSoADeviceCollection finalD(queue, n);
      // NOTE: the tail runs here on the freshly imported rows only to validate it against stock; in the chain it
      // runs after the engine (building + backward search) on the same CandStore product.
      mkfitdev::CandStoreDeviceCollection storeTail(queue, ::mkfitdev::candStoreSizes(n, hps));
      alpaka::memcpy(queue, storeTail.buffer(), store.buffer());
      mkfitdev::seeds::runChainTail(queue,
                                    storeTail.view().seeds(),
                                    storeTail.view().slots(),
                                    storeTail.view().hots(),
                                    hps,
                                    &seedsD.view().nKept(),
                                    n,
                                    false,
                                    itconf.m_backward_params.minHitsQF,
                                    true,
                                    dc,
                                    pixPriority ? pixMask : nullptr,
                                    exportedD.view(),
                                    finalD.view());
      if (validate_) {
        alpaka::memcpy(queue, hc->exH.buffer(), exportedD.buffer());
        alpaka::memcpy(queue, hc->fiH.buffer(), finalD.buffer());
        validate(iEvent, iSetup, seedsIn, *hc, n, hps);
      }
      iEvent.emplace(candStoreToken_, std::move(store));
      iEvent.emplace(tracksToken_, std::move(finalD));
    }

  private:
    struct HostCopies {
      HostCopies(Queue& q, int n, int hps)
          : sH(q, n), storeH(q, ::mkfitdev::candStoreSizes(n, hps)), tH(q, n), exH(q, n), fiH(q, n) {}
      ::mkfitdev::SeedsHostCollection sH;
      ::mkfitdev::CandStoreHostCollection storeH;
      ::mkfitdev::TrackSoAHostCollection tH, exH, fiH;
    };

    // identical track (state bitwise, score, chi2, label, status fields, hit list)
    static bool sameTrack(mkfit::Track const& got, mkfit::Track const& ref, bool ignoreDup = false) {
      const float gSc = got.score(), rSc = ref.score(), gCh = got.chi2(), rCh = ref.chi2();
      bool ok = got.label() == ref.label() && got.nTotalHits() == ref.nTotalHits() &&
                got.nFoundHits() == ref.nFoundHits() && got.charge() == ref.charge() &&
                std::memcmp(&gSc, &rSc, 4) == 0 && std::memcmp(&gCh, &rCh, 4) == 0 &&
                std::memcmp(got.errors().Array(), ref.errors().Array(), 21 * 4) == 0;
      for (int k = 0; k < 6; ++k) {
        const float a = got.parameters()[k], b = ref.parameters()[k];
        ok = ok && std::memcmp(&a, &b, 4) == 0;
      }
      const auto gs = got.getStatus(), rs = ref.getStatus();
      ok = ok && gs.n_seed_hits == rs.n_seed_hits && gs.eta_region == rs.eta_region && gs.algorithm == rs.algorithm &&
           gs.n_overlaps == rs.n_overlaps && (ignoreDup || gs.duplicate == rs.duplicate);
      for (int h = 0; ok && h < ref.nTotalHits(); ++h)
        ok = got.getHitOnTrack(h).index == ref.getHitOnTrack(h).index &&
             got.getHitOnTrack(h).layer == ref.getHitOnTrack(h).layer;
      return ok;
    }

    // ignoreDup: the device cleaner flags the 'duplicate' column of its input in place (stock flags its own copy)
    static long compareVec(mkfit::TrackVec const& ref, ::mkfitdev::TrackSoAConstView v, bool ignoreDup = false) {
      long mis = std::abs(long(ref.size()) - long(v.nTracks()));
      const int m = std::min<int>(ref.size(), v.nTracks());
      for (int r = 0; r < m; ++r)
        mis += !sameTrack(::mkfitdev::trackFromSoA(v, r), ref[r], ignoreDup);
      return mis;
    }

    void validate(device::Event& iEvent,
                  device::EventSetup const& iSetup,
                  mkfit::TrackVec const& seedsIn,
                  HostCopies& hc,
                  int n,
                  int hps) const {
      auto& queue = iEvent.queue();
      auto& sH = hc.sH;
      auto& tH = hc.tH;

      // STOCK: run_OneIteration up to find_tracks_load_seeds (seed cleaner off in the LST step -> seeds_sorted false)
      const auto& eoh = iEvent.get(eohToken_).get();
      const auto& itconf = iSetup.getData(iterConfigToken_);
      const auto& ti = iSetup.getData(mkFitGeomToken_).trackerInfo();
      mkfit::MkJob job({ti, itconf, eoh, eoh.refBeamSpot(), nullptr});
      auto builder = mkfit::MkBuilder::make_builder(true);
      builder->begin_event(&job, nullptr, "MkFitAlpakaSeedImportCheck");
      mkfit::TrackVec sv = seedsIn;
      builder->seed_post_cleaning(sv);
      const bool sortHits = itconf.m_requires_seed_hit_sorting;
      if (sortHits)
        for (auto& s : sv)
          s.sortHitsByLayer();
      builder->find_tracks_load_seeds(sv, false);
      const auto& eocc = builder->ref_eocc();

      alpaka::wait(queue);
      auto sv_ = sH.const_view();
      auto sc = hc.storeH.const_view().seeds();
      auto sl = hc.storeH.const_view().slots();
      auto ho = hc.storeH.const_view().hots();
      auto tv = tH.const_view();

      const int nStock = eocc.size();
      const int nKept = sv_.nKept();
      long orderMis = 0, regionMis = 0, originMis = 0, labelMis = 0, candMis = 0, stateMis = 0, statusMis = 0,
           hotMis = 0, scoreMis = 0, exportMis = 0, sepMis = 0;
      // stock separators from the region of each imported cand (regions are contiguous in stock order)
      int stockEnd[::mkfitdev::kNSeedRegions] = {0, 0, 0, 0, 0};
      for (int p = 0; p < nStock; ++p)
        stockEnd[eocc[p].front().getEtaRegion()] = p + 1;
      for (int r = 1; r < ::mkfitdev::kNSeedRegions; ++r)
        if (stockEnd[r] < stockEnd[r - 1])
          stockEnd[r] = stockEnd[r - 1];
      for (int r = 0; r < ::mkfitdev::kNSeedRegions; ++r)
        sepMis += sv_.regionEnd().v[r] != stockEnd[r];

      const int nCmp = std::min(nStock, nKept);
      for (int p = 0; p < nCmp; ++p) {
        const mkfit::CombCandidate& cc = eocc[p];
        const mkfit::TrackCand& c = cc.front();
        const int i = sv_[p].order();
        const bool sameSeed = sv_[i].cleanIdx() == cc.seed_origin_index();
        orderMis += !sameSeed;
        originMis += sc[p].seedOriginIdx() != cc.seed_origin_index();
        regionMis += sc[p].region() != c.getEtaRegion();
        if (!sameSeed)
          continue;
        const auto book = sl[::mkfitdev::candSlotRow(p, 0, 0)].book();
        const auto st = sl[::mkfitdev::candSlotRow(p, 0, 0)].state();
        labelMis += st.label != c.label();
        bool candOk = sc[p].nCands() == int(cc.size()) && sc[p].pickupLayer() == cc.pickupLayer() &&
                      sc[p].state() == int(cc.state()) && book.nFound == c.nFoundHits() &&
                      book.nMissing == c.nMissingHits() && book.lastHitIdx == c.lastCcIndex() &&
                      book.nOverlap == c.nOverlapHits() && book.nInsideMinusOne == c.nInsideMinusOneHits() &&
                      book.nTailMinusOne == c.nTailMinusOneHits() && sc[p].nHots() == cc.hotsSize();
        candMis += !candOk;
        const float cScore = c.score(), cChi2 = c.chi2();
        scoreMis += !(std::memcmp(&book.score, &cScore, 4) == 0 && std::memcmp(&book.chi2, &cChi2, 4) == 0);
        bool stOk = st.charge == c.charge() && std::memcmp(st.err, c.errors().Array(), 21 * 4) == 0;
        for (int k = 0; k < 6; ++k) {
          const float b = c.parameters()[k];
          stOk = stOk && std::memcmp(&st.par[k], &b, 4) == 0;
        }
        stateMis += !stOk;
        const auto cst = c.getStatus();
        uint32_t cstb;
        std::memcpy(&cstb, &cst, 4);
        statusMis += st.status != cstb;
        bool hotOk = true;
        for (int k = 0; k < cc.hotsSize() && k < hps; ++k) {
          const auto& a = ho[::mkfitdev::hotRow(p, k, hps)].node();
          const auto& b = cc.hot_node(k);
          hotOk = hotOk && a.index == b.m_hot.index && a.layer == b.m_hot.layer && a.prev == b.m_prev_idx &&
                  std::memcmp(&a.chi2, &b.m_chi2, 4) == 0;
        }
        hotMis += !hotOk;
        // export: stock TrackCand::exportTrack(true) vs TrackSoA row p (every imported seed has one candidate)
        if (p < tv.nTracks()) {
          exportMis += !sameTrack(::mkfitdev::trackFromSoA(tv, p), c.exportTrack(true));
        } else {
          ++exportMis;
        }
      }
      int nSilly = 0, maxSeedHits = 0;
      for (int i = 0; i < n; ++i) {
        nSilly += sv_[i].silly();
        maxSeedHits = std::max(maxSeedHits, seedsIn[i].nTotalHits());
      }
      // STOCK chain tail on the imported candidates (run_OneIteration with the backward fit on):
      // filter_comb_cands(post filter && nan_n_silly, true) -> export_best_comb_cands(true) -> duplicate cleaner
      job.switch_to_backward();
      mkfit::filter_candidates_func post_filter = [&](const mkfit::TrackCand& tc, const mkfit::MkJob& jb) -> bool {
        return itconf.m_post_bkfit_filter(tc, jb) && mkfit::StdSeq::qfilter_nan_n_silly<mkfit::TrackCand>(tc, jb);
      };
      builder->filter_comb_cands(post_filter, true);
      mkfit::TrackVec stockOut;
      builder->export_best_comb_cands(stockOut, true);
      const long tailExportMis = compareVec(stockOut, hc.exH.const_view(), true);
      int devFlagged = 0;
      for (int r = 0; r < hc.exH.const_view().nTracks(); ++r)
        devFlagged += hc.exH.const_view()[r].duplicate() != 0;
      const int nStockExported = stockOut.size();
      itconf.m_duplicate_cleaner(stockOut, itconf, ti);
      const long tailFinalMis = compareVec(stockOut, hc.fiH.const_view());

      edm::LogPrint("MkFitAlpakaSeedImport")
          << "SEED_COMPARE " << EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE) << " event " << iEvent.id().event()
          << " seeds " << n << " maxSeedHits " << maxSeedHits << " silly " << nSilly << " kept " << nKept
          << " stockKept " << nStock << " orderMismatch " << orderMis << " regionMismatch " << regionMis
          << " originMismatch " << originMis << " labelMismatch " << labelMis << " sepMismatch " << sepMis
          << " candMismatch " << candMis << " stateMismatch " << stateMis << " statusMismatch " << statusMis
          << " scoreMismatch " << scoreMis << " hotMismatch " << hotMis << " exported " << tv.nTracks()
          << " exportMismatch " << exportMis << " | tail: filtered " << hc.exH.const_view().nTracks() << " stock "
          << nStockExported << " tailExportMismatch " << tailExportMis << " flaggedDup " << devFlagged << " cleaned "
          << hc.fiH.const_view().nTracks() << " stock " << stockOut.size() << " tailFinalMismatch " << tailFinalMis
          << " overflow hots " << sc.nOverflowHots() << " seedHits " << sv_.nOverflowHits() << " trkHits "
          << tv.nOverflowHits();
      builder->end_event();
      builder->release_memory();
    }

    const edm::EDGetTokenT<MkFitSeedWrapper> seedsToken_;
    const edm::EDGetTokenT<MkFitHitWrapper> pixelHitsToken_;
    const edm::EDGetTokenT<MkFitHitWrapper> stripHitsToken_;
    const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
    const device::EDPutToken<mkfitdev::CandStoreDeviceCollection> candStoreToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> tracksToken_;
    edm::EDGetTokenT<MkFitEventOfHits> eohToken_;
    device::EDGetToken<mkfitdev::EventOfHitsDeviceCollection> devEohToken_;
    bool useDeviceHits_ = false;
    edm::ESGetToken<mkfit::IterationConfig, TrackerRecoGeometryRecord> iterConfigToken_;
    const bool validate_;
    const int hotsPerSeed_;
    const int injectSillyEvery_;
    const bool runTail_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaSeedImportProducer);
