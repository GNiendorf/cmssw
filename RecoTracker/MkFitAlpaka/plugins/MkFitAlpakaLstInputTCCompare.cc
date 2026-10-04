// Lane lstin (round 6): LST track-candidate counts with the host-made input (hltLST) vs the device-made input
// (MkFitAlpakaLstInputProducer). Per type counts and, per type, how many candidates have an identical OT hit list
// (multiset match; the pixel part is not compared: the device pLS index is the Patatrack SoA row).
#include <algorithm>
#include <map>
#include <vector>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "RecoTracker/LSTCore/interface/Common.h"
#include "RecoTracker/LSTCore/interface/TrackCandidatesHostCollection.h"

class MkFitAlpakaLstInputTCCompare : public edm::global::EDAnalyzer<> {
public:
  explicit MkFitAlpakaLstInputTCCompare(edm::ParameterSet const& cfg)
      : refToken_(consumes(cfg.getParameter<edm::InputTag>("reference"))),
        tstToken_(consumes(cfg.getParameter<edm::InputTag>("test"))) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("reference", edm::InputTag("hltLST"));
    desc.add<edm::InputTag>("test", edm::InputTag("hltLSTDevIn"));
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& iEvent, edm::EventSetup const&) const override {
    auto const& r = iEvent.get(refToken_).const_view();
    auto const& t = iEvent.get(tstToken_).const_view();
    using Key = std::pair<int, std::vector<unsigned int>>;
    auto collect = [](auto const& v, std::map<int, int>& counts, std::map<Key, int>& keys) {
      for (unsigned int i = 0; i < v.nTrackCandidates(); ++i) {
        const int type = static_cast<int>(v.trackCandidateType()[i]);
        counts[type]++;
        std::vector<unsigned int> hits;
        for (unsigned int l = lst::Params_TC::kPixelLayerSlots; l < lst::Params_TC::kLayers; ++l)
          for (unsigned int h = 0; h < lst::Params_TC::kHitsPerLayer; ++h) {
            const unsigned int x = v.hitIndices()[i][l][h];
            if (x != lst::kTCEmptyHitIdx)
              hits.push_back(x);
          }
        keys[Key(type, hits)]++;
      }
    };
    std::map<int, int> cr, ct;
    std::map<Key, int> kr, kt;
    collect(r, cr, kr);
    collect(t, ct, kt);
    std::map<int, int> same;
    for (auto const& [k, n] : kr) {
      auto it = kt.find(k);
      if (it != kt.end())
        same[k.first] += std::min(n, it->second);
    }
    auto line = edm::LogPrint("MkFitAlpakaLstInputTCCompare");
    line << "LSTIN_TC event " << iEvent.id().event() << " ref " << r.nTrackCandidates() << " test "
         << t.nTrackCandidates();
    for (int type : {4, 5, 7, 8, 9})
      line << " | t" << type << " " << cr[type] << " " << ct[type] << " same " << same[type];
  }

private:
  const edm::EDGetTokenT<lst::TrackCandidatesBaseHostCollection> refToken_;
  const edm::EDGetTokenT<lst::TrackCandidatesBaseHostCollection> tstToken_;
};

DEFINE_FWK_MODULE(MkFitAlpakaLstInputTCCompare);
