#ifndef RecoTracker_LST_interface_LSTProtoTrackSeed_h
#define RecoTracker_LST_interface_LSTProtoTrackSeed_h

// Local patch (MkFitAlpaka lane lstin, rounds 7 and 9): SeedGeneratorFromProtoTracksEDProducer's recipe for ONE proto
// track in the menu configuration of hltInitialStepSeeds (valid hits ordered by radius; GlobalTrackingRegion(pT of the
// proto track, its vertex, 0.2, 0.2); the SeedCreatorPSet's creator; includeFourthHit). Shared by LSTOutputConverter
// (lazyPixelSeeds) and LSTPixelSeedFailMask (the pixel tracks whose seed the host creator rejects, known before LST).
#include <cmath>
#include <vector>

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrajectorySeed/interface/TrajectorySeedCollection.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "RecoTracker/TkSeedGenerator/interface/SeedCreator.h"
#include "RecoTracker/TkSeedingLayers/interface/SeedingHitSet.h"
#include "RecoTracker/TkTrackingRegions/interface/GlobalTrackingRegion.h"

namespace lst {

  using ProtoTrackHit = SeedingHitSet::ConstRecHitPointer;

  // the proto track's valid hits ordered by radius (HitLessByRadius; a stable insertion of the few hits = libstdc++'s
  // std::sort for <= 16 elements, distinct hits never tie)
  inline std::vector<ProtoTrackHit> protoTrackHitsByRadius(reco::Track const& proto) {
    std::vector<ProtoTrackHit> hits;
    for (unsigned int iHit = 0, nHits = proto.recHitsSize(); iHit < nHits; ++iHit) {
      TrackingRecHitRef refHit = proto.recHit(iHit);
      if (refHit->isValid()) {
        ProtoTrackHit h = (ProtoTrackHit) & (*refHit);
        auto it = hits.end();
        while (it != hits.begin() && h->globalPosition().perp2() < (*(it - 1))->globalPosition().perp2())
          --it;
        hits.insert(it, h);
      }
    }
    return hits;
  }

  // the seed of the proto track into out (cleared first); false when none is made (< 2 hits, or the creator rejects)
  inline bool makeProtoTrackSeed(SeedCreator& creator,
                                 reco::Track const& proto,
                                 std::vector<ProtoTrackHit> const& hits,
                                 bool includeFourthHit,
                                 edm::EventSetup const& iSetup,
                                 TrajectorySeedCollection& out) {
    out.clear();
    if (hits.size() <= 1)
      return false;
    GlobalPoint vtx(proto.vertex().x(), proto.vertex().y(), proto.vertex().z());
    double mom_perp = sqrt(proto.momentum().x() * proto.momentum().x() + proto.momentum().y() * proto.momentum().y());
    GlobalTrackingRegion region(mom_perp, vtx, 0.2, 0.2);
    creator.init(region, iSetup, nullptr);
    if (hits.size() > 3 and not includeFourthHit)
      creator.makeSeed(out, {hits[0], hits[1], hits[2]});
    else
      creator.makeSeed(out, hits);
    return !out.empty();
  }

}  // namespace lst

#endif
