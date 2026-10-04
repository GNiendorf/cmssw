#include "HeterogeneousCore/AlpakaCore/interface/alpaka/Event.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EventSetup.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/global/EDProducer.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDGetToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/EDPutToken.h"
#include "HeterogeneousCore/AlpakaCore/interface/alpaka/ESGetToken.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"

#include "DataFormats/TrackerRecHit2D/interface/Phase2TrackerRecHit1D.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeedCollection.h"

#include "FWCore/Utilities/interface/transform.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "DataFormats/TrackerRecHit2D/interface/SiStripMatchedRecHit2DCollection.h"
#include "TrackingTools/Records/interface/TransientRecHitRecord.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"
#include "TrackingTools/TransientTrackingRecHit/interface/TransientTrackingRecHitBuilder.h"
#include "TrackingTools/PatternTools/interface/TSCBLBuilderNoMaterial.h"
#include "TrackingTools/TrajectoryState/interface/PerigeeConversions.h"

#include "RecoTracker/LSTCore/interface/LSTInputHostCollection.h"
// local patch (MkFitAlpaka round 8, lane otdev, stage D): OT hits from the device OT rechit SoA (host copy)
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/OTRecHitSoA.h"
// local patch (MkFitAlpaka round 9, lane lstin): otKeysOnly (OT values filled on the device by hltInputLSTDevice)
#include "DataFormats/Phase2TrackerCluster/interface/Phase2TrackerCluster1D.h"
#include <cstring>
#include "RecoTracker/LSTCore/interface/LSTOTHits.h"
#include "RecoTracker/LSTCore/interface/LSTPrepareInput.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE {

  class LSTInputProducer : public global::EDProducer<> {
  public:
    LSTInputProducer(edm::ParameterSet const& iConfig);
    ~LSTInputProducer() override = default;

    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

  private:
    void produce(edm::StreamID, device::Event& iEvent, const device::EventSetup& iSetup) const override;

    const double ptCut_;

    edm::EDGetTokenT<Phase2TrackerRecHit1DCollectionNew> phase2OTRecHitToken_;
    // local patch (stage D): otSoA set = OT detId / size / global position from the OT rechit SoA, no hit pointers
    // (the LST output converter then makes the OT hits on demand); compareOTTo = the legacy rechits, bitwise check
    edm::EDGetTokenT<mkfitdev::OTRecHitHostCollection> otSoAToken_;
    bool otSoA_ = false;
    bool compareOT_ = false;
    // local patch (round 9, lane lstin): otKeysOnly set = the OT clusters; the OT rows keep only what the pixel seeds
    // read (detId, cluster size, per cluster key); x/y/z are left 0 and no OT SoA host copy or legacy rechit is read:
    // MkFitAlpakaLstInputProducer (fromHostInput) copies this collection to the device and fills the OT rows from the
    // device OT rechit SoA (the same values bitwise). Validation: compare with an unpatched clone.
    edm::EDGetTokenT<Phase2TrackerCluster1DCollectionNew> otKeysOnlyToken_;
    bool otKeysOnly_ = false;

    const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mfToken_;
    const edm::EDGetTokenT<reco::BeamSpot> beamSpotToken_;
    const std::vector<edm::EDGetTokenT<TrajectorySeedCollection>> seedTokens_;
    const bool producePixelSeeds_;
    const edm::EDPutTokenT<TrajectorySeedCollection> lstPixelSeedsPutToken_;

    const edm::EDPutTokenT<lst::LSTInputHostCollection> lstInputPutToken_;
    // OT hit pointers stay on the host (read only by LSTOutputConverter), same order as the OT hits of lstInput
    const edm::EDPutTokenT<lst::LSTOTHits> lstOTHitsPutToken_;
  };

  LSTInputProducer::LSTInputProducer(edm::ParameterSet const& iConfig)
      : EDProducer<>(iConfig),
        ptCut_(iConfig.getParameter<double>("ptCut")),

        mfToken_(esConsumes()),
        beamSpotToken_(consumes(iConfig.getParameter<edm::InputTag>("beamSpot"))),
        seedTokens_(
            edm::vector_transform(iConfig.getParameter<std::vector<edm::InputTag>>("pixelSeeds"),
                                  [&](const edm::InputTag& tag) { return consumes<TrajectorySeedCollection>(tag); })),
        producePixelSeeds_(iConfig.getParameter<bool>("producePixelSeeds")),
        lstPixelSeedsPutToken_(producePixelSeeds_ ? edm::EDPutTokenT<TrajectorySeedCollection>(produces())
                                                  : edm::EDPutTokenT<TrajectorySeedCollection>{}),
        lstInputPutToken_(produces()),
        lstOTHitsPutToken_(produces()) {
    otKeysOnly_ = !iConfig.getParameter<edm::InputTag>("otKeysOnly").label().empty();
    if (otKeysOnly_) {
      otKeysOnlyToken_ = consumes(iConfig.getParameter<edm::InputTag>("otKeysOnly"));
      return;
    }
    otSoA_ = !iConfig.getParameter<edm::InputTag>("otSoA").label().empty();
    compareOT_ = otSoA_ && !iConfig.getParameter<edm::InputTag>("compareOTTo").label().empty();
    if (otSoA_)
      otSoAToken_ = consumes(iConfig.getParameter<edm::InputTag>("otSoA"));
    if (!otSoA_)
      phase2OTRecHitToken_ = consumes(iConfig.getParameter<edm::InputTag>("phase2OTRecHits"));
    else if (compareOT_)
      phase2OTRecHitToken_ = consumes(iConfig.getParameter<edm::InputTag>("compareOTTo"));
  }

  void LSTInputProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;

    desc.add<double>("ptCut", 0.8);

    desc.add<edm::InputTag>("phase2OTRecHits", edm::InputTag("siPhase2RecHits"));
    desc.add<edm::InputTag>("otSoA", edm::InputTag(""))
        ->setComment("local patch (stage D): OT rechit SoA (MkFitAlpakaOTRecHitsProducer, host copy) instead of "
                     "phase2OTRecHits; needs the LST output converter's otClustersOnDemand");
    desc.add<edm::InputTag>("compareOTTo", edm::InputTag(""))
        ->setComment("validation with otSoA: legacy OT rechits to compare the OT hits with bitwise (LSTIN_OT lines)");
    desc.add<edm::InputTag>("otKeysOnly", edm::InputTag(""))
        ->setComment("local patch (round 9): the OT clusters; OT rows with detId/size only (x/y/z = 0), filled on the "
                     "device by MkFitAlpakaLstInputProducer fromHostInput; otSoA and phase2OTRecHits are not read");

    desc.add<edm::InputTag>("beamSpot", edm::InputTag("offlineBeamSpot"));
    desc.add<std::vector<edm::InputTag>>(
        "pixelSeeds",
        std::vector<edm::InputTag>{edm::InputTag("initialStepSeeds"), edm::InputTag("highPtTripletStepSeeds")});
    desc.add<bool>("producePixelSeeds", true)
        ->setComment("put a copy of all pixelSeeds in the event, for consumers that need them as one collection");

    descriptions.addWithDefaultLabel(desc);
  }

  void LSTInputProducer::produce(edm::StreamID iID, device::Event& iEvent, const device::EventSetup& iSetup) const {
    std::vector<unsigned int> ph2_detId;
    std::vector<uint16_t> ph2_clustSize;
    std::vector<float> ph2_x;
    std::vector<float> ph2_y;
    std::vector<float> ph2_z;
    std::vector<TrackingRecHit const*> ph2_hits;
    if (otKeysOnly_) {
      // per cluster key: the detset's DetId and the cluster size (all the pixel seeds' OT hits read); x/y/z/pointer
      // rows exist (the hit block layout) but are filled on the device
      auto const& clu = iEvent.get(otKeysOnlyToken_);
      const uint32_t n = clu.dataSize();
      ph2_detId.resize(n);
      ph2_clustSize.resize(n);
      ph2_x.assign(n, 0.f);
      ph2_y.assign(n, 0.f);
      ph2_z.assign(n, 0.f);
      ph2_hits.assign(n, nullptr);
      uint32_t k = 0;
      for (auto const& ds : clu) {
        const uint32_t id = ds.detId();
        for (auto const& c : ds) {
          ph2_detId[k] = id;
          ph2_clustSize[k++] = c.size();
        }
      }
    } else if (otSoA_) {
      auto const v = iEvent.get(otSoAToken_).const_view();
      const uint32_t n = v.nHits();
      ph2_detId.assign(v.metadata().addressOf_detId(), v.metadata().addressOf_detId() + n);
      ph2_clustSize.assign(v.metadata().addressOf_clustSize(), v.metadata().addressOf_clustSize() + n);
      ph2_x.assign(v.metadata().addressOf_gx(), v.metadata().addressOf_gx() + n);
      ph2_y.assign(v.metadata().addressOf_gy(), v.metadata().addressOf_gy() + n);
      ph2_z.assign(v.metadata().addressOf_gz(), v.metadata().addressOf_gz() + n);
      ph2_hits.assign(n, nullptr);
      if (compareOT_) {
        uint64_t rows = 0, mis = 0, misPos = 0;
        auto bits = [](float x) {
          uint32_t u;
          std::memcpy(&u, &x, 4);
          return u;
        };
        for (auto const& it : iEvent.get(phase2OTRecHitToken_)) {
          for (auto const& hit : it) {
            const uint32_t k = rows++;
            if (k >= n || ph2_detId[k] != it.detId() || ph2_clustSize[k] != hit.cluster()->size()) {
              ++mis;
              continue;
            }
            misPos += bits(ph2_x[k]) != bits(hit.globalPosition().x()) ||
                      bits(ph2_y[k]) != bits(hit.globalPosition().y()) ||
                      bits(ph2_z[k]) != bits(hit.globalPosition().z());
          }
        }
        edm::LogPrint("LSTInputProducer") << "LSTIN_OT event " << iEvent.id().event() << " rows " << rows << " soa " << n
                                          << " idMis " << mis << " posMis " << misPos;
      }
    }
    // Get the phase2OTRecHits
    static const Phase2TrackerRecHit1DCollectionNew kNoHits;
    auto const& phase2OTHits = (otSoA_ || otKeysOnly_) ? kNoHits : iEvent.get(phase2OTRecHitToken_);
    ph2_detId.reserve(phase2OTHits.dataSize());
    ph2_clustSize.reserve(phase2OTHits.dataSize());
    ph2_x.reserve(phase2OTHits.dataSize());
    ph2_y.reserve(phase2OTHits.dataSize());
    ph2_z.reserve(phase2OTHits.dataSize());
    ph2_hits.reserve(phase2OTHits.dataSize());

    for (auto const& it : phase2OTHits) {
      const DetId hitId = it.detId();
      for (auto const& hit : it) {
        ph2_detId.push_back(hitId.rawId());
        ph2_clustSize.push_back(hit.cluster()->size());
        ph2_x.push_back(hit.globalPosition().x());
        ph2_y.push_back(hit.globalPosition().y());
        ph2_z.push_back(hit.globalPosition().z());
        ph2_hits.push_back(&hit);
      }
    }

    // Get the pixel seeds
    auto const& mf = iSetup.getData(mfToken_);
    auto const& bs = iEvent.get(beamSpotToken_);

    TSCBLBuilderNoMaterial tscblBuilder;

    // Vector definitions
    std::vector<float> see_px;
    std::vector<float> see_py;
    std::vector<float> see_pz;
    std::vector<float> see_dxy;
    std::vector<float> see_dz;
    std::vector<float> see_ptErr;
    std::vector<float> see_etaErr;
    std::vector<float> see_stateTrajGlbX;
    std::vector<float> see_stateTrajGlbY;
    std::vector<float> see_stateTrajGlbZ;
    std::vector<float> see_stateTrajGlbPx;
    std::vector<float> see_stateTrajGlbPy;
    std::vector<float> see_stateTrajGlbPz;
    std::vector<int> see_q;
    std::vector<std::vector<int>> see_hitIdx;
    std::vector<std::vector<int>> see_hitType;
    TrajectorySeedCollection see_seeds;

    for (auto const& seedToken : seedTokens_) {
      auto const& seeds = iEvent.get(seedToken);

      if (seeds.empty())
        continue;

      for (auto const& seed : seeds) {
        const TrackingRecHit* lastRecHit = &*(seed.recHits().end() - 1);
        TrajectoryStateOnSurface tsos =
            trajectoryStateTransform::transientState(seed.startingState(), lastRecHit->surface(), &mf);
        auto const& stateGlobal = tsos.globalParameters();

        // Propagate to beam line to get perigee parameters (replicates TrackFromSeedProducer)
        TrajectoryStateClosestToBeamLine tscbl = tscblBuilder(*(tsos.freeState()), bs);
        const bool tscblValid = tscbl.isValid();

        float px = 0, py = 0, pz = 0, dxy = 0, dz = 0, ptErr = 0, etaErr = 0;
        int charge = tscblValid ? tsos.charge() : 0;

        if (tscblValid) {
          auto const& fts = tscbl.trackStateAtPCA();
          auto const& mom = fts.momentum();
          auto const& pos = fts.position();
          auto const& bsPos = bs.position();
          px = mom.x();
          py = mom.y();
          pz = mom.z();
          dxy = (-(pos.x() - bsPos.x()) * mom.y() + (pos.y() - bsPos.y()) * mom.x()) / mom.perp();
          dz = (pos.z() - bsPos.z()) - ((pos.x() - bsPos.x()) * mom.x() + (pos.y() - bsPos.y()) * mom.y()) /
                                           mom.perp() * (mom.z() / mom.perp());

          // Compute ptErr and etaErr replicating reco::TrackBase::ptError() and etaError()
          PerigeeTrajectoryError periErr = PerigeeConversions::ftsToPerigeeError(fts);
          auto const& errMat = periErr.covarianceMatrix();
          double pt = mom.perp();
          double p = mom.mag();
          double pz = mom.z();
          double q = static_cast<double>(charge);
          // Full error propagation matching reco::TrackBase::ptError2():
          //   pt2*p2/q2 * cov(qoverp,qoverp) + 2*sqrt(p2*pt2)/q * pz * cov(qoverp,lambda) + pz2 * cov(lambda,lambda)
          double pt2 = pt * pt;
          double p2 = p * p;
          ptErr = std::sqrt(pt2 * p2 / (q * q) * errMat(0, 0) + 2.0 * std::sqrt(p2 * pt2) / q * pz * errMat(0, 1) +
                            pz * pz * errMat(1, 1));
          // etaError() = sqrt(cov(lambda,lambda)) * p/pt
          etaErr = std::sqrt(errMat(1, 1)) * p / pt;
        }

        std::vector<int> hitIdx;
        std::vector<int> hitType;
        for (auto const& hit : seed.recHits()) {
          auto det = hit.geographicalId().det();
          if (det == DetId::Tracker) {
            const BaseTrackerRecHit* bhit = dynamic_cast<const BaseTrackerRecHit*>(&hit);
            const auto& clusterRef = bhit->firstClusterRef();
            hitIdx.push_back(clusterRef.index());
            if (clusterRef.isPixel()) {
              hitType.push_back(static_cast<int>(lst::HitType::Pixel));
            } else if (clusterRef.isPhase2()) {
              hitType.push_back(static_cast<int>(lst::HitType::Phase2OT));
            } else {
              throw cms::Exception("LSTInputProducer") << "Unknown tracker hit type found!";
            }
          } else {
            throw cms::Exception("LSTInputProducer") << "Not tracker hit found!";
          }
        }

        // Fill output
        see_px.push_back(px);
        see_py.push_back(py);
        see_pz.push_back(pz);
        see_dxy.push_back(dxy);
        see_dz.push_back(dz);
        see_ptErr.push_back(ptErr);
        see_etaErr.push_back(etaErr);
        see_stateTrajGlbX.push_back(stateGlobal.position().x());
        see_stateTrajGlbY.push_back(stateGlobal.position().y());
        see_stateTrajGlbZ.push_back(stateGlobal.position().z());
        see_stateTrajGlbPx.push_back(stateGlobal.momentum().x());
        see_stateTrajGlbPy.push_back(stateGlobal.momentum().y());
        see_stateTrajGlbPz.push_back(stateGlobal.momentum().z());
        see_q.push_back(charge);
        see_hitIdx.emplace_back(std::move(hitIdx));
        see_hitType.emplace_back(std::move(hitType));
        if (producePixelSeeds_)
          see_seeds.push_back(seed);
      }
    }

    auto lstInputHC = lst::prepareInput(see_px,
                                        see_py,
                                        see_pz,
                                        see_dxy,
                                        see_dz,
                                        see_ptErr,
                                        see_etaErr,
                                        see_stateTrajGlbX,
                                        see_stateTrajGlbY,
                                        see_stateTrajGlbZ,
                                        see_stateTrajGlbPx,
                                        see_stateTrajGlbPy,
                                        see_stateTrajGlbPz,
                                        see_q,
                                        see_hitIdx,
                                        see_hitType,
                                        {},
                                        ph2_detId,
                                        ph2_clustSize,
                                        ph2_x,
                                        ph2_y,
                                        ph2_z,
                                        ptCut_,
                                        iEvent.queue());

    iEvent.emplace(lstInputPutToken_, std::move(lstInputHC));
    if (producePixelSeeds_)
      iEvent.emplace(lstPixelSeedsPutToken_, std::move(see_seeds));
    iEvent.emplace(lstOTHitsPutToken_, lst::LSTOTHits{std::move(ph2_hits)});
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE

#include "HeterogeneousCore/AlpakaCore/interface/alpaka/MakerMacros.h"
DEFINE_FWK_ALPAKA_MODULE(LSTInputProducer);
