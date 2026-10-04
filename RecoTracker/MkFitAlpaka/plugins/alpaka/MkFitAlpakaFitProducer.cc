// Device mkFit final fit, drop-in for the stock MkFitFitProducer (procModifier trackingMkFitFit) in replay jobs:
// same inputs (stock MkFitOutputWrapper of the building + cleaning, MkFitHitWrapper hits), same candCutSel
// preselection (MkFitFitProducer.cc:128-142), same output type (MkFitOutputWrapper, states at the first hit), so the
// stock MkFitOutputTrackConverter and the harness comparators consume it unchanged.
// Round 3: hits and tracks are packed on the host from the stock products and copied to the device; in the full
// chain they come from the device hit input and the device building output.
// CPE (round 4, DESIGN D-H1 step 2): cpe = true (default, the menu behaviour) applies the device PixelCPEGeneric with
// the track angles on every pixel hit. Its tables are an ES product (round 5, R4-H2: MkFitAlpakaFitCpeESProducer,
// interface/fit/CpeESData.h), checked once per IOV against the stock CPE object the menu fit uses (pixelCPE,
// 'PixelCPEGeneric'; R4-H1, cpeCheck). The per-hit cluster quantities are computed on the host from the
// SiPixelClusters of MkFitClusterIndexToHit, only for the hits on the input tracks (stand-in for a device cluster
// product). cpe = false is the no-CPE variant: compare it with the private stock copy run with disableCPE = true.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESInputTag.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/SynchronizingEDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "CondFormats/DataRecord/interface/SiPixelGenErrorDBObjectRcd.h"
#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHit.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoLocalTracker/Records/interface/PixelCPEFastParamsRecord.h"
#include "RecoLocalTracker/Records/interface/TkPixelCPERecord.h"
#include "RecoLocalTracker/ClusterParameterEstimator/interface/PixelClusterParameterEstimator.h"
#include "RecoTracker/MkFit/interface/MkFitClusterIndexToHit.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFitCore/interface/TrackerInfo.h"
#include "RecoTracker/MkFit/interface/MkFitHitWrapper.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFitCore/interface/Track.h"
#include "RecoTracker/MkFitCore/interface/HitStateOnTrack.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitAlpaka/interface/alpaka/EventOfHitsProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESData.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/CpeESData.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsHostCollections.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsHostSetup.h"
#include "RecoTracker/MkFitAlpaka/interface/SupportedConfig.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/alpaka/EventOfHitsDeviceCollections.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"
#include "RecoTracker/MkFitAlpaka/plugins/alpaka/MkFitAlpakaFitCpeTables.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/fit/FitTracks.h"

namespace mkfitdev::fit {
  // job-wide state of MkFitAlpakaFitProducer (GlobalCache): statistics and the per-IOV CPE cross-check bookkeeping
  struct FitProducerShared {
    mutable std::atomic<long> nEvents{0}, nTracks{0}, nRefit{0}, nRemoved{0}, nNaN{0}, nMismatch{0}, nOverflow{0},
        nShadowed{0}, usAcquire{0}, usClus{0}, usWait{0}, usProduce{0};
    mutable std::atomic<int> nHitCheckEvents{0};
    mutable std::mutex checkMutex;
    mutable std::atomic<unsigned long long> checkedIds[2] = {0, 0};
    mutable ::mkfitdev::cpe::CpeCheckStats hitCheck;
    int cpeCheckHitEvents = 0;
  };
}  // namespace mkfitdev::fit

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  // R4-M2 "no wait": acquire() packs and enqueues (copies, 6 fit kernels, copy back); the framework waits for the
  // queue asynchronously (ExternalWork), produce() unpacks on the host. No alpaka::wait on a TBB thread.
  class MkFitAlpakaFitProducer
      : public stream::SynchronizingEDProducer<edm::GlobalCache<::mkfitdev::fit::FitProducerShared>> {
  public:
    using Shared = ::mkfitdev::fit::FitProducerShared;

    static std::unique_ptr<Shared> initializeGlobalCache(edm::ParameterSet const& iConfig) {
      auto s = std::make_unique<Shared>();
      s->cpeCheckHitEvents = iConfig.getParameter<int>("cpeCheckHitEvents");
      return s;
    }

    static void globalEndJob(Shared* g) {
      const long n = g->nEvents;
      auto ms = [n](long us) { return n ? 1e-3 * us / n : 0.; };
      edm::LogPrint("MkFitAlpakaFitProducer")
          << "[fit] events " << n << " tracks " << g->nTracks << " refit(pass 2) " << g->nRefit << " removedHits "
          << g->nRemoved << " NaN-kept " << g->nNaN << " nFoundHits-mismatch " << g->nMismatch << " overflow "
          << g->nOverflow << " collision-shadowed outliers " << g->nShadowed << " | ms/event: acquire (host packing + "
          << "enqueue) " << ms(g->usAcquire) << " (of which CPE clusters " << ms(g->usClus)
          << ") acquire->produce (device, async) " << ms(g->usWait) << " produce (host unpacking) "
          << ms(g->usProduce);
      if (g->cpeCheckHitEvents > 0)
        edm::LogPrint("MkFitAlpakaFitProducer")
            << "[fit] CPE per-hit check vs the stock CPE object (real clusters on the input tracks, first "
            << g->cpeCheckHitEvents << " events): " << g->hitCheck.summary();
    }

    explicit MkFitAlpakaFitProducer(edm::ParameterSet const& iConfig, Shared const*)
        : SynchronizingEDProducer(iConfig),
          tracksToken_{consumes(iConfig.getParameter<edm::InputTag>("tracks"))},
          esToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"))},
          candCutSel_{iConfig.getParameter<bool>("candCutSel")},
          candMinPtCut_{float(iConfig.getParameter<double>("candMinPtCut"))},
          candMinNHitsCut_{iConfig.getParameter<int>("candMinNHitsCut")},
          candMinPtRelaxedCut_{float(iConfig.getParameter<double>("candMinPtRelaxedCut"))},
          candMinAbsEtaForRelaxedCut_{float(iConfig.getParameter<double>("candMinAbsEtaForRelaxedCut"))},
          putToken_{produces()},
          useCpe_{iConfig.getParameter<bool>("cpe")},
          storeHitStates_{iConfig.getParameter<bool>("storeHitStates")} {
      fitOpt_.edgeOutliers = iConfig.getParameter<bool>("edgeOutliers");
      fitOpt_.edgeChi2Cut = float(iConfig.getParameter<double>("edgeChi2Cut"));
      fitOpt_.outlierRounds = iConfig.getParameter<int>("outlierRounds");
      if (fitOpt_.outlierRounds < 1)
        throw cms::Exception("Configuration") << "outlierRounds must be >= 1 (1 = stock)";
      fitOpt_.firstHitProp = iConfig.getParameter<int>("firstHitProp");  // DEVIATION D7
      if (auto tag = iConfig.getParameter<edm::InputTag>("eventOfHits"); !tag.label().empty()) {
        eohToken_ = consumes(tag);
        devHits_ = true;
      }
      // hits from the device EventOfHits: only the hit counts are needed on the host, from the cluster-index maps
      // (stock converters and the index-only producers both provide them); the MkFitHitWrapper only without it
      if (devHits_) {
        pixelIdxToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelHits"));
        stripIdxToken_ = consumes(iConfig.getParameter<edm::InputTag>("stripHits"));
      } else {
        pixelHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelHits"));
        stripHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("stripHits"));
      }
      if (useCpe_) {
        pixelClustersToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelHits"));
        clusterCollToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelClusters"));
        cpeToken_ = esConsumes(edm::ESInputTag("", iConfig.getParameter<std::string>("cpeTables")));
        mkFitGeomToken_ = esConsumes();
        const auto mode = iConfig.getParameter<std::string>("cpeCheck");
        if (mode != "throw" && mode != "warn" && mode != "off")
          throw cms::Exception("Configuration") << "MkFitAlpakaFitProducer: cpeCheck must be throw, warn or off";
        cpeCheckThrow_ = mode == "throw";
        cpeCheckHitEvents_ = iConfig.getParameter<int>("cpeCheckHitEvents");
        if (mode != "off" || cpeCheckHitEvents_ > 0) {
          stockCpeToken_ = esConsumes(edm::ESInputTag("", iConfig.getParameter<std::string>("pixelCPE")));
          geomToken_ = esConsumes();
          cpeCheck_ = mode != "off";
        }
      }
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("tracks", edm::InputTag("hltInitialStepTrackCandidatesMkFit"));
      desc.add<edm::InputTag>("pixelHits", edm::InputTag("hltMkFitSiPixelHits"));
      desc.add<edm::InputTag>("stripHits", edm::InputTag("hltMkFitSiPhase2Hits"));
      desc.add<edm::ESInputTag>("esData", edm::ESInputTag("", ""));
      // stock MkFitFitProducer candCutSel ("emulate MkFitOutputConverter"); the menu values are set in the config
      desc.add<bool>("candCutSel", false);
      desc.add<double>("candMinPtCut", 0);
      desc.add<int>("candMinNHitsCut", 0);
      desc.add<double>("candMinPtRelaxedCut", 0);
      desc.add<double>("candMinAbsEtaForRelaxedCut", 0);
      desc.add<bool>("cpe", true)->setComment(
          "apply the device PixelCPEGeneric with track angles (stock menu); false = no-CPE variant (validation only)");
      desc.add<std::string>("cpeTables", "MkFitAlpakaFitCpe")
          ->setComment("ComponentName of the MkFitAlpakaFitCpeESProducer product (device CPE tables)");
      desc.add<std::string>("pixelCPE", "PixelCPEGeneric")
          ->setComment("stock CPE object of the menu fit (MkFitFitProducer pixelCPE): the device CPE is checked against it");
      desc.add<std::string>("cpeCheck", "throw")
          ->setComment("per-IOV synthetic-cluster cross-check vs pixelCPE (R4-H1): throw | warn | off");
      desc.add<int>("cpeCheckHitEvents", 0)
          ->setComment("validation: compare every pixel hit on the input tracks with pixelCPE in the first N events");
      desc.add<edm::InputTag>("pixelClusters", edm::InputTag("hltSiPixelClusters"))
          ->setComment("the clusters of the pixel rechits (read by index, avoids one Ref resolution per hit)");
      desc.add<edm::InputTag>("eventOfHits", edm::InputTag(""))
          ->setComment("device EventOfHits product (hits block, same rows as the hit wrappers); empty = host packing");
      desc.add<bool>("edgeOutliers", false)
          ->setComment("DEVIATION D3 (switch, stock = false): the first/last fitted hit is an outlier when the pass that "
                       "predicts it from all other hits has chi2 > edgeChi2Cut (as the KF EstimateCut)");
      desc.add<double>("edgeChi2Cut", 20.)->setComment("DEVIATION D3: the edge-hit outlier cut");
      desc.add<int>("outlierRounds", 1)
          ->setComment("DEVIATION D3 (switch, stock = 1): refits after outlier removal; all but the last are checked again");
      desc.add<int>("firstHitProp", 0)
          ->setComment("DEVIATION D7 (switch, stock = 0): propagate to the first hit of a fit pass instead of updating "
                       "there without propagation (trackreco#186); 1 = in the refits after outlier removal, 2 = every pass");
      desc.add<bool>("storeHitStates", false)
          ->setComment(
              "stock MkFitFitProducer storeHitStates (trackreco#186): the smoothed state of the final fit at every hit, "
              "for the converter's full TrackExtras / Trajectories; off: no per-hit work");
      descriptions.addWithDefaultLabel(desc);
    }

    void acquire(device::Event const& iEvent, device::EventSetup const& iSetup) override {
      using Clock = std::chrono::steady_clock;
      const auto t0 = Clock::now();
      auto const* g = globalCache();
      auto& queue = iEvent.queue();
      auto const& es = iSetup.getData(esToken_);
      ::mkfitdev::checkSupportedFitConfig(es.hostConfigValue());  // review H3

      // MkFitFitProducer.cc:127-142
      mkfit::TrackVec intracks = iEvent.get(tracksToken_).tracks();
      if (candCutSel_) {
        mkfit::TrackVec reduced;
        for (auto const& t : intracks) {
          const auto minPtCutForCand =
              (candMinPtRelaxedCut_ > 0 && std::abs(t.momEta()) > candMinAbsEtaForRelaxedCut_) ? candMinPtRelaxedCut_
                                                                                               : candMinPtCut_;
          if (t.pT() < minPtCutForCand || t.nTotalHits() < candMinNHitsCut_)
            continue;
          reduced.push_back(t);
        }
        intracks.swap(reduced);
      }
      const int nTracks = intracks.size();

      const bool devHits = devHits_;
      const uint32_t nPix = devHits ? iEvent.get(pixelIdxToken_).hits().size() : iEvent.get(pixelHitsToken_).hits().size();
      const uint32_t nStr = devHits ? iEvent.get(stripIdxToken_).hits().size() : iEvent.get(stripHitsToken_).hits().size();

      ::mkfitdev::HitsHostCollection hHits(queue, devHits ? 0 : int(nPix + nStr));
      if (!devHits) {
        auto const& pix = iEvent.get(pixelHitsToken_).hits();
        auto const& str = iEvent.get(stripHitsToken_).hits();
        const std::vector<int> noLayers;
        ::mkfitdev::fillHits(pix, noLayers, 0, hHits.view());
        ::mkfitdev::fillHits(str, noLayers, nPix, hHits.view());
        hHits.view().nPixel() = nPix;
        hHits.view().nStrip() = nStr;
      }

      hTracks_.emplace(queue, nTracks);
      auto& hTracks = *hTracks_;
      ::mkfitdev::tracksToSoA(intracks, hTracks.view());
      if (hTracks.view().nOverflowHits() > 0)
        edm::LogWarning("MkFitAlpakaFitProducer") << hTracks.view().nOverflowHits() << " tracks with > kMaxTrkHits hits";

      // CPE tables (once per IOV) and per pixel hit cluster quantities
      // pinned host buffer from the caching allocator (no per-event page faults); only the entries of hits on the
      // input tracks are written, the kernels read no other entry
      std::optional<cms::alpakatools::host_buffer<::mkfitdev::cpe::ClusterCpe[]>> hClusBuf;
      ::mkfitdev::cpe::CpeTablesHost const* tables = nullptr;
      ::mkfitdev::cpe::CpeTables dCpe{};
      const auto tc0 = Clock::now();
      if (useCpe_) {
        auto const& cpeES = iSetup.getData(cpeToken_);
        tables = cpeES.host.get();
        dCpe = cpeES.view();
        if (cpeCheck_)
          checkCpePerIOV(iSetup, *tables);
        auto const& idx = iEvent.get(pixelClustersToken_).hits();
        auto const clusH = iEvent.getHandle(clusterCollToken_);
        auto const& clusData = clusH->data();
        const edm::ProductID clusId = clusH.id();
        hClusBuf.emplace(
            cms::alpakatools::make_host_buffer<::mkfitdev::cpe::ClusterCpe[]>(queue, std::max<uint32_t>(nPix, 1)));
        ::mkfitdev::cpe::ClusterCpe* hClus = hClusBuf->data();
        // only the pixel hits on the fit's input tracks need cluster data (stock calls the CPE only for them)
        auto const& ti = iSetup.getData(mkFitGeomToken_).trackerInfo();
        std::vector<uint8_t> need(nPix, 0);
        for (auto const& t : intracks)
          for (int h = 0; h < t.nTotalHits(); ++h) {
            const auto hot = t.getHitOnTrack(h);
            if (hot.index >= 0 && uint32_t(hot.index) < nPix && ti.layer(hot.layer).is_pixel())
              need[hot.index] = 1;
          }
        uint32_t lastRaw = 0;  // hits come module by module (cluster DetSetVector order): one map lookup per module
        int lastMod = -1;
        for (uint32_t i = 0; i < nPix; ++i) {
          if (!need[i])
            continue;
          hClus[i].module = -1;
          auto const* rh = i < idx.size() ? dynamic_cast<SiPixelRecHit const*>(idx[i]) : nullptr;
          if (!rh)
            continue;
          const uint32_t raw = rh->geographicalId().rawId();
          if (raw != lastRaw || lastMod < 0) {
            auto it = tables->rawToModule.find(raw);
            lastRaw = raw;
            lastMod = it == tables->rawToModule.end() ? -1 : it->second;
          }
          auto const& oref = rh->omniClusterRef();
          SiPixelCluster const& cl =
              (oref.id() == clusId && oref.key() < clusData.size()) ? clusData[oref.key()] : *rh->cluster();
          hClus[i] = ::mkfitdev::cpe::clusterCpe(cl, lastMod);
        }
        if (cpeCheckHitEvents_ > 0 && g->nHitCheckEvents.fetch_add(1) < cpeCheckHitEvents_)
          checkCpeHits(iSetup, *tables, idx, clusData, clusId, need);
      }

      const auto t1 = Clock::now();
      g->usClus += long(std::chrono::duration_cast<std::chrono::microseconds>(t1 - tc0).count());
      // CPU backends (device memory = host memory): the fit runs in place on the host buffers (no host-to-host copies of
      // the tracks, hits and the per-hit cluster data)
      constexpr bool kHostMem = std::is_same_v<Device, alpaka_common::DevHost>;
      mkfitdev::HitsDeviceCollection dHits(queue, (devHits || kHostMem) ? 0 : int(nPix + nStr));
      if (!devHits && !kHostMem)
        alpaka::memcpy(queue, dHits.buffer(), hHits.const_buffer());
      mkfitdev::TrackSoADeviceCollection dTracks(queue, kHostMem ? 0 : nTracks);
      if (!kHostMem)
        alpaka::memcpy(queue, dTracks.buffer(), hTracks.const_buffer());
      // per-hit smoothed states (storeHitStates): [nTracks * kMaxTrkHits], device side (host side on CPU backends)
      std::optional<cms::alpakatools::device_buffer<Device, ::mkfitdev::fit::HitStateDev[]>> dHs;
      ::mkfitdev::fit::HitStateDev* hsPtr = nullptr;
      if (storeHitStates_ && nTracks > 0) {
        const uint32_t nHs = uint32_t(nTracks) * ::mkfitdev::kMaxTrkHits;
        hHs_.emplace(cms::alpakatools::make_host_buffer<::mkfitdev::fit::HitStateDev[]>(queue, nHs));
        if constexpr (kHostMem) {
          hsPtr = hHs_->data();
        } else {
          dHs.emplace(cms::alpakatools::make_device_buffer<::mkfitdev::fit::HitStateDev[]>(queue, nHs));
          hsPtr = dHs->data();
        }
      }
      auto dCounters = cms::alpakatools::make_device_buffer<mkfitdev::fit::FitCounters>(queue);
      alpaka::memset(queue, dCounters, 0);

      std::optional<cms::alpakatools::device_buffer<Device, ::mkfitdev::cpe::ClusterCpe[]>> dClus;
      const ::mkfitdev::cpe::ClusterCpe* clusPtr = nullptr;
      if (useCpe_) {
        // the tables are the ES product (one device copy per IOV); only the per-hit cluster data moves per event
        if constexpr (kHostMem) {
          clusPtr = hClusBuf->data();
        } else {
          dClus.emplace(
              cms::alpakatools::make_device_buffer<::mkfitdev::cpe::ClusterCpe[]>(queue, std::max<uint32_t>(nPix, 1)));
          if (nPix > 0)
            alpaka::memcpy(queue, *dClus, *hClusBuf);
          clusPtr = dClus->data();
        }
      }

      mkfitdev::fit::runFinalFit(queue,
                                 es.view(),
                                 devHits ? iEvent.get(eohToken_).const_view().hits()
                                         : (kHostMem ? hHits.const_view() : dHits.const_view()),
                                 nPix,
                                 kHostMem ? hTracks.view() : dTracks.view(),
                                 nTracks,
                                 dCounters.data(),
                                 useCpe_,
                                 dCpe,
                                 clusPtr,
                                 hsPtr,
                                 nullptr,
                                 fitOpt_);
      if (dHs)
        alpaka::memcpy(queue, *hHs_, *dHs);

      if (!kHostMem)
        alpaka::memcpy(queue, hTracks.buffer(), dTracks.const_buffer());
      hCounters_.emplace(cms::alpakatools::make_host_buffer<mkfitdev::fit::FitCounters>(queue));
      alpaka::memcpy(queue, *hCounters_, dCounters);
      intracks_ = std::move(intracks);
      tAcquireEnd_ = Clock::now();
      g->usAcquire += long(std::chrono::duration_cast<std::chrono::microseconds>(tAcquireEnd_ - t0).count());
    }

    void produce(device::Event& iEvent, device::EventSetup const&) override {
      using Clock = std::chrono::steady_clock;
      const auto t2 = Clock::now();
      auto const* g = globalCache();
      auto const& intracks = intracks_;
      const int nTracks = intracks.size();
      // back to mkfit::Track: state + chi2, removed outliers via Track::removeHit (index -1, nFoundHits - 1)
      mkfit::TrackVec out;
      out.reserve(nTracks);
      auto v = hTracks_->const_view();
      for (int i = 0; i < nTracks; ++i) {
        mkfit::Track t = intracks[i];
        for (int k = 0; k < 6; ++k)
          t.parameters_nc()[k] = v[i].params().v[k];
        std::memcpy(t.errors_nc().Array(), v[i].errors().v, sizeof(float) * 21);
        t.setChi2(v[i].chi2());
        const int nh = std::min(t.nTotalHits(), ::mkfitdev::kMaxTrkHits);
        for (int h = 0; h < nh; ++h)
          if (t.getHitIdx(h) >= 0 && v[i].hits().hot[h].index == -1)
            t.removeHit(h);
        out.push_back(std::move(t));
      }

      const auto t3 = Clock::now();
      auto us = [](auto a, auto b) { return long(std::chrono::duration_cast<std::chrono::microseconds>(b - a).count()); };
      g->usWait += us(tAcquireEnd_, t2);
      g->usProduce += us(t2, t3);
      auto const& c = *hCounters_->data();
      ++g->nEvents;
      g->nTracks += nTracks;
      g->nRefit += c.nRefit;
      g->nRemoved += c.nRemovedHits;
      g->nNaN += c.nNaN;
      g->nMismatch += c.nHitCountMismatch;
      g->nOverflow += c.nOverflow;
      g->nShadowed += c.nShadowed;
      intracks_.clear();
      hTracks_.reset();
      hCounters_.reset();

      if (storeHitStates_) {
        // stock MkBuilder: one HitStatesOnTrack per track, nTotalHits entries (HitOnTrack positions)
        std::vector<mkfit::HitStatesOnTrack> hitStates(nTracks);
        for (int i = 0; i < nTracks; ++i) {
          const int nTot = out[i].nTotalHits();
          hitStates[i].assign(nTot, mkfit::HitStateOnTrack{});
          for (int p = 0; p < std::min(nTot, ::mkfitdev::kMaxTrkHits); ++p) {
            auto const& d = hHs_->data()[size_t(i) * ::mkfitdev::kMaxTrkHits + p];
            auto& o = hitStates[i][p];
            std::memcpy(o.par, d.par, sizeof(o.par));
            std::memcpy(o.err, d.err, sizeof(o.err));
            o.chi2 = d.chi2;
            o.pzSign = d.pzSign;
            o.kind = d.kind;
            o.valid = d.valid != 0;
          }
        }
        hHs_.reset();
        iEvent.emplace(putToken_, std::move(out), true, std::move(hitStates));
        return;
      }
      iEvent.emplace(putToken_, std::move(out), true);
    }

  private:
    // R4-H1: once per IOV of the two CPE records, the device CPE (tables of the ES product) against the stock CPE
    // object of the menu fit, on synthetic clusters. Read-only; the mutex only serialises the check itself.
    void checkCpePerIOV(device::EventSetup const& iSetup, ::mkfitdev::cpe::CpeTablesHost const& t) const {
      edm::EventSetup const& es = iSetup;
      const unsigned long long ids[2] = {es.get<TkPixelCPERecord>().cacheIdentifier(),
                                         es.get<PixelCPEFastParamsRecord>().cacheIdentifier()};
      auto const* g = globalCache();
      // R6-M5: lock-free fast path; the ids are stored only after the check has passed (or warned)
      if (g->checkedIds[0].load(std::memory_order_acquire) == ids[0] &&
          g->checkedIds[1].load(std::memory_order_acquire) == ids[1])
        return;
      std::lock_guard<std::mutex> lock(g->checkMutex);
      if (g->checkedIds[0].load() == ids[0] && g->checkedIds[1].load() == ids[1])
        return;
      const auto st = ::mkfitdev::cpe::crossCheckCpeSynthetic(
          iSetup.getData(stockCpeToken_), iSetup.getData(geomToken_), t, kCheckModuleStride);
      edm::LogPrint("MkFitAlpakaFitProducer") << "[fit] CPE cross-check vs the stock CPE object (synthetic clusters, "
                                              << t.modules.size() << " modules / " << kCheckModuleStride
                                              << " + first of each template): " << st.summary();
      if (st.nBad > 0) {
        if (cpeCheckThrow_)
          throw cms::Exception("MkFitAlpakaFitCpe")
              << "the device PixelCPEGeneric differs from the stock CPE object of the menu fit: " << st.summary()
              << ". Check the CPE ES producers' parameters (PixelCPEGeneric vs PixelCPEFastParams) and "
                 "interface/fit/CpeGeneric.h (menu settings).";
        edm::LogWarning("MkFitAlpakaFitCpe") << "device CPE differs from the stock CPE object: " << st.summary();
      }
      g->checkedIds[1].store(ids[1], std::memory_order_release);
      g->checkedIds[0].store(ids[0], std::memory_order_release);
    }

    // validation: every pixel hit on the input tracks (real clusters), angles of a straight line from the origin
    void checkCpeHits(device::EventSetup const& iSetup,
                      ::mkfitdev::cpe::CpeTablesHost const& t,
                      std::vector<TrackingRecHit const*> const& idx,
                      std::vector<SiPixelCluster> const& clusData,
                      edm::ProductID clusId,
                      std::vector<uint8_t> const& need) const {
      auto const& stock = iSetup.getData(stockCpeToken_);
      const CpeTablesView tv = t.view();
      ::mkfitdev::cpe::CpeCheckStats st;
      for (uint32_t i = 0; i < need.size(); ++i) {
        if (!need[i])
          continue;
        auto const* rh = i < idx.size() ? dynamic_cast<SiPixelRecHit const*>(idx[i]) : nullptr;
        if (!rh)
          continue;
        auto it = t.rawToModule.find(rh->geographicalId().rawId());
        if (it == t.rawToModule.end())
          continue;
        auto const& oref = rh->omniClusterRef();
        SiPixelCluster const& cl =
            (oref.id() == clusId && oref.key() < clusData.size()) ? clusData[oref.key()] : *rh->cluster();
        auto const& det = *rh->detUnit();
        const GlobalPoint gp = det.surface().toGlobal(rh->localPosition());
        const LocalVector d = det.surface().toLocal(GlobalVector(gp.x(), gp.y(), gp.z()));
        if (d.z() == 0)
          continue;
        ::mkfitdev::cpe::compareCpe(stock,
                                    det,
                                    cl,
                                    tv,
                                    it->second,
                                    d.x() / d.z(),
                                    d.y() / d.z(),
                                    rh->localPosition().x(),
                                    rh->localPosition().y(),
                                    st);
      }
      std::lock_guard<std::mutex> lock(globalCache()->checkMutex);
      globalCache()->hitCheck.merge(st);
    }

    static constexpr int kCheckModuleStride = 8;
    using CpeTablesView = ::mkfitdev::cpe::CpeTables;

    const edm::EDGetTokenT<MkFitOutputWrapper> tracksToken_;
    edm::EDGetTokenT<MkFitHitWrapper> pixelHitsToken_;
    edm::EDGetTokenT<MkFitHitWrapper> stripHitsToken_;
    edm::EDGetTokenT<MkFitClusterIndexToHit> pixelIdxToken_;
    edm::EDGetTokenT<MkFitClusterIndexToHit> stripIdxToken_;
    const device::ESGetToken<::mkfitdev::ESData<Device>, TrackerRecoGeometryRecord> esToken_;
    const bool candCutSel_;
    const float candMinPtCut_;
    const int candMinNHitsCut_;
    const float candMinPtRelaxedCut_;
    const float candMinAbsEtaForRelaxedCut_;
    const edm::EDPutTokenT<MkFitOutputWrapper> putToken_;
    const bool useCpe_;
    device::EDGetToken<mkfitdev::EventOfHitsDeviceCollection> eohToken_;
    bool devHits_ = false;
    edm::EDGetTokenT<MkFitClusterIndexToHit> pixelClustersToken_;
    edm::EDGetTokenT<edmNew::DetSetVector<SiPixelCluster>> clusterCollToken_;
    device::ESGetToken<::mkfitdev::cpe::CpeESData<Device>, PixelCPEFastParamsRecord> cpeToken_;
    edm::ESGetToken<PixelClusterParameterEstimator, TkPixelCPERecord> stockCpeToken_;
    edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
    bool cpeCheck_ = false, cpeCheckThrow_ = true;
    int cpeCheckHitEvents_ = 0;
    // per stream, between acquire() and produce()
    mkfit::TrackVec intracks_;
    const bool storeHitStates_;
    mkfitdev::fit::FitOptions fitOpt_;
    std::optional<cms::alpakatools::host_buffer<::mkfitdev::fit::HitStateDev[]>> hHs_;
    std::optional<::mkfitdev::TrackSoAHostCollection> hTracks_;
    std::optional<cms::alpakatools::host_buffer<mkfitdev::fit::FitCounters>> hCounters_;
    std::chrono::steady_clock::time_point tAcquireEnd_;

  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaFitProducer);
