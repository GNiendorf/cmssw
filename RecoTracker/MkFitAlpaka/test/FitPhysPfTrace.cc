// FitPhysPfTrace (round 10, lane fitphys; validation only, test library): follows every highPurity pT > 1 GeV track of
// hltGeneralTracks through the PF chain and prints where it is lost:
//   stage 0 no PFRecTrack (hltPfTrack), 1 PFRecTrack but no PFBlock TRACK element (hltParticleFlowBlock, includes the
//   TICL veto), 2 in a block but not used by a barrel PF candidate (hltParticleFlowTmpBarrel), 3 used.
// Lines (LogPrint): "[pftrace] run lumi event | n1 noPfRec noBlock blockNoCand used ticlVeto inTiclPf"
//                   "[pftrk] run lumi event key used stage pt eta phi ptErr algo origAlgo nValid nLostTrk nLostIn nLostOut
//                    layers pixLayers chi2 ndof dxyPV dzPV q inTicl blockSize nEcal nHcal nTrkBlk minDistEcal minDistHcal"
//   for every lost track and every 10th used one (key % 10 == 0) in the same selection.
// No output file.

#include <cmath>
#include <map>
#include <sstream>
#include <vector>

#include "DataFormats/HGCalReco/interface/TICLCandidate.h"
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidate.h"
#include "DataFormats/ParticleFlowReco/interface/PFBlock.h"
#include "DataFormats/ParticleFlowReco/interface/PFRecTrack.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/VertexReco/interface/Vertex.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"

class FitPhysPfTrace : public edm::global::EDAnalyzer<> {
public:
  explicit FitPhysPfTrace(edm::ParameterSet const& ps)
      : tracksToken_(consumes(ps.getParameter<edm::InputTag>("tracks"))),
        pfRecToken_(consumes(ps.getParameter<edm::InputTag>("pfRecTracks"))),
        blocksToken_(consumes(ps.getParameter<edm::InputTag>("blocks"))),
        pfBarrelToken_(consumes(ps.getParameter<edm::InputTag>("pfBarrel"))),
        pfTiclToken_(consumes(ps.getParameter<edm::InputTag>("pfTicl"))),
        ticlToken_(consumes(ps.getParameter<edm::InputTag>("ticlCandidates"))),
        vtxToken_(consumes(ps.getParameter<edm::InputTag>("vertices"))),
        minPt_(ps.getParameter<double>("minPt")),
        sampleEvery_(ps.getParameter<int>("sampleEvery")) {}

  static void fillDescriptions(edm::ConfigurationDescriptions& d) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("tracks", edm::InputTag("hltGeneralTracks"));
    desc.add<edm::InputTag>("pfRecTracks", edm::InputTag("hltPfTrack"));
    desc.add<edm::InputTag>("blocks", edm::InputTag("hltParticleFlowBlock"));
    desc.add<edm::InputTag>("pfBarrel", edm::InputTag("hltParticleFlowTmpBarrel"));
    desc.add<edm::InputTag>("pfTicl", edm::InputTag("hltPfTICL"));
    desc.add<edm::InputTag>("ticlCandidates", edm::InputTag("hltTiclCandidate"));
    desc.add<edm::InputTag>("vertices", edm::InputTag("hltOfflinePrimaryVertices"));
    desc.add<double>("minPt", 1.0);
    desc.add<int>("sampleEvery", 10);
    d.addWithDefaultLabel(desc);
  }

  void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const&) const override {
    auto hTrk = ev.getHandle(tracksToken_);
    if (!hTrk.isValid())
      return;
    const size_t n = hTrk->size();
    std::vector<char> inPfRec(n, 0), inPfBarrel(n, 0), inPfTicl(n, 0), inTicl(n, 0);
    std::vector<int> blk(n, -1), elem(n, -1);
    auto keyOf = [&](reco::TrackRef const& r) -> long {
      return (r.isNonnull() && r.id() == hTrk.id() && r.key() < n) ? long(r.key()) : -1L;
    };
    if (auto h = ev.getHandle(pfRecToken_); h.isValid())
      for (auto const& t : *h)
        if (long k = keyOf(t.trackRef()); k >= 0)
          inPfRec[k] = 1;
    auto hBlk = ev.getHandle(blocksToken_);
    if (hBlk.isValid())
      for (size_t b = 0; b < hBlk->size(); ++b) {
        auto const& els = (*hBlk)[b].elements();
        for (size_t e = 0; e < els.size(); ++e)
          if (els[e].type() == reco::PFBlockElement::TRACK)
            if (long k = keyOf(els[e].trackRef()); k >= 0) {
              blk[k] = b;
              elem[k] = e;
            }
      }
    if (auto h = ev.getHandle(pfBarrelToken_); h.isValid())
      for (auto const& c : *h)
        if (long k = keyOf(c.trackRef()); k >= 0)
          inPfBarrel[k] = 1;
    if (auto h = ev.getHandle(pfTiclToken_); h.isValid())
      for (auto const& c : *h)
        if (long k = keyOf(c.trackRef()); k >= 0)
          inPfTicl[k] = 1;
    if (auto h = ev.getHandle(ticlToken_); h.isValid())
      for (auto const& c : *h)
        if (c.trackPtr().isNonnull() && c.trackPtr().id() == hTrk.id() && c.trackPtr().key() < n)
          inTicl[c.trackPtr().key()] = 1;
    reco::Vertex::Point pv(0, 0, 0);
    if (auto h = ev.getHandle(vtxToken_); h.isValid() && !h->empty())
      pv = h->front().position();

    int n1 = 0, s0 = 0, s1 = 0, s2 = 0, used = 0, ticlVeto = 0, ticlPf = 0;
    std::ostringstream lines;
    lines.precision(5);
    for (size_t i = 0; i < n; ++i) {
      auto const& t = (*hTrk)[i];
      if (t.pt() < minPt_ || !t.quality(reco::TrackBase::highPurity))
        continue;
      ++n1;
      const bool isUsed = inPfBarrel[i] || inPfTicl[i];
      int stage = 3;
      if (inPfTicl[i])
        ++ticlPf;
      if (isUsed)
        ++used;
      else if (!inPfRec[i]) {
        stage = 0;
        ++s0;
      } else if (blk[i] < 0) {
        stage = 1;
        ++s1;
        if (inTicl[i])
          ++ticlVeto;
      } else {
        stage = 2;
        ++s2;
      }
      if (!isUsed || (sampleEvery_ > 0 && i % sampleEvery_ == 0)) {
        int bsize = -1, nE = 0, nH = 0, nT = 0;
        double dE = -1, dH = -1;
        if (blk[i] >= 0) {
          auto const& b = (*hBlk)[blk[i]];
          bsize = b.elements().size();
          for (auto const& e : b.elements()) {
            nE += e.type() == reco::PFBlockElement::ECAL;
            nH += e.type() == reco::PFBlockElement::HCAL;
            nT += e.type() == reco::PFBlockElement::TRACK;
          }
          std::multimap<double, unsigned> m;
          b.associatedElements(elem[i], b.linkData(), m, reco::PFBlockElement::ECAL, reco::PFBlock::LINKTEST_ALL);
          if (!m.empty())
            dE = m.begin()->first;
          m.clear();
          b.associatedElements(elem[i], b.linkData(), m, reco::PFBlockElement::HCAL, reco::PFBlock::LINKTEST_ALL);
          if (!m.empty())
            dH = m.begin()->first;
        }
        auto const& hp = t.hitPattern();
        lines << "\n[pftrk] " << ev.id().run() << " " << ev.id().luminosityBlock() << " " << ev.id().event() << " " << i
              << " " << int(isUsed) << " " << stage << " " << t.pt() << " " << t.eta() << " " << t.phi() << " "
              << t.ptError() << " " << int(t.algo()) << " " << int(t.originalAlgo()) << " "
              << hp.numberOfValidTrackerHits() << " " << hp.numberOfLostTrackerHits(reco::HitPattern::TRACK_HITS) << " "
              << hp.numberOfLostTrackerHits(reco::HitPattern::MISSING_INNER_HITS) << " "
              << hp.numberOfLostTrackerHits(reco::HitPattern::MISSING_OUTER_HITS) << " "
              << hp.trackerLayersWithMeasurement() << " " << hp.pixelLayersWithMeasurement() << " " << t.chi2() << " "
              << t.ndof() << " " << t.dxy(pv) << " " << t.dz(pv) << " " << t.charge() << " " << int(inTicl[i]) << " "
              << bsize << " " << nE << " " << nH << " " << nT << " " << dE << " " << dH;
      }
    }
    edm::LogPrint("FitPhysPfTrace") << "[pftrace] " << ev.id().run() << " " << ev.id().luminosityBlock() << " "
                                    << ev.id().event() << " | " << n1 << " " << s0 << " " << s1 << " " << s2 << " "
                                    << used << " " << ticlVeto << " " << ticlPf << lines.str();
  }

private:
  const edm::EDGetTokenT<reco::TrackCollection> tracksToken_;
  const edm::EDGetTokenT<reco::PFRecTrackCollection> pfRecToken_;
  const edm::EDGetTokenT<reco::PFBlockCollection> blocksToken_;
  const edm::EDGetTokenT<reco::PFCandidateCollection> pfBarrelToken_;
  const edm::EDGetTokenT<reco::PFCandidateCollection> pfTiclToken_;
  const edm::EDGetTokenT<std::vector<TICLCandidate>> ticlToken_;
  const edm::EDGetTokenT<reco::VertexCollection> vtxToken_;
  const double minPt_;
  const int sampleEvery_;
};

DEFINE_FWK_MODULE(FitPhysPfTrace);
