// MkFitAlpakaFitDeviceProducer (round 6, lane gpu): the device mkFit final fit as an ASYNCHRONOUS Alpaka producer,
// fed directly by the device building output. Replaces, in the trackingMkFitFit menu, the host round trip
//   build TrackSoA (device) -> host copy -> MkFitOutputWrapper -> [MkFitAlpakaFitProducer: host candCutSel, host
//   packing, device fit, wait, host unpacking] -> MkFitOutputWrapper
// by
//   build TrackSoA (device) -> [this module: device candCutSel, device fit] -> fitted TrackSoA (device)
//   -> framework device->host copy -> MkFitAlpakaOutputWrapperFromTrackSoA -> MkFitOutputWrapper (stock consumers).
// - global::EDProducer: produce() only enqueues (no alpaka::wait, no ExternalWork): no CMSSW thread is held while the
//   device works, as the build module.
// - Fit = the same entry point and kernels as MkFitAlpakaFitProducer (src/alpaka/FitTracks.dev.cc, lane fit); the row
//   count comes from the device (candCutSel output), the grid from the capacity.
// - CPE cluster quantities (ClusterCpe) for EVERY pixel hit, no dependency on the host tracks:
//   clusterSource = "auto" (default): "device" on GPU backends, "tracks" on CPU backends.
//   "tracks" (CPU backends only): the selection runs synchronously, so the host computes clusterCpe() only for the
//   pixel hits on the selected tracks (as the round-5 fit), straight from the cluster collection (no Ref resolution).
//   "device": computed on the device from the pixel digi + cluster SoA (handoff::
//   buildClusterCpe); the host only writes, per mkFit pixel row (= legacy cluster key, as stock convertHits), the SoA
//   module index and SiPixelCluster::originalId (4 + 4 bytes, no pixel loop). Clusters without originalId (persisted
//   replay clusters) make the event fall back to "host".
//   "host": the round-5 host computation (clusterCpe() over every cluster, pinned buffer + copy).
//   "check" (validation): device values used, host values computed too and compared bitwise (one sync per event).
// - Output: TrackSoA (capacity = the building's), rows [0, nTracks) = fitted tracks in stock order; status fields,
//   label, score, charge copied; removed outliers have index -1 and nFoundHits decremented (stock Track::removeHit).
//   MkFitAlpakaOutputWrapperFromTrackSoA(propagatedToFirstLayer = True) turns it into the stock fit output.
// - Round 8 (lane mem, D7-f): the building's TrackSoA is read first, so the module takes over the building's queue (=
//   the EventOfHits queue). eventOfHitsQueue set (the target menu deletes the device EventOfHits right after this
//   module, canDeleteEarly): if this module's queue is not the one the EventOfHits was allocated on, it waits for its
//   own work before returning, so the caching allocator cannot hand the block to another queue while the fit reads it
//   (counted; EOH_RELEASE at endJob).

#include <algorithm>
#include <cstring>
#include <atomic>
#include <mutex>
#include <optional>
#include <type_traits>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "CondFormats/DataRecord/interface/SiPixelGenErrorDBObjectRcd.h"
#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/SiPixelClusterSoA/interface/alpaka/SiPixelClustersSoACollection.h"
#include "DataFormats/SiPixelDigiSoA/interface/alpaka/SiPixelDigisSoACollection.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/ESInputTag.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoLocalTracker/ClusterParameterEstimator/interface/PixelClusterParameterEstimator.h"
#include "RecoLocalTracker/Records/interface/PixelCPEFastParamsRecord.h"
#include "RecoLocalTracker/Records/interface/TkPixelCPERecord.h"
#include "RecoTracker/MkFit/interface/MkFitClusterIndexToHit.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitAlpaka/interface/SupportedConfig.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/EventOfHitsProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/FitOuterStateProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/StatusCollect.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/StatusProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESData.h"
#include "RecoTracker/MkFitAlpaka/interface/fit/CpeESData.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/alpaka/EventOfHitsDeviceCollections.h"
#include "RecoTracker/MkFitAlpaka/plugins/alpaka/MkFitAlpakaFitCpeTables.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/engine/FitHandoff.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/fit/FitTracks.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaFitDeviceProducer : public global::EDProducer<> {
  public:
    explicit MkFitAlpakaFitDeviceProducer(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          tracksToken_{consumes(iConfig.getParameter<edm::InputTag>("tracks"))},
          eohToken_{consumes(iConfig.getParameter<edm::InputTag>("eventOfHits"))},
          pixelHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
          esToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"))},
          putToken_{produces()},
          statusToken_{produces()},
          useCpe_{iConfig.getParameter<bool>("cpe")},
          reportStats_{iConfig.getParameter<bool>("reportStats")},
          storeOuter_{iConfig.getParameter<bool>("storeOuterState")} {
      if (auto const q = iConfig.getParameter<edm::InputTag>("eventOfHitsQueue"); !q.label().empty()) {
        eohQueueToken_ = consumes(q);
        eohQueueCheck_ = true;
      }
      if (storeOuter_)
        outerToken_ = produces();
      fitOpt_.edgeOutliers = iConfig.getParameter<bool>("edgeOutliers");
      fitOpt_.edgeChi2Cut = float(iConfig.getParameter<double>("edgeChi2Cut"));
      fitOpt_.outlierRounds = iConfig.getParameter<int>("outlierRounds");
      if (fitOpt_.outlierRounds < 1)
        throw cms::Exception("Configuration") << "outlierRounds must be >= 1 (1 = stock)";
      fitOpt_.firstHitProp = iConfig.getParameter<int>("firstHitProp");  // DEVIATION D7
      sel_.enabled = iConfig.getParameter<bool>("candCutSel");
      sel_.minPt = float(iConfig.getParameter<double>("candMinPtCut"));
      sel_.minNHits = iConfig.getParameter<int>("candMinNHitsCut");
      sel_.minPtRelaxed = float(iConfig.getParameter<double>("candMinPtRelaxedCut"));
      sel_.minAbsEtaRelaxed = float(iConfig.getParameter<double>("candMinAbsEtaForRelaxedCut"));
      if (useCpe_) {
        clusterCollToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelClusters"));
        const auto src = iConfig.getParameter<std::string>("clusterSource");
        if (src != "auto" && src != "host" && src != "device" && src != "check" && src != "tracks")
          throw cms::Exception("Configuration")
              << "MkFitAlpakaFitDeviceProducer: clusterSource must be auto, device, tracks, host or check";
        constexpr bool kCpuBackend = std::is_same_v<Device, alpaka_common::DevHost>;
        if (src == "tracks" && !kCpuBackend)
          throw cms::Exception("Configuration") << "MkFitAlpakaFitDeviceProducer: clusterSource tracks is for CPU backends";
        clusTracks_ = src == "tracks" || (src == "auto" && kCpuBackend);
        clusDevice_ = src == "device" || src == "check" || (src == "auto" && !kCpuBackend);
        clusCheck_ = src == "check";
        if (clusDevice_) {
          digisToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelClustersSoA"));
          clustersSoAToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelClustersSoA"));
        }
        cpeToken_ = esConsumes(edm::ESInputTag("", iConfig.getParameter<std::string>("cpeTables")));
        const auto mode = iConfig.getParameter<std::string>("cpeCheck");
        if (mode != "throw" && mode != "warn" && mode != "off")
          throw cms::Exception("Configuration") << "MkFitAlpakaFitDeviceProducer: cpeCheck must be throw, warn or off";
        cpeCheckThrow_ = mode == "throw";
        cpeCheck_ = mode != "off";
        if (cpeCheck_) {
          stockCpeToken_ = esConsumes(edm::ESInputTag("", iConfig.getParameter<std::string>("pixelCPE")));
          geomToken_ = esConsumes();
        }
      }
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<edm::InputTag>("tracks", edm::InputTag("hltInitialStepTrackCandidatesMkFitDevice"))
          ->setComment("device TrackSoA of the building (MkFitAlpakaBuildProducer)");
      desc.add<edm::InputTag>("eventOfHits", edm::InputTag("hltMkFitEventOfHits"))
          ->setComment("device EventOfHits product (hits block, same rows as the hit wrappers)");
      desc.add<edm::InputTag>("pixelHits", edm::InputTag("hltMkFitSiPixelHits"))
          ->setComment("MkFitClusterIndexToHit of the pixel hits: only its size (strip hit row base)");
      desc.add<edm::ESInputTag>("esData", edm::ESInputTag("", ""));
      desc.add<bool>("candCutSel", false);
      desc.add<double>("candMinPtCut", 0);
      desc.add<int>("candMinNHitsCut", 0);
      desc.add<double>("candMinPtRelaxedCut", 0);
      desc.add<double>("candMinAbsEtaForRelaxedCut", 0);
      desc.add<bool>("cpe", true)->setComment("device PixelCPEGeneric with track angles (stock menu); false = no-CPE");
      desc.add<std::string>("cpeTables", "MkFitAlpakaFitCpe");
      desc.add<std::string>("pixelCPE", "PixelCPEGeneric");
      desc.add<std::string>("cpeCheck", "throw")
          ->setComment("per-IOV synthetic-cluster cross-check vs pixelCPE (R4-H1): throw | warn | off");
      desc.add<edm::InputTag>("pixelClusters", edm::InputTag("hltSiPixelClusters"))
          ->setComment("the clusters of the pixel rechits (mkFit pixel hit row = cluster key)");
      desc.add<std::string>("clusterSource", "auto")
          ->setComment("CPE cluster quantities: auto (device on GPU, tracks on CPU) | device (from the pixel digi/cluster "
                       "SoA) | tracks (CPU backends: host, hits on the selected tracks only) | host (all hits) | check");
      desc.add<edm::InputTag>("pixelClustersSoA", edm::InputTag("hltPhase2SiPixelClustersSoA"))
          ->setComment("producer of the pixel digi + cluster SoA (SiPixelDigisSoACollection, SiPixelClustersSoACollection)");
      desc.add<bool>("edgeOutliers", false)
          ->setComment("DEVIATION D3 (switch, stock = false): the first/last fitted hit is an outlier when the pass that "
                       "predicts it from all other hits has chi2 > edgeChi2Cut (as the KF EstimateCut)");
      desc.add<double>("edgeChi2Cut", 20.)->setComment("DEVIATION D3: the edge-hit outlier cut");
      desc.add<int>("outlierRounds", 1)
          ->setComment("DEVIATION D3 (switch, stock = 1): refits after outlier removal; all but the last are checked again");
      desc.add<int>("firstHitProp", 0)
          ->setComment("DEVIATION D7 (switch, stock = 0): propagate to the first hit of a fit pass instead of updating "
                       "there without propagation (trackreco#186); 1 = in the refits after outlier removal, 2 = every pass");
      desc.add<bool>("storeOuterState", false)
          ->setComment("DEVIATION D6 (switch, stock = false): also put the final fit's state at the outermost fitted hit "
                       "per row (FitOuterState collection) for the TrackExtras (MkFitAlpakaOutputTrackConverter outerStates)");
      desc.add<bool>("reportStats", false)
          ->setComment("validation: read the fit counters back every event (one sync) and print totals at endJob");
      desc.add<edm::InputTag>("eventOfHitsQueue", edm::InputTag(""))
          ->setComment(
              "D7-f: the EventOfHits producer's 'queue' product, set when the device EventOfHits is deleted "
              "early after this module; a different queue here makes the module wait for its work (counted)");
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      auto const& inD = iEvent.get(tracksToken_);  // first: take over the building's queue (D7-f)
      auto& queue = iEvent.queue();
      const int capacity = inD.const_view().metadata().size();
      mkfitdev::TrackSoADeviceCollection out(queue, capacity);
      // review H4: per-event status (fit overflow / hit-count mismatch, CPE cluster consistency)
      mkfitdev::MkFitStatusDeviceObject status(queue);
      mkfitdev::zeroStatus(queue, status);
      std::optional<mkfitdev::FitOuterStateDeviceCollection> outer;  // DEVIATION D6
      if (storeOuter_) {
        outer.emplace(queue, capacity);
        if (capacity > 0) {
          auto obuf = outer->buffer();
          alpaka::memset(queue, obuf, 0xff);  // pos = -1: no state
        }
      }
      if (capacity == 0) {  // R4-M1: skipped or seedless event; zero-filled scalars
        auto buf = out.buffer();
        alpaka::memset(queue, buf, 0x00);
        if (outer)
          iEvent.emplace(outerToken_, std::move(*outer));
        iEvent.emplace(putToken_, std::move(out));
        iEvent.emplace(statusToken_, std::move(status));
        releaseGuard(iEvent, queue);
        return;
      }
      auto const& es = iSetup.getData(esToken_);
      ::mkfitdev::checkSupportedFitConfig(es.hostConfigValue());  // review H3
      const uint32_t nPix = iEvent.get(pixelHitsToken_).hits().size();

      // 1. stock candCutSel on the device, order-preserving; out.nTracks() = kept count (device)
      mkfitdev::handoff::selectFitInput(queue, inD.const_view(), capacity, sel_, out.view());

      // 2. CPE cluster quantities (clusterSource): device kernels from the pixel digi/cluster SoA (GPU), host values for
      //    the hits on the selected tracks (CPU backends), or host values for every pixel hit (host / fallback)
      constexpr bool kHostMem = std::is_same_v<Device, alpaka_common::DevHost>;
      std::optional<cms::alpakatools::host_buffer<::mkfitdev::cpe::ClusterCpe[]>> hClus;
      std::optional<cms::alpakatools::device_buffer<Device, ::mkfitdev::cpe::ClusterCpe[]>> dClus;
      const ::mkfitdev::cpe::ClusterCpe* clusPtr = nullptr;
      ::mkfitdev::cpe::CpeTables dCpe{};
      std::optional<cms::alpakatools::device_buffer<Device, ::mkfitdev::handoff::ClusterCpeCounters>> dCnt;
      if (useCpe_) {
        auto const& cpeES = iSetup.getData(cpeToken_);
        auto const& tables = *cpeES.host;
        dCpe = cpeES.view();
        if (cpeCheck_)
          checkCpePerIOV(iSetup, tables);
        auto const& dsv = iEvent.get(clusterCollToken_);
        const uint32_t nAlloc = std::max<uint32_t>(nPix, 1);
        bool onDevice = clusDevice_;
        if (clusTracks_) {  // CPU backends: the selection above has completed (blocking queue); hits on its tracks only
          alpaka::wait(queue);  // no-op for the blocking serial queue, keeps the contract explicit
          hClus.emplace(cms::alpakatools::make_host_buffer<::mkfitdev::cpe::ClusterCpe[]>(queue, nAlloc));
          fillClusterCpeOnTracks(dsv, tables, es.view(), out.const_view(), nPix, hClus->data());
        }
        std::optional<cms::alpakatools::host_buffer<::mkfitdev::handoff::ClusterRef[]>> hRefs;
        if (onDevice) {
          hRefs.emplace(cms::alpakatools::make_host_buffer<::mkfitdev::handoff::ClusterRef[]>(queue, nAlloc));
          onDevice = fillClusterRefs(dsv, tables, nPix, hRefs->data());
          if (!onDevice)
            ++nHostFallback_;
        }
        if ((!onDevice && !clusTracks_) || clusCheck_) {
          hClus.emplace(cms::alpakatools::make_host_buffer<::mkfitdev::cpe::ClusterCpe[]>(queue, nAlloc));
          fillClusterCpe(dsv, tables, nPix, hClus->data());
        }
        if (onDevice) {
          auto const& digis = iEvent.get(digisToken_);
          auto const& clus = iEvent.get(clustersSoAToken_);
          dClus.emplace(cms::alpakatools::make_device_buffer<::mkfitdev::cpe::ClusterCpe[]>(queue, nAlloc));
          auto dRefs = cms::alpakatools::make_device_buffer<::mkfitdev::handoff::ClusterRef[]>(queue, nAlloc);
          alpaka::memcpy(queue, dRefs, *hRefs);
          dCnt.emplace(cms::alpakatools::make_device_buffer<::mkfitdev::handoff::ClusterCpeCounters>(queue));
          alpaka::memset(queue, *dCnt, 0);
          mkfitdev::handoff::buildClusterCpe(queue,
                                             digis.const_view(),
                                             digis.nDigis(),
                                             clus.const_view(),
                                             clus.nClusters(),
                                             dRefs.data(),
                                             nPix,
                                             dClus->data(),
                                             dCnt->data());
          clusPtr = dClus->data();
          if (clusCheck_)
            checkClusters(queue, *dClus, *hClus, *dCnt, nPix);
        } else if constexpr (kHostMem) {
          clusPtr = hClus->data();
        } else {
          dClus.emplace(cms::alpakatools::make_device_buffer<::mkfitdev::cpe::ClusterCpe[]>(queue, nAlloc));
          alpaka::memcpy(queue, *dClus, *hClus);
          clusPtr = dClus->data();
        }
      }

      // 3. the fit, in place on the selected rows; row count from the device
      mkfitdev::FitOuterStateSoAView outerView;
      if (outer)
        outerView = outer->view();
      auto dCounters = cms::alpakatools::make_device_buffer<mkfitdev::fit::FitCounters>(queue);
      alpaka::memset(queue, dCounters, 0);
      mkfitdev::fit::runFinalFit(queue,
                                 es.view(),
                                 iEvent.get(eohToken_).const_view().hits(),
                                 nPix,
                                 out.view(),
                                 capacity,
                                 dCounters.data(),
                                 useCpe_,
                                 dCpe,
                                 clusPtr,
                                 nullptr,  // hitStates: storeHitStates is not offered on the device-handoff path
                                 &out.view().nTracks(),
                                 fitOpt_,
                                 outer ? &outerView : nullptr);
      {
        mkfitdev::StatusSources src;
        src.add(&dCounters.data()->nOverflow, ::mkfitdev::kFitOverflow);
        src.add(&dCounters.data()->nHitCountMismatch, ::mkfitdev::kFitHitCountMismatch);
        if (dCnt) {
          src.add(&dCnt->data()->nClusterOverflow, ::mkfitdev::kCpeClusterOverflow);
          src.add(&dCnt->data()->nRefOutOfRange, ::mkfitdev::kCpeClusterRefErrors);
        }
        mkfitdev::collectStatus(queue, status, src);
      }
      if (reportStats_) {  // validation only: one sync per event
        auto h = cms::alpakatools::make_host_buffer<mkfitdev::fit::FitCounters>(queue);
        alpaka::memcpy(queue, h, dCounters);
        auto hn = cms::alpakatools::make_host_buffer<int32_t>(queue);
        alpaka::memcpy(queue, hn, cms::alpakatools::make_device_view(alpaka::getDev(queue), out.view().nTracks()));
        alpaka::wait(queue);
        auto const& c = *h.data();
        ++nEvents_;
        nIn_ += capacity;
        nTracks_ += *hn.data();
        nRefit_ += c.nRefit;
        nRemoved_ += c.nRemovedHits;
        nNaN_ += c.nNaN;
        nMismatch_ += c.nHitCountMismatch;
        nOverflow_ += c.nOverflow;
        nShadowed_ += c.nShadowed;
      }
      if (outer)
        iEvent.emplace(outerToken_, std::move(*outer));
      iEvent.emplace(putToken_, std::move(out));
      iEvent.emplace(statusToken_, std::move(status));
      releaseGuard(iEvent, queue);
    }

    void endJob() override {
      if (eohQueueCheck_)
        edm::LogPrint("MkFitAlpakaFitDeviceProducer")
            << "[fitdev] EOH_RELEASE sameQueue " << nSameQueue_ << " waited " << nQueueWait_;
      if (reportStats_)
        edm::LogPrint("MkFitAlpakaFitDeviceProducer")
            << "[fitdev] events " << nEvents_ << " input capacity " << nIn_ << " fitted tracks " << nTracks_
            << " refit(pass 2) " << nRefit_ << " removedHits " << nRemoved_ << " NaN-kept " << nNaN_
            << " nFoundHits-mismatch " << nMismatch_ << " overflow " << nOverflow_ << " collision-shadowed outliers "
            << nShadowed_;
      if (clusCheck_)
        edm::LogPrint("MkFitAlpakaFitDeviceProducer")
            << "[fitdev] CLUSTER_CHECK events " << nCheckEvents_ << " rows " << nCheckRows_ << " (host module >= 0) "
            << "mismatched " << nCheckBad_ << " module-differs " << nCheckModule_ << " | device counters: cluster "
            << "overflow " << nClusOverflow_ << " refOutOfRange " << nRefOut_ << " bigClusters(>256 px) " << nBig_;
      if (clusDevice_ || nHostFallback_ > 0)
        edm::LogPrint("MkFitAlpakaFitDeviceProducer")
            << "[fitdev] cluster source: device, host fallback events (no originalId) " << nHostFallback_;
    }

  private:
    // D7-f: the device EventOfHits is deleted right after this module (canDeleteEarly). Its buffer goes back to the
    // caching allocator with a marker on the queue it was allocated on; on any other queue, wait for this module's work
    // (which follows the building's) so no other queue can reuse the block while it is read.
    void releaseGuard(device::Event& iEvent, Queue& queue) const {
      if (!eohQueueCheck_)
        return;
#if !(defined(ALPAKA_ACC_CPU_B_SEQ_T_SEQ_ENABLED) || defined(ALPAKA_ACC_CPU_B_TBB_T_SEQ_ENABLED))
      {
        const auto eohQueue = iEvent.get(eohQueueToken_);
        if (eohQueue == reinterpret_cast<unsigned long long>(alpaka::getNativeHandle(queue))) {
          ++nSameQueue_;
          return;
        }
        alpaka::wait(queue);
        if (nQueueWait_++ == 0)
          edm::LogWarning("MkFitAlpakaFitDeviceProducer")
              << "event " << iEvent.id().event() << ": the fit does not run on the device EventOfHits queue; it waits "
              << "for its work before the early deletion (totals at endJob: EOH_RELEASE)";
      }
#else
      (void)iEvent;  // CPU backends: blocking queue, the work is done when produce() returns
      (void)queue;
#endif
    }

    // mkFit pixel hit row = cluster key (stock convertHits: the HitVec and the index map are keyed by the cluster
    // key); rows without a cluster (none expected: size = max(last key + 1, data size)) get module -1 (no CPE).
    static void fillClusterCpe(edmNew::DetSetVector<SiPixelCluster> const& dsv,
                               ::mkfitdev::cpe::CpeTablesHost const& tables,
                               uint32_t nPix,
                               ::mkfitdev::cpe::ClusterCpe* out) {
      auto const& data = dsv.data();
      const SiPixelCluster* base = data.data();
      for (auto const& ds : dsv) {
        auto it = tables.rawToModule.find(ds.detId());
        const int mod = it == tables.rawToModule.end() ? -1 : it->second;
        for (auto const& cl : ds) {
          const uint32_t key = &cl - base;
          if (key < nPix)
            out[key] = ::mkfitdev::cpe::clusterCpe(cl, mod);
        }
      }
      for (uint32_t i = data.size(); i < nPix; ++i) {
        out[i] = ::mkfitdev::cpe::ClusterCpe{};
        out[i].module = -1;
      }
    }

    // CPU backends: clusterCpe() for the pixel hits on the selected tracks (the round-5 fit's set), other rows untouched
    static void fillClusterCpeOnTracks(edmNew::DetSetVector<SiPixelCluster> const& dsv,
                                       ::mkfitdev::cpe::CpeTablesHost const& tables,
                                       ::mkfitdev::ESView const& es,
                                       ::mkfitdev::TrackSoAConstView trk,
                                       uint32_t nPix,
                                       ::mkfitdev::cpe::ClusterCpe* out) {
      std::vector<uint8_t> need(nPix, 0);
      for (int t = 0; t < trk.nTracks(); ++t) {
        const int nh = std::min<int>(trk[t].nTotalHits(), ::mkfitdev::kMaxTrkHits);
        for (int h = 0; h < nh; ++h) {
          const auto hot = trk[t].hits().hot[h];
          if (hot.index >= 0 && uint32_t(hot.index) < nPix && es.layers[hot.layer].is_pixel())
            need[hot.index] = 1;
        }
      }
      auto const& data = dsv.data();
      const SiPixelCluster* base = data.data();
      for (auto const& ds : dsv) {
        int mod = -2;  // looked up on the first needed cluster of the module
        for (auto const& cl : ds) {
          const uint32_t key = &cl - base;
          if (key >= nPix || !need[key])
            continue;
          if (mod == -2) {
            auto it = tables.rawToModule.find(ds.detId());
            mod = it == tables.rawToModule.end() ? -1 : it->second;
          }
          out[key] = ::mkfitdev::cpe::clusterCpe(cl, mod);
        }
      }
      for (uint32_t i = data.size(); i < nPix; ++i)
        if (need[i]) {
          out[i] = ::mkfitdev::cpe::ClusterCpe{};
          out[i].module = -1;
        }
    }

    // device mode: per mkFit pixel row the SoA module index and the SoA cluster id (SiPixelCluster::originalId);
    // false if a cluster has no originalId (persisted clusters): the caller falls back to the host computation
    static bool fillClusterRefs(edmNew::DetSetVector<SiPixelCluster> const& dsv,
                                ::mkfitdev::cpe::CpeTablesHost const& tables,
                                uint32_t nPix,
                                ::mkfitdev::handoff::ClusterRef* out) {
      auto const& data = dsv.data();
      const SiPixelCluster* base = data.data();
      for (auto const& ds : dsv) {
        auto it = tables.rawToModule.find(ds.detId());
        const int mod = it == tables.rawToModule.end() ? -1 : it->second;
        for (auto const& cl : ds) {
          const uint32_t key = &cl - base;
          if (cl.originalId() == SiPixelCluster::invalidClusterId)
            return false;
          if (key < nPix)
            out[key] = ::mkfitdev::handoff::ClusterRef{mod, int32_t(cl.originalId())};
        }
      }
      for (uint32_t i = data.size(); i < nPix; ++i)
        out[i] = ::mkfitdev::handoff::ClusterRef{-1, 0};
      return true;
    }

    // validation (clusterSource = "check"): device ClusterCpe vs the host one, bitwise, rows with a known module
    void checkClusters(Queue& queue,
                       cms::alpakatools::device_buffer<Device, ::mkfitdev::cpe::ClusterCpe[]> const& d,
                       cms::alpakatools::host_buffer<::mkfitdev::cpe::ClusterCpe[]> const& h,
                       cms::alpakatools::device_buffer<Device, ::mkfitdev::handoff::ClusterCpeCounters> const& dCnt,
                       uint32_t nPix) const {
      auto hd = cms::alpakatools::make_host_buffer<::mkfitdev::cpe::ClusterCpe[]>(queue, std::max<uint32_t>(nPix, 1));
      alpaka::memcpy(queue, hd, d);
      auto hc = cms::alpakatools::make_host_buffer<::mkfitdev::handoff::ClusterCpeCounters>(queue);
      alpaka::memcpy(queue, hc, dCnt);
      alpaka::wait(queue);
      long rows = 0, bad = 0, mod = 0;
      for (uint32_t i = 0; i < nPix; ++i) {
        auto const& a = h.data()[i];
        auto const& b = hd.data()[i];
        if (a.module < 0)
          continue;
        ++rows;
        if (a.module != b.module) {
          ++mod;
          continue;
        }
        if (std::memcmp(&a.e, &b.e, sizeof(a.e)) != 0 || a.charge != b.charge) {
          if (++bad <= 3 && nCheckBad_ < 3)
            edm::LogPrint("MkFitAlpakaFitDeviceProducer")
                << "[fitdev] CLUSTER_MISMATCH row " << i << " module " << a.module << " host rows " << a.e.minRow << "-"
                << a.e.maxRow << " cols " << a.e.minCol << "-" << a.e.maxCol << " q " << a.e.qfX << "/" << a.e.qlX << "/"
                << a.e.qfY << "/" << a.e.qlY << " charge " << a.charge << " | device rows " << b.e.minRow << "-"
                << b.e.maxRow << " cols " << b.e.minCol << "-" << b.e.maxCol << " q " << b.e.qfX << "/" << b.e.qlX << "/"
                << b.e.qfY << "/" << b.e.qlY << " charge " << b.charge;
        }
      }
      ++nCheckEvents_;
      nCheckRows_ += rows;
      nCheckBad_ += bad;
      nCheckModule_ += mod;
      nClusOverflow_ += hc.data()->nClusterOverflow;
      nRefOut_ += hc.data()->nRefOutOfRange;
      nBig_ += hc.data()->nBigClusters;
    }

    // R4-H1 (as MkFitAlpakaFitProducer): once per IOV of the two CPE records, the device CPE tables against the stock
    // CPE object of the menu fit, on synthetic clusters.
    void checkCpePerIOV(device::EventSetup const& iSetup, ::mkfitdev::cpe::CpeTablesHost const& t) const {
      edm::EventSetup const& es = iSetup;
      const unsigned long long ids[2] = {es.get<TkPixelCPERecord>().cacheIdentifier(),
                                         es.get<PixelCPEFastParamsRecord>().cacheIdentifier()};
      // R6-M5: lock-free fast path once the IOV is checked; the ids are stored only after the check has passed (or
      // warned), so with a failing check under 'throw' every stream checks and throws
      if (checkedIds_[0].load(std::memory_order_acquire) == ids[0] &&
          checkedIds_[1].load(std::memory_order_acquire) == ids[1])
        return;
      std::lock_guard<std::mutex> lock(checkMutex_);
      if (checkedIds_[0].load() == ids[0] && checkedIds_[1].load() == ids[1])
        return;
      const auto st = ::mkfitdev::cpe::crossCheckCpeSynthetic(
          iSetup.getData(stockCpeToken_), iSetup.getData(geomToken_), t, kCheckModuleStride);
      edm::LogPrint("MkFitAlpakaFitDeviceProducer")
          << "[fitdev] CPE cross-check vs the stock CPE object (synthetic clusters): " << st.summary();
      if (st.nBad > 0) {
        if (cpeCheckThrow_)
          throw cms::Exception("MkFitAlpakaFitCpe")
              << "the device PixelCPEGeneric differs from the stock CPE object of the menu fit: " << st.summary();
        edm::LogWarning("MkFitAlpakaFitCpe") << "device CPE differs from the stock CPE object: " << st.summary();
      }
      checkedIds_[1].store(ids[1], std::memory_order_release);
      checkedIds_[0].store(ids[0], std::memory_order_release);
    }

    static constexpr int kCheckModuleStride = 8;

    const device::EDGetToken<mkfitdev::TrackSoADeviceCollection> tracksToken_;
    const device::EDGetToken<mkfitdev::EventOfHitsDeviceCollection> eohToken_;
    const edm::EDGetTokenT<MkFitClusterIndexToHit> pixelHitsToken_;
    const device::ESGetToken<::mkfitdev::ESData<Device>, TrackerRecoGeometryRecord> esToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> putToken_;
    const device::EDPutToken<mkfitdev::MkFitStatusDeviceObject> statusToken_;
    device::EDPutToken<mkfitdev::FitOuterStateDeviceCollection> outerToken_;  // DEVIATION D6 (unset: off)
    const bool useCpe_;
    const bool reportStats_;
    const bool storeOuter_;  // DEVIATION D6 switch
    mkfitdev::fit::FitOptions fitOpt_;
    ::mkfitdev::handoff::CandCutSel sel_;
    edm::EDGetTokenT<edmNew::DetSetVector<SiPixelCluster>> clusterCollToken_;
    device::ESGetToken<::mkfitdev::cpe::CpeESData<Device>, PixelCPEFastParamsRecord> cpeToken_;
    edm::ESGetToken<PixelClusterParameterEstimator, TkPixelCPERecord> stockCpeToken_;
    edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    bool cpeCheck_ = false, cpeCheckThrow_ = true;
    bool clusDevice_ = false, clusCheck_ = false, clusTracks_ = false;
    device::EDGetToken<SiPixelDigisSoACollection> digisToken_;
    device::EDGetToken<SiPixelClustersSoACollection> clustersSoAToken_;
    mutable std::atomic<long> nHostFallback_{0}, nCheckEvents_{0}, nCheckRows_{0}, nCheckBad_{0}, nCheckModule_{0},
        nClusOverflow_{0}, nRefOut_{0}, nBig_{0};
    mutable std::mutex checkMutex_;
    mutable std::atomic<unsigned long long> checkedIds_[2] = {0, 0};
    mutable std::atomic<long> nEvents_{0}, nIn_{0}, nTracks_{0}, nRefit_{0}, nRemoved_{0}, nNaN_{0}, nMismatch_{0},
        nOverflow_{0}, nShadowed_{0};
    edm::EDGetTokenT<unsigned long long> eohQueueToken_;
    bool eohQueueCheck_ = false;
    mutable std::atomic<long> nSameQueue_{0}, nQueueWait_{0};
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaFitDeviceProducer);
