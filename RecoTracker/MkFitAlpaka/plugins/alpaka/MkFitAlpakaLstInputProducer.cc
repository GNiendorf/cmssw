// Stage-B prototype (round 6, lane lstin; doc/lstin.txt): LST's input device collection made on the device.
// Replaces hltInputLST (host packing of hltSiPhase2RecHits + the host KF-refit seeds hltInitialStepSeeds) for the
// LST producer behind a customise switch that is OFF by default.
//   OT hits : host staging of LOCAL quantities only (no toGlobal) + a per-IOV module table; global x/y/z on the device
//   pLS     : option (i) = Patatrack's device pixel tracks (fit state at the beam-spot PCA); seedIdx = SoA track index
// otSoA (round 9): the OT hits straight from the device OT rechit SoA of full stage D (MkFitAlpakaOTRecHitsProducer,
// columns gx/gy/gz/detId/clustSize; row = cluster key = legacy rechit index): no host pass over OT rechits, no OT
// staging copies; the CA-row -> key map comes from the cluster detset sizes. seedFailMask (round 9): the pixel tracks
// whose host seed creator rejects them (LSTPixelSeedFailMask) give no pLS, as in stock (R7-H2 change (ii)).
// fromHostInput (round 9, NO physics change): the collection is hltInputLST's (host, pLS from the host seeds as stock;
// LSTInputProducer otKeysOnly leaves the OT x/y/z rows empty and reads no OT SoA host copy): copied to the device and
// the OT rows filled from the device OT rechit SoA. compareTo = an unpatched hltInputLST clone: every column bitwise
// (LSTIN_FULLCMP lines).
// compare = True: copies the result back, compares it with hltInputLST (OT hits bitwise; pLS matched by their hit keys)
// and writes LSTIN_CMP lines (+ per-pair dump lines into dumpFile) - validation only, synchronous.
#include <Eigen/Core>  // before any SoA header (Eigen columns of TracksSoA)
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <array>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeedCollection.h"
#include "DataFormats/TrackSoA/interface/TracksHost.h"
#include "DataFormats/TrackSoA/interface/alpaka/TracksSoACollection.h"
#include "DataFormats/TrackerRecHit2D/interface/Phase2TrackerRecHit1D.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHitCollection.h"
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/TrackingRecHitSoA/interface/alpaka/TrackingRecHitsSoACollection.h"
#include "DataFormats/SiStripDetId/interface/StripSubdetector.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/stream/SynchronizingEDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoTracker/LSTCore/interface/Common.h"
#include "RecoTracker/LSTCore/interface/LSTInputHostCollection.h"
#include "RecoTracker/LSTCore/interface/LSTOTHits.h"
#include "RecoTracker/LSTCore/interface/alpaka/LSTInputDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitModuleTableESData.h"
#include "RecoTracker/MkFitAlpaka/interface/math/TkBfield.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/alpaka/OTRecHitDeviceCollection.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

#include "MkFitAlpakaLstInputKernels.h"


namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class MkFitAlpakaLstInputProducer : public stream::SynchronizingEDProducer<> {
    using HMS = std::vector<uint32_t>;
    using OTModule = ::mkfitdev::lstin::OTModule;
    using PLS = ::mkfitdev::lstin::PLS;
    static constexpr uint32_t kNoKey = ::mkfitdev::lstin::kNoKey;
    using LstInputDevice = ALPAKA_ACCELERATOR_NAMESPACE::lst::LSTInputDeviceCollection;

  public:
    explicit MkFitAlpakaLstInputProducer(edm::ParameterSet const& cfg)
        : SynchronizingEDProducer<>(cfg),
          ptCut_(cfg.getParameter<double>("ptCut")),
          contract_(cfg.getParameter<int>("contract")),
          compare_(cfg.getParameter<bool>("compare")),
          replace_(cfg.getParameter<bool>("replace")),
          replaceHits_(cfg.getParameter<bool>("replaceHits")),
          replaceHitSlots_(cfg.getParameter<int>("replaceHitSlots")),
          pseudoLastHit_(cfg.getParameter<int>("pseudoLastHit")),
          pcaAnchor_(cfg.getParameter<int>("pcaAnchor")),
          seedsFromHost_(cfg.getParameter<bool>("seedsFromHost")),
          seedIdxFromTracks_(cfg.getParameter<bool>("seedIdxFromTracks")),
          timers_(cfg.getParameter<bool>("timers")),
          ptFieldCorrection_(cfg.getParameter<bool>("ptFieldCorrection")),
          dumpFile_(cfg.getParameter<std::string>("dumpFile")),
          otSoA_(!cfg.getParameter<edm::InputTag>("otSoA").label().empty()),
          fromHostInput_(cfg.getParameter<bool>("fromHostInput")),
          pixToken_(consumes(cfg.getParameter<edm::InputTag>("pixelRecHits"))),
          otCluToken_(consumes(cfg.getParameter<edm::InputTag>("otClusters"))),
          pixCluToken_(consumes(cfg.getParameter<edm::InputTag>("pixelClusters"))),
          pixHMSToken_(consumes(cfg.getParameter<edm::InputTag>("pixelRecHits"))),
          otHMSToken_(consumes(cfg.getParameter<edm::InputTag>("otRecHitsSoA"))),
          tracksFromHost_(cfg.getParameter<bool>("tracksFromHost")),
          hitsSoAToken_(consumes(cfg.getParameter<edm::InputTag>("hitsSoA"))),
          bsToken_(consumes(cfg.getParameter<edm::InputTag>("beamSpot"))),
          geomToken_(esConsumes()),
          mfToken_(esConsumes()),
          outToken_(produces()) {
      if (otSoA_)
        otSoAToken_ = consumes(cfg.getParameter<edm::InputTag>("otSoA"));
      else
        otToken_ = consumes(cfg.getParameter<edm::InputTag>("phase2OTRecHits"));
      if (auto const t = cfg.getParameter<edm::InputTag>("seedFailMask"); !t.label().empty()) {
        if (!seedIdxFromTracks_)
          throw cms::Exception("Configuration") << "MkFitAlpakaLstInputProducer: seedFailMask needs seedIdxFromTracks";
        failMaskToken_ = consumes(t);
      }
      if (tracksFromHost_)
        tracksToken_ = consumes(cfg.getParameter<edm::InputTag>("pixelTracksSoA"));
      else
        tracksDevToken_ = consumes(cfg.getParameter<edm::InputTag>("pixelTracksSoA"));
      if (compare_ || replace_ || fromHostInput_)
        refToken_ = consumes(cfg.getParameter<edm::InputTag>("reference"));
      if (fromHostInput_ && !otSoA_)
        throw cms::Exception("Configuration") << "MkFitAlpakaLstInputProducer: fromHostInput needs otSoA";
      if (auto const t = cfg.getParameter<edm::InputTag>("compareTo"); !t.label().empty())
        fullRefToken_ = consumes(t);
      if (seedsFromHost_ && seedIdxFromTracks_)
        throw cms::Exception("Configuration") << "MkFitAlpakaLstInputProducer: seedsFromHost and seedIdxFromTracks exclude each other";
      if (seedsFromHost_ || seedIdxFromTracks_) {
        indToEdmToken_ = consumes(cfg.getParameter<edm::InputTag>("pixelTracks"));
        recoTracksToken_ = consumes(cfg.getParameter<edm::InputTag>("pixelTracks"));
        if (seedsFromHost_)
          seedsToken_ = consumes(cfg.getParameter<edm::InputTag>("seeds"));
        ptrPutToken_ = produces("otRecHitPtrs");
      }
      if (auto const mt = cfg.getParameter<std::string>("moduleTable"); !mt.empty()) {
        moduleTableToken_ = esConsumes(edm::ESInputTag("", mt));
        moduleTableES_ = true;
      }
      if (compare_ && !dumpFile_.empty()) {
        std::lock_guard<std::mutex> lock(dumpMutex_);
        if (!dump_)
          dump_ = std::make_unique<std::ofstream>(dumpFile_);
      }
    }

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<double>("ptCut", 0.8);
      desc.add<int>("contract", 1)->setComment("OT localToGlobal fma pattern: 0 fma(c,d,a*b), 1 fma(a,b,c*d), 2 none");
      desc.add<bool>("compare", false);
      desc.add<bool>("tracksFromHost", false);
      desc.add<bool>("timers", false)->setComment("LSTIN_T lines: host time of the acquire steps (us)");
      desc.add<bool>("ptFieldCorrection", false)
          ->setComment("pLS momentum x <Bz>_hits / Bz(0) with the closed-form tracker field (round 8; false = round 7)");
      desc.add<bool>("seedsFromHost", false)
          ->setComment("switch ON form: seedIdx = index in 'seeds' (via pixelTracks' SoA->edm map), pLS only for tracks with a "
                       "host seed, and a host product 'otRecHitPtrs' (OT rechit pointers) for LSTOutputConverter");
      desc.add<bool>("seedIdxFromTracks", false)
          ->setComment("switch ON form without host seeds: seedIdx = index in 'pixelTracks' (its SoA->edm map), pLS only for "
                       "SoA tracks with an edm track, + 'otRecHitPtrs'; for LSTOutputConverter with lazyPixelSeeds = True");
      desc.add<std::string>("moduleTable", "")
          ->setComment("ComponentName of the MkFitAlpakaEventOfHitsModuleTableESProducer product (the module table shared with "
                       "the device hit input); empty: the module's own per-IOV table");
      desc.add<edm::InputTag>("pixelTracks", edm::InputTag("hltPhase2PixelTracks"));
      desc.add<edm::InputTag>("seeds", edm::InputTag("hltInitialStepSeeds"));
      desc.add<bool>("replace", false)->setComment("validation arm: host input with the device pLS parameters only");
      desc.add<bool>("replaceHits", true)
          ->setComment("round 9 (pT3 tracer split of change (i)): false = replace keeps the host pLS pseudo-hits (PCA point, "
                       "PCA momentum, last-hit position) and takes only the pLS fields from the device");
      desc.add<int>("replaceHitSlots", 7)
          ->setComment("round 10 (stageb), replace with replaceHits: which device pseudo-hits replace the host ones, bit 0 = "
                       "PCA point (+ dxy, dz of pseudo-hit 3), bit 1 = PCA momentum (pt, eta, phi), bit 2 = last-hit "
                       "position (+ x of pseudo-hit 3)");
      desc.add<int>("pseudoLastHit", 0)
          ->setComment("round 10 (stageb): the pLS last-hit pseudo-hit r3LH; 0 = the outermost hit (rounds 7-9), 1 = the "
                       "Patatrack helix point at the outermost hit's transverse radius (KF-state-like, as the host seed)");
      desc.add<int>("pcaAnchor", 0)
          ->setComment("round 10 (stageb): the pLS PCA quantities (PCA point and momentum, dxy, dz, superbin); 0 = Patatrack's "
                       "PCA (rounds 7-9), 1 = the PCA of the Patatrack helix re-anchored on the outermost hit (same curvature, "
                       "tangent there), as the host takes the TSCBL of the KF seed state on the last hit");
      desc.add<std::string>("dumpFile", "");
      desc.add<edm::InputTag>("phase2OTRecHits", edm::InputTag("hltSiPhase2RecHits"));
      desc.add<edm::InputTag>("otSoA", edm::InputTag(""))
          ->setComment("round 9: OT hits from this device OT rechit SoA (full stage D, MkFitAlpakaOTRecHitsProducer) instead "
                       "of phase2OTRecHits; otRecHitsSoA must then be its CA hitModuleStart; the 'otRecHitPtrs' product has "
                       "no OT rows (LSTOutputConverter needs otClustersOnDemand)");
      desc.add<bool>("fromHostInput", false)
          ->setComment("round 9: the collection = 'reference' (hltInputLST with otKeysOnly) copied to the device, OT rows "
                       "from otSoA; no physics change");
      desc.add<edm::InputTag>("compareTo", edm::InputTag(""))
          ->setComment("validation with fromHostInput: an unpatched hltInputLST; every column compared bitwise (LSTIN_FULLCMP)");
      desc.add<edm::InputTag>("seedFailMask", edm::InputTag(""))
          ->setComment("round 9, with seedIdxFromTracks: indices in pixelTracks whose host seed creator rejects them "
                       "(LSTPixelSeedFailMask); they give no pLS, as in stock");
      desc.add<edm::InputTag>("pixelRecHits", edm::InputTag("hltSiPixelRecHits"));
      desc.add<edm::InputTag>("otClusters", edm::InputTag("hltSiPhase2Clusters"))->setComment("the clusters of the OT rechits");
      desc.add<edm::InputTag>("pixelClusters", edm::InputTag("hltSiPixelClusters"))->setComment("the clusters of the pixel rechits");
      desc.add<edm::InputTag>("otRecHitsSoA", edm::InputTag("hltPhase2OtRecHitsSoA"));
      desc.add<edm::InputTag>("pixelTracksSoA", edm::InputTag("hltPhase2PixelTrackTorchHighPuritySelector"));
      desc.add<edm::InputTag>("hitsSoA", edm::InputTag("hltPhase2PixelRecHitsExtendedSoA"));
      desc.add<edm::InputTag>("beamSpot", edm::InputTag("hltOnlineBeamSpot"));
      desc.add<edm::InputTag>("reference", edm::InputTag("hltInputLST"));
      descriptions.addWithDefaultLabel(desc);
    }

  private:
    // per-IOV geometry: module table (per GeomDet index), detId -> OT-SoA module id (P sensors of TOB PS modules,
    // in detUnits order, as Phase2OTRecHitsSoAConverter / PixelTrackProducerFromSoAAlpaka)
    struct Geo {
      std::vector<OTModule> modules;
      std::unordered_map<uint32_t, int> otModuleId;
    };

    std::shared_ptr<const Geo> geo(device::EventSetup const& iSetup) const {
      edm::EventSetup const& es = iSetup;
      const auto id = es.get<TrackerDigiGeometryRecord>().cacheIdentifier();
      std::lock_guard<std::mutex> lock(geoMutex_);
      if (geo_ && geoId_ == id)
        return geo_;
      auto const& geom = iSetup.getData(geomToken_);
      auto g = std::make_shared<Geo>();
      uint32_t n = 0;
      for (auto const* d : geom.detUnits())
        n = std::max<uint32_t>(n, d->index() + 1);
      if (!moduleTableES_)
        g->modules.assign(n, OTModule{{0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0}, -1, 0});
      int nP = 0;
      for (auto const* d : geom.detUnits()) {
        const auto& sf = d->surface();
        const auto& R = sf.rotation();
        if (!moduleTableES_)  // same values as mkfitdev::buildHitModuleTable (the ES product)
          g->modules[d->index()] = OTModule{{R.xx(), R.xy(), R.xz(), R.yx(), R.yy(), R.yz(), R.zx(), R.zy(), R.zz()},
                                            {sf.position().x(), sf.position().y(), sf.position().z()},
                                            -1,
                                            0};
        const DetId detId = d->geographicalId();
        if (geom.getDetectorType(detId) == TrackerGeometry::ModuleType::Ph2PSP &&
            detId.subdetId() == StripSubdetector::TOB)
          g->otModuleId[detId.rawId()] = nP++;
      }
      geo_ = std::move(g);
      geoId_ = id;
      return geo_;
    }

    template <typename T>
    static auto hostView(const T* p, uint32_t n) {
      return cms::alpakatools::make_host_view(const_cast<T*>(p), n);
    }

    // acquire: host staging into pinned buffers, H2D copies, the per-track pLS kernels and the counts D2H; produce
    // (after the framework's synchronisation, no blocking wait on the host thread) allocates the exact-size collection
    void acquire(device::Event const& iEvent, device::EventSetup const& iSetup) override {
      if (fromHostInput_)
        return;  // everything in produce (no device-to-host readback needed)
      using clk = std::chrono::steady_clock;
      const auto t0 = clk::now();
      auto& queue = iEvent.queue();
      auto const g = geo(iSetup);
      static const Phase2TrackerRecHit1DCollectionNew kNoOTHits;
      auto const& otHits = otSoA_ ? kNoOTHits : iEvent.get(otToken_);
      auto const& pixHits = iEvent.get(pixToken_);
      auto const& pixHMS = iEvent.get(pixHMSToken_);
      auto const& otHMS = iEvent.get(otHMSToken_);
      auto const& bs = iEvent.get(bsToken_);
      auto const& mf = iSetup.getData(mfToken_);
      auto const& hitsSoA = iEvent.get(hitsSoAToken_);
      auto const& otClu = iEvent.get(otCluToken_);
      auto const& pixClu = iEvent.get(pixCluToken_);

      // ---- host staging: OT local quantities (legacy order = hltInputLST's), OT-SoA row -> legacy OT index,
      //      pixel SoA row -> legacy cluster key (as PixelTrackProducerFromSoAAlpaka's hitmap, inverted)
      const uint32_t nOT = otSoA_ ? otClu.dataSize() : otHits.dataSize();
      if (otSoA_) {
        otSoAView_ = iEvent.get(otSoAToken_).const_view();
        if (uint32_t(otSoAView_.metadata().size()) != nOT)
          throw cms::Exception("MkFitAlpakaLstInput") << "OT rechit SoA rows " << otSoAView_.metadata().size()
                                                      << " != OT clusters " << nOT;
      }
      const uint32_t nPixelSoA = otHMS.empty() ? 0 : otHMS[0];
      const uint32_t nOTSoA = otHMS.empty() ? 0 : otHMS.back() - nPixelSoA;
      const uint32_t nOTb = otSoA_ ? 1u : std::max<uint32_t>(nOT, 1);  // OT staging: legacy mode only
      const uint32_t nOTSoAb = std::max<uint32_t>(nOTSoA, 1);
      const uint32_t nPixb = std::max<uint32_t>(nPixelSoA, 1);
      hLx_.emplace(cms::alpakatools::make_host_buffer<float[]>(queue, nOTb));
      hLy_.emplace(cms::alpakatools::make_host_buffer<float[]>(queue, nOTb));
      hMod_.emplace(cms::alpakatools::make_host_buffer<int32_t[]>(queue, nOTb));
      hClu_.emplace(cms::alpakatools::make_host_buffer<uint16_t[]>(queue, nOTb));
      hDet_.emplace(cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nOTb));
      hOtKey_.emplace(cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nOTSoAb));
      hPixKey_.emplace(cms::alpakatools::make_host_buffer<uint32_t[]>(queue, nPixb));
      float* lx = hLx_->data();
      float* ly = hLy_->data();
      int32_t* mod = hMod_->data();
      uint16_t* clu = hClu_->data();
      uint32_t* det = hDet_->data();
      uint32_t* otKey = hOtKey_->data();
      uint32_t* pixKey = hPixKey_->data();
      const auto t1 = clk::now();
      std::fill(otKey, otKey + nOTSoAb, kNoKey);
      if ((seedsFromHost_ || seedIdxFromTracks_) && !otSoA_)
        otPtrs_.assign(nOT, nullptr);
      std::fill(pixKey, pixKey + nPixb, kNoKey);
      if (otSoA_) {
        // CA OT rows (P sensors of the TOB PS modules, detUnits order, key order inside a module) -> cluster key
        uint32_t first = 0;
        for (auto const& ds : otClu) {
          const uint32_t size = ds.size();
          if (size > 0) {
            if (auto it = g->otModuleId.find(ds.detId()); it != g->otModuleId.end()) {
              const uint32_t row = otHMS[it->second] - nPixelSoA;
              for (uint32_t j = 0; j < size && row + j < nOTSoA; ++j)
                otKey[row + j] = first + j;
            }
          }
          first += size;
        }
      } else {
        uint32_t i = 0;
        for (auto const& ds : otHits) {
          if (ds.empty())
            continue;
          const int32_t gind = ds.begin()->det()->index();
          const uint32_t rawId = ds.detId();
          auto it = g->otModuleId.find(rawId);
          int32_t soa = -1;
          if (it != g->otModuleId.end())
            soa = int32_t(otHMS[it->second]) - int32_t(nPixelSoA);
          for (auto const& h : ds) {
            const auto lp = h.localPosition();
            lx[i] = lp.x();
            ly[i] = lp.y();
            mod[i] = gind;
            clu[i] = otClu.data()[h.firstClusterRef().index()].size();  // by key (a Ref deref per hit is slow)
            if (seedsFromHost_ || seedIdxFromTracks_)
              otPtrs_[i] = &h;
            det[i] = rawId;
            if (soa >= 0 && uint32_t(soa) < nOTSoA)
              otKey[soa++] = i;
            ++i;
          }
        }
      }
      const auto t2 = clk::now();
      for (auto const& ds : pixHits) {
        if (ds.empty())
          continue;
        const uint32_t start = pixHMS[ds.begin()->det()->index()];
        for (auto const& h : ds) {
          const uint32_t k = h.firstClusterRef().index();
          const uint32_t soa = start + pixClu.data()[k].originalId();
          if (soa < nPixelSoA)
            pixKey[soa] = k;
        }
      }

      const auto t3 = clk::now();
      auto& p = p_;
      p.ptCut = ptCut_;
      p.bsx = bs.x0();
      p.bsy = bs.y0();
      p.bsz = bs.z0();
      p.k = mf.inInverseGeV(GlobalPoint(0, 0, 0)).z();  // PixelRecoUtilities::fieldInInvGev (Patatrack's fit field)
      p.nPixelSoA = nPixelSoA;
      p.nOTSoA = nOTSoA;
      p.nOT = nOT;
      p.nPixKeys = nPixelSoA;
      p.contract = contract_;
      p.minQuality = static_cast<int>(pixelTrack::Quality::tight);
      p.ptFieldCorrection = ptFieldCorrection_;
      p.bz0 = ::mkfitdev::field::tkBz(0.f, 0.f, 0.f);
      p.pseudoLH = pseudoLastHit_;
      p.pcaAnchor = pcaAnchor_;

      // ---- device copies (the module table once per IOV and stream)
      if (!otSoA_) {
        dLx_.emplace(cms::alpakatools::make_device_buffer<float[]>(queue, nOTb));
        dLy_.emplace(cms::alpakatools::make_device_buffer<float[]>(queue, nOTb));
        dMod_.emplace(cms::alpakatools::make_device_buffer<int32_t[]>(queue, nOTb));
        dClu_.emplace(cms::alpakatools::make_device_buffer<uint16_t[]>(queue, nOTb));
        dDet_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nOTb));
        alpaka::memcpy(queue, *dLx_, *hLx_);
        alpaka::memcpy(queue, *dLy_, *hLy_);
        alpaka::memcpy(queue, *dMod_, *hMod_);
        alpaka::memcpy(queue, *dClu_, *hClu_);
        alpaka::memcpy(queue, *dDet_, *hDet_);
      }
      dOtKey_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nOTSoAb));
      dPixKey_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nPixb));
      alpaka::memcpy(queue, *dOtKey_, *hOtKey_);
      alpaka::memcpy(queue, *dPixKey_, *hPixKey_);
      if (moduleTableES_) {
        modulesPtr_ = iSetup.getData(moduleTableToken_).data();
      } else if (!dModules_ || dModulesFor_ != g.get()) {
        dModules_.emplace(cms::alpakatools::make_device_buffer<OTModule[]>(queue, g->modules.size()));
        alpaka::memcpy(queue, *dModules_, hostView(g->modules.data(), g->modules.size()));
        alpaka::wait(queue);  // once per IOV: the host table is shared, keep the copy simple
        dModulesFor_ = g.get();
        geoKeep_ = g;
      }
      if (!moduleTableES_)
        modulesPtr_ = dModules_->data();

      // ---- pass 1: pLS per track + deterministic scan; counts to the host
      ::reco::TrackBlocksConstView tracksView;
      if (tracksFromHost_) {
        auto const& tracksHost = iEvent.get(tracksToken_);
        tracksView = tracksHost.const_view();
        if constexpr (!std::is_same_v<Device, alpaka::DevCpu>) {
          tracksDev_.emplace(queue,
                             int(tracksHost.const_view().tracks().metadata().size()),
                             int(tracksHost.const_view().trackHits().metadata().size()));
          alpaka::memcpy(queue, tracksDev_->buffer(), tracksHost.const_buffer());
          tracksView = tracksDev_->const_view();
        }
      } else {
        tracksView = iEvent.get(tracksDevToken_).const_view();
      }
      maxTracks_ = tracksView.tracks().metadata().size();
      p.nSeedMap = 0;
      if (seedsFromHost_) {
        // SoA track -> edm track (PixelTrackProducerFromSoAAlpaka's map) -> host seed (SeedGeneratorFromProtoTracks
        // makes one seed per track in track order unless makeSeed fails: identity when the counts agree, else matched
        // in order by the first seed hit)
        auto const& i2e = iEvent.get(indToEdmToken_);
        auto const& trks = iEvent.get(recoTracksToken_);
        auto const& seeds = iEvent.get(seedsToken_);
        std::vector<int32_t> e2s(trks.size(), -1);
        if (seeds.size() == trks.size()) {
          for (size_t e = 0; e < trks.size(); ++e)
            e2s[e] = int32_t(e);
        } else {
          size_t sd = 0;
          for (size_t e = 0; e < trks.size() && sd < seeds.size(); ++e) {
            auto const& first = *seeds[sd].recHits().begin();
            bool same = false;
            for (size_t k = 0; k < trks[e].recHitsSize() && !same; ++k)
              same = first.sharesInput(&*trks[e].recHit(k), TrackingRecHit::all);
            if (same)
              e2s[e] = int32_t(sd++);
          }
        }
        const uint32_t n = std::max<uint32_t>(i2e.size(), 1);
        hSeedOf_.emplace(cms::alpakatools::make_host_buffer<int32_t[]>(queue, n));
        for (uint32_t t = 0; t < n; ++t)
          hSeedOf_->data()[t] = (t < i2e.size() && i2e[t] < e2s.size()) ? e2s[i2e[t]] : -1;
        dSeedOf_.emplace(cms::alpakatools::make_device_buffer<int32_t[]>(queue, n));
        alpaka::memcpy(queue, *dSeedOf_, *hSeedOf_);
        p.nSeedMap = i2e.size();
      } else if (seedIdxFromTracks_) {
        // SoA track -> edm track index (PixelTrackProducerFromSoAAlpaka's map); the converter makes the pixel seed of
        // that edm track itself (lazyPixelSeeds), so no host seed collection is read
        auto const& i2e = iEvent.get(indToEdmToken_);
        const uint32_t nTrk = iEvent.get(recoTracksToken_).size();
        const uint32_t n = std::max<uint32_t>(i2e.size(), 1);
        hSeedOf_.emplace(cms::alpakatools::make_host_buffer<int32_t[]>(queue, n));
        for (uint32_t t = 0; t < n; ++t)
          hSeedOf_->data()[t] = (t < i2e.size() && i2e[t] < nTrk) ? int32_t(i2e[t]) : -1;
        if (!failMaskToken_.isUninitialized()) {
          // tracks whose host seed creator fails: no pLS (stock has no seed, hence no pLS, for them)
          std::vector<char> fails(nTrk, 0);
          for (uint32_t e : iEvent.get(failMaskToken_))
            if (e < nTrk)
              fails[e] = 1;
          for (uint32_t t = 0; t < n; ++t)
            if (int32_t const e = hSeedOf_->data()[t]; e >= 0 && fails[e]) {
              hSeedOf_->data()[t] = -1;
              ++nMasked_;
            }
        }
        dSeedOf_.emplace(cms::alpakatools::make_device_buffer<int32_t[]>(queue, n));
        alpaka::memcpy(queue, *dSeedOf_, *hSeedOf_);
        p.nSeedMap = std::max<uint32_t>(i2e.size(), 1);
      }
      const uint32_t nChunks = (maxTracks_ + 255) / 256;
      mt_ = std::max<uint32_t>(maxTracks_, 1);
      dScratch_.emplace(cms::alpakatools::make_device_buffer<PLS[]>(queue, mt_));
      dPIdx_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue, mt_));
      dHOff_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue, mt_));
      const uint32_t nCounts = ::mkfitdev::lstin::kNCounts + 2 * nChunks;
      dCounts_.emplace(cms::alpakatools::make_device_buffer<uint32_t[]>(queue, nCounts));
      alpaka::memset(queue, *dCounts_, 0);
      ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstin::launchPLS(queue,
                                                              tracksView,
                                                              maxTracks_,
                                                              hitsSoA.const_view(),
                                                              dPixKey_->data(),
                                                              dOtKey_->data(),
                                                              otSoA_ ? otSoAView_.metadata().addressOf_detId() : dDet_->data(),
                                                              otSoA_ ? otSoAView_.metadata().addressOf_clustSize() : dClu_->data(),
                                                              dSeedOf_ ? dSeedOf_->data() : nullptr,
                                                              p,
                                                              dScratch_->data(),
                                                              dPIdx_->data(),
                                                              dHOff_->data(),
                                                              dCounts_->data());
      const auto t4 = clk::now();
      hCounts_.emplace(cms::alpakatools::make_host_buffer<uint32_t[]>(queue, ::mkfitdev::lstin::kNCounts));
      alpaka::memcpy(queue,
                     *hCounts_,
                     cms::alpakatools::make_device_view(queue, dCounts_->data(), ::mkfitdev::lstin::kNCounts));
      if (timers_) {
        auto us = [](auto a, auto b) { return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count(); };
        const auto t5 = clk::now();
        edm::LogPrint("MkFitAlpakaLstInput") << "LSTIN_T event " << iEvent.id().event() << " gets+alloc " << us(t0, t1)
                                             << " otLoop " << us(t1, t2) << " pixLoop " << us(t2, t3) << " copies+seedmap+pLS "
                                             << us(t3, t4) << " countsCopy " << us(t4, t5) << " nOT " << p_.nOT;
      }
    }

    void produce(device::Event& iEvent, device::EventSetup const& iSetup) override {
      auto& queue = iEvent.queue();
      if (fromHostInput_) {
        produceFromHostInput(iEvent, queue);
        return;
      }
      uint32_t const* hCounts = hCounts_->data();
      const uint32_t nPLSAll = hCounts[0];
      const uint32_t nHitsIT = hCounts[1];
      const uint32_t nPLS = std::min<uint32_t>(nPLSAll, ::lst::n_max_pixel_segments_per_module);
      if (hCounts[3] || hCounts[4] || hCounts[5])
        edm::LogWarning("MkFitAlpakaLstInput") << "tracks with > kMaxTrackHits or < 3 hits " << hCounts[3]
                                               << ", hits without a legacy key " << hCounts[4]
                                               << ", non-finite tracks " << hCounts[5];

      // ---- pass 2: the LST input collection
      LstInputDevice out(queue, int(p_.nOT + nHitsIT), int(nPLS), int(nHitsIT));
      if (otSoA_)
        ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstin::launchFillSoA(queue,
                                                                    out.view(),
                                                                    maxTracks_,
                                                                    nPLS,
                                                                    otSoAView_,
                                                                    p_,
                                                                    dScratch_->data(),
                                                                    dPIdx_->data(),
                                                                    dHOff_->data());
      else
        ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstin::launchFill(queue,
                                                               out.view(),
                                                               maxTracks_,
                                                               nPLS,
                                                               modulesPtr_,
                                                               dMod_->data(),
                                                               dDet_->data(),
                                                               dLx_->data(),
                                                               dLy_->data(),
                                                               dClu_->data(),
                                                               p_,
                                                               dScratch_->data(),
                                                               dPIdx_->data(),
                                                               dHOff_->data(),
                                                               dCounts_->data());
      if (compare_)
        compare(iEvent, queue, out, *dScratch_, mt_, hCounts);
      if (replace_) {
        // physics-test arm: the host input with ONLY the pLS parameters taken from the device (matched by hit keys)
        auto rep = replaced(iEvent, queue, out);
        iEvent.emplace(outToken_, std::move(rep));
      } else {
        iEvent.emplace(outToken_, std::move(out));
      }
      if (seedsFromHost_ || seedIdxFromTracks_) {
        // host product for LSTOutputConverter: only the OT rechit pointers (rows [0, nOT)) are read there
        // (otSoA: no rows; the converter makes the OT hits on demand from the cluster keys)
        // O10-1 (#280): the pointers are the lst::LSTOTHits product the converter reads
        ::lst::LSTOTHits ptrs;
        if (!otSoA_)
          ptrs.hits = otPtrs_;
        iEvent.emplace(ptrPutToken_, std::move(ptrs));
      }
      // per-event buffers: the caching allocators keep them alive until the queue has used them
      hLx_.reset();
      hLy_.reset();
      hMod_.reset();
      hClu_.reset();
      hDet_.reset();
      hOtKey_.reset();
      hPixKey_.reset();
      dLx_.reset();
      dLy_.reset();
      dMod_.reset();
      dClu_.reset();
      dDet_.reset();
      dOtKey_.reset();
      dPixKey_.reset();
      dScratch_.reset();
      dPIdx_.reset();
      dHOff_.reset();
      dCounts_.reset();
      hCounts_.reset();
      tracksDev_.reset();
      hSeedOf_.reset();
      dSeedOf_.reset();
    }

    // fromHostInput: hltInputLST's host collection (OT rows without x/y/z) -> device, OT rows from the device OT SoA
    void produceFromHostInput(device::Event& iEvent, Queue& queue) {
      auto const& ref = iEvent.get(refToken_);
      auto const ot = iEvent.get(otSoAToken_).const_view();
      const int nH = ref.const_view().hits().metadata().size(), nP = ref.const_view().pixelSeeds().metadata().size();
      const uint32_t nOT = ref.const_view().hits().nHitsOT();
      if (uint32_t(ot.metadata().size()) != nOT)
        throw cms::Exception("MkFitAlpakaLstInput")
            << "fromHostInput: OT rechit SoA rows " << ot.metadata().size() << " != hltInputLST OT rows " << nOT;
      LstInputDevice out(queue, nH, nP, ref.const_view().hitsIT().metadata().size());
      alpaka::memcpy(queue, out.buffer(), ref.const_buffer());
      ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstin::launchFillOTSoAOnly(queue, out.view(), ot, nOT);
      if (!fullRefToken_.isUninitialized())
        fullCompare(iEvent, queue, out);
      iEvent.emplace(outToken_, std::move(out));
    }

    // validation (fromHostInput + compareTo): every column of the device collection vs an unpatched hltInputLST
    void fullCompare(device::Event& iEvent, Queue& queue, LstInputDevice const& out) const {
      auto const& ref = iEvent.get(fullRefToken_);
      const int nH = out.const_view().hits().metadata().size(), nP = out.const_view().pixelSeeds().metadata().size();
      const int nIT = out.const_view().hitsIT().metadata().size();
      ::lst::LSTInputHostCollection h(queue, nH, nP, nIT);
      alpaka::memcpy(queue, h.buffer(), out.const_buffer());
      alpaka::wait(queue);
      auto a = h.const_view().hits(), b = ref.const_view().hits();
      auto ai = h.const_view().hitsIT(), bi = ref.const_view().hitsIT();
      auto pa = h.const_view().pixelSeeds(), pb = ref.const_view().pixelSeeds();
      uint64_t bad = 0, badPLS = 0;
      const bool sizes = nH == b.metadata().size() && nP == pb.metadata().size() && nIT == bi.metadata().size() &&
                         a.nHitsOT() == b.nHitsOT();
      auto eq = [](auto x, auto y) { return std::memcmp(&x, &y, sizeof(x)) == 0; };
      if (sizes) {
        for (int i = 0; i < nH; ++i)
          bad += !(eq(a.xs()[i], b.xs()[i]) && eq(a.ys()[i], b.ys()[i]) && eq(a.zs()[i], b.zs()[i]) &&
                   ::lst::hitOrigIdx(a, ai, i) == ::lst::hitOrigIdx(b, bi, i) && a.detid()[i] == b.detid()[i] &&
                   a.clustsize()[i] == b.clustsize()[i]);
        for (int i = 0; i < nP; ++i)
          badPLS += !(pa.firstHit()[i] == pb.firstHit()[i] && pa.nHits()[i] == pb.nHits()[i] &&
                      pa.hitDetBits()[i] == pb.hitDetBits()[i] && eq(pa.deltaPhi()[i], pb.deltaPhi()[i]) &&
                      pa.seedIdx()[i] == pb.seedIdx()[i] && pa.charge()[i] == pb.charge()[i] &&
                      pa.superbin()[i] == pb.superbin()[i] && pa.pixelType()[i] == pb.pixelType()[i] &&
                      pa.isQuad()[i] == pb.isQuad()[i] && eq(pa.ptIn()[i], pb.ptIn()[i]) &&
                      eq(pa.ptErr()[i], pb.ptErr()[i]) && eq(pa.px()[i], pb.px()[i]) && eq(pa.py()[i], pb.py()[i]) &&
                      eq(pa.pz()[i], pb.pz()[i]) && eq(pa.etaErr()[i], pb.etaErr()[i]) &&
                      eq(pa.eta()[i], pb.eta()[i]) && eq(pa.phi()[i], pb.phi()[i]));
      }
      edm::LogPrint("MkFitAlpakaLstInput") << "LSTIN_FULLCMP event " << iEvent.id().event() << " hits " << nH << " pLS "
                                           << nP << " nOT " << a.nHitsOT() << " sizesEqual " << sizes << " badHits " << bad
                                           << " badPLS " << badPLS;
    }

    void endStream() override {
      if (!failMaskToken_.isUninitialized())
        edm::LogPrint("MkFitAlpakaLstInput") << "LSTIN_MASK totals (stream): pLS dropped by seedFailMask " << nMasked_;
    }

    // ---- validation: device result vs hltInputLST
    // replace = True (validation arm): hltInputLST's collection (same OT hits, pLS set, order, seedIdx and hit keys)
    // with the pLS parameters and pseudo-hits of the matched device pLS (option (i)); unmatched host pLS keep host values
    LstInputDevice replaced(device::Event& iEvent, Queue& queue, LstInputDevice const& out) const {
      auto const& ref = iEvent.get(refToken_);
      ::lst::LSTInputHostCollection h(queue,
                                      out.const_view().hits().metadata().size(),
                                      out.const_view().pixelSeeds().metadata().size(),
                                      out.const_view().hitsIT().metadata().size());
      alpaka::memcpy(queue, h.buffer(), out.const_buffer());
      const int nrH = ref.const_view().hits().metadata().size(), nrP = ref.const_view().pixelSeeds().metadata().size();
      const int nrIT = ref.const_view().hitsIT().metadata().size();
      ::lst::LSTInputHostCollection n(queue, nrH, nrP, nrIT);
      alpaka::memcpy(queue, n.buffer(), ref.const_buffer());
      alpaka::wait(queue);
      auto dh = h.const_view().hits();
      auto di = h.const_view().hitsIT();
      auto dp = h.const_view().pixelSeeds();
      auto nh = n.view().hits();
      auto ni = n.view().hitsIT();
      auto np = n.view().pixelSeeds();
      std::map<std::array<uint32_t, 4>, int> devIdx;
      for (int i = 0; i < dp.metadata().size(); ++i)
        devIdx.emplace(keyOf(dh, di, dp, i), i);
      int nRep = 0, nKeep = 0;
      for (int j = 0; j < nrP; ++j) {
        auto it = devIdx.find(keyOf(nh, ni, np, j));
        if (it == devIdx.end()) {
          ++nKeep;
          continue;
        }
        const int i = it->second;
        ++nRep;
        np.deltaPhi()[j] = dp.deltaPhi()[i];
        np.charge()[j] = dp.charge()[i];
        np.superbin()[j] = dp.superbin()[i];
        np.pixelType()[j] = dp.pixelType()[i];
        np.ptIn()[j] = dp.ptIn()[i];
        np.ptErr()[j] = dp.ptErr()[i];
        np.px()[j] = dp.px()[i];
        np.py()[j] = dp.py()[i];
        np.pz()[j] = dp.pz()[i];
        np.etaErr()[j] = dp.etaErr()[i];
        np.eta()[j] = dp.eta()[i];
        np.phi()[j] = dp.phi()[i];
        const uint32_t fd = dp.firstHit()[i], fr = np.firstHit()[j];
        const uint32_t k = !replaceHits_ ? 0 : (np.nHits()[j] > 3 ? 4 : 3);
        for (uint32_t s = 0; s < k; ++s) {
          // slot -> replaceHitSlots bit: 0 PCA point, 1 PCA momentum, 2 last hit; slot 3 = (last-hit x, dxy, dz)
          const int bx = s == 3 ? 2 : int(s), byz = s == 3 ? 0 : int(s);
          if (replaceHitSlots_ & (1 << bx))
            nh.xs()[fr + s] = dh.xs()[fd + s];
          if (replaceHitSlots_ & (1 << byz)) {
            nh.ys()[fr + s] = dh.ys()[fd + s];
            nh.zs()[fr + s] = dh.zs()[fd + s];
          }
        }
      }
      LstInputDevice d(queue, nrH, nrP, nrIT);
      alpaka::memcpy(queue, d.buffer(), n.const_buffer());
      alpaka::wait(queue);
      edm::LogPrint("MkFitAlpakaLstInput")
          << "LSTIN_REPLACE event " << iEvent.id().event() << " pLS " << nrP << " replaced " << nRep << " keptHost " << nKeep;
      return d;
    }

    // pLS key = the stored hit keys (3 or 4) of the seed
    template <typename HV, typename IV, typename PV>
    static std::array<uint32_t, 4> keyOf(HV hv, IV iv, PV pv, int i) {  // views by value (cheap)
      const uint32_t f = pv.firstHit()[i];
      const uint32_t n = pv.nHits()[i] > 3 ? 4 : 3;
      std::array<uint32_t, 4> k{kNoKey, kNoKey, kNoKey, kNoKey};
      for (uint32_t j = 0; j < n; ++j)
        k[j] = f + j < hv.nHitsOT() ? f + j : iv.idxs()[f + j - hv.nHitsOT()];  // O10-1 (#280): lst::hitOrigIdx
      return k;
    }

    template <typename TScratch>
    void compare(device::Event& iEvent,
                 Queue& queue,
                 LstInputDevice const& out,
                 TScratch const& dScratch,
                 uint32_t mt,
                 uint32_t const* counts) const {
      auto const& ref = iEvent.get(refToken_);
      ::lst::LSTInputHostCollection h(queue,
                                      out.const_view().hits().metadata().size(),
                                      out.const_view().pixelSeeds().metadata().size(),
                                      out.const_view().hitsIT().metadata().size());
      alpaka::memcpy(queue, h.buffer(), out.const_buffer());
      auto hScratch = cms::alpakatools::make_host_buffer<PLS[]>(queue, mt);
      alpaka::memcpy(queue, hScratch, dScratch);
      alpaka::wait(queue);

      auto dh = h.const_view().hits();
      auto rh = ref.const_view().hits();
      auto di = h.const_view().hitsIT();
      auto ri = ref.const_view().hitsIT();
      auto dp = h.const_view().pixelSeeds();
      auto rp = ref.const_view().pixelSeeds();
      const uint32_t nOTd = dh.nHitsOT(), nOTr = rh.nHitsOT();
      uint32_t badX = 0, badY = 0, badZ = 0, badDet = 0, badClu = 0, badIdx = 0;
      const uint32_t nOT = std::min(nOTd, nOTr);
      for (uint32_t i = 0; i < nOT; ++i) {
        badX += std::bit_cast<uint32_t>(dh.xs()[i]) != std::bit_cast<uint32_t>(rh.xs()[i]);
        badY += std::bit_cast<uint32_t>(dh.ys()[i]) != std::bit_cast<uint32_t>(rh.ys()[i]);
        badZ += std::bit_cast<uint32_t>(dh.zs()[i]) != std::bit_cast<uint32_t>(rh.zs()[i]);
        badDet += dh.detid()[i] != rh.detid()[i];
        badClu += dh.clustsize()[i] != rh.clustsize()[i];
        badIdx += ::lst::hitOrigIdx(dh, di, i) != ::lst::hitOrigIdx(rh, ri, i);  // identity for OT rows (O10-1, #280)
      }

      const int nr = rp.metadata().size(), nd = dp.metadata().size();
      std::map<std::array<uint32_t, 4>, int> refIdx;
      int refDupKeys = 0;
      for (int i = 0; i < nr; ++i)
        if (!refIdx.emplace(keyOf(rh, ri, rp, i), i).second)
          ++refDupKeys;
      int matched = 0, sameType = 0, sameCharge = 0, sameSuperbin = 0, sameBits = 0, sameNHits = 0;
      std::vector<char> refUsed(nr, 0);
      std::lock_guard<std::mutex> lock(dumpMutex_);
      for (int i = 0; i < nd; ++i) {
        auto it = refIdx.find(keyOf(dh, di, dp, i));
        if (it == refIdx.end())
          continue;
        const int j = it->second;
        refUsed[j] = 1;
        ++matched;
        sameType += dp.pixelType()[i] == rp.pixelType()[j];
        sameCharge += dp.charge()[i] == rp.charge()[j];
        sameSuperbin += dp.superbin()[i] == rp.superbin()[j];
        sameBits += dp.hitDetBits()[i] == rp.hitDetBits()[j];
        sameNHits += dp.nHits()[i] == rp.nHits()[j];
        if (dump_) {
          // per pair: device | host values of the LST pLS fields and pseudo-hits; trailing Patatrack sigmas
          const uint32_t fd = dp.firstHit()[i], fr = rp.firstHit()[j];
          auto& o = *dump_;
          o << "P " << iEvent.id().event() << ' ' << int(dp.nHits()[i]) << ' ' << int(rp.nHits()[j]) << ' '
            << int(dp.pixelType()[i]) << ' ' << int(rp.pixelType()[j]) << ' ' << dp.charge()[i] << ' '
            << rp.charge()[j] << ' ' << dp.superbin()[i] << ' ' << rp.superbin()[j];
          auto f = [&o](float a, float b) { o << ' ' << a << ' ' << b; };
          f(dp.ptIn()[i], rp.ptIn()[j]);
          f(dp.ptErr()[i], rp.ptErr()[j]);
          f(dp.eta()[i], rp.eta()[j]);
          f(dp.etaErr()[i], rp.etaErr()[j]);
          f(dp.phi()[i], rp.phi()[j]);
          f(dp.deltaPhi()[i], rp.deltaPhi()[j]);
          f(dp.px()[i], rp.px()[j]);
          f(dp.py()[i], rp.py()[j]);
          f(dp.pz()[i], rp.pz()[j]);
          for (int s = 0; s < 3; ++s) {
            f(dh.xs()[fd + s], rh.xs()[fr + s]);
            f(dh.ys()[fd + s], rh.ys()[fr + s]);
            f(dh.zs()[fd + s], rh.zs()[fr + s]);
          }
          if (dp.nHits()[i] > 3 && rp.nHits()[j] > 3) {
            f(dh.ys()[fd + 3], rh.ys()[fr + 3]);  // dxy
            f(dh.zs()[fd + 3], rh.zs()[fr + 3]);  // dz
          } else {
            f(0, 0);
            f(0, 0);
          }
          o << '\n';
        }
      }
      int refOnly = 0;
      for (int j = 0; j < nr; ++j)
        refOnly += !refUsed[j];
      // what the unmatched host pLS look like (pt), for the selection-difference table
      if (dump_) {
        for (int j = 0; j < nr; ++j)
          if (!refUsed[j])
            *dump_ << "R " << iEvent.id().event() << ' ' << rp.ptIn()[j] << ' ' << rp.ptErr()[j] << ' '
                   << rp.eta()[j] << ' ' << int(rp.nHits()[j]) << '\n';
        std::map<std::array<uint32_t, 4>, int> devIdx;
        for (int i = 0; i < nd; ++i)
          devIdx.emplace(keyOf(dh, di, dp, i), i);
        for (int i = 0; i < nd; ++i)
          if (!refIdx.count(keyOf(dh, di, dp, i)))
            *dump_ << "D " << iEvent.id().event() << ' ' << dp.ptIn()[i] << ' ' << dp.ptErr()[i] << ' '
                   << dp.eta()[i] << ' ' << int(dp.nHits()[i]) << '\n';
      }
      edm::LogPrint("MkFitAlpakaLstInput")
          << "LSTIN_CMP event " << iEvent.id().event() << " OT " << nOTd << " refOT " << nOTr << " badX " << badX
          << " badY " << badY << " badZ " << badZ << " badDet " << badDet << " badClu " << badClu << " badIdx "
          << badIdx << " | pLS dev " << nd << " refPLS " << nr << " matched " << matched << " refOnly " << refOnly
          << " devOnly " << (nd - matched) << " refDupKeys " << refDupKeys << " sameType " << sameType
          << " sameCharge " << sameCharge << " sameSuperbin " << sameSuperbin << " sameBits " << sameBits
          << " sameNHits " << sameNHits << " | hitsIT dev " << counts[1] << " tracks " << counts[2]
          << " longTracks " << counts[3] << " noKey " << counts[4] << " nonFinite " << counts[5];
    }

    const float ptCut_;
    const int contract_;
    const bool compare_;
    const bool replace_;
    const bool replaceHits_;
    const int replaceHitSlots_;
    const int pseudoLastHit_;
    const int pcaAnchor_;
    const bool seedsFromHost_;
    const bool seedIdxFromTracks_;
    bool moduleTableES_ = false;
    device::ESGetToken<::mkfitdev::HitModuleTableESData<Device>, TrackerRecoGeometryRecord> moduleTableToken_;
    OTModule const* modulesPtr_ = nullptr;
    const bool timers_;
    const bool ptFieldCorrection_;
    edm::EDGetTokenT<std::vector<uint32_t>> indToEdmToken_;
    edm::EDGetTokenT<::reco::TrackCollection> recoTracksToken_;
    edm::EDGetTokenT<TrajectorySeedCollection> seedsToken_;
    edm::EDPutTokenT<::lst::LSTOTHits> ptrPutToken_;
    const std::string dumpFile_;
    const bool otSoA_;
    const bool fromHostInput_;
    edm::EDGetTokenT<::lst::LSTInputHostCollection> fullRefToken_;
    edm::EDGetTokenT<Phase2TrackerRecHit1DCollectionNew> otToken_;
    device::EDGetToken<mkfitdev::OTRecHitDeviceCollection> otSoAToken_;
    ::mkfitdev::OTRecHitSoA::ConstView otSoAView_;
    edm::EDGetTokenT<std::vector<uint32_t>> failMaskToken_;
    unsigned long nMasked_ = 0;
    const edm::EDGetTokenT<SiPixelRecHitCollection> pixToken_;
    const edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> otCluToken_;
    const edm::EDGetTokenT<SiPixelClusterCollectionNew> pixCluToken_;
    const edm::EDGetTokenT<HMS> pixHMSToken_;
    const edm::EDGetTokenT<HMS> otHMSToken_;
    // pixel tracks: the device product (default) or, with tracksFromHost, its host copy (made for hltPhase2PixelTracks)
    // copied H2D here
    const bool tracksFromHost_;
    device::EDGetToken<reco::TracksSoACollection> tracksDevToken_;
    edm::EDGetTokenT<::reco::TracksHost> tracksToken_;
    const device::EDGetToken<reco::TrackingRecHitsSoACollection> hitsSoAToken_;
    const edm::EDGetTokenT<::reco::BeamSpot> bsToken_;
    const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> geomToken_;
    const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mfToken_;
    const device::EDPutToken<LstInputDevice> outToken_;
    edm::EDGetTokenT<::lst::LSTInputHostCollection> refToken_;

    mutable std::mutex geoMutex_;
    mutable std::shared_ptr<const Geo> geo_;
    mutable unsigned long long geoId_ = 0;
    // dump stream shared by the stream instances
    static std::mutex dumpMutex_;
    static std::unique_ptr<std::ofstream> dump_;

    // per-event buffers (acquire -> produce) and the per-stream device module table
    template <typename T>
    using HBuf = std::optional<cms::alpakatools::host_buffer<T>>;
    template <typename T>
    using DBuf = std::optional<cms::alpakatools::device_buffer<Device, T>>;
    HBuf<float[]> hLx_, hLy_;
    HBuf<int32_t[]> hMod_;
    HBuf<uint16_t[]> hClu_;
    HBuf<uint32_t[]> hDet_, hOtKey_, hPixKey_, hCounts_;
    DBuf<float[]> dLx_, dLy_;
    DBuf<int32_t[]> dMod_;
    DBuf<uint16_t[]> dClu_;
    DBuf<uint32_t[]> dDet_, dOtKey_, dPixKey_, dPIdx_, dHOff_, dCounts_;
    DBuf<PLS[]> dScratch_;
    DBuf<OTModule[]> dModules_;
    Geo const* dModulesFor_ = nullptr;
    std::shared_ptr<const Geo> geoKeep_;
    std::optional<reco::TracksSoACollection> tracksDev_;
    ::mkfitdev::lstin::Params p_{};
    std::vector<TrackingRecHit const*> otPtrs_;
    HBuf<int32_t[]> hSeedOf_;
    DBuf<int32_t[]> dSeedOf_;
    uint32_t maxTracks_ = 0, mt_ = 1;
  };

  std::mutex MkFitAlpakaLstInputProducer::dumpMutex_;
  std::unique_ptr<std::ofstream> MkFitAlpakaLstInputProducer::dump_;

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

DEFINE_FWK_ALPAKA_MODULE(MkFitAlpakaLstInputProducer);
