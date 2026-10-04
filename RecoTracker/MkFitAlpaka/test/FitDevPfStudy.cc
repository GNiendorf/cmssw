// FitDevPfStudy (round 9, lane fitdev; validation only, test library): one line per event with the PF / MET / TICL
// quantities that read the TrackExtra states of the tracks (DEVIATION D6 check, D9-d). Printed with LogPrint as
//   [pfstudy] run lumi event | genMET x y | <met label> x y ... | tracks n outerOk n | pf nCh nChFwd sumPtCh sumPtChFwd
//   | ticl n nTrk nTrkFwd sumE sumETrk | jets HT30 chHad30 n30
// Missing products print -999 (so the line is always complete). No output file.

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include "DataFormats/HGCalReco/interface/TICLCandidate.h"
#include "DataFormats/HepMCCandidate/interface/GenParticle.h"
#include "DataFormats/JetReco/interface/PFJet.h"
#include "DataFormats/METReco/interface/MET.h"
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidate.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

class FitDevPfStudy : public edm::global::EDAnalyzer<> {
public:
  explicit FitDevPfStudy(edm::ParameterSet const& ps)
      : genToken_(consumes(ps.getParameter<edm::InputTag>("genParticles"))),
        tracksToken_(consumes(ps.getParameter<edm::InputTag>("tracks"))),
        pfToken_(consumes(ps.getParameter<edm::InputTag>("pfCandidates"))),
        ticlToken_(consumes(ps.getParameter<edm::InputTag>("ticlCandidates"))),
        jetsToken_(consumes(ps.getParameter<edm::InputTag>("jets"))),
        fwdEta_(ps.getParameter<double>("fwdEta")) {
    for (auto const& t : ps.getParameter<std::vector<edm::InputTag>>("mets")) {
      metTokens_.push_back(consumes<edm::View<reco::MET>>(t));
      metLabels_.push_back(t.label());
    }
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& d) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("genParticles", edm::InputTag("genParticles"));
    desc.add<edm::InputTag>("tracks", edm::InputTag("hltGeneralTracks"));
    desc.add<edm::InputTag>("pfCandidates", edm::InputTag("hltParticleFlowTmp"));
    desc.add<edm::InputTag>("ticlCandidates", edm::InputTag("hltTiclCandidate"));
    desc.add<edm::InputTag>("jets", edm::InputTag("hltAK4PFPuppiJets"));
    desc.add<std::vector<edm::InputTag>>(
        "mets", {edm::InputTag("hltPFPuppiMETTypeOne"), edm::InputTag("hltPFPuppiMET"), edm::InputTag("hltPFMET")});
    desc.add<double>("fwdEta", 1.5);
    d.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const&) const override {
    std::ostringstream o;
    o.precision(6);
    o << "[pfstudy] " << ev.id().run() << " " << ev.id().luminosityBlock() << " " << ev.id().event();
    // gen MET = vector sum of the stable invisible particles (neutrinos)
    double gx = 0, gy = 0;
    if (auto h = ev.getHandle(genToken_); h.isValid()) {
      for (auto const& g : *h) {
        const int a = std::abs(g.pdgId());
        if (g.status() == 1 && (a == 12 || a == 14 || a == 16)) {
          gx += g.px();
          gy += g.py();
        }
      }
      o << " | gen " << gx << " " << gy;
    } else
      o << " | gen -999 -999";
    for (size_t i = 0; i < metTokens_.size(); ++i) {
      auto h = ev.getHandle(metTokens_[i]);
      if (h.isValid() && !h->empty())
        o << " | " << metLabels_[i] << " " << h->front().px() << " " << h->front().py();
      else
        o << " | " << metLabels_[i] << " -999 -999";
    }
    auto hTrk = ev.getHandle(tracksToken_);
    auto hPf = ev.getHandle(pfToken_);
    if (hTrk.isValid()) {
      int nOut = 0;
      for (auto const& t : *hTrk)
        if (t.extra().isAvailable() && t.outerOk() && t.innerOk())
          ++nOut;
      o << " | tracks " << hTrk->size() << " " << nOut;
    } else
      o << " | tracks -999 -999";
    // tracks with pT > 1 GeV: count, sum pT, highPurity, mean ptError/pT (|eta| < 1.5), and those NOT used by any PF
    // candidate (count, sum pT; all and pT > 10 GeV)
    if (hTrk.isValid() && hPf.isValid()) {
      std::vector<char> used(hTrk->size(), 0);
      for (auto const& c : *hPf)
        if (c.trackRef().isNonnull() && c.trackRef().id() == hTrk.id() && c.trackRef().key() < used.size())
          used[c.trackRef().key()] = 1;
      int n1 = 0, nHP = 0, nC = 0, nNot = 0, nNot10 = 0;
      double s1 = 0, rel = 0, sNot = 0, sNot10 = 0;
      for (size_t i = 0; i < hTrk->size(); ++i) {
        auto const& t = (*hTrk)[i];
        if (t.pt() < 1)
          continue;
        ++n1;
        s1 += t.pt();
        if (t.quality(reco::TrackBase::highPurity))
          ++nHP;
        if (std::abs(t.eta()) < 1.5) {
          ++nC;
          rel += t.ptError() / t.pt();
        }
        if (!used[i]) {
          ++nNot;
          sNot += t.pt();
          if (t.pt() > 10) {
            ++nNot10;
            sNot10 += t.pt();
          }
        }
      }
      o << " | trk1 " << n1 << " " << s1 << " " << nHP << " " << (nC ? rel / nC : 0.) << " " << nNot << " " << sNot
        << " " << nNot10 << " " << sNot10;
    } else
      o << " | trk1 -999 -999 -999 -999 -999 -999 -999 -999";
    if (auto const& h = hPf; h.isValid()) {
      int nCh = 0, nChFwd = 0;
      double sCh = 0, sChFwd = 0;
      for (auto const& c : *h) {
        if (c.trackRef().isNull())
          continue;
        ++nCh;
        sCh += c.pt();
        if (std::abs(c.eta()) > fwdEta_) {
          ++nChFwd;
          sChFwd += c.pt();
        }
      }
      o << " | pf " << nCh << " " << nChFwd << " " << sCh << " " << sChFwd;
    } else
      o << " | pf -999 -999 -999 -999";
    if (auto h = ev.getHandle(ticlToken_); h.isValid()) {
      int nTrk = 0, nTrkFwd = 0;
      double sE = 0, sETrk = 0;
      for (auto const& c : *h) {
        sE += c.rawEnergy();
        if (c.trackPtr().isNonnull()) {
          ++nTrk;
          sETrk += c.rawEnergy();
          if (std::abs(c.trackPtr()->eta()) > fwdEta_)
            ++nTrkFwd;
        }
      }
      o << " | ticl " << h->size() << " " << nTrk << " " << nTrkFwd << " " << sE << " " << sETrk;
    } else
      o << " | ticl -999 -999 -999 -999 -999";
    if (auto h = ev.getHandle(jetsToken_); h.isValid()) {
      double ht = 0, ch = 0;
      int n = 0;
      for (auto const& j : *h)
        if (j.pt() > 30 && std::abs(j.eta()) < 2.4) {
          ht += j.pt();
          ch += j.chargedHadronEnergy();
          ++n;
        }
      o << " | jets " << ht << " " << ch << " " << n;
    } else
      o << " | jets -999 -999 -999";
    edm::LogPrint("FitDevPfStudy") << o.str();
  }

private:
  const edm::EDGetTokenT<reco::GenParticleCollection> genToken_;
  const edm::EDGetTokenT<reco::TrackCollection> tracksToken_;
  const edm::EDGetTokenT<reco::PFCandidateCollection> pfToken_;
  const edm::EDGetTokenT<std::vector<TICLCandidate>> ticlToken_;
  const edm::EDGetTokenT<reco::PFJetCollection> jetsToken_;
  const double fwdEta_;
  std::vector<edm::EDGetTokenT<edm::View<reco::MET>>> metTokens_;
  std::vector<std::string> metLabels_;
};

DEFINE_FWK_MODULE(FitDevPfStudy);
