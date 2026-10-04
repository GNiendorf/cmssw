#ifndef RecoTracker_MkFitAlpaka_test_engine_step_format_h
#define RecoTracker_MkFitAlpaka_test_engine_step_format_h

// Reader of the clone-engine step dumps written by the engine lane's private instrumented stock MkFitCore
// (r3_engine/stockdump, src/RecoTracker/MkFitCore/src/EngineStepDump.h + hooks in MkBuilder::find_tracks_in_layers).
// Files step_<pid>_<n>.bin, one per thread; records (magic "ESD1"):
//   kind 0 START  {call region iterDir startSeed nSeeds}  Seed[nSeeds]                 (call entry)
//   kind 1 STEP   {call region iterDir startSeed nSeeds planIdx layer prevLayer pickupOnly isPixel isBarrel
//                  nWin c2[4] chi2CutMin maxClusterSize maxHolesPerCand maxConsecHoles maxCandsPerSeed pTCutOverlap
//                  recheckOverlap minPtCut inFwd}  nListed Listed[nListed]  nLive Seed[nLive]   (after the update loop)
//   kind 2 FINAL  {call region iterDir startSeed nSeeds}  Seed[nSeeds]                 (after mergeCandsAndBestShortOne)
// Listed = the K2 outputs of one listed candidate (raw WSR before find_tracks_handle_missed_layers) + its hits.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace enginedump {

  constexpr uint32_t kMagic = 0x31445345u;

  struct Cand {
    float score, chi2;
    int32_t lastHitIdx, nFound, nMissing, nOverlap, nInsideMinusOne, nTailMinusOne, originIndex, label, hasCC;
    int32_t ovHit[2], ovModule[2];
    float ovChi2[2];
    float par[6], err[21];
    int32_t charge, nSeedHits;
    uint32_t status;
  };
  static_assert(sizeof(Cand) == 4 * (2 + 9 + 6 + 27 + 3));

  struct HoT {
    int32_t index, layer;
    float chi2;
    int32_t prev;
  };

  struct Seed {
    int32_t seedIdx, state, pickupLayer, lastHitIdxBeforeBkw, nInsideBkw, nTailBkw;
    Cand best;
    std::vector<Cand> cands;
    std::vector<HoT> hots;
  };

  struct Hit {
    int32_t idx;
    uint32_t module, spanRows;
    float pos[3], err[6], nrm[3], dir[3], pnt[3];
  };

  struct Listed {
    int32_t seed, ic, wsr, inGap, nHits;
    float par[6], err[21];
    int32_t charge;
    std::vector<Hit> hits;
  };

  struct StepHeader {
    int32_t planIdx, layer, prevLayer, pickupOnly, isPixel, isBarrel, nWin;
    float c2[4];
    float chi2CutMin;
    int32_t maxClusterSize, maxHolesPerCand, maxConsecHoles, maxCandsPerSeed;
    float pTCutOverlap;
    int32_t recheckOverlap;
    float minPtCut;
    int32_t inFwd;
  };

  struct Record {
    int32_t kind, call, region, iterDir, startSeed, nSeeds;
    StepHeader step{};
    std::vector<Listed> listed;
    std::vector<Seed> seeds;
  };

  class Reader {
  public:
    explicit Reader(const std::string& fn) : f_(std::fopen(fn.c_str(), "rb")) {}
    ~Reader() {
      if (f_)
        std::fclose(f_);
    }
    bool ok() const { return f_ != nullptr; }

    bool next(Record& r) {
      uint32_t magic;
      if (!rd(magic))
        return false;
      if (magic != kMagic)
        return false;
      if (!(rd(r.kind) && rd(r.call) && rd(r.region) && rd(r.iterDir) && rd(r.startSeed) && rd(r.nSeeds)))
        return false;
      r.listed.clear();
      r.seeds.clear();
      int32_t nSeedRecs = r.nSeeds;
      if (r.kind == 1) {
        StepHeader& h = r.step;
        if (!(rd(h.planIdx) && rd(h.layer) && rd(h.prevLayer) && rd(h.pickupOnly) && rd(h.isPixel) &&
              rd(h.isBarrel) && rd(h.nWin) && rdn(h.c2, 4) && rd(h.chi2CutMin) && rd(h.maxClusterSize) &&
              rd(h.maxHolesPerCand) && rd(h.maxConsecHoles) && rd(h.maxCandsPerSeed) && rd(h.pTCutOverlap) &&
              rd(h.recheckOverlap) && rd(h.minPtCut) && rd(h.inFwd)))
          return false;
        int32_t nl;
        if (!rd(nl))
          return false;
        r.listed.resize(nl);
        for (auto& l : r.listed) {
          if (!(rd(l.seed) && rd(l.ic) && rd(l.wsr) && rd(l.inGap) && rd(l.nHits) && rdn(l.par, 6) && rdn(l.err, 21) &&
                rd(l.charge)))
            return false;
          l.hits.resize(l.nHits > 0 ? l.nHits : 0);
          for (auto& h : l.hits)
            if (!(rd(h.idx) && rd(h.module) && rd(h.spanRows) && rdn(h.pos, 3) && rdn(h.err, 6) && rdn(h.nrm, 3) &&
                  rdn(h.dir, 3) && rdn(h.pnt, 3)))
              return false;
        }
        if (!rd(nSeedRecs))
          return false;
      }
      r.seeds.resize(nSeedRecs);
      for (auto& s : r.seeds) {
        if (!(rd(s.seedIdx) && rd(s.state) && rd(s.pickupLayer) && rd(s.lastHitIdxBeforeBkw) && rd(s.nInsideBkw) &&
              rd(s.nTailBkw) && rd(s.best)))
          return false;
        int32_t nc;
        if (!rd(nc))
          return false;
        s.cands.resize(nc);
        if (nc > 0 && !rdn(s.cands.data(), nc))
          return false;
        int32_t nh;
        if (!rd(nh))
          return false;
        s.hots.resize(nh);
        if (nh > 0 && !rdn(s.hots.data(), nh))
          return false;
      }
      return true;
    }

  private:
    template <class T>
    bool rd(T& v) {
      return std::fread(&v, sizeof(T), 1, f_) == 1;
    }
    template <class T>
    bool rdn(T* v, int n) {
      return std::fread(v, sizeof(T), n, f_) == size_t(n);
    }
    FILE* f_;
  };

}  // namespace enginedump

#endif
