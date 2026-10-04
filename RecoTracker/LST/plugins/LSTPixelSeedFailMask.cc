// Local patch (MkFitAlpaka round 9, lane lstin; RecoTracker/MkFitAlpaka/doc/lstin.txt): the pixel tracks whose
// hltInitialStepSeeds seed the host creator would REJECT, known before LST. With the device LST input in lazy mode
// (hltInitialStepSeeds out of the menu) such a pixel track would still give a pLS, which stock never has (R7-H2:
// "change (ii)", the endcap loss of 'lazy'); hltInputLSTDevice drops the pLS of every listed track.
// Output: the indices (ascending) in pixelTracks of the rejected tracks.
// Exactness: the creator (SeedGeneratorFromProtoTracksEDProducer's recipe, interface/LSTProtoTrackSeed.h) runs only on
// the tracks whose radius-ordered seed hits have two consecutive hits closer than maxDr in radius (every rejection seen
// in round 8 has its first two hits within 0.05 cm: two faces of one disk or a barrel overlap; FastHelix and the
// first KF steps are then near-degenerate); maxDr < 0 runs it on every track (exact by construction). validate = True
// also runs it on the tracks outside the pre-filter and counts the rejections the pre-filter misses (LSTMASK lines).
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeedCollection.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "RecoTracker/LST/interface/LSTProtoTrackSeed.h"
#include "RecoTracker/TkSeedGenerator/interface/SeedCreator.h"
#include "RecoTracker/TkSeedGenerator/interface/SeedCreatorFactory.h"

class LSTPixelSeedFailMask : public edm::stream::EDProducer<> {
public:
  explicit LSTPixelSeedFailMask(edm::ParameterSet const& cfg)
      : tracksToken_(consumes(cfg.getParameter<edm::InputTag>("pixelTracks"))),
        includeFourthHit_(cfg.getParameter<bool>("includeFourthHit")),
        maxDr_(cfg.getParameter<double>("maxDr")),
        validate_(cfg.getParameter<bool>("validate")),
        creator_(SeedCreatorFactory::get()->create(
            "SeedFromConsecutiveHitsCreator", cfg.getParameter<edm::ParameterSet>("SeedCreatorPSet"), consumesCollector())),
        putToken_(produces()) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("pixelTracks", edm::InputTag("hltPhase2PixelTracks"));
    desc.add<bool>("includeFourthHit", true);
    desc.add<double>("maxDr", 0.05)
        ->setComment("run the creator only when two consecutive radius-ordered seed hits are closer than this in radius "
                     "[cm]; < 0: on every track (exact by construction)");
    desc.add<bool>("validate", false)->setComment("also run the creator outside the pre-filter, count misses (LSTMASK)");
    edm::ParameterSetDescription creator;
    creator.setAllowAnything();
    desc.add<edm::ParameterSetDescription>("SeedCreatorPSet", creator)
        ->setComment("the SeedCreatorPSet of hltInitialStepSeeds (SeedFromConsecutiveHitsCreator)");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::Event& iEvent, edm::EventSetup const& iSetup) override {
    auto const& tracks = iEvent.get(tracksToken_);
    std::vector<uint32_t> failed;
    TrajectorySeedCollection tmp;
    uint32_t nChecked = 0, nMissed = 0;
    for (uint32_t e = 0; e < tracks.size(); ++e) {
      auto const& proto = tracks[e];
      auto const hits = lst::protoTrackHitsByRadius(proto);
      const size_t nUsed = (hits.size() > 3 && !includeFourthHit_) ? 3 : hits.size();
      float minDr = 1e9f;  // smallest radius step between consecutive radius-ordered seed hits
      for (size_t i = 1; i < nUsed; ++i)
        minDr = std::min(minDr, std::abs(hits[i]->globalPosition().perp() - hits[i - 1]->globalPosition().perp()));
      const bool check = hits.size() <= 1 || maxDr_ < 0 || minDr < maxDr_;
      nChecked += check;
      if (!check && !validate_)
        continue;
      const bool ok = lst::makeProtoTrackSeed(*creator_, proto, hits, includeFourthHit_, iSetup, tmp);
      if (!ok && check)
        failed.push_back(e);
      if (!ok && validate_) {
        nMissed += !check;
        edm::LogPrint("LSTPixelSeedFailMask")
            << (check ? "LSTMASK_FAIL" : "LSTMASK_MISS") << " event " << iEvent.id().event() << " minDr " << minDr
            << " pt " << proto.pt() << " eta " << proto.eta() << " nh " << hits.size();
      }
    }
    if (validate_)
      edm::LogPrint("LSTPixelSeedFailMask")
          << "LSTMASK event " << iEvent.id().event() << " tracks " << tracks.size() << " checked " << nChecked
          << " failed " << failed.size() << " missed " << nMissed;
    nTracks_ += tracks.size();
    nChecked_ += nChecked;
    nFailed_ += failed.size();
    nMissed_ += nMissed;
    iEvent.emplace(putToken_, std::move(failed));
  }

  void endStream() override {
    edm::LogPrint("LSTPixelSeedFailMask") << "LSTMASK totals (stream): tracks " << nTracks_ << " checked " << nChecked_
                                          << " failed " << nFailed_ << " missed " << nMissed_
                                          << (validate_ ? "" : " (misses not counted: validate = False)");
  }

private:
  const edm::EDGetTokenT<reco::TrackCollection> tracksToken_;
  const bool includeFourthHit_;
  const double maxDr_;
  const bool validate_;
  std::unique_ptr<SeedCreator> creator_;
  const edm::EDPutTokenT<std::vector<uint32_t>> putToken_;
  unsigned long nTracks_ = 0, nChecked_ = 0, nFailed_ = 0, nMissed_ = 0;
};

DEFINE_FWK_MODULE(LSTPixelSeedFailMask);
