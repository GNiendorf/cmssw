#include <sstream>
#include "DataFormats/Common/interface/Ref.h"
#include "DataFormats/TrackerRecHit2D/interface/Phase2TrackerRecHit1D.h"
#include "DataFormats/TrackerRecHit2D/interface/SiPixelRecHitCollection.h"
#include "DataFormats/TrackCandidate/interface/TrackCandidateCollection.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrackReco/interface/SeedStopInfo.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeedCollection.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/transform.h"
#include "Geometry/CommonTopologies/interface/GeomDet.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
// local patch (MkFitAlpaka round 8, lane otdev, stage D): OT hits of the TCs made on demand from their clusters
#include <optional>
#include "RecoLocalTracker/Phase2TrackerRecHits/interface/Phase2TrackerRecHitOnDemand.h"
#include "RecoLocalTracker/Records/interface/TkPhase2OTCPERecord.h"
#include "RecoTracker/LST/interface/LSTProtoTrackSeed.h"
#include "RecoTracker/LSTCore/interface/LSTOTHits.h"
#include "RecoTracker/LSTCore/interface/TrackCandidatesHostCollection.h"
#include "RecoTracker/TkSeedingLayers/interface/SeedingHitSet.h"

#include "RecoTracker/TkSeedGenerator/interface/SeedCreator.h"
#include "RecoTracker/TkSeedGenerator/interface/SeedCreatorFactory.h"

#include "RecoTracker/TkTrackingRegions/interface/GlobalTrackingRegion.h"
#include "TrackingTools/GeomPropagators/interface/Propagator.h"
#include "TrackingTools/Records/interface/TrackingComponentsRecord.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"

#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <cmath>
#include <atomic>
#include <ranges>
#include <unordered_map>

#include <oneapi/tbb/concurrent_unordered_map.h>

// local patch (MkFitAlpaka round 9, lane hostrest): job-wide memo of TrackerGeometry::getDetectorType (a linear scan of
// the detid range table) shared by all streams, so each detId is scanned once per job instead of once per stream (the
// per-stream memo kept paying its warm-up in every one of the 16 streams of a short job). Same values.
struct LSTOutputConverterGlobalCache {
  mutable tbb::concurrent_unordered_map<uint32_t, TrackerGeometry::ModuleType> detType;
  mutable std::atomic<TrackerGeometry const*> geom{nullptr};  // the geometry the shared memo belongs to
};

class LSTOutputConverter : public edm::stream::EDProducer<edm::GlobalCache<LSTOutputConverterGlobalCache>> {
public:
  LSTOutputConverter(edm::ParameterSet const& iConfig, LSTOutputConverterGlobalCache const*);
  ~LSTOutputConverter() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  static std::unique_ptr<LSTOutputConverterGlobalCache> initializeGlobalCache(edm::ParameterSet const&) {
    return std::make_unique<LSTOutputConverterGlobalCache>();
  }
  static void globalEndJob(LSTOutputConverterGlobalCache const*) {}

private:
  void produce(edm::Event& iEvent, const edm::EventSetup& iSetup) override;

  const edm::EDGetTokenT<lst::TrackCandidatesBaseHostCollection> lstOutputToken_;
  // O10-1 (#280): OT hit pointers from the lst::LSTOTHits product of lstInput (not read with otClustersOnDemand); the
  // pixel seeds from the lstPixelSeeds collections in order (not read with lazyPixelSeeds)
  edm::EDGetTokenT<lst::LSTOTHits> lstOTHitsToken_;
  std::vector<edm::EDGetTokenT<TrajectorySeedCollection>> lstPixelSeedTokens_;
  // lazyPixelSeeds (stage B, MkFitAlpaka lane lstin): the pixel seeds are made here, only for the TCs that use one
  // (pLS, pT3, pT5), from the pixel tracks with the same creator and region as SeedGeneratorFromProtoTracksEDProducer
  // (hltInitialStepSeeds), instead of reading a seed collection made for every pixel track; pixelSeedIndex = pixel
  // track index
  const bool lazyPixelSeeds_;
  // local patch (stage D): otClustersOnDemand set = the LST input's OT hit pointers are not read (the input may
  // come from the device OT rechit SoA without legacy rechits)
  edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> otClustersToken_;
  edm::ESGetToken<ClusterParameterEstimator<Phase2TrackerCluster1D>, TkPhase2OTCPERecord> otCpeToken_;
  const bool includeFourthHit_;
  edm::EDGetTokenT<reco::TrackCollection> pixelTracksToken_;
  edm::EDGetTokenT<TrajectorySeedCollection> lazyCheckToken_;
  std::unique_ptr<SeedCreator> pixelSeedCreator_;
  long lazyNeeded_ = 0, lazyFailed_ = 0, lazySkipped_ = 0, checkSeeds_ = 0, checkBad_ = 0, checkEvSkipped_ = 0;
  const bool includeT5s_;
  const bool includeNonpLSTSs_;
  const bool dropOTHitsPurePLS_;
  const int maxITHitsToDropOTHitsPurePLS_;
  const bool produceSeeds_;
  const bool produceTrackCandidates_;
  const bool seedStates_;
  const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mfToken_;
  const edm::ESGetToken<Propagator, TrackingComponentsRecord> propagatorAlongToken_;
  const edm::ESGetToken<Propagator, TrackingComponentsRecord> propagatorOppositeToken_;
  const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> tGeomToken_;
  const edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> tTopoToken_;
  std::unique_ptr<SeedCreator> seedCreator_;
  const edm::EDPutTokenT<TrajectorySeedCollection> trajectorySeedPutToken_;
  const edm::EDPutTokenT<TrajectorySeedCollection> trajectorySeedpLSPutToken_;
  const edm::EDPutTokenT<TrackCandidateCollection> trackCandidatePutToken_;
  const edm::EDPutTokenT<TrackCandidateCollection> trackCandidatepTCPutToken_;
  const edm::EDPutTokenT<TrackCandidateCollection> trackCandidateT4T5TCPutToken_;
  const edm::EDPutTokenT<TrackCandidateCollection> trackCandidateNopLSTCPutToken_;
  const edm::EDPutTokenT<TrackCandidateCollection> trackCandidatepTTCPutToken_;
  const edm::EDPutTokenT<TrackCandidateCollection> trackCandidatepLSTCPutToken_;
  const edm::EDPutTokenT<std::vector<SeedStopInfo>> seedStopInfoPutToken_;
  const edm::EDPutTokenT<std::vector<SeedStopInfo>> pTCsSeedStopInfoPutToken_;
  const edm::EDPutTokenT<std::vector<SeedStopInfo>> t4t5TCsSeedStopInfoPutToken_;
  const edm::EDPutTokenT<std::vector<SeedStopInfo>> pTTCsSeedStopInfoPutToken_;
  // TrackerGeometry::getDetectorType is a linear scan of the detid range table (~25% of this module's samples in the
  // GPU target menu); per-stream memo of its result, reset when the geometry object changes (identical results)
  std::unordered_map<uint32_t, TrackerGeometry::ModuleType> detTypeCache_;
  const TrackerGeometry* detTypeGeom_ = nullptr;
};

LSTOutputConverter::LSTOutputConverter(edm::ParameterSet const& iConfig, LSTOutputConverterGlobalCache const*)
    : lstOutputToken_(consumes(iConfig.getParameter<edm::InputTag>("lstOutput"))),
      lazyPixelSeeds_(iConfig.getParameter<bool>("lazyPixelSeeds")),
      includeFourthHit_(iConfig.getParameter<bool>("includeFourthHit")),
      includeT5s_(iConfig.getParameter<bool>("includeT5s")),
      includeNonpLSTSs_(iConfig.getParameter<bool>("includeNonpLSTSs")),
      dropOTHitsPurePLS_(iConfig.getParameter<bool>("dropOTHitsPurePLS")),
      maxITHitsToDropOTHitsPurePLS_(iConfig.getParameter<int>("maxITHitsToDropOTHitsPurePLS")),
      produceSeeds_(iConfig.getParameter<bool>("produceSeeds")),
      produceTrackCandidates_(iConfig.getParameter<bool>("produceTrackCandidates")),
      seedStates_(iConfig.getParameter<bool>("seedStates")),
      mfToken_(esConsumes()),
      propagatorAlongToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("propagatorAlong"))},
      propagatorOppositeToken_{esConsumes(iConfig.getParameter<edm::ESInputTag>("propagatorOpposite"))},
      tGeomToken_(esConsumes()),
      tTopoToken_(esConsumes()),
      seedCreator_(SeedCreatorFactory::get()->create("SeedFromConsecutiveHitsCreator",
                                                     iConfig.getParameter<edm::ParameterSet>("SeedCreatorPSet"),
                                                     consumesCollector())),
      trajectorySeedPutToken_(produces<TrajectorySeedCollection>("")),
      trajectorySeedpLSPutToken_(produceSeeds_ ? produces<TrajectorySeedCollection>("pLSTSsLST")
                                               : edm::EDPutTokenT<TrajectorySeedCollection>{}),
      trackCandidatePutToken_(produceTrackCandidates_ ? produces<TrackCandidateCollection>("")
                                                      : edm::EDPutTokenT<TrackCandidateCollection>{}),
      trackCandidatepTCPutToken_(produceTrackCandidates_ ? produces<TrackCandidateCollection>("pTCsLST")
                                                         : edm::EDPutTokenT<TrackCandidateCollection>{}),
      trackCandidateT4T5TCPutToken_(produceTrackCandidates_ ? produces<TrackCandidateCollection>("t4t5TCsLST")
                                                            : edm::EDPutTokenT<TrackCandidateCollection>{}),
      trackCandidateNopLSTCPutToken_(produceTrackCandidates_ ? produces<TrackCandidateCollection>("nopLSTCsLST")
                                                             : edm::EDPutTokenT<TrackCandidateCollection>{}),
      trackCandidatepTTCPutToken_(produceTrackCandidates_ ? produces<TrackCandidateCollection>("pTTCsLST")
                                                          : edm::EDPutTokenT<TrackCandidateCollection>{}),
      trackCandidatepLSTCPutToken_(produceTrackCandidates_ ? produces<TrackCandidateCollection>("pLSTCsLST")
                                                           : edm::EDPutTokenT<TrackCandidateCollection>{}),
      seedStopInfoPutToken_(produceTrackCandidates_ ? produces<std::vector<SeedStopInfo>>("")
                                                    : edm::EDPutTokenT<std::vector<SeedStopInfo>>{}),
      pTCsSeedStopInfoPutToken_(produceTrackCandidates_ ? produces<std::vector<SeedStopInfo>>("pTCsLST")
                                                        : edm::EDPutTokenT<std::vector<SeedStopInfo>>{}),
      t4t5TCsSeedStopInfoPutToken_(produceTrackCandidates_ ? produces<std::vector<SeedStopInfo>>("t4t5TCsLST")
                                                           : edm::EDPutTokenT<std::vector<SeedStopInfo>>{}),
      pTTCsSeedStopInfoPutToken_(produceTrackCandidates_ ? produces<std::vector<SeedStopInfo>>("pTTCsLST")
                                                         : edm::EDPutTokenT<std::vector<SeedStopInfo>>{}) {
  if (auto const ot = iConfig.getParameter<edm::InputTag>("otClustersOnDemand"); !ot.label().empty()) {
    otClustersToken_ = consumes(ot);
    otCpeToken_ = esConsumes(iConfig.getParameter<edm::ESInputTag>("Phase2StripCPE"));
  }
  if (otClustersToken_.isUninitialized())
    lstOTHitsToken_ = consumes(iConfig.getParameter<edm::InputTag>("lstInput"));
  if (!lazyPixelSeeds_) {
    lstPixelSeedTokens_ =
        edm::vector_transform(iConfig.getParameter<std::vector<edm::InputTag>>("lstPixelSeeds"),
                              [this](edm::InputTag const& tag) { return consumes<TrajectorySeedCollection>(tag); });
  } else {
    if (produceTrackCandidates_)
      throw cms::Exception("Configuration")
          << "LSTOutputConverter: lazyPixelSeeds = True needs produceTrackCandidates = False (no pixel seed collection "
             "for the track candidates' seed references)";
    pixelTracksToken_ = consumes(iConfig.getParameter<edm::InputTag>("pixelTracks"));
    pixelSeedCreator_ = SeedCreatorFactory::get()->create("SeedFromConsecutiveHitsCreator",
                                                          iConfig.getParameter<edm::ParameterSet>("pixelSeedCreatorPSet"),
                                                          consumesCollector());
    if (auto const& t = iConfig.getParameter<edm::InputTag>("lazyCheckSeeds"); !t.label().empty())
      lazyCheckToken_ = consumes(t);
  }
}

void LSTOutputConverter::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;

  desc.add<edm::InputTag>("lstOutput", edm::InputTag("lstProducer"));
  desc.add<edm::InputTag>("lstInput", edm::InputTag("lstInputProducer"));
  desc.add<std::vector<edm::InputTag>>("lstPixelSeeds", {edm::InputTag("lstInputProducer")})
      ->setComment("the pixelSeeds collections of the LST input, in the same order, or its copy of them");
  desc.add<bool>("includeT5s", true);
  desc.add<bool>("includeNonpLSTSs", false);
  desc.add<bool>("dropOTHitsPurePLS", false);
  desc.add<int>("maxITHitsToDropOTHitsPurePLS", 3);
  desc.add<bool>("produceSeeds", true);
  desc.add<bool>("produceTrackCandidates", true);
  desc.add<bool>("seedStates", true)
      ->setComment("false (only with produceTrackCandidates = false): T5/T4/pT3/pT5 seeds carry the seed hits and a "
                   "placeholder state instead of the seed creator's fit (FastHelix + KF); for a consumer that fits "
                   "the seed state itself (MkFitAlpaka device LST seed fit, O6-1 option b)");
  desc.add<edm::InputTag>("otClustersOnDemand", edm::InputTag(""))
      ->setComment("local patch (stage D): OT hits of the TCs made on demand from these clusters with Phase2StripCPE "
                   "(= the legacy rechits at the same cluster key); empty = the LST input's hit pointers");
  desc.add<edm::ESInputTag>("Phase2StripCPE", edm::ESInputTag("phase2StripCPEESProducer", "Phase2StripCPE"));
  desc.add<bool>("lazyPixelSeeds", false)
      ->setComment("true: pixelSeedIndex = index in 'pixelTracks'; the pixel seeds of pLS/pT3/pT5 TCs are made here with "
                   "pixelSeedCreatorPSet and SeedGeneratorFromProtoTracksEDProducer's region (lstPixelSeeds unused)");
  desc.add<edm::InputTag>("pixelTracks", edm::InputTag("hltPhase2PixelTracks"));
  desc.add<bool>("includeFourthHit", true)->setComment("as SeedGeneratorFromProtoTracksEDProducer's includeFourthHit");
  desc.add<edm::InputTag>("lazyCheckSeeds", edm::InputTag(""))
      ->setComment("validation: compare every lazy pixel seed bitwise with this collection (same index; events whose "
                   "size differs from pixelTracks are skipped)");
  desc.add("propagatorAlong", edm::ESInputTag{"", "PropagatorWithMaterial"});
  desc.add("propagatorOpposite", edm::ESInputTag{"", "PropagatorWithMaterialOpposite"});

  edm::ParameterSetDescription psd0;
  psd0.add<std::string>("ComponentName", std::string("SeedFromConsecutiveHitsCreator"));
  psd0.add<std::string>("propagator", std::string("PropagatorWithMaterial"));
  psd0.add<double>("SeedMomentumForBOFF", 5.0);
  psd0.add<double>("OriginTransverseErrorMultiplier", 1.0);
  psd0.add<double>("MinOneOverPtError", 1.0);
  psd0.add<std::string>("magneticField", std::string(""));
  psd0.add<std::string>("TTRHBuilder", std::string("WithTrackAngle"));
  psd0.add<bool>("forceKinematicWithRegionDirection", false);
  desc.add<edm::ParameterSetDescription>("SeedCreatorPSet", psd0);
  desc.add<edm::ParameterSetDescription>("pixelSeedCreatorPSet", psd0)
      ->setComment("lazyPixelSeeds: the SeedCreatorPSet of the pixel-seed producer (hltInitialStepSeeds)");

  descriptions.addWithDefaultLabel(desc);
}

void LSTOutputConverter::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  // Setup
  auto const& lstOutput = iEvent.get(lstOutputToken_);
  // LST pixel seed indices run over the lstPixelSeeds collections in order
  std::vector<edm::Handle<TrajectorySeedCollection>> pixelSeedHandles;
  std::vector<unsigned int> pixelSeedOffsets;
  unsigned int nPixelSeeds = 0;
  for (auto const& token : lstPixelSeedTokens_) {
    pixelSeedHandles.push_back(iEvent.getHandle(token));
    pixelSeedOffsets.push_back(nPixelSeeds);
    nPixelSeeds += pixelSeedHandles.back()->size();
  }
  auto pixelSeedRef = [&](unsigned int seedIndex) {
    unsigned int collection = pixelSeedOffsets.size() - 1;
    while (pixelSeedOffsets[collection] > seedIndex)
      --collection;
    return edm::Ref<TrajectorySeedCollection>(pixelSeedHandles[collection], seedIndex - pixelSeedOffsets[collection]);
  };
  // lazy pixel seeds: made on first use per pixel track (state: -2 not tried, -1 failed, >= 0 index in lazySeeds)
  reco::TrackCollection const* pixelTracks = lazyPixelSeeds_ ? &iEvent.get(pixelTracksToken_) : nullptr;
  std::vector<int> lazyState(pixelTracks ? pixelTracks->size() : 0, -2);
  std::vector<std::unique_ptr<TrajectorySeed>> lazySeeds;
  TrajectorySeedCollection lazyTmp;
  // the recipe of SeedGeneratorFromProtoTracksEDProducer for one proto track: interface/LSTProtoTrackSeed.h
  auto protoHits = [&](unsigned int e) { return lst::protoTrackHitsByRadius((*pixelTracks)[e]); };
  auto lazySeed = [&](unsigned int e) -> TrajectorySeed const* {
    if (e >= lazyState.size())
      return nullptr;
    if (lazyState[e] == -2) {
      ++lazyNeeded_;
      lazyState[e] = -1;
      auto const& proto = (*pixelTracks)[e];
      auto hits = protoHits(e);
      if (lst::makeProtoTrackSeed(*pixelSeedCreator_, proto, hits, includeFourthHit_, iSetup, lazyTmp)) {
        if (!lazyCheckToken_.isUninitialized() &&
            std::abs(hits[1]->globalPosition().perp() - hits[0]->globalPosition().perp()) < 0.05f)
          edm::LogPrint("LSTOutputConverter")  // validation: a made seed whose first two hits are at the same radius
              << "LSTOUT_LAZYNEAROK pt " << proto.pt() << " eta " << proto.eta();
        lazyState[e] = int(lazySeeds.size());
        lazySeeds.emplace_back(std::make_unique<TrajectorySeed>(lazyTmp.front()));
      }
      if (lazyState[e] < 0) {
        ++lazyFailed_;
        if (!lazyCheckToken_.isUninitialized()) {  // validation (lazycheck): the failed proto track's hits, radius order
          std::ostringstream os;
          os << "LSTOUT_LAZYFAIL pt " << proto.pt() << " eta " << proto.eta() << " nh " << hits.size() << " |";
          for (auto const& h : hits)
            os << " " << h->geographicalId().subdetId() << ":" << h->globalPosition().perp() << ","
               << h->globalPosition().z();
          edm::LogPrint("LSTOutputConverter") << os.str();
        }
      }
    }
    return lazyState[e] >= 0 ? lazySeeds[lazyState[e]].get() : nullptr;
  };
  auto const& mf = iSetup.getData(mfToken_);
  auto const& propAlo = iSetup.getData(propagatorAlongToken_);
  auto const& propOppo = iSetup.getData(propagatorOppositeToken_);
  auto const& tracker = iSetup.getData(tGeomToken_);
  if (detTypeGeom_ != &tracker) {
    detTypeCache_.clear();
    detTypeGeom_ = &tracker;
  }
  auto const* gc = globalCache();
  {
    TrackerGeometry const* none = nullptr;
    gc->geom.compare_exchange_strong(none, &tracker);
  }
  bool const sharedDetType = gc->geom.load() == &tracker;  // else (another geometry IOV): the per-stream memo
  auto detType = [&](DetId id) {
    if (sharedDetType) {
      if (auto it = gc->detType.find(id.rawId()); it != gc->detType.end())
        return it->second;
      auto const t = tracker.getDetectorType(id);
      gc->detType.emplace(id.rawId(), t);
      return t;
    }
    auto [it, isNew] = detTypeCache_.try_emplace(id.rawId(), TrackerGeometry::ModuleType::UNKNOWN);
    if (isNew)
      it->second = tracker.getDetectorType(id);
    return it->second;
  };
  const TrackerTopology& tTopo = iSetup.getData(tTopoToken_);

  auto lstOutput_view = lstOutput.const_view();
  unsigned int nTrackCandidates = lstOutput_view.nTrackCandidates();

  auto const outputTSRP = iEvent.getRefBeforePut(trajectorySeedPutToken_);

  TrajectorySeedCollection outputTS, outputpLSTS;
  outputTS.reserve(nTrackCandidates);
  outputpLSTS.reserve(nTrackCandidates);
  TrackCandidateCollection outputTC, outputpTC, outputT4T5TC, outputNopLSTC, outputpTTC, outputpLSTC;
  outputTC.reserve(nTrackCandidates);
  outputpTC.reserve(nTrackCandidates);
  outputT4T5TC.reserve(nTrackCandidates);
  outputNopLSTC.reserve(nTrackCandidates);
  outputpTTC.reserve(nTrackCandidates);
  outputpLSTC.reserve(nTrackCandidates);

  static const lst::LSTOTHits kNoOTHits;
  auto const& OTHits = lstOTHitsToken_.isUninitialized() ? kNoOTHits.hits : iEvent.get(lstOTHitsToken_).hits;
  std::optional<Phase2TrackerRecHitOnDemand> otOnDemand;
  if (!otClustersToken_.isUninitialized())
    otOnDemand.emplace(iEvent.getHandle(otClustersToken_), tracker, iSetup.getData(otCpeToken_));

  TrajectorySeedCollection seeds;
  using Hit = SeedingHitSet::ConstRecHitPointer;
  std::vector<Hit> hitsForSeed;
  // local patch (MkFitAlpaka round 9, lane hostrest): the candidate's hits are kept as pointers (pixel seed hits, LST
  // input OT hits, or on-demand OT hits held in otStore) and cloned only for an output that keeps them; seeds are
  // moved into the outputs. Same hits and order as before (std::sort, same comparator, same sequence = OwnVector::sort).
  constexpr unsigned int kMaxOTHitsPerTC =
      (lst::Params_TC::kLayers - lst::Params_TC::kPixelLayerSlots) * lst::Params_TC::kHitsPerLayer;
  std::vector<TrackingRecHit const*> hitPtrs;
  hitPtrs.reserve(lst::Params_TC::kLayers * lst::Params_TC::kHitsPerLayer + 16);
  std::vector<Phase2TrackerRecHit1D> otStore;
  otStore.reserve(kMaxOTHitsPerTC);  // at most one entry per OT slot: never reallocated, the pointers stay valid
  TrajectorySeed madeSeed;           // a T5/T4 or OT-dropped pLS seed that the track candidate also needs
  auto hitLess = [](TrackingRecHit const* pa, TrackingRecHit const* pb) {
    const auto& a = *pa;
    const auto& b = *pb;
    const auto asub = a.det()->subDetector();
    const auto bsub = b.det()->subDetector();
    if (GeomDetEnumerators::isInnerTracker(asub) && GeomDetEnumerators::isOuterTracker(bsub)) {
      return true;
    } else if (GeomDetEnumerators::isOuterTracker(asub) && GeomDetEnumerators::isInnerTracker(bsub)) {
      return false;
    } else if (asub != bsub) {
      return asub < bsub;
    } else {
      const auto& apos = a.surface();
      const auto& bpos = b.surface();
      if (GeomDetEnumerators::isBarrel(asub)) {
        return apos->rSpan().first < bpos->rSpan().first;
      } else {
        return std::abs(apos->zSpan().first) < std::abs(bpos->zSpan().first);
      }
    }
  };

  LogDebug("LSTOutputConverter") << "nTrackCandidates " << nTrackCandidates;
  for (unsigned int i = 0; i < nTrackCandidates; i++) {
    auto iType = lstOutput_view.trackCandidateType()[i];
    bool const isT5orT4 = (iType == lst::LSTObjType::T5 || iType == lst::LSTObjType::T4);
    const auto iSeed = lstOutput_view.pixelSeedIndex()[i];
    TrajectorySeed const* pixelSeed = nullptr;
    if (iType != lst::LSTObjType::T5 && iType != lst::LSTObjType::T4) {
      pixelSeed = lazyPixelSeeds_ ? lazySeed(iSeed) : &*pixelSeedRef(iSeed);
      if (pixelSeed == nullptr) {
        // lazy only: no seed for this pixel track (the creator failed; stock would have had no pLS for it)
        ++lazySkipped_;
        continue;
      }
    }
    const bool dropHitsOTpL =
        iType == lst::LSTObjType::pLS && dropOTHitsPurePLS_ &&
        std::prev(pixelSeed->recHits().end())->geographicalId().subdetId() > PixelSubdetector::PixelEndcap &&
        std::ranges::count_if(pixelSeed->recHits(), [](const auto& h) {
          return h.geographicalId().subdetId() <= PixelSubdetector::PixelEndcap;
        }) <= maxITHitsToDropOTHitsPurePLS_;
    LogDebug("LSTOutputConverter") << " cand " << i << " " << iType << " " << iSeed;
    TrajectorySeed const* seed = nullptr;  // the candidate's seed (pixel seed or madeSeed)
    edm::RefToBase<TrajectorySeed> seedRef;
    hitPtrs.clear();
    otStore.clear();
    if (!isT5orT4 && !dropHitsOTpL) {
      seed = pixelSeed;
      if (!lazyPixelSeeds_)
        seedRef = edm::RefToBase<TrajectorySeed>(pixelSeedRef(iSeed));

      for (auto const& hit : pixelSeed->recHits())
        hitPtrs.push_back(&hit);
    }

    if (iType != lst::LSTObjType::pLS) {
      // The pixel hits are packed into first kPixelLayerSlots layer slots.
      for (unsigned int layerSlot = lst::Params_TC::kPixelLayerSlots; layerSlot < lst::Params_TC::kLayers;
           ++layerSlot) {
        for (unsigned int hitSlot = 0; hitSlot < lst::Params_TC::kHitsPerLayer; ++hitSlot) {
          unsigned int hitIdx = lstOutput_view.hitIndices()[i][layerSlot][hitSlot];
          if (hitIdx == lst::kTCEmptyHitIdx)
            continue;
          // stage D: the hit the legacy collection holds at this cluster key (LST OT hit index = cluster key)
          TrackingRecHit const* otHitPtr;
          if (otOnDemand) {
            otStore.push_back(otOnDemand->make(hitIdx));
            otHitPtr = &otStore.back();
          } else {
            otHitPtr = OTHits[hitIdx];
          }
          bool hitOK = true;
          for (auto const* hit : hitPtrs)
            if (hit->sharesInput(otHitPtr, TrackingRecHit::all)) {
              hitOK = false;
              break;
            }
          if (hitOK)
            hitPtrs.push_back(otHitPtr);
          else if (otOnDemand)
            otStore.pop_back();
        }
      }

      std::sort(hitPtrs.begin(), hitPtrs.end(), hitLess);

      // For T5/T4: makeSeed is needed whenever seeds or TCs are produced, since the resulting
      // seed is the only source of initial state for T5/T4 track candidates.
      // For other pT objects: makeSeed is only needed for seed output.
      if ((isT5orT4 && (produceSeeds_ || produceTrackCandidates_)) ||
          (!isT5orT4 && produceSeeds_ && includeNonpLSTSs_)) {
        hitsForSeed.clear();
        hitsForSeed.reserve(hitPtrs.size());
        int n = 0;
        unsigned int firstLayer;
        for (auto const* hitp : hitPtrs) {
          auto const& hit = *hitp;
          // getDetectorType is a linear search: only call it for the hits whose type decides.
          if (iType == lst::LSTObjType::T5) {
            if (n < 2 && detType(hit.geographicalId()) != TrackerGeometry::ModuleType::Ph2PSP)
              continue;  // the first two should be P
          }
          if (iType == lst::LSTObjType::T4) {
            unsigned int hitLayer = tTopo.layer(hit.geographicalId());
            if (n == 0)
              firstLayer = hitLayer;
            else if (hitLayer == firstLayer && detType(hit.geographicalId()) == TrackerGeometry::ModuleType::Ph2PSS)
              continue;
          }
          hitsForSeed.emplace_back(dynamic_cast<Hit>(hitp));
          n++;
        }
        seeds.clear();
        if (!seedStates_ && !produceTrackCandidates_ && !hitsForSeed.empty()) {
          // light seed: the selected seed hits + a placeholder state on the last hit's det (unit momentum along
          // the module normal); the consumer replaces the state (MkFitAlpaka device seed fit)
          edm::OwnVector<TrackingRecHit> seedHits;
          for (auto const& h : hitsForSeed)
            seedHits.push_back(h->hit()->clone());
          auto const* last = hitsForSeed.back()->hit();
          const LocalPoint lp = last->localPosition();
          const LocalTrajectoryParameters ltp(1.f, 0.f, 0.f, lp.x(), lp.y(), 1.f);
          float err[15] = {1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 1.f};
          seeds.emplace_back(PTrajectoryStateOnDet(ltp, 1.f, err, last->geographicalId().rawId(), 0),
                             std::move(seedHits),
                             alongMomentum);
        } else {
          GlobalTrackingRegion region;
          seedCreator_->init(region, iSetup, nullptr);
          seedCreator_->makeSeed(seeds, hitsForSeed);
        }
        if (seeds.empty()) {
          edm::LogInfo("LSTOutputConverter") << "failed to convert a LST object to a seed" << i << " " << iType << " "
                                             << lstOutput_view.pixelSeedIndex()[i];
          if (isT5orT4)
            continue;
        }
        if (isT5orT4) {
          seedRef = edm::RefToBase<TrajectorySeed>(edm::Ref(outputTSRP, outputTS.size()));
          if (produceTrackCandidates_) {
            madeSeed = std::move(seeds[0]);
            seed = &madeSeed;
            outputTS.emplace_back(madeSeed);
          } else {
            outputTS.emplace_back(std::move(seeds[0]));
          }
        } else if (seeds.empty()) {
          outputTS.emplace_back(*pixelSeed);  // pT3/pT5 whose seed fit failed: the pixel seed
        } else {
          outputTS.emplace_back(std::move(seeds[0]));
        }
        auto const& ss = outputTS.back().startingState();
        LogDebug("LSTOutputConverter") << "Created a seed with " << outputTS.back().nHits() << " " << ss.detId() << " "
                                       << ss.pt() << " " << ss.parameters().vector() << " " << ss.error(0);
      }
    } else if (produceSeeds_ || (produceTrackCandidates_ && dropHitsOTpL)) {  // pLS handling
      if (dropHitsOTpL) {                                                     // true if need to drop OT hits
        hitsForSeed.clear();
        hitsForSeed.reserve(std::ranges::size(pixelSeed->recHits()));
        for (auto const& hit : pixelSeed->recHits()) {
          if (hit.geographicalId().subdetId() > PixelSubdetector::PixelEndcap)
            continue;
          hitsForSeed.emplace_back(dynamic_cast<Hit>(&hit));
          hitPtrs.push_back(&hit);
        }
        GlobalTrackingRegion region;
        seedCreator_->init(region, iSetup, nullptr);
        seeds.clear();
        seedCreator_->makeSeed(seeds, hitsForSeed);
        if (seeds.empty())
          edm::LogInfo("LSTOutputConverter") << "failed to convert a pLS object to a seed" << i << iSeed;
        madeSeed = std::move(seeds[0]);
        seed = &madeSeed;
        seedRef = edm::RefToBase<TrajectorySeed>(edm::Ref(outputTSRP, outputTS.size()));
      }
      outputTS.emplace_back(*seed);
      if (produceSeeds_) {  // matches the logic for iEvent.emplace
        if (dropHitsOTpL && !produceTrackCandidates_)
          outputpLSTS.emplace_back(std::move(madeSeed));
        else
          outputpLSTS.emplace_back(*seed);
      }
    }

    if (!produceTrackCandidates_)
      continue;

    edm::OwnVector<TrackingRecHit> recHits;
    recHits.reserve(hitPtrs.size());
    for (auto const* h : hitPtrs)
      recHits.push_back(h->clone());

    TrajectoryStateOnSurface tsos =
        trajectoryStateTransform::transientState(seed->startingState(), (seed->recHits().end() - 1)->surface(), &mf);
    tsos.rescaleError(100.);
    auto tsosPair = propOppo.propagateWithPath(tsos, *recHits[0].surface());
    if (!tsosPair.first.isValid()) {
      LogDebug("LSTOutputConverter") << "Propagating to startingState opposite to momentum failed, trying along next";
      tsosPair = propAlo.propagateWithPath(tsos, *recHits[0].surface());
    }
    if (tsosPair.first.isValid()) {
      PTrajectoryStateOnDet st =
          trajectoryStateTransform::persistentState(tsosPair.first, recHits[0].det()->geographicalId().rawId());

      if (!includeT5s_ && isT5orT4)
        continue;

      auto tc = TrackCandidate(recHits, *seed, st, seedRef);
      outputTC.emplace_back(tc);
      if (isT5orT4) {
        outputT4T5TC.emplace_back(tc);
        outputNopLSTC.emplace_back(std::move(tc));
      } else {
        outputpTC.emplace_back(tc);
        if (iType != lst::LSTObjType::pLS) {
          outputNopLSTC.emplace_back(tc);
          outputpTTC.emplace_back(std::move(tc));
        } else {
          outputpLSTC.emplace_back(std::move(tc));
        }
      }
    } else {
      edm::LogInfo("LSTOutputConverter") << "Failed to make a candidate initial state. Seed state is " << tsos
                                         << " TC cand " << i << " " << lstOutput_view.pixelSeedIndex()[i] << " "
                                         << lstOutput_view.pixelSeedIndex()[i] << " first hit "
                                         << recHits.front().globalPosition() << " last hit "
                                         << recHits.back().globalPosition();
    }
  }

  if (lazyPixelSeeds_ && !lazyCheckToken_.isUninitialized()) {
    // validation: every lazy seed vs the reference collection's seed of the same pixel track (bitwise)
    auto const& ref = iEvent.get(lazyCheckToken_);
    {
      // the reference seed of a pixel track: same index when no host seed failed, else found by its first and last
      // hit clusters (the creator makes at most one seed per track, in track order)
      auto hitKey = [](TrajectorySeed const& s) {
        auto const& f = *s.recHits().begin();
        auto const& l = *(s.recHits().end() - 1);
        auto ck = [](TrackingRecHit const& h) {
          auto const* b = dynamic_cast<BaseTrackerRecHit const*>(&h);
          return b ? uint64_t(b->firstClusterRef().index()) : uint64_t(0xffffffff);
        };
        return std::array<uint64_t, 5>{
            uint64_t(s.nHits()), uint64_t(f.geographicalId().rawId()), ck(f), uint64_t(l.geographicalId().rawId()), ck(l)};
      };
      std::map<std::array<uint64_t, 5>, size_t> refIdx;
      if (ref.size() != pixelTracks->size())
        for (size_t k = 0; k < ref.size(); ++k)
          refIdx.emplace(hitKey(ref[k]), k);
      long bad = 0, n = 0, noRef = 0, failed = 0;
      for (size_t e = 0; e < lazyState.size(); ++e) {
        if (lazyState[e] == -2)
          continue;
        ++n;
        if (lazyState[e] < 0) {
          ++failed;  // no lazy seed: the creator failed (no host seed either, if bitwise)
          continue;
        }
        auto const& a = *lazySeeds[lazyState[e]];
        size_t k = e;
        if (ref.size() != pixelTracks->size()) {
          auto it = refIdx.find(hitKey(a));
          if (it == refIdx.end()) {
            ++noRef;
            ++bad;
            continue;
          }
          k = it->second;
        }
        auto const& b = ref[k];
        bool same = a.nHits() == b.nHits() && a.direction() == b.direction() &&
                    a.startingState().detId() == b.startingState().detId() &&
                    a.startingState().surfaceSide() == b.startingState().surfaceSide();
        auto const& pa = a.startingState().parameters();
        auto const& pb = b.startingState().parameters();
        for (int k = 0; same && k < 5; ++k)
          same = std::bit_cast<uint32_t>(float(pa.vector()[k])) == std::bit_cast<uint32_t>(float(pb.vector()[k]));
        for (int k = 0; same && k < 15; ++k)
          same = std::bit_cast<uint32_t>(a.startingState().error(k)) == std::bit_cast<uint32_t>(b.startingState().error(k));
        same = same && pa.charge() == pb.charge();
        if (same) {
          auto ia = a.recHits().begin();
          for (auto const& hb : b.recHits()) {
            auto const& ha = *ia++;
            same = same && ha.geographicalId() == hb.geographicalId() &&
                   std::bit_cast<uint32_t>(ha.localPosition().x()) == std::bit_cast<uint32_t>(hb.localPosition().x()) &&
                   std::bit_cast<uint32_t>(ha.localPosition().y()) == std::bit_cast<uint32_t>(hb.localPosition().y()) &&
                   std::bit_cast<uint32_t>(ha.localPositionError().xx()) ==
                       std::bit_cast<uint32_t>(hb.localPositionError().xx()) &&
                   std::bit_cast<uint32_t>(ha.localPositionError().yy()) ==
                       std::bit_cast<uint32_t>(hb.localPositionError().yy()) &&
                   ha.sharesInput(&hb, TrackingRecHit::all);
          }
        }
        bad += !same;
      }
      checkSeeds_ += n;
      checkBad_ += bad;
      checkEvSkipped_ += ref.size() != pixelTracks->size();
      edm::LogPrint("LSTOutputConverter") << "LSTOUT_LAZY event " << iEvent.id().event() << " pixelTracks "
                                          << pixelTracks->size() << " hostSeeds " << ref.size() << " lazySeeds " << n
                                          << " failed " << failed << " noRef " << noRef << " notBitwise " << bad
                                          << " totals: made " << lazyNeeded_ << " failed " << lazyFailed_
                                          << " TCsSkipped " << lazySkipped_ << " checked " << checkSeeds_
                                          << " notBitwise " << checkBad_ << " eventsMatchedByHits " << checkEvSkipped_;
    }
  }

  LogDebug("LSTOutputConverter") << "done with conversion: Track candidate output size = " << outputpTC.size()
                                 << " (p* objects) + " << outputT4T5TC.size() << " (T5 objects)";

  iEvent.emplace(trajectorySeedPutToken_, std::move(outputTS));
  if (produceSeeds_)
    iEvent.emplace(trajectorySeedpLSPutToken_, std::move(outputpLSTS));
  if (produceTrackCandidates_) {
    //dummy (for now) stop infos: one per used kind of candidates
    iEvent.emplace(seedStopInfoPutToken_, std::vector<SeedStopInfo>(nPixelSeeds));
    iEvent.emplace(pTCsSeedStopInfoPutToken_, std::vector<SeedStopInfo>(nPixelSeeds));
    iEvent.emplace(t4t5TCsSeedStopInfoPutToken_, std::vector<SeedStopInfo>(outputT4T5TC.size()));
    iEvent.emplace(pTTCsSeedStopInfoPutToken_, std::vector<SeedStopInfo>(nPixelSeeds));
    iEvent.emplace(trackCandidatePutToken_, std::move(outputTC));
    iEvent.emplace(trackCandidatepTCPutToken_, std::move(outputpTC));
    iEvent.emplace(trackCandidateT4T5TCPutToken_, std::move(outputT4T5TC));
    iEvent.emplace(trackCandidateNopLSTCPutToken_, std::move(outputNopLSTC));
    iEvent.emplace(trackCandidatepTTCPutToken_, std::move(outputpTTC));
    iEvent.emplace(trackCandidatepLSTCPutToken_, std::move(outputpLSTC));
  }
}

DEFINE_FWK_MODULE(LSTOutputConverter);
