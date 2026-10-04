// MkFitAlpakaBuildProducer (round 4, lane build): the device equivalent of the stock MkFitProducer of the LST step
// (hltInitialStepTrackCandidatesMkFit): seed import -> forward search -> pre-filter -> backward fit -> backward search
// -> post-filter (the ONE repack, D4-C2) -> export + duplicate cleaner -> TrackSoA.
// - Inputs: host seeds (MkFitSeedWrapper), the device EventOfHits product, the MkFitAlpaka ES product, MkFitGeometry.
// - Candidate storage is module-internal scratch (D4-M4): two EngineBuffers sets + the K2 list/props/sels. D7-c:
//   per-stream scratch (scratchReuse): allocated once per EDM stream at a grow-only seed capacity and reused every
//   event (serial backend: no page faults from uncached buffers; GPU: no allocator calls). Rows past the event's seed
//   count are never read (grids and loops are bounded by the seed count and the device row counts).
// - Engine tables (layer plans, layer/module tables), iteration parameters and propagation flags are built from the ES
//   product ONCE per IOV and device (review C1 c, M2), not per event.
// - No host synchronization on the event path: the seed count after import and the survivor counts of both filters
//   stay on the device (EngineBuffers::nRowsDev); grids are sized from the seed capacity.
// - stages = True (validation): stage exports in stock snapshot form for the harness stockStages references:
//   'fwd' (all candidates, exportTrack(false)), 'bkfit' (front), 'bkwsearch' (all, backward chain), 'postFilter'
//   (front, exportTrack(false)); 'export' (before the cleaner) is always produced.
// - checkStatus = True (validation): one sync at the end of the event reads the overflow / C2 counters and warns on
//   non-zero values (totals at endJob). The production status product is integ's (D4-H4).
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Framework/interface/EventSetup.h"
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
#include "RecoTracker/MkFit/interface/MkFitClusterIndexToHit.h"
#include "RecoTracker/MkFit/interface/MkFitSeedWrapper.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "RecoTracker/MkFitAlpaka/interface/SupportedConfig.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/EventOfHitsProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/StatusCollect.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/StatusProduct.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsPackDevice.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/LstSeedFit.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/LstSeedFitRunner.h"
#include "RecoTracker/MkFitAlpaka/interface/alpaka/tracks/TrackSoADeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/EngineFromES.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandsEngine.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESData.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/SeedsHostPack.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsAlgo.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/SeedsDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/PropagationFlagsAdapter.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/bkfit/BkFitLaunch.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/engine/EngineSelectBridge.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  namespace {
    // Everything the engine needs from the ES, built once per (device, IOV).
    struct BuildTables {
      BuildTables(Queue& queue, ::mkfitdev::ESDataHost const& esH, ::mkfitdev::ESView const& esView, ::mkfitdev::SeedPartitionLimits lim)
          : plan(::mkfitdev::makeEnginePlan(esH.hostConfigValue())),
            fwd(queue, ::mkfitdev::makeEngineStepTables(plan, true)),
            bkw(queue, ::mkfitdev::makeEngineStepTables(plan, false)),
            layers(cms::alpakatools::make_device_buffer<::mkfitdev::EngineLayerParams[]>(
                queue, esH.sizes.nLayers > 0 ? esH.sizes.nLayers : 1)),
            modules(cms::alpakatools::make_device_buffer<::mkfitdev::EngineModule[]>(
                queue, esH.sizes.nModules > 0 ? esH.sizes.nModules : 1)),
            limits(lim) {
        const ::mkfitdev::ESConfig& cfg = esH.hostConfigValue();
        const auto layersV = ::mkfitdev::makeEngineLayerParams(esH.layers->const_view(), esH.sizes.nLayers);
        const auto modulesV = ::mkfitdev::makeEngineModules(esH.modules->const_view(), esH.sizes.nModules);
        alpaka::memcpy(queue, layers, cms::alpakatools::make_host_view(layersV.data(), layersV.size()));
        alpaka::memcpy(queue, modules, cms::alpakatools::make_host_view(modulesV.data(), modulesV.size()));
        alpaka::wait(queue);  // once per IOV: the host vectors go away, other events' queues use the tables
        pc = mkfitdev::EnginePropConfig{cfg.prop_config.finding_inter_layer_pflags,
                                        cfg.prop_config.finding_intra_layer_pflags,
                                        esView.material,
                                        cfg.prop_config.finding_requires_propagation_to_hit_pos};
        bkfitPF = ::mkfitdev::prop::makePropagationFlags(cfg.prop_config.backward_fit_pflags, esView.material);
        ipFwd = ::mkfitdev::makeEngineIterParams(cfg.params);
        ipBkw = ::mkfitdev::makeEngineIterParams(cfg.backward_params);
        minHitsQFPre = cfg.params.minHitsQF;
        minHitsQFPost = cfg.backward_params.minHitsQF;
        backwardFitMinHits = cfg.backward_fit_min_hits;
        dc[0] = cfg.dc_fracSharedHits;
        dc[1] = cfg.dc_drth_central;
        dc[2] = cfg.dc_drth_obarrel;
        dc[3] = cfg.dc_drth_forward;
        // cms-sw#52015
        pixelPriority = cfg.duplicate_cleaner == ::mkfitdev::DuplicateCleaner::SharedHitsPixelPriority;
        for (int w = 0; w < 4; ++w)
          pixelLayers[w] = cfg.pixel_layer_mask[w];
        bkfitOutliers = ::mkfitdev::bkfit::OutlierParams{
            cfg.backward_fit_outlier_chi2, cfg.backward_fit_max_outliers, cfg.backward_fit_outlier_min_pt};
        bkwGate = ::mkfitdev::BkwSearchGate{cfg.backward_search_min_pixel_layers,
                                            cfg.backward_search_prompt_max_d0,
                                            {cfg.pixel_layer_mask[0],
                                             cfg.pixel_layer_mask[1],
                                             cfg.pixel_layer_mask[2],
                                             cfg.pixel_layer_mask[3]},
                                            0.f,
                                            0.f};
      }
      ::mkfitdev::EnginePlan plan;
      mkfitdev::EngineStepTablesDevice fwd, bkw;
      cms::alpakatools::device_buffer<Device, ::mkfitdev::EngineLayerParams[]> layers;
      cms::alpakatools::device_buffer<Device, ::mkfitdev::EngineModule[]> modules;
      ::mkfitdev::SeedPartitionLimits limits;
      mkfitdev::EnginePropConfig pc;
      ::mkfitdev::prop::PropagationFlags bkfitPF;
      ::mkfitdev::EngineIterParams ipFwd, ipBkw;
      int minHitsQFPre = 0, minHitsQFPost = 0, backwardFitMinHits = 0;
      float dc[4] = {0.f, 0.f, 0.f, 0.f};
      bool pixelPriority = false;
      uint64_t pixelLayers[4] = {0, 0, 0, 0};
      ::mkfitdev::bkfit::OutlierParams bkfitOutliers{};
      ::mkfitdev::BkwSearchGate bkwGate{};
    };

    // D7-c: clone-engine scratch of one EDM stream. The EDM stream starts the next event only after the device work
    // of this one has completed (EDMetadata synchronizes when the event's products go away), so reuse is safe on
    // every backend.
    struct BuildScratch {
      int cap = 0, hps = 0;
      uint64_t nAllocs = 0;
      std::optional<mkfitdev::EngineBuffers> b, work;
      std::optional<PortableCollection<::mkfitdev::SelListSoA>> list;
      std::optional<PortableCollection<::mkfitdev::PropStateSoA>> props;
      std::optional<PortableCollection<::mkfitdev::SelHitsSoA>> sels;

      // Capacity for n seeds: exact without reuse (the per-event path), else grow-only with 25% headroom.
      void prepare(Queue& queue, int n, int hotsPerSeed, bool reuse) {
        if (!reuse || n > cap || hotsPerSeed != hps || !b) {
          cap = reuse ? ((n + n / 4 + 1023) / 1024) * 1024 : n;
          hps = hotsPerSeed;
          b.reset();  // free before allocating the larger set
          work.reset();
          list.reset();
          props.reset();
          sels.reset();
          b.emplace(queue, cap, hps);  // the constructor zeroes the seed rows
          work.emplace(queue, cap, hps);
          const int nList = cap * ::mkfitdev::kMaxCandsPerSeed;
          list.emplace(queue, nList);
          props.emplace(queue, nList);
          sels.emplace(queue, nList);
          ++nAllocs;
          return;
        }
        // reuse: the state of freshly constructed EngineBuffers (seed rows zeroed, no device row count)
        for (mkfitdev::EngineBuffers* e : {&*b, &*work}) {
          auto seedsBuf = e->seeds.buffer();
          alpaka::memset(queue, seedsBuf, 0);
          e->nRowsDev = nullptr;
        }
      }
    };
  }  // namespace

  class MkFitAlpakaBuildProducer : public global::EDProducer<edm::StreamCache<BuildScratch>> {
  public:
    explicit MkFitAlpakaBuildProducer(edm::ParameterSet const& iConfig)
        : EDProducer<edm::StreamCache<BuildScratch>>(iConfig),
          seedsToken_{consumes(iConfig.getParameter<edm::InputTag>("seeds"))},
          pixelHitsToken_{consumes(iConfig.getParameter<edm::InputTag>("pixelHits"))},
          eohToken_{consumes(iConfig.getParameter<edm::InputTag>("eventOfHits"))},
          beamSpotToken_{consumes(iConfig.getParameter<edm::InputTag>("beamSpot"))},
          mkFitGeomToken_{esConsumes()},
          esToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"))},
          esHostToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("esData"))},
          tracksToken_{produces()},
          exportToken_{produces("export")},
          fwdToken_{produces("fwd")},
          bkfitToken_{produces("bkfit")},
          bkwToken_{produces("bkwsearch")},
          postToken_{produces("postFilter")},
          hotsPerSeed_{iConfig.getParameter<int>("hotsPerSeed")},
          removeDuplicates_{iConfig.getParameter<bool>("removeDuplicates")},
          backwardFit_{iConfig.getParameter<bool>("backwardFit")},
          stages_{iConfig.getParameter<bool>("stages")},
          checkStatus_{iConfig.getParameter<bool>("checkStatus")},
          groupScan_{iConfig.getParameter<bool>("k2GroupScan")},
          countTies_{iConfig.getParameter<bool>("countSelTies")},
          reportMemory_{iConfig.getParameter<bool>("reportMemory")},
          putExport_{iConfig.getParameter<bool>("putExport")},
          statusToken_{produces()},
          lstSeedFit_{iConfig.getParameter<bool>("lstSeedFit")},
          scratchReuse_{iConfig.getParameter<int>("scratchReuse")},
          lstSeedDump_{iConfig.getParameter<std::string>("lstSeedFitDump"),
                       iConfig.getParameter<int>("lstSeedFitDumpEvents")} {
      lstSeedCfg_.passes = iConfig.getParameter<int>("lstSeedFitPasses");
      lstSeedCfg_.errScale = iConfig.getParameter<double>("lstSeedFitErrScale");
      lstSeedCfg_.dropFailed = iConfig.getParameter<bool>("lstSeedFitDropFailed");
      lstSeedCfg_.originPrior = iConfig.getParameter<int>("lstSeedFitOriginPrior");
      lstSeedCfg_.hlBackwardTol = iConfig.getParameter<double>("lstSeedFitBackwardTol");
      lstSeedCfg_.hlPixelFallback = iConfig.getParameter<bool>("lstSeedFitPixelFallback");
      // stock MkFitProducer parameters the device build depends on (review H3; checked against the ES per event)
      moduleCfg_.clustersToSkip = iConfig.getParameter<edm::InputTag>("clustersToSkip").label();
      moduleCfg_.buildingRoutine = iConfig.getParameter<std::string>("buildingRoutine");
      moduleCfg_.seedCleaning = iConfig.getParameter<bool>("seedCleaning");
      moduleCfg_.removeDuplicates = removeDuplicates_;
      moduleCfg_.backwardFitInCMSSW = iConfig.getParameter<bool>("backwardFitInCMSSW");
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add("seeds", edm::InputTag{"hltInitialStepMkFitSeeds"});
      desc.add("pixelHits", edm::InputTag{"hltMkFitSiPixelHits"})
          ->setComment("MkFitClusterIndexToHit, only for nPixel (strip hit row base); the stock converter or the "
                       "index-only MkFitAlpakaPixelClusterIndexToHit");
      desc.add("eventOfHits", edm::InputTag{"hltMkFitEventOfHitsAlpaka"})->setComment("device EventOfHits product");
      desc.add("beamSpot", edm::InputTag{"hltOnlineBeamSpot"})
          ->setComment("beam spot of the stock MkFitEventOfHitsProducer (backward-search gate, cms-sw#52015)");
      desc.add("esData", edm::ESInputTag{"", ""})->setComment("MkFitAlpakaESProducer ComponentName");
      desc.add("hotsPerSeed", 256)->setComment("HoT pool rows per seed (overflow: seed failed and counted)");
      desc.add("removeDuplicates", true);
      // stock MkFitProducer parameters, accepted only inside the validated envelope (interface/SupportedConfig.h)
      desc.add("clustersToSkip", edm::InputTag())->setComment("must be empty: no hit mask on the device");
      desc.add<std::string>("buildingRoutine", "cloneEngine");
      desc.add("seedCleaning", true);
      desc.add("backwardFitInCMSSW", false);
      desc.add("backwardFit", true)->setComment("false: identity backward fit (debug)");
      desc.add("stages", false)
          ->setComment("validation: put the stage snapshots 'fwd', 'bkfit', 'bkwsearch', 'postFilter' (stock form)");
      desc.add("checkStatus", false)
          ->setComment("validation: read the overflow / C2 counters per event (one sync) and warn on non-zero values");
      desc.add("k2GroupScan", true)
          ->setComment("GPU: K2 hit scan with several lanes per candidate (round 5); false = thread per candidate");
      desc.add("countSelTies", false)
          ->setComment("validation (GPU group scan): count K2 candidates whose selection meets an exact ddphi tie "
                       "(one sync per event; SEL_TIES at endJob)");
      desc.add("lstSeedFit", false)
          ->setComment("O6-1 option (b), DEVIATION candidate, OFF by default: the state of every seed with an "
                       "outer-tracker hit (LST T5/T4/pT3/pT5) from a device mkFit Kalman fit of its hits; pLS keep "
                       "the copied pixel state (interface/seeds/alpaka/LstSeedFit.h)");
      desc.add("lstSeedFitPasses", 3)->setComment("3 = forward, backward, forward (validated, round 6); 1 = one forward pass");
      desc.add("lstSeedFitErrScale", 1.0)->setComment("final seed errors scaled by this factor");
      desc.add("lstSeedFitOriginPrior", 0)
          ->setComment("beam-line prior as the host seed creator: 0 none, 1 OT-only seeds (T5/T4), 2 all fitted seeds");
      desc.add("lstSeedFitBackwardTol", 0.01)
          ->setComment("originPrior 3: a step is 'against the momentum' only below -tol cm (0 = round 7)");
      desc.add("lstSeedFitPixelFallback", true)
          ->setComment("originPrior 3 + dropFailed: failed pT3/pT5 seeds refitted on their pixel hits and truncated");
      desc.add("lstSeedFitDropFailed", false)
          ->setComment("failed device seed fits removed (true; host seeds with placeholder states) or kept (false)");
      desc.add<std::string>("lstSeedFitDump", "")
          ->setComment("validation: file with host vs device seed states per seed (one sync per event)");
      desc.add("lstSeedFitDumpEvents", 100)->setComment("validation: events dumped");
      desc.add("putExport", false)
          ->setComment("validation: put the 'export' TrackSoA (tracks before the duplicate cleaner); otherwise an "
                       "empty 'export' collection is put and the buffer is freed with the module's scratch");
      desc.add("scratchReuse", 1)
          ->setComment(
              "D7-c per-stream engine scratch reused every event: 0 = per-event allocation, 1 = CPU backends "
              "(default), 2 = every backend");
      desc.add("reportMemory", false)
          ->setComment("diagnostics: device bytes this module allocates per event (no sync; BUILD_MEM at endJob)");
      descriptions.addWithDefaultLabel(desc);
    }

    std::unique_ptr<BuildScratch> beginStream(edm::StreamID) const override { return std::make_unique<BuildScratch>(); }

    void endStream(edm::StreamID sid) const override {
      if (reportMemory_)
        scratchAllocs_ += streamCache(sid)->nAllocs;
    }

    void produce(edm::StreamID sid, device::Event& iEvent, device::EventSetup const& iSetup) const override {
      // D7-f: the device EventOfHits first, so this module takes over its queue (doc/mem.txt)
      const auto& eohD = iEvent.get(eohToken_);
      auto& queue = iEvent.queue();
      const auto& seedsIn = iEvent.get(seedsToken_).seeds();
      const uint32_t nPixel = iEvent.get(pixelHitsToken_).hits().size();  // = stock HitVec size (convertHits)
      const auto& esD = iSetup.getData(esToken_);
      const ::mkfitdev::ESView esView = esD.view();
      const int n = seedsIn.size();
      const int hps = hotsPerSeed_;
      const int nStage = stages_ ? n : 0;
      ::mkfitdev::checkSupportedBuildConfig(iSetup.getData(esHostToken_).hostConfigValue(),
                                            moduleCfg_);  // review H3 (no allocation when it passes)
      // review H4: per-event status product, zeroed here, counters collected after the tail
      mkfitdev::MkFitStatusDeviceObject statusProduct(queue);
      mkfitdev::zeroStatus(queue, statusProduct);
      // R4-M1: an EventOfHits beyond the device build limits arrives empty (nLayers == 0; host-readable metadata)
      const bool eohSkipped = eohD.const_view().layers().metadata().size() == 0;
      if (eohSkipped)
        mkfitdev::addStatus(queue, statusProduct, ::mkfitdev::kEventSkipped, 1);
      if (n == 0 || eohSkipped) {
        iEvent.emplace(statusToken_, std::move(statusProduct));
        for (auto const* t : {&tracksToken_, &exportToken_, &fwdToken_, &bkfitToken_, &bkwToken_, &postToken_}) {
          mkfitdev::TrackSoADeviceCollection empty(queue, 0);
          auto buf = empty.buffer();
          alpaka::memset(queue, buf, 0x00);  // defined scalars (nTracks = 0, overflow counters 0)
          iEvent.emplace(*t, std::move(empty));
        }
        return;
      }
      const BuildTables& T = tables(queue, iSetup, esView);

      // 1. seed import (lane io) into the engine buffers; the kept-seed count stays on the device
      // io's packer (review R4-L3): CPU backends pack straight into the device collection (no staging copy)
      mkfitdev::SeedsDeviceCollection seedsD = mkfitdev::seeds::packSeedsToDevice(queue, seedsIn);
      const auto ev = eohD.const_view();
      if (lstSeedFit_)  // O6-1 (b): device seed state for the LST seeds with OT hits (switch-gated, default off)
        lstSeedDump_.run(queue, iEvent.id().event(), esView, ev.hits(), nPixel, seedsD, n, lstSeedCfg_);
      mkfitdev::seeds::DeviceHitPositions hitPos;
      hitPos.x = ev.hits().metadata().addressOf_x();
      hitPos.y = ev.hits().metadata().addressOf_y();
      hitPos.z = ev.hits().metadata().addressOf_z();
      hitPos.layerHitBase = ev.layers().metadata().addressOf_hitBase();
      BuildScratch eventScratch;  // per-event path (scratchReuse off for this backend): freed at the end of produce
      const bool reuse = scratchReuse_ >= 2 || (scratchReuse_ == 1 && kCpuBackend);
      BuildScratch& S = reuse ? *streamCache(sid) : eventScratch;
      S.prepare(queue, n, hps, reuse);
      mkfitdev::EngineBuffers& b = *S.b;
      mkfitdev::EngineBuffers& work = *S.work;
      mkfitdev::seeds::importSeeds(
          queue, seedsD.view(), n, T.limits, b.seeds.view(), b.slots.view(), b.hots.view(), hps, hitPos);
      b.nRowsDev = &seedsD.view().nKept();

      // 2. event inputs of the engine kernels
      const auto hv = ev.hits();
      const ::mkfitdev::EngineHitInputs in{hv.metadata().addressOf_x(),
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
                                           T.modules.data(),
                                           T.layers.data()};

      // 3. K2 (select) through the engine bridge; list capacity nSeeds * kMaxCandsPerSeed, count on the device
      auto& list = *S.list;
      auto& props = *S.props;
      auto& sels = *S.sels;
      std::optional<cms::alpakatools::device_buffer<Device, uint32_t[]>> tiesD;
      if (countTies_) {
        tiesD.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue, 2));
        alpaka::memset(queue, *tiesD, 0);
      }
      const mkfitdev::EngineSelectK2 k2{list.view(),
                                        props.view(),
                                        sels.view(),
                                        esView,
                                        ev.layers(),
                                        ev.binnedHits(),
                                        ev.bins(),
                                        ev.hits(),
                                        n,
                                        countTies_ ? tiesD->data() : nullptr,
                                        groupScan_};
      mkfitdev::EngineBackwardFitFn bkfit;
      if (backwardFit_)
        bkfit = mkfitdev::makeEngineBackwardFit(in, T.bkfitPF, T.bkfitOutliers);
      // cms-sw#52015 backward-search gate: stock MkBuilder::beginBkwSearch reads the EventOfHits beam spot
      // (MkFitEventOfHitsProducer: mkfit::BeamSpot(bs.x0(), bs.y0(), ...), floats)
      ::mkfitdev::BkwSearchGate gate = T.bkwGate;
      if (gate.minPixelLayers > 0) {
        const auto& bs = iEvent.get(beamSpotToken_);
        gate.bsX = bs.x0();
        gate.bsY = bs.y0();
      }

      // 4. clone engine, sync-free; optional stage snapshots
      mkfitdev::TrackSoADeviceCollection fwdD(queue, nStage * ::mkfitdev::kMaxCandsPerSeed);
      mkfitdev::TrackSoADeviceCollection bkfitD(queue, nStage);
      mkfitdev::TrackSoADeviceCollection bkwD(queue, nStage * ::mkfitdev::kMaxCandsPerSeed);
      mkfitdev::TrackSoADeviceCollection postD(queue, nStage);
      mkfitdev::EngineStageHook stage;
      if (stages_)
        stage = [&](Queue& q, mkfitdev::EngineBuffers& eb, mkfitdev::EngineStage st, int cap) {
          if (st == mkfitdev::kStageFwd)
            mkfitdev::engineExportStage(q, eb, false, false, fwdD.view(), cap);
          else if (st == mkfitdev::kStageBkFit)
            mkfitdev::engineExportStage(q, eb, false, true, bkfitD.view(), cap);
          else
            mkfitdev::engineExportStage(q, eb, true, false, bkwD.view(), cap);
        };
      mkfitdev::EngineBuffers& res = mkfitdev::engineRunChainAsync(queue,
                                                                    b,
                                                                    work,
                                                                    T.fwd,
                                                                    T.bkw,
                                                                    k2,
                                                                    k2,
                                                                    bkfit,
                                                                    in,
                                                                    in,
                                                                    T.pc,
                                                                    T.ipFwd,
                                                                    T.ipBkw,
                                                                    T.minHitsQFPre,
                                                                    T.minHitsQFPost,
                                                                    T.backwardFitMinHits,
                                                                    n,
                                                                    stage,
                                                                    gate);
      if (stages_)
        mkfitdev::engineExportStage(queue, res, false, true, postD.view(), n);

      // 5. tail (lane io + clean): export + duplicate cleaner, NO filter (D4-C2); rows = the post-filter survivors
      auto status = cms::alpakatools::make_device_buffer<uint32_t[]>(queue, 1);
      alpaka::memset(queue, status, 0);
      mkfitdev::TrackSoADeviceCollection exportedD(queue, n);
      mkfitdev::TrackSoADeviceCollection finalD(queue, n);
      mkfitdev::seeds::exportAndClean(queue,
                                      res.seeds.const_view(),
                                      res.slots.const_view(),
                                      res.hots.const_view(),
                                      hps,
                                      res.nRowsDev,
                                      n,
                                      removeDuplicates_,
                                      T.dc,
                                      T.pixelPriority ? T.pixelLayers : nullptr,
                                      exportedD.view(),
                                      finalD.view(),
                                      status.data());
      {
        mkfitdev::StatusSources src;
        src.add(seedsD.view().metadata().addressOf_nOverflowHits(), ::mkfitdev::kSeedHitsTruncated);
        src.add(ev.layers().metadata().addressOf_nOverflowFirst(), ::mkfitdev::kEohOverflowFirst);
        src.add(ev.layers().metadata().addressOf_nOverflowCount(), ::mkfitdev::kEohOverflowCount);
        // the engine's compactions carry the seed-pool counters forward: the result buffer holds the totals
        src.add(res.seeds.view().metadata().addressOf_nOverflowHots(), ::mkfitdev::kHotOverflowSeeds);
        src.add(res.seeds.view().metadata().addressOf_nOverflowOpts(), ::mkfitdev::kOptsOverflow);
        src.add(res.seeds.view().metadata().addressOf_nOverflowExtras(), ::mkfitdev::kExtrasOverflow);
        // D4-C2: repack applied more than once (engine bit) or a broken HoT chain at export (exportAndClean)
        src.add(res.seeds.view().metadata().addressOf_nRepackRepeat(), ::mkfitdev::kRepackErrors);
        src.add(status.data(), ::mkfitdev::kRepackErrors);
        // the cleaner copies the export counters into the final TrackSoA
        src.add(finalD.view().metadata().addressOf_nOverflowTracks(), ::mkfitdev::kTrackOverflow);
        src.add(finalD.view().metadata().addressOf_nOverflowHits(), ::mkfitdev::kTrackHitsOverflow);
        mkfitdev::collectStatus(queue, statusProduct, src);
      }
      if (checkStatus_)
        check(queue, iEvent.id().event(), res, status, exportedD);
      if (reportMemory_) {
        auto by = [](auto const& c) -> uint64_t { return alpaka::getExtentProduct(c.buffer()); };
        auto eb = [&](mkfitdev::EngineBuffers const& e) -> uint64_t {
          return by(e.seeds) + by(e.slots) + by(e.hots) + by(e.opts) + by(e.extras) + by(e.upds) + by(e.sel) + by(e.c2) +
                 uint64_t(e.maxSeeds) * (sizeof(int8_t) + sizeof(int32_t)) + sizeof(int32_t);
        };
        const uint64_t tot = eb(b) + eb(work) + by(list) + by(props) + by(sels) + by(seedsD) + by(exportedD) +
                             by(finalD) + by(fwdD) + by(bkfitD) + by(bkwD) + by(postD);
        const uint64_t hotsB = by(b.hots) + by(work.hots);
        memSum_ += tot;
        memHots_ += hotsB;
        uint64_t prev = memMax_.load();
        while (tot > prev && !memMax_.compare_exchange_weak(prev, tot)) {
        }
        ++memEv_;
      }
      if (countTies_) {
        auto h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, 2);
        alpaka::memcpy(queue, h, *tiesD);
        alpaka::wait(queue);
        ties_[0] += h.data()[0];
        ties_[1] += h.data()[1];
      }
      iEvent.emplace(statusToken_, std::move(statusProduct));

      if (putExport_)
        iEvent.emplace(exportToken_, std::move(exportedD));
      else
        iEvent.emplace(exportToken_, mkfitdev::TrackSoADeviceCollection(queue, 0));
      iEvent.emplace(tracksToken_, std::move(finalD));
      iEvent.emplace(fwdToken_, std::move(fwdD));
      iEvent.emplace(bkfitToken_, std::move(bkfitD));
      iEvent.emplace(bkwToken_, std::move(bkwD));
      iEvent.emplace(postToken_, std::move(postD));
    }

    void endJob() override {
      if (reportMemory_ && memEv_ > 0)
        edm::LogPrint("MkFitAlpakaBuild")
            << "BUILD_MEM events " << memEv_ << " meanMB " << 1e-6 * memSum_ / memEv_ << " maxMB " << 1e-6 * memMax_
            << " hotsMeanMB " << 1e-6 * memHots_ / memEv_ << " scratchAllocs " << scratchAllocs_;
      if (countTies_)
        edm::LogPrint("MkFitAlpakaBuild") << "SEL_TIES tieCandidates " << ties_[0] << " scannedCandidates " << ties_[1];
      if (checkStatus_)
        edm::LogPrint("MkFitAlpakaBuild") << "BUILD_STATUS events " << nEv_ << " overflowHots " << tot_[0]
                                          << " overflowOpts " << tot_[1] << " overflowExtras " << tot_[2]
                                          << " repackRepeat " << tot_[3] << " brokenChains " << tot_[4]
                                          << " exportOverflowTracks " << tot_[5] << " exportOverflowHits "
                                          << tot_[6];
    }

  private:
    const BuildTables& tables(Queue& queue, device::EventSetup const& iSetup, ::mkfitdev::ESView const& esView) const {
      edm::EventSetup const& es = iSetup;
      const auto key = std::make_pair(static_cast<long>(alpaka::getNativeHandle(alpaka::getDev(queue))),
                                      es.get<TrackerRecoGeometryRecord>().cacheIdentifier());
      std::lock_guard<std::mutex> guard(mutex_);
      auto it = cache_.find(key);
      if (it == cache_.end()) {
        const auto& esH = iSetup.getData(esHostToken_);
        const auto& ti = iSetup.getData(mkFitGeomToken_).trackerInfo();
        // tables of earlier IOVs stay alive until the end of the job (events of other streams may still use them)
        it = cache_.emplace(key, std::make_unique<BuildTables>(queue, esH, esView, ::mkfitdev::seedPartitionLimits(ti)))
                 .first;
        edm::LogPrint("MkFitAlpakaBuild") << "BUILD_TABLES device " << key.first << " iov " << key.second
                                          << " regions " << it->second->fwd.nRegions << " fwd steps "
                                          << it->second->fwd.nSteps << " bkw steps " << it->second->bkw.nSteps;
      }
      return *it->second;
    }

    void check(Queue& queue,
               unsigned long long evt,
               mkfitdev::EngineBuffers& res,
               cms::alpakatools::device_buffer<Device, uint32_t[]>& status,
               mkfitdev::TrackSoADeviceCollection& exportedD) const {
      auto sv = res.seeds.view();
      auto h = cms::alpakatools::make_host_buffer<uint32_t[]>(queue, 5);
      auto hs = cms::alpakatools::make_host_buffer<int32_t[]>(queue, 2);
      auto dev = alpaka::getDev(queue);
      alpaka::memcpy(queue,
                     cms::alpakatools::make_host_view(h.data() + 0, 1),
                     cms::alpakatools::make_device_view(dev, &sv.nOverflowHots(), 1));
      alpaka::memcpy(queue,
                     cms::alpakatools::make_host_view(h.data() + 1, 1),
                     cms::alpakatools::make_device_view(dev, &sv.nOverflowOpts(), 1));
      alpaka::memcpy(queue,
                     cms::alpakatools::make_host_view(h.data() + 2, 1),
                     cms::alpakatools::make_device_view(dev, &sv.nOverflowExtras(), 1));
      alpaka::memcpy(queue,
                     cms::alpakatools::make_host_view(h.data() + 3, 1),
                     cms::alpakatools::make_device_view(dev, &sv.nRepackRepeat(), 1));
      alpaka::memcpy(
          queue, cms::alpakatools::make_host_view(h.data() + 4, 1), cms::alpakatools::make_device_view(dev, status.data(), 1));
      auto ev = exportedD.view();
      alpaka::memcpy(queue,
                     cms::alpakatools::make_host_view(hs.data() + 0, 1),
                     cms::alpakatools::make_device_view(dev, &ev.nOverflowTracks(), 1));
      alpaka::memcpy(queue,
                     cms::alpakatools::make_host_view(hs.data() + 1, 1),
                     cms::alpakatools::make_device_view(dev, &ev.nOverflowHits(), 1));
      alpaka::wait(queue);
      const uint64_t v[7] = {h.data()[0], h.data()[1], h.data()[2], h.data()[3], h.data()[4], uint64_t(hs.data()[0]), uint64_t(hs.data()[1])};
      bool any = false;
      for (int i = 0; i < 7; ++i) {
        tot_[i] += v[i];
        any |= v[i] != 0;
      }
      ++nEv_;
      if (any)
        edm::LogWarning("MkFitAlpakaBuild") << "event " << evt << " non-zero status: overflowHots " << v[0]
                                            << " overflowOpts " << v[1] << " overflowExtras " << v[2]
                                            << " repackRepeat " << v[3] << " brokenChains " << v[4]
                                            << " exportOverflowTracks " << v[5] << " exportOverflowHits " << v[6];
    }

    const edm::EDGetTokenT<MkFitSeedWrapper> seedsToken_;
    const edm::EDGetTokenT<MkFitClusterIndexToHit> pixelHitsToken_;
    const device::EDGetToken<mkfitdev::EventOfHitsDeviceCollection> eohToken_;
    const edm::EDGetTokenT<reco::BeamSpot> beamSpotToken_;  // cms-sw#52015 backward-search gate (stock EOH beam spot)
    const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> mkFitGeomToken_;
    const device::ESGetToken<::mkfitdev::ESData<Device>, TrackerRecoGeometryRecord> esToken_;
    const edm::ESGetToken<::mkfitdev::ESDataHost, TrackerRecoGeometryRecord> esHostToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> tracksToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> exportToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> fwdToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> bkfitToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> bkwToken_;
    const device::EDPutToken<mkfitdev::TrackSoADeviceCollection> postToken_;
    const int hotsPerSeed_;
    const bool removeDuplicates_;
    const bool backwardFit_;
    const bool stages_;
    const bool checkStatus_;
    const bool groupScan_;
    const bool countTies_;
    const bool reportMemory_;
    const bool putExport_;
    const device::EDPutToken<mkfitdev::MkFitStatusDeviceObject> statusToken_;
    const bool lstSeedFit_;
    const int scratchReuse_;
    static constexpr bool kCpuBackend = std::is_same_v<Device, alpaka::DevCpu>;
    mutable mkfitdev::lstseeds::LstSeedFitRunner lstSeedDump_;
    ::mkfitdev::lstseeds::LstSeedFitConfig lstSeedCfg_;
    ::mkfitdev::BuildModuleConfig moduleCfg_;
    mutable std::mutex mutex_;
    mutable std::map<std::pair<long, unsigned long long>, std::unique_ptr<BuildTables>> cache_;
    mutable std::atomic<uint64_t> tot_[7] = {};
    mutable std::atomic<uint64_t> nEv_{0};
    mutable std::atomic<uint64_t> ties_[2] = {};
    mutable std::atomic<uint64_t> memSum_{0}, memMax_{0}, memHots_{0}, memEv_{0}, scratchAllocs_{0};
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaBuildProducer);
