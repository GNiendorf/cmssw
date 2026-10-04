// MkFitAlpakaLstInputPLSTruth (lane lstin, round 7): the pLS source of the device LST input vs TRUTH.
// For every device pLS whose pixel track (seedIdx = hltPhase2PixelTracks index: seedIdxFromTracks / 'lazy' modes) is
// associated to a TrackingParticle (tpToHLTpixelTrackAssociation), one line
//   P <event> <e> <nHits> | TP: pt eta phi dxy dz | device (option i): ptIn eta phi dxy dz | host seed: ptIn eta phi dxy dz
// host = what hltInputLST computes from hltInitialStepSeeds[e] (LSTInputProducer formulas: pT of the state on the last
// hit, eta/phi/dxy/dz at the beam-line PCA by TSCBLBuilderNoMaterial); dxy/dz of the device pLS only for quads (pseudo
// hit 3), else nan. TP dxy/dz: straight line from the production vertex, wrt the beam spot.
#include <cmath>
#include <fstream>
#include <limits>
#include <string>

#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeedCollection.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "RecoTracker/LSTCore/interface/LSTInputHostCollection.h"
#include "SimDataFormats/Associations/interface/TrackToTrackingParticleAssociator.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "TrackingTools/PatternTools/interface/TSCBLBuilderNoMaterial.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateOnSurface.h"
#include "TrackingTools/TrajectoryState/interface/TrajectoryStateTransform.h"
#include "TrackingTools/TrajectoryState/interface/FreeTrajectoryState.h"

class MkFitAlpakaLstInputPLSTruth : public edm::one::EDAnalyzer<> {
public:
  explicit MkFitAlpakaLstInputPLSTruth(edm::ParameterSet const& cfg)
      : inputToken_(consumes(cfg.getParameter<edm::InputTag>("lstInput"))),
        tracksToken_(consumes(cfg.getParameter<edm::InputTag>("pixelTracks"))),
        seedsToken_(consumes(cfg.getParameter<edm::InputTag>("seeds"))),
        assocToken_(consumes(cfg.getParameter<edm::InputTag>("association"))),
        bsToken_(consumes(cfg.getParameter<edm::InputTag>("beamSpot"))),
        mfToken_(esConsumes()),
        out_(cfg.getParameter<std::string>("fileName")) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("lstInput", edm::InputTag("hltInputLSTDevice"));
    desc.add<edm::InputTag>("pixelTracks", edm::InputTag("hltPhase2PixelTracks"));
    desc.add<edm::InputTag>("seeds", edm::InputTag("hltInitialStepSeeds"));
    desc.add<edm::InputTag>("association", edm::InputTag("tpToHLTpixelTrackAssociation"));
    desc.add<edm::InputTag>("beamSpot", edm::InputTag("hltOnlineBeamSpot"));
    desc.add<std::string>("fileName", "pls_truth.txt");
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::Event const& ev, edm::EventSetup const& es) override {
    auto const& in = ev.get(inputToken_);
    auto hTracks = ev.getHandle(tracksToken_);
    auto const& seeds = ev.get(seedsToken_);
    auto const& r2s = ev.get(assocToken_);
    auto const& bs = ev.get(bsToken_);
    auto const& mf = es.getData(mfToken_);
    if (seeds.size() != hTracks->size()) {
      ++skipped_;
      return;
    }
    const float nan = std::numeric_limits<float>::quiet_NaN();
    auto hv = in.const_view().hits();
    auto pv = in.const_view().pixelSeeds();
    const double bx = bs.x0(), by = bs.y0(), bz = bs.z0();
    auto d0dz = [&](double x, double y, double z, double px, double py, double pz, double& dxy, double& dz) {
      const double pt = std::hypot(px, py);
      dxy = (-(x - bx) * py + (y - by) * px) / pt;
      dz = (z - bz) - ((x - bx) * px + (y - by) * py) / pt * (pz / pt);
    };
    TSCBLBuilderNoMaterial tscblBuilder;
    for (int r = 0; r < pv.metadata().size(); ++r) {
      const unsigned int e = pv.seedIdx()[r];
      if (e >= hTracks->size())
        continue;
      auto it = r2s.find(hTracks->refAt(e));
      if (it == r2s.end() || it->val.empty())
        continue;
      auto const& tp = *it->val.front().first;
      double tdxy, tdz;
      d0dz(tp.vx(), tp.vy(), tp.vz(), tp.px(), tp.py(), tp.pz(), tdxy, tdz);
      // device pLS (option i)
      float ddxy = nan, ddz = nan;
      if (pv.nHits()[r] > 3) {
        ddxy = hv.ys()[pv.firstHit()[r] + 3];
        ddz = hv.zs()[pv.firstHit()[r] + 3];
      }
      // host pLS from the host seed (LSTInputProducer)
      auto const& seed = seeds[e];
      const TrackingRecHit* last = &*(seed.recHits().end() - 1);
      TrajectoryStateOnSurface tsos = trajectoryStateTransform::transientState(seed.startingState(), last->surface(), &mf);
      float hpt = tsos.globalMomentum().perp(), heta = nan, hphi = nan, hdxy = nan, hdz = nan;
      TrajectoryStateClosestToBeamLine tscbl = tscblBuilder(*(tsos.freeState()), bs);
      if (tscbl.isValid()) {
        auto const& fts = tscbl.trackStateAtPCA();
        heta = fts.momentum().eta();
        hphi = fts.momentum().phi();
        double a, b;
        d0dz(fts.position().x(), fts.position().y(), fts.position().z(), fts.momentum().x(), fts.momentum().y(),
             fts.momentum().z(), a, b);
        hdxy = a;
        hdz = b;
      }
      out_ << "P " << ev.id().event() << ' ' << e << ' ' << int(pv.nHits()[r]) << ' ' << tp.pt() << ' ' << tp.eta()
           << ' ' << tp.phi() << ' ' << tdxy << ' ' << tdz << ' ' << pv.ptIn()[r] << ' ' << pv.eta()[r] << ' '
           << pv.phi()[r] << ' ' << ddxy << ' ' << ddz << ' ' << hpt << ' ' << heta << ' ' << hphi << ' ' << hdxy
           << ' ' << hdz << '\n';
    }
  }

  void endJob() override { out_ << "# events skipped (seed count != pixel track count): " << skipped_ << '\n'; }

private:
  const edm::EDGetTokenT<lst::LSTInputHostCollection> inputToken_;
  const edm::EDGetTokenT<edm::View<reco::Track>> tracksToken_;
  const edm::EDGetTokenT<TrajectorySeedCollection> seedsToken_;
  const edm::EDGetTokenT<reco::RecoToSimCollection> assocToken_;
  const edm::EDGetTokenT<reco::BeamSpot> bsToken_;
  const edm::ESGetToken<MagneticField, IdealMagneticFieldRecord> mfToken_;
  std::ofstream out_;
  long skipped_ = 0;
};

DEFINE_FWK_MODULE(MkFitAlpakaLstInputPLSTruth);
