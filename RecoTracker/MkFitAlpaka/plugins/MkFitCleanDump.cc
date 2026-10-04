// MkFitCleanDump: writes stock mkFit output tracks before duplicate cleaning (and before the quality filters) together
// with the STOCK decisions of StdSeq::clean_duplicates_sharedhits_pixelseed and of the LST-step candidate filter
// (phase1:qfilter_n_hits_pixseed && qfilter_nan_n_silly) to a flat binary file, for the device comparison test
// test/alpaka/cleanCompare.dev.cc. Also checks that stock cleaning of the no-cleaning tracks reproduces the stock
// production collection exactly.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/one/EDAnalyzer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "RecoTracker/MkFit/interface/MkFitEventOfHits.h"
#include "RecoTracker/MkFit/interface/MkFitGeometry.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFitCMS/interface/MkStdSeqs.h"
#include "RecoTracker/MkFitCore/interface/IterationConfig.h"
#include "RecoTracker/MkFitCore/interface/MkJob.h"
#include "RecoTracker/MkFitCore/interface/Track.h"
#include "RecoTracker/MkFitCore/interface/TrackStructures.h"
#include "RecoTracker/MkFitCore/interface/HitStructures.h"
#include "RecoTracker/Record/interface/TrackerRecoGeometryRecord.h"

namespace {
  constexpr char kMagic[8] = {'M', 'K', 'F', 'C', 'L', 'N', '1', 0};

  void writeTrack(FILE* f, const mkfit::Track& t) {
    float par[6], err[21];
    for (int i = 0; i < 6; ++i)
      par[i] = t.parameters()[i];
    std::memcpy(err, t.errors().Array(), sizeof(err));
    int32_t ints[4];
    ints[0] = t.charge();
    ints[1] = t.label();
    const auto st = t.getStatus();
    std::memcpy(&ints[2], &st, 4);
    ints[3] = t.nTotalHits();
    const int32_t nFound = t.nFoundHits();
    const float fl[2] = {t.chi2(), t.score()};
    std::fwrite(par, sizeof(par), 1, f);
    std::fwrite(err, sizeof(err), 1, f);
    std::fwrite(fl, sizeof(fl), 1, f);
    std::fwrite(ints, sizeof(ints), 1, f);
    std::fwrite(&nFound, 4, 1, f);
    for (int h = 0; h < t.nTotalHits(); ++h) {
      const mkfit::HitOnTrack hot = t.getHitOnTrack(h);
      int32_t w;
      std::memcpy(&w, &hot, 4);
      std::fwrite(&w, 4, 1, f);
    }
  }

  // Stock duplicate decisions: run the stock cleaner on a copy whose chi2 (not read by the cleaner) carries the
  // index, then mark the survivors.
  std::vector<int8_t> stockDupFlags(const mkfit::TrackVec& in,
                                    const mkfit::IterationConfig& itconf,
                                    const mkfit::TrackerInfo& ti,
                                    double& ms) {
    mkfit::TrackVec work(in);
    for (size_t i = 0; i < work.size(); ++i)
      work[i].setChi2(float(i));
    const auto t0 = std::chrono::steady_clock::now();
    itconf.m_duplicate_cleaner(work, itconf, ti);  // the iteration's cleaner (pixelseed or #52015 pixelpriority)
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::vector<int8_t> dup(in.size(), 1);
    for (auto const& t : work)
      dup[int(t.chi2())] = 0;
    return dup;
  }
}  // namespace

class MkFitCleanDump : public edm::one::EDAnalyzer<> {
public:
  explicit MkFitCleanDump(edm::ParameterSet const& ps);
  ~MkFitCleanDump() override;
  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);

private:
  void analyze(edm::Event const& ev, edm::EventSetup const& es) override;

  const edm::EDGetTokenT<MkFitOutputWrapper> stockToken_;
  const edm::EDGetTokenT<MkFitOutputWrapper> noDCToken_;
  const edm::EDGetTokenT<MkFitOutputWrapper> noFiltToken_;
  const edm::EDGetTokenT<MkFitEventOfHits> eohToken_;
  const edm::ESGetToken<MkFitGeometry, TrackerRecoGeometryRecord> geomToken_;
  const edm::ESGetToken<mkfit::IterationConfig, TrackerRecoGeometryRecord> itconfToken_;
  const std::string fileName_;
  FILE* file_ = nullptr;
  bool headerWritten_ = false;
  long nEvents_ = 0, nIdentical_ = 0;
  double stockMs_ = 0;
};

MkFitCleanDump::MkFitCleanDump(edm::ParameterSet const& ps)
    : stockToken_{consumes(ps.getParameter<edm::InputTag>("stockTracks"))},
      noDCToken_{consumes(ps.getParameter<edm::InputTag>("noDCTracks"))},
      noFiltToken_{consumes(ps.getParameter<edm::InputTag>("noFiltTracks"))},
      eohToken_{consumes(ps.getParameter<edm::InputTag>("eventOfHits"))},
      geomToken_{esConsumes()},
      itconfToken_{esConsumes(ps.getParameter<edm::ESInputTag>("config"))},
      fileName_{ps.getParameter<std::string>("fileName")} {
  file_ = std::fopen(fileName_.c_str(), "wb");
  if (!file_)
    throw cms::Exception("MkFitCleanDump") << "cannot open " << fileName_;
}

MkFitCleanDump::~MkFitCleanDump() {
  if (file_)
    std::fclose(file_);
  edm::LogPrint("MkFitCleanDump") << "MkFitCleanDump: " << nEvents_ << " events, stock cleaner on no-cleaning tracks "
                                  << "reproduces the stock collection exactly in " << nIdentical_ << " events; stock "
                                  << "clean_duplicates_sharedhits_pixelseed " << (nEvents_ ? stockMs_ / nEvents_ : 0.)
                                  << " ms/event (1 thread, incl. remove_duplicates)";
}

void MkFitCleanDump::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("stockTracks", edm::InputTag("hltInitialStepTrackCandidatesMkFit"));
  desc.add<edm::InputTag>("noDCTracks", edm::InputTag("cleanMkFitNoDC"));
  desc.add<edm::InputTag>("noFiltTracks", edm::InputTag("cleanMkFitNoFilt"));
  desc.add<edm::InputTag>("eventOfHits", edm::InputTag("hltMkFitEventOfHits"));
  desc.add<edm::ESInputTag>("config", edm::ESInputTag("", "hltInitialStepTrackCandidatesMkFitConfig"));
  desc.add<std::string>("fileName", "clean_dump.bin");
  descriptions.addWithDefaultLabel(desc);
}

void MkFitCleanDump::analyze(edm::Event const& ev, edm::EventSetup const& es) {
  const auto& stock = ev.get(stockToken_).tracks();
  const auto& noDC = ev.get(noDCToken_).tracks();
  const auto& noFilt = ev.get(noFiltToken_).tracks();
  const auto& eoh = ev.get(eohToken_).get();
  const auto& geom = es.getData(geomToken_);
  const auto& itconf = es.getData(itconfToken_);

  if (!headerWritten_) {
    std::fwrite(kMagic, 8, 1, file_);
    const float fp[4] = {
        itconf.dc_fracSharedHits, itconf.dc_drth_central, itconf.dc_drth_obarrel, itconf.dc_drth_forward};
    const int32_t ip[2] = {itconf.m_params.minHitsQF, itconf.m_backward_params.minHitsQF};
    std::fwrite(fp, sizeof(fp), 1, file_);
    std::fwrite(ip, sizeof(ip), 1, file_);
    headerWritten_ = true;
  }

  // 1) duplicate cleaner: stock decisions on the no-cleaning tracks; consistency with the production collection
  double ms = 0;
  const auto dup = stockDupFlags(noDC, itconf, es.getData(geomToken_).trackerInfo(), ms);
  stockMs_ += ms;
  bool identical = true;
  {
    size_t k = 0;
    for (size_t i = 0; i < noDC.size(); ++i) {
      if (dup[i])
        continue;
      if (k >= stock.size() || stock[k].label() != noDC[i].label() || stock[k].score() != noDC[i].score() ||
          stock[k].nTotalHits() != noDC[i].nTotalHits() || stock[k].parameters()[3] != noDC[i].parameters()[3])
        identical = false;
      ++k;
    }
    if (k != stock.size())
      identical = false;
  }
  ++nEvents_;
  if (identical)
    ++nIdentical_;

  // 2) LST-step filter decisions (stock registered function + nan_n_silly, as run_OneIteration composes them),
  //    forward params (pre-backward-fit) and backward params (post-backward-fit)
  auto filt = mkfit::IterationConfig::get_candidate_filter("phase1:qfilter_n_hits_pixseed");
  mkfit::MkJob job({geom.trackerInfo(), itconf, eoh, eoh.refBeamSpot(), nullptr});
  std::vector<int8_t> passPre(noFilt.size()), passPost(noFilt.size());
  for (size_t i = 0; i < noFilt.size(); ++i) {
    mkfit::TrackCand tc(noFilt[i], nullptr);
    tc.setNFoundHits(noFilt[i].nFoundHits());
    passPre[i] = filt(tc, job) && mkfit::StdSeq::qfilter_nan_n_silly<mkfit::TrackCand>(tc, job);
  }
  job.switch_to_backward();
  for (size_t i = 0; i < noFilt.size(); ++i) {
    mkfit::TrackCand tc(noFilt[i], nullptr);
    tc.setNFoundHits(noFilt[i].nFoundHits());
    passPost[i] = filt(tc, job) && mkfit::StdSeq::qfilter_nan_n_silly<mkfit::TrackCand>(tc, job);
  }

  // record: event id, consistency flag, then the two track lists with stock decisions
  const uint64_t eid[3] = {ev.id().run(), ev.id().luminosityBlock(), ev.id().event()};
  const int32_t hdr[4] = {identical ? 1 : 0, int32_t(stock.size()), int32_t(noDC.size()), int32_t(noFilt.size())};
  std::fwrite(eid, sizeof(eid), 1, file_);
  std::fwrite(hdr, sizeof(hdr), 1, file_);
  for (size_t i = 0; i < noDC.size(); ++i) {
    writeTrack(file_, noDC[i]);
    std::fwrite(&dup[i], 1, 1, file_);
  }
  for (size_t i = 0; i < noFilt.size(); ++i) {
    writeTrack(file_, noFilt[i]);
    std::fwrite(&passPre[i], 1, 1, file_);
    std::fwrite(&passPost[i], 1, 1, file_);
  }
  std::fflush(file_);

  edm::LogPrint("MkFitCleanDump") << "event " << ev.id().event() << ": stock " << stock.size() << ", noDC "
                                  << noDC.size() << " (stock-cleaned " << (noDC.size() - std::count(dup.begin(), dup.end(), 1))
                                  << ", identical=" << identical << ", stock cleaner " << ms << " ms), noFilt " << noFilt.size();
}

DEFINE_FWK_MODULE(MkFitCleanDump);
