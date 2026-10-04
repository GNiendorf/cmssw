// MkFitAlpakaLstInputTruthDump (lane lstin, round 7): per-TrackingParticle dump for CROSS-JOB paired truth (switch ON vs
// OFF are separate jobs on the same events). One line per TP that passes the loose selection:
//   T <run> <event> <tpKey> <mtv> <pt> <eta> <phi> <vxy> <vz> <found_0> ... <found_{n-1}>
// mtv = 1 when the TP also passes the MTV efficiency selection (tpSelection = the hltTrackValidator TP parameters);
// found_i = 1 when the i-th track collection has a track associated to the TP (MTV associator, sim -> reco).
// A python join on (run, event, tpKey) gives the McNemar paired efficiency difference per region.
#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "DataFormats/Common/interface/View.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "SimDataFormats/Associations/interface/TrackToTrackingParticleAssociator.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "SimTracker/Common/interface/TrackingParticleSelector.h"

class MkFitAlpakaLstInputTruthDump : public edm::one::EDAnalyzer<> {
public:
  explicit MkFitAlpakaLstInputTruthDump(edm::ParameterSet const& cfg)
      : tpToken_(consumes(cfg.getParameter<edm::InputTag>("trackingParticles"))),
        assocToken_(consumes(cfg.getParameter<edm::InputTag>("associator"))),
        out_(cfg.getParameter<std::string>("fileName")) {
    for (auto const& t : cfg.getParameter<std::vector<edm::InputTag>>("tracks"))
      trackTokens_.push_back(consumes<edm::View<reco::Track>>(t));
    tpSel_ = makeSel(cfg.getParameter<edm::ParameterSet>("tpSelection"));
    looseSel_ = makeSel(cfg.getParameter<edm::ParameterSet>("looseSelection"));
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<std::vector<edm::InputTag>>("tracks", {edm::InputTag("hltGeneralTracks")});
    desc.add<edm::InputTag>("trackingParticles", edm::InputTag("mix", "MergedTrackTruth"));
    desc.add<edm::InputTag>("associator", edm::InputTag("hltTrackAssociatorByHits"));
    desc.add<std::string>("fileName", "truth_dump.txt");
    desc.add<edm::ParameterSetDescription>("tpSelection", selDesc(0.9, 2.5, 3.5, 30.));
    desc.add<edm::ParameterSetDescription>("looseSelection", selDesc(0.5, 4.5, 60., 30.));
    descriptions.addWithDefaultLabel(desc);
  }

  void analyze(edm::Event const& ev, edm::EventSetup const&) override {
    std::vector<edm::Handle<edm::View<reco::Track>>> h;
    for (auto const& t : trackTokens_) {
      h.push_back(ev.getHandle(t));
      if (!h.back().isValid())
        return;  // events without tracking
    }
    auto hTP = ev.getHandle(tpToken_);
    auto const& assoc = ev.get(assocToken_);
    std::vector<reco::SimToRecoCollection> s2r;
    for (auto const& hh : h)
      s2r.push_back(assoc.associateSimToReco(hh, hTP));
    for (size_t i = 0; i < hTP->size(); ++i) {
      TrackingParticleRef tp(hTP, i);
      if (!looseSel_(*tp))
        continue;
      out_ << "T " << ev.id().run() << ' ' << ev.id().event() << ' ' << i << ' ' << int(tpSel_(*tp)) << ' '
           << tp->pt() << ' ' << tp->eta() << ' ' << tp->phi() << ' ' << std::sqrt(tp->vertex().perp2()) << ' '
           << tp->vz();
      for (auto const& m : s2r) {
        auto it = m.find(tp);
        out_ << ' ' << int(it != m.end() && !it->val.empty());
      }
      out_ << '\n';
    }
  }

private:
  static edm::ParameterSetDescription selDesc(double ptMin, double eta, double tip, double lip) {
    edm::ParameterSetDescription sel;  // = MultiTrackValidator TP selection parameters
    sel.add<double>("ptMinTP", ptMin);
    sel.add<double>("ptMaxTP", 1e100);
    sel.add<double>("minRapidityTP", -eta);
    sel.add<double>("maxRapidityTP", eta);
    sel.add<double>("tipTP", tip);
    sel.add<double>("lipTP", lip);
    sel.add<int>("minHitTP", 0);
    sel.add<bool>("signalOnlyTP", true);
    sel.add<bool>("intimeOnlyTP", false);
    sel.add<bool>("chargedOnlyTP", true);
    sel.add<bool>("stableOnlyTP", false);
    sel.add<std::vector<int>>("pdgIdTP", {});
    sel.add<bool>("invertRapidityCutTP", false);
    sel.add<double>("minPhi", -3.2);
    sel.add<double>("maxPhi", 3.2);
    return sel;
  }
  static TrackingParticleSelector makeSel(edm::ParameterSet const& p) {
    return TrackingParticleSelector(p.getParameter<double>("ptMinTP"),
                                    p.getParameter<double>("ptMaxTP"),
                                    p.getParameter<double>("minRapidityTP"),
                                    p.getParameter<double>("maxRapidityTP"),
                                    p.getParameter<double>("tipTP"),
                                    p.getParameter<double>("lipTP"),
                                    p.getParameter<int>("minHitTP"),
                                    p.getParameter<bool>("signalOnlyTP"),
                                    p.getParameter<bool>("intimeOnlyTP"),
                                    p.getParameter<bool>("chargedOnlyTP"),
                                    p.getParameter<bool>("stableOnlyTP"),
                                    p.getParameter<std::vector<int>>("pdgIdTP"),
                                    p.getParameter<bool>("invertRapidityCutTP"),
                                    p.getParameter<double>("minPhi"),
                                    p.getParameter<double>("maxPhi"));
  }

  std::vector<edm::EDGetTokenT<edm::View<reco::Track>>> trackTokens_;
  const edm::EDGetTokenT<TrackingParticleCollection> tpToken_;
  const edm::EDGetTokenT<reco::TrackToTrackingParticleAssociator> assocToken_;
  std::ofstream out_;
  TrackingParticleSelector tpSel_, looseSel_;
};

DEFINE_FWK_MODULE(MkFitAlpakaLstInputTruthDump);
