// Integration (round 3): the device mkFit chain of the LST step in one module, wired from the lanes' entry points.
//   seeds (host MkFitSeedWrapper -> device seed table) -> seeds::importSeeds (lane seeds) into the engine buffers
//   -> engine forward search with select's K2 (EngineSelectK2) -> pre-filter + compaction -> compactify
//   -> backward fit (lane bkfit, makeEngineBackwardFit) -> beginBkwSearch -> backward search (K2 again)
//   -> post-filter with repack + compaction (engineRunChain, lane engine)
//   -> seeds::runChainTail (export of the front candidate + duplicate cleaner, lane seeds/clean) -> TrackSoA.
// Products: TrackSoA (final, = stock MkFitProducer output) and TrackSoA "export" (before the duplicate cleaner);
// with stages = True also "fwd" and "bkfit" (front candidate per seed after the forward search / the backward fit).
// Inputs from the device EventOfHits product (lane hits/seeds) and the MkFitAlpaka ES product (lane es).
// No physics of its own: every step is a lane's exported function.
#include <memory>
#include <vector>

#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "DataFormats/Portable/interface/alpaka/PortableCollection.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFit/interface/MkFitHitWrapper.h"
#include "RecoTracker/MkFit/interface/MkFitSeedWrapper.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitAlpaka/interface/alpaka/EventOfHitsProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/EngineFromES.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandsEngine.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESData.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/SeedsHostPack.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsAlgo.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/SupportedConfig.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/StatusCollect.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/StatusProduct.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/PropagationFlagsAdapter.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/bkfit/BkFitLaunch.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/engine/EngineSelectBridge.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaChainProducer : public global::EDProducer<> {
  public:
    explicit MkFitAlpakaChainProducer(edm::ParameterSet const& iConfig)
        : EDProducer<>(iConfig),
          seedsToken_{consumes(iConfig.getParameter<edm::InputTag>("seeds"))},
          pixelHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
          eohToken_{consumes(iConfig.getParameter<edm::InputTag>("eventOfHits"))},
          mkFitGeomToken_{esConsumes()},
          esToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"))},
          esHostToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"))},
          tracksToken_{produces()},
          exportToken_{produces("export")},
          fwdToken_{produces("fwd")},
          bkfitToken_{produces("bkfit")},
          hotsPerSeed_{iConfig.getParameter<int>("hotsPerSeed")},
          removeDuplicates_{iConfig.getParameter<bool>("removeDuplicates")},
          backwardFit_{iConfig.getParameter<bool>("backwardFit")},
          verbose_{iConfig.getParameter<bool>("verbose")},
          stages_{iConfig.getParameter<bool>("stages")},
          statusToken_{produces()} {
      // stock MkFitProducer parameters the device chain depends on (review H3; checked against the ES per event)
      moduleCfg_.clustersToSkip = iConfig.getParameter<edm::InputTag>("clustersToSkip").label();
      moduleCfg_.buildingRoutine = iConfig.getParameter<std::string>("buildingRoutine");
      moduleCfg_.seedCleaning = iConfig.getParameter<bool>("seedCleaning");
      moduleCfg_.removeDuplicates = removeDuplicates_;
      moduleCfg_.backwardFitInCMSSW = iConfig.getParameter<bool>("backwardFitInCMSSW");
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add("seeds", edm::InputTag{"hltInitialStepMkFitSeeds"});
      desc.add("pixelHits", edm::InputTag{"hltMkFitSiPixelHits"})->setComment("only for nPixel (strip hit base)");
      desc.add("eventOfHits", edm::InputTag{"seedsEventOfHits"})->setComment("device EventOfHits product");
      desc.add("esData", edm::ESInputTag{"", ""})->setComment("MkFitAlpakaESProducer ComponentName");
      desc.add("hotsPerSeed", 256);
      desc.add("removeDuplicates", true);
      // stock MkFitProducer parameters, accepted only inside the validated envelope (interface/SupportedConfig.h)
      desc.add("clustersToSkip", edm::InputTag())->setComment("must be empty: no hit mask on the device");
      desc.add<std::string>("buildingRoutine", "cloneEngine");
      desc.add("seedCleaning", true);
      desc.add("backwardFitInCMSSW", false);
      desc.add("backwardFit", true)->setComment("false: identity backward fit (debug)");
      desc.add("verbose", false);
      desc.add("stages", false)
          ->setComment(
              "also put the front candidate of every seed after the forward search ('fwd') and after the backward "
              "fit ('bkfit') as TrackSoA (exportTrack(true) form); runs the engineRunChain steps one by one");
      descriptions.addWithDefaultLabel(desc);
    }

    void produce(edm::StreamID, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      auto& queue = iEvent.queue();
      const auto& seedsIn = iEvent.get(seedsToken_).seeds();
      const uint32_t nPixel = iEvent.get(pixelHitsToken_).hits().size();
      const auto& eohD = iEvent.get(eohToken_);
      const auto& ti = iSetup.getData(mkFitGeomToken_).trackerInfo();
      const auto& esD = iSetup.getData(esToken_);
      const auto& esH = iSetup.getData(esHostToken_);
      const ::mkfitdev::ESConfig& cfg = esH.hostConfigValue();
      // cms-sw#52015 backward-fit outliers and backward-search gate are wired only in the production build module
      if (cfg.backward_fit_outlier_chi2 > 0.f || cfg.backward_search_min_pixel_layers > 0)
        throw cms::Exception("MkFitAlpakaUnsupportedConfig")
            << "MkFitAlpakaChainProducer (validation) does not implement the cms-sw#52015 backward-fit outlier "
               "rejection / backward-search gate; use MkFitAlpakaBuildProducer";
      ::mkfitdev::checkSupportedBuildConfig(cfg, moduleCfg_);  // review H3 (no allocation when it passes)
      const int n = seedsIn.size();
      const int hps = hotsPerSeed_;
      // review H4: per-event status product, zeroed here, counters collected after the tail
      mkfitdev::MkFitStatusDeviceObject status(queue);
      mkfitdev::zeroStatus(queue, status);
      if (n == 0) {
        iEvent.emplace(statusToken_, std::move(status));
        iEvent.emplace(tracksToken_, mkfitdev::TrackSoADeviceCollection(queue, 0));
        iEvent.emplace(exportToken_, mkfitdev::TrackSoADeviceCollection(queue, 0));
        iEvent.emplace(fwdToken_, mkfitdev::TrackSoADeviceCollection(queue, 0));
        iEvent.emplace(bkfitToken_, mkfitdev::TrackSoADeviceCollection(queue, 0));
        return;
      }

      // 1. seed import (lane seeds) straight into the engine buffers
      ::mkfitdev::SeedsHostCollection seedsH(queue, n);
      ::mkfitdev::packSeeds(seedsIn, [](int, int, float*) {}, seedsH.view());
      mkfitdev::SeedsDeviceCollection seedsD(queue, n);
      alpaka::memcpy(queue, seedsD.buffer(), seedsH.buffer());
      const auto ev = eohD.const_view();
      mkfitdev::seeds::DeviceHitPositions hitPos;
      hitPos.x = ev.hits().metadata().addressOf_x();
      hitPos.y = ev.hits().metadata().addressOf_y();
      hitPos.z = ev.hits().metadata().addressOf_z();
      hitPos.layerHitBase = ev.layers().metadata().addressOf_hitBase();
      mkfitdev::EngineBuffers b(queue, n, hps);
      mkfitdev::EngineBuffers work(queue, n, hps);
      mkfitdev::seeds::importSeeds(queue,
                                   seedsD.view(),
                                   n,
                                   ::mkfitdev::seedPartitionLimits(ti),
                                   b.seeds.view(),
                                   b.slots.view(),
                                   b.hots.view(),
                                   hps,
                                   hitPos);
      auto nKeptH = cms::alpakatools::make_host_buffer<int32_t>(queue);
      alpaka::memcpy(queue,
                     nKeptH,
                     cms::alpakatools::make_device_view<int32_t>(alpaka::getDev(queue), seedsD.view().nKept()));

      // 2. engine tables from the ES product (host config + host layer/module tables, copied per event)
      const auto plan = ::mkfitdev::makeEnginePlan(cfg);
      mkfitdev::EngineStepTablesDevice fwdTables(queue, ::mkfitdev::makeEngineStepTables(plan, true));
      mkfitdev::EngineStepTablesDevice bkwTables(queue, ::mkfitdev::makeEngineStepTables(plan, false));
      const auto layersV = ::mkfitdev::makeEngineLayerParams(esH.layers->const_view(), esH.sizes.nLayers);
      const auto modulesV = ::mkfitdev::makeEngineModules(esH.modules->const_view(), esH.sizes.nModules);
      auto layersD = cms::alpakatools::make_device_buffer<::mkfitdev::EngineLayerParams[]>(queue, layersV.size());
      auto modulesD = cms::alpakatools::make_device_buffer<::mkfitdev::EngineModule[]>(queue, modulesV.size());
      alpaka::memcpy(queue, layersD, cms::alpakatools::make_host_view(layersV.data(), layersV.size()));
      alpaka::memcpy(queue, modulesD, cms::alpakatools::make_host_view(modulesV.data(), modulesV.size()));

      const auto hv = ev.hits();
      ::mkfitdev::EngineHitInputs in{hv.metadata().addressOf_x(),
                                     hv.metadata().addressOf_y(),
                                     hv.metadata().addressOf_z(),
                                     hv.metadata().addressOf_e00(),
                                     hv.metadata().addressOf_e10(),
                                     hv.metadata().addressOf_e11(),
                                     hv.metadata().addressOf_e20(),
                                     hv.metadata().addressOf_e21(),
                                     hv.metadata().addressOf_e22(),
                                     hv.metadata().addressOf_packed(),
                                     nPixel,
                                     modulesD.data(),
                                     layersD.data()};
      const ::mkfitdev::ESView esView = esD.view();
      mkfitdev::EnginePropConfig pc{cfg.prop_config.finding_inter_layer_pflags,
                                    cfg.prop_config.finding_intra_layer_pflags,
                                    esView.material,
                                    cfg.prop_config.finding_requires_propagation_to_hit_pos};
      const auto ipFwd = ::mkfitdev::makeEngineIterParams(cfg.params);
      const auto ipBkw = ::mkfitdev::makeEngineIterParams(cfg.backward_params);

      // 3. K2 (lane select) through the engine bridge; list/output capacity nSeeds * kMaxCandsPerSeed
      const int nList = n * ::mkfitdev::kMaxCandsPerSeed;
      PortableCollection<::mkfitdev::SelListSoA> list(queue, nList);
      PortableCollection<::mkfitdev::PropStateSoA> props(queue, nList);
      PortableCollection<::mkfitdev::SelHitsSoA> sels(queue, nList);

      alpaka::wait(queue);
      const int nKept = *nKeptH.data();
      mkfitdev::EngineSelectK2 k2{list.view(),
                                  props.view(),
                                  sels.view(),
                                  esView,
                                  ev.layers(),
                                  ev.binnedHits(),
                                  ev.bins(),
                                  ev.hits(),
                                  nKept};
      // 4. backward fit (lane bkfit)
      mkfitdev::EngineBackwardFitFn bkfit;
      if (backwardFit_)
        bkfit = mkfitdev::makeEngineBackwardFit(
            in, ::mkfitdev::prop::propagationFlags(cfg, esView.material, ::mkfitdev::prop::PropStage::BackwardFit));

      // 5. clone engine (lane engine)
      int nOut = 0;
      mkfitdev::TrackSoADeviceCollection fwdD(queue, stages_ ? n : 0);
      mkfitdev::TrackSoADeviceCollection bkfitD(queue, stages_ ? n : 0);
      mkfitdev::EngineBuffers* resP = nullptr;
      if (!stages_) {
        resP = &mkfitdev::engineRunChain(queue,
                                         b,
                                         work,
                                         fwdTables,
                                         bkwTables,
                                         k2,
                                         k2,
                                         bkfit,
                                         in,
                                         in,
                                         pc,
                                         ipFwd,
                                         ipBkw,
                                         cfg.params.minHitsQF,
                                         cfg.backward_fit_min_hits,
                                         nKept,
                                         nOut);
      } else {
        // the same calls as engineRunChain (Cands.dev.cc), with stage exports in between
        auto nD = cms::alpakatools::make_device_buffer<int32_t>(queue);
        auto nH = cms::alpakatools::make_host_buffer<int32_t>(queue);
        auto exportFront = [&](mkfitdev::EngineBuffers& eb, int ns, mkfitdev::TrackSoADeviceCollection& out) {
          *nH.data() = ns;
          alpaka::memcpy(queue, nD, nH);
          mkfitdev::seeds::exportBestCands(queue,
                                           eb.seeds.const_view(),
                                           eb.slots.const_view(),
                                           eb.hots.const_view(),
                                           hps,
                                           nD.data(),
                                           n,
                                           nullptr,
                                           out.view());
          alpaka::wait(queue);
        };
        mkfitdev::engineSearch(queue, b, fwdTables, true, k2, in, pc, ipFwd, nKept);
        exportFront(b, nKept, fwdD);
        int m = mkfitdev::engineFilterCompact(queue, b, work, false, cfg.params.minHitsQF, nKept);
        mkfitdev::engineCompactifyBeginBkw(queue, work, false, cfg.backward_fit_min_hits, true, false, m);
        if (bkfit)
          bkfit(queue, work, m);
        exportFront(work, m, bkfitD);
        mkfitdev::engineCompactifyBeginBkw(queue, work, false, cfg.backward_fit_min_hits, false, true, m);
        mkfitdev::engineSearch(queue, work, bkwTables, false, k2, in, pc, ipBkw, m);
        nOut = mkfitdev::engineFilterCompact(queue, work, b, true, cfg.params.minHitsQF, m);
        resP = &b;
      }
      mkfitdev::EngineBuffers& res = *resP;

      // 6. chain tail (lane seeds + clean): the engine's post-filter already repacked the front candidates, so the
      //    tail's filter runs in the normal representation (bkwRep = false; it passes every surviving seed again)
      auto nOutD = cms::alpakatools::make_device_buffer<int32_t>(queue);
      alpaka::memset(queue, nOutD, 0);
      auto nOutH = cms::alpakatools::make_host_buffer<int32_t>(queue);
      *nOutH.data() = nOut;
      alpaka::memcpy(queue, nOutD, nOutH);
      const float dc[4] = {cfg.dc_fracSharedHits, cfg.dc_drth_central, cfg.dc_drth_obarrel, cfg.dc_drth_forward};
      const bool pixPriority = cfg.duplicate_cleaner == ::mkfitdev::DuplicateCleaner::SharedHitsPixelPriority;
      mkfitdev::TrackSoADeviceCollection exportedD(queue, n);
      mkfitdev::TrackSoADeviceCollection finalD(queue, n);
      mkfitdev::seeds::runChainTail(queue,
                                    res.seeds.view(),
                                    res.slots.view(),
                                    res.hots.view(),
                                    hps,
                                    nOutD.data(),
                                    n,
                                    false,
                                    cfg.backward_params.minHitsQF,
                                    removeDuplicates_,
                                    dc,
                                    pixPriority ? cfg.pixel_layer_mask : nullptr,
                                    exportedD.view(),
                                    finalD.view());
      if (verbose_) {
        alpaka::wait(queue);
        edm::LogPrint("MkFitAlpakaChain") << "CHAIN event " << iEvent.id().event() << " seeds " << n << " kept "
                                          << nKept << " afterBkw " << nOut << " minHitsQF fwd/bkw "
                                          << cfg.params.minHitsQF << "/" << cfg.backward_params.minHitsQF;
      }
      {
        mkfitdev::StatusSources src;
        src.add(seedsD.view().metadata().addressOf_nOverflowHits(), ::mkfitdev::kSeedHitsTruncated);
        src.add(ev.layers().metadata().addressOf_nOverflowFirst(), ::mkfitdev::kEohOverflowFirst);
        src.add(ev.layers().metadata().addressOf_nOverflowCount(), ::mkfitdev::kEohOverflowCount);
        // the engine's compactions carry the seed-pool counters forward: the result buffer holds the totals
        src.add(res.seeds.view().metadata().addressOf_nOverflowHots(), ::mkfitdev::kHotOverflowSeeds);
        src.add(res.seeds.view().metadata().addressOf_nOverflowOpts(), ::mkfitdev::kOptsOverflow);
        src.add(res.seeds.view().metadata().addressOf_nOverflowExtras(), ::mkfitdev::kExtrasOverflow);
        // the cleaner copies the export counters into the final TrackSoA
        src.add(finalD.view().metadata().addressOf_nOverflowTracks(), ::mkfitdev::kTrackOverflow);
        src.add(finalD.view().metadata().addressOf_nOverflowHits(), ::mkfitdev::kTrackHitsOverflow);
        mkfitdev::collectStatus(queue, status, src);
      }
      iEvent.emplace(statusToken_, std::move(status));
      iEvent.emplace(exportToken_, std::move(exportedD));
      iEvent.emplace(tracksToken_, std::move(finalD));
      iEvent.emplace(fwdToken_, std::move(fwdD));
      iEvent.emplace(bkfitToken_, std::move(bkfitD));
    }

  private:
    const edm::EDGetTokenT<MkFitSeedWrapper> seedsToken_;
    const edm::EDGetTokenT<MkFitHitWrapper> pixelHitsToken_;
    const device::EDGetToken<mkfitdev::EventOfHitsDeviceCollection> eohToken_;
    const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
    const device::ESGetToken<::mkfitdev::ESData<Device>, TrackerRecoGeometryRecord> esToken_;
    const edm::ESGetToken<::mkfitdev::ESDataHost, TrackerRecoGeometryRecord> esHostToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> tracksToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> exportToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> fwdToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> bkfitToken_;
    const int hotsPerSeed_;
    const bool removeDuplicates_;
    const bool backwardFit_;
    const bool verbose_;
    const bool stages_;
    const device::EDPutToken<mkfitdev::MkFitStatusDeviceObject> statusToken_;
    ::mkfitdev::BuildModuleConfig moduleCfg_;
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaChainProducer);
