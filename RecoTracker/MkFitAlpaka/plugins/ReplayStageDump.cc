// MkFitAlpakaReplayStageDump / MkFitAlpakaReplayStageDumpSoA (harness lane): flat binary dump of mkFit-level track
// collections (MkFitOutputWrapper, or a port's mkfitdev::TrackSoA host collection), one file per collection, for
// comparisons ACROSS jobs (e.g. the per-stage D-M4 floor: stockStages in a v3 job vs a v2 job; or a port dump vs a
// stock dump). Compare with test/replay_stage_compare.py.
// Record (little endian, packed, 278 bytes): uint64 event; uint32 run, lumi; int32 label, pos; int16 nTot, nFound;
//   float par[6] (x, y, z, 1/pT, phi, theta); float err[6] (diagonal of the 6x6 errors); float chi2, score;
//   int16 layer[kMaxHits]; int16 pad; int32 index[kMaxHits]   with kMaxHits = 32 (longer hit lists: nTot keeps the
//   true count and only the first 32 are stored).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDAnalyzer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "RecoTracker/MkFit/interface/MkFitOutputWrapper.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"
#include "RecoTracker/MkFitCore/interface/Track.h"

namespace mkfitdev_harness {
  constexpr int kDumpMaxHits = 32;
#pragma pack(push, 1)
  struct DumpRec {
    uint64_t event;
    uint32_t run, lumi;
    int32_t label, pos;
    int16_t nTot, nFound;
    float par[6], err[6], chi2, score;
    int16_t layer[kDumpMaxHits];
    int16_t pad;
    int32_t index[kDumpMaxHits];
  };
#pragma pack(pop)
  static_assert(sizeof(DumpRec) == 8 + 8 + 8 + 4 + 56 + 64 + 2 + 128, "DumpRec layout");

  struct WrapperSrc {
    using Collection = MkFitOutputWrapper;
    static size_t size(Collection const& c) { return c.tracks().size(); }
    static mkfit::Track const& at(Collection const& c, size_t i) { return c.tracks()[i]; }
  };
  struct SoASrc {
    using Collection = mkfitdev::TrackSoAHostCollection;
    static size_t size(Collection const& c) { return size_t(std::max(0, int(c.const_view().nTracks()))); }
    static mkfit::Track at(Collection const& c, size_t i) { return mkfitdev::trackFromSoA(c.const_view(), int(i)); }
  };

  template <typename Src>
  class StageDump : public edm::global::EDAnalyzer<> {
  public:
    explicit StageDump(edm::ParameterSet const& cfg) {
      auto srcs = cfg.getParameter<std::vector<edm::InputTag>>("src");
      auto files = cfg.getParameter<std::vector<std::string>>("files");
      if (srcs.size() != files.size())
        throw cms::Exception("Configuration") << "src and files must have the same length";
      for (size_t i = 0; i < srcs.size(); ++i) {
        tokens_.push_back(consumes<typename Src::Collection>(srcs[i]));
        FILE* f = fopen(files[i].c_str(), "wb");
        if (!f)
          throw cms::Exception("Configuration") << "cannot open " << files[i];
        files_.push_back(f);
      }
      mutex_ = std::make_unique<std::mutex>();
    }
    ~StageDump() override {
      for (auto* f : files_)
        fclose(f);
    }
    static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
      edm::ParameterSetDescription desc;
      desc.add<std::vector<edm::InputTag>>("src", {});
      desc.add<std::vector<std::string>>("files", {});
      descriptions.addWithDefaultLabel(desc);
    }
    void analyze(edm::StreamID, edm::Event const& ev, edm::EventSetup const&) const override {
      for (size_t s = 0; s < tokens_.size(); ++s) {
        auto const& c = ev.get(tokens_[s]);
        const size_t n = Src::size(c);
        std::vector<DumpRec> recs(n);
        for (size_t i = 0; i < n; ++i) {
          auto const& t = Src::at(c, i);
          DumpRec& r = recs[i];
          r = DumpRec{};
          r.event = ev.id().event();
          r.run = ev.id().run();
          r.lumi = ev.id().luminosityBlock();
          r.label = t.label();
          r.pos = int32_t(i);
          r.nTot = int16_t(t.nTotalHits());
          r.nFound = int16_t(t.nFoundHits());
          for (int k = 0; k < 6; ++k) {
            r.par[k] = t.parameters()[k];
            r.err[k] = t.errors()(k, k);
          }
          r.chi2 = t.chi2();
          r.score = t.score();
          for (int h = 0; h < std::min<int>(t.nTotalHits(), kDumpMaxHits); ++h) {
            auto hot = t.getHitOnTrack(h);
            r.layer[h] = int16_t(hot.layer);
            r.index[h] = hot.index;
          }
        }
        std::lock_guard<std::mutex> lk(*mutex_);
        fwrite(recs.data(), sizeof(DumpRec), recs.size(), files_[s]);
      }
    }

  private:
    std::vector<edm::EDGetTokenT<typename Src::Collection>> tokens_;
    std::vector<FILE*> files_;
    std::unique_ptr<std::mutex> mutex_;
  };
}  // namespace mkfitdev_harness

using MkFitAlpakaReplayStageDump = mkfitdev_harness::StageDump<mkfitdev_harness::WrapperSrc>;
using MkFitAlpakaReplayStageDumpSoA = mkfitdev_harness::StageDump<mkfitdev_harness::SoASrc>;
DEFINE_FWK_MODULE(MkFitAlpakaReplayStageDump);
DEFINE_FWK_MODULE(MkFitAlpakaReplayStageDumpSoA);
