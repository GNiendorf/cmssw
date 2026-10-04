#ifndef RecoTracker_MkFitAlpaka_test_bkfitDumpIO_h
#define RecoTracker_MkFitAlpaka_test_bkfitDumpIO_h

// bkfit lane: reader of the stock backward-fit dump (MKFIT_BKFIT_DUMP hook in MkBuilder::fit_cands, doc/bkfit.txt)
// and the agreement summary shared by the device test and the stock re-run.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "RecoTracker/MkFitAlpaka/src/alpaka/bkfit/BkFitTypes.h"

namespace mkfitdev::bkfit::dump {

  struct Reader {
    std::vector<char> buf;
    size_t pos = 0;
    template <typename T>
    T get() {
      T v;
      std::memcpy(&v, buf.data() + pos, sizeof(T));
      pos += sizeof(T);
      return v;
    }
    bool more() const { return pos + 4 <= buf.size(); }
  };

  struct StockOut {
    int32_t callSeq, seed, label;
    FlatOut out;
  };

  struct Dump {
    int nbZ = 0, nbR = 0;
    float rngZ = 0, rngR = 0;
    std::vector<float> bbxi, radl;
    std::vector<FlatCand> cands;
    std::vector<FlatNode> nodes;
    std::vector<StockOut> stock;
    long nSameLayerRuns = 0, nInvalidNodes = 0;
    int nEvents = 0;
  };

  // returns 0 on success
  inline int readDump(const char* fn, long maxCands, Dump& D) {
    int& nbZ = D.nbZ;
    int& nbR = D.nbR;
    float& rngZ = D.rngZ;
    float& rngR = D.rngR;
    auto& bbxi = D.bbxi;
    auto& radl = D.radl;
    auto& cands = D.cands;
    auto& nodes = D.nodes;
    auto& stock = D.stock;
    long& nSameLayerRuns = D.nSameLayerRuns;
    long& nInvalidNodes = D.nInvalidNodes;
  Reader rd;
  {
    std::FILE* f = std::fopen(fn, "rb");
    if (!f) {
      std::printf("cannot open %s\n", fn);
      return 2;
    }
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    rd.buf.resize(sz);
    if (std::fread(rd.buf.data(), 1, sz, f) != size_t(sz)) {
      std::printf("short read\n");
      return 2;
    }
    std::fclose(f);
  }

  while (rd.more() && (maxCands < 0 || long(cands.size()) < maxCands)) {
    const int32_t magic = rd.get<int32_t>();
    if (magic == 0xBF00) {
      nbZ = rd.get<int32_t>();
      nbR = rd.get<int32_t>();
      rngZ = rd.get<float>();
      rngR = rd.get<float>();
      bbxi.resize(nbZ * nbR);
      radl.resize(nbZ * nbR);
      for (auto& x : bbxi)
        x = rd.get<float>();
      for (auto& x : radl)
        x = rd.get<float>();
      continue;
    }
    if (magic != 0xBF17) {
      std::printf("bad record magic %x at %zu\n", magic, rd.pos);
      return 3;
    }
    StockOut so;
    so.callSeq = rd.get<int32_t>();
    so.seed = rd.get<int32_t>();
    so.label = rd.get<int32_t>();
    FlatCand c{};
    c.nFound = rd.get<int32_t>();
    c.nMissing = rd.get<int32_t>();
    c.nOverlap = rd.get<int32_t>();
    c.nInsideMinusOne = rd.get<int32_t>();
    c.nTailMinusOne = rd.get<int32_t>();
    rd.get<float>();  // chi2 in
    rd.get<float>();  // score in
    c.charge = rd.get<int32_t>();
    for (auto& x : c.par)
      x = rd.get<float>();
    for (auto& x : c.err)
      x = rd.get<float>();
    c.lastNode = rd.get<int32_t>();
    c.nNodes = rd.get<int32_t>();
    c.nodeBegin = nodes.size();
    int prevLayer = -999;
    for (int n = 0; n < c.nNodes; ++n) {
      FlatNode fn{};
      fn.index = rd.get<int32_t>();
      fn.layer = rd.get<int32_t>();
      rd.get<float>();  // node chi2
      fn.prev = rd.get<int32_t>();
      if (fn.index >= 0) {
        for (auto& x : fn.msPar)
          x = rd.get<float>();
        for (auto& x : fn.msErr)
          x = rd.get<float>();
        rd.get<uint32_t>();  // detIDinLayer
        for (auto& x : fn.pnt)
          x = rd.get<float>();
        for (auto& x : fn.nrm)
          x = rd.get<float>();
        for (auto& x : fn.dir)
          x = rd.get<float>();
        if (fn.layer == prevLayer)
          ++nSameLayerRuns;
        prevLayer = fn.layer;
      } else {
        ++nInvalidNodes;
      }
      nodes.push_back(fn);
    }
    so.out.charge = rd.get<int32_t>();
    for (auto& x : so.out.par)
      x = rd.get<float>();
    for (auto& x : so.out.err)
      x = rd.get<float>();
    so.out.chi2 = rd.get<float>();
    so.out.score = rd.get<float>();
    cands.push_back(c);
    stock.push_back(so);
  }
    for (auto& s : stock)
      D.nEvents = std::max(D.nEvents, s.callSeq + 1);
    return 0;
  }

  inline bool isFin(float x) { return std::isfinite(x); }

  // relative difference with a floor so that values near 0 compare absolutely
  inline double relDiff(float a, float b, double floor) {
    const double d = std::abs(double(a) - double(b));
    return d / std::max(floor, std::abs(double(b)));
  }

  // Agreement summary (fractions of the finite candidates; chgFrac/flipFrac of all).
  struct Metrics {
    double chgFrac = 0, flipFrac = 0, chi2Close = 0;
    double par[6] = {0}, all[6] = {0};  // fraction within thr = 0, 1e-6, 1e-5, 1e-4, 1e-3, 1e-2
    bool pass = false;                  // absolute rule (no floor): charge, flips, >= 99% within 1e-3
  };

  // D-M4: 'port' no worse than 'floor' (worst stock arm vs stock v3 -Ofast) on every fraction from 1e-5 up, chi2, charge
  // and finiteness; slack = 2 binomial sigma of the floor fraction (the two are measured on the same candidates). Charge
  // changes and finiteness flips are rare counts: the Poisson slack of test/replay_floor_check.py (n <= f + 3 sqrt(f) + 1).
  inline bool noWorseThanFloor(const Metrics& port, const Metrics& floor, long n) {
    auto ok = [n](double p, double f) { return p >= f - 2 * std::sqrt(std::max(f * (1 - f), 1e-12) / n); };
    auto rare = [n](double p, double f) { return p * n <= f * n + 3 * std::sqrt(f * n) + 1 + 1e-9; };
    bool r = rare(1 - port.chgFrac, 1 - floor.chgFrac) && rare(port.flipFrac, floor.flipFrac) &&
             ok(port.chi2Close, floor.chi2Close);
    for (int t = 2; t < 6; ++t)
      r = r && ok(port.par[t], floor.par[t]) && ok(port.all[t], floor.all[t]);
    return r;
  }

  // D7-a: element-wise worst of two floor arms (lower agreement fractions, more charge changes / finiteness flips).
  inline Metrics worstOf(const Metrics& a, const Metrics& b) {
    Metrics w = a;
    w.chgFrac = std::min(a.chgFrac, b.chgFrac);
    w.flipFrac = std::max(a.flipFrac, b.flipFrac);
    w.chi2Close = std::min(a.chi2Close, b.chi2Close);
    for (int t = 0; t < 6; ++t) {
      w.par[t] = std::min(a.par[t], b.par[t]);
      w.all[t] = std::min(a.all[t], b.all[t]);
    }
    w.pass = a.pass && b.pass;
    return w;
  }

  // Agreement of 'got' with the stock outputs of the dump; prints the summary.
  inline Metrics compare(const Dump& D, const FlatOut* got, const char* tag) {
    const auto& stock = D.stock;
    const auto& cands = D.cands;
    const int nc = cands.size();
    std::printf("---- %s vs stock dump ----\n", tag);
  const double thr[] = {0., 1e-6, 1e-5, 1e-4, 1e-3, 1e-2};
  constexpr int nthr = 6;
  long nWithin[nthr] = {0}, nWithinPar[nthr] = {0};
  long chgSame = 0, finFlip = 0, nonFinBoth = 0, chi2Same = 0, chi2Close = 0, scoreSame = 0, scoreClose = 0;
  double worst = 0;
  int worstIdx = -1;
  std::FILE* fd = nullptr;
  if (const char* p = std::getenv("BKFIT_DIFFS"))
    fd = std::fopen(p, "w");
  for (int i = 0; i < nc; ++i) {
    const FlatOut& a = got[i];
    const FlatOut& b = stock[i].out;
    chgSame += a.charge == b.charge;
    bool finA = true, finB = true;
    for (int e = 0; e < 6; ++e) {
      finA &= isFin(a.par[e]);
      finB &= isFin(b.par[e]);
    }
    for (int e = 0; e < 21; ++e) {
      finA &= isFin(a.err[e]);
      finB &= isFin(b.err[e]);
    }
    if (finA != finB) {
      ++finFlip;
      continue;
    }
    if (!finA) {
      ++nonFinBoth;
      continue;
    }
    chi2Same += (a.chi2 == b.chi2);
    chi2Close += relDiff(a.chi2, b.chi2, 1.0) <= 1e-4;
    scoreSame += (a.score == b.score);
    scoreClose += relDiff(a.score, b.score, 1.0) <= 1e-5;
    double mPar = 0, mAll = 0;
    for (int e = 0; e < 6; ++e) {
      // phi (4) compared on the circle
      double d = e == 4 ? std::abs(std::remainder(double(a.par[e]) - double(b.par[e]), 2 * M_PI)) /
                              std::max(1e-3, std::abs(double(b.par[e])))
                        : relDiff(a.par[e], b.par[e], 1e-3);
      mPar = std::max(mPar, d);
    }
    mAll = mPar;
    // errors: |d err_ij| / sqrt(err_ii err_jj) (correlation-normalised; = relative difference on the diagonal)
    for (int ei = 0; ei < 6; ++ei)
      for (int ej = 0; ej <= ei; ++ej) {
        const int k = ei * (ei + 1) / 2 + ej;
        const double norm = std::sqrt(std::abs(double(b.err[ei * (ei + 3) / 2]) * double(b.err[ej * (ej + 3) / 2])));
        mAll = std::max(mAll, std::abs(double(a.err[k]) - double(b.err[k])) / std::max(norm, 1e-30));
      }
    if (a.charge != b.charge)
      mAll = std::max(mAll, 1.0);
    for (int t = 0; t < nthr; ++t) {
      nWithin[t] += mAll <= thr[t];
      nWithinPar[t] += mPar <= thr[t];
    }
    if (mAll > worst) {
      worst = mAll;
      worstIdx = i;
    }
    if (fd)
      std::fprintf(fd, "%d %d %d %g %g %d\n", stock[i].callSeq, stock[i].seed, stock[i].label, mPar, mAll,
                   cands[i].nNodes);
  }
  if (fd)
    std::fclose(fd);
  const long nfin = nc - finFlip - nonFinBoth;
  std::printf("charge agree %ld/%d (%.4f%%), finiteness flips %ld, non-finite in both %ld\n",
                chgSame, nc, 100. * chgSame / nc, finFlip, nonFinBoth);
    std::printf("chi2 bit-identical %.3f%%, within 1e-4 (abs below 1) %.3f%%\n", 100. * chi2Same / nfin,
                100. * chi2Close / nfin);
  std::printf("score bit-identical %ld/%ld (%.3f%%), within 1e-5 rel %ld (%.3f%%)\n",
              scoreSame,
              nfin,
              100. * scoreSame / nfin,
              scoreClose,
              100. * scoreClose / nfin);
  for (int t = 0; t < nthr; ++t)
    std::printf("  max rel diff <= %-6g : params %.4f%%   params+errors %.4f%%\n",
                thr[t],
                100. * nWithinPar[t] / nfin,
                100. * nWithin[t] / nfin);
  if (worstIdx >= 0) {
    std::printf("worst candidate: event %d seed %d label %d nodes %d, max rel diff %g\n",
                stock[worstIdx].callSeq,
                stock[worstIdx].seed,
                stock[worstIdx].label,
                cands[worstIdx].nNodes,
                worst);
    for (int e = 0; e < 6; ++e)
      std::printf("   par[%d] port %.9g stock %.9g\n", e, got[worstIdx].par[e], stock[worstIdx].out.par[e]);
  }
    Metrics M;
    M.chgFrac = double(chgSame) / nc;
    M.flipFrac = double(finFlip) / nc;
    M.chi2Close = double(chi2Close) / nfin;
    for (int t = 0; t < nthr; ++t) {
      M.par[t] = double(nWithinPar[t]) / nfin;
      M.all[t] = double(nWithin[t]) / nfin;
    }
    M.pass = chgSame >= 0.999 * nc && finFlip <= 1e-4 * nc && nWithin[4] >= 0.99 * nfin;
    return M;
  }

}  // namespace mkfitdev::bkfit::dump

#endif
