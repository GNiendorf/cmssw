// Tie-order study of the per-seed selection. Synthetic option lists with forced exact score ties are run through
// (a) a line-by-line host transliteration of stock CandCloner::processSeedRange using std::sort (libstdc++) and
// (b) mkfitdev::selectSeedCandidates on the device (one thread per list). Prints the agreement for lists with
// n <= 16 options (libstdc++ insertion sort, stable; must agree) and n > 16 (introsort; NOT reproduced since D-ties,
// reported only). Exit code 1 on a disagreement for n <= 16.
// Usage: <binary> [nTrials] [seed]

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/devices.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandSelection.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE;

namespace md = ::mkfitdev;

namespace {

  struct Out {  // canonical description of a selection result
    std::vector<int> src, hit, upd, updHit;
    std::vector<float> score;
    float bestScore;
    int bestSrc;
    bool operator==(const Out& o) const {
      return src == o.src && hit == o.hit && upd == o.upd && updHit == o.updHit && score == o.score &&
             std::memcmp(&bestScore, &o.bestScore, 4) == 0 && bestSrc == o.bestSrc;
    }
  };

  // Stock transliteration (CandCloner.cc:127-252) on plain data; extras are identified by -100 - e.
  Out stockSelect(const std::vector<md::CandOption>& optsIn,
                  const std::vector<float>& extraScore,
                  float bestShortScore,
                  int maxCands) {
    std::vector<md::CandOption> h = optsIn;
    std::sort(h.begin(), h.end(), [](const md::CandOption& a, const md::CandOption& b) { return a.score > b.score; });
    Out o;
    o.bestScore = bestShortScore;
    o.bestSrc = -1;
    size_t ei = 0;
    int nPushed = 0;
    for (size_t ih = 0; ih < h.size(); ++ih) {
      const md::CandOption& a = h[ih];
      if (a.hitIdx == -2) {
        if (a.score > o.bestScore) {
          o.bestScore = a.score;
          o.bestSrc = a.trkIdx;
        }
        continue;
      }
      while (ei < extraScore.size() && extraScore[ei] > a.score && nPushed < maxCands) {
        o.src.push_back(-100 - int(ei));
        o.hit.push_back(0);
        o.score.push_back(extraScore[ei]);
        ++nPushed;
        ++ei;
      }
      if (nPushed >= maxCands)
        break;
      if (a.hitIdx >= 0) {
        o.upd.push_back(nPushed);
        o.updHit.push_back(a.hitIdx);
      }
      o.src.push_back(a.trkIdx);
      o.hit.push_back(a.hitIdx);
      o.score.push_back(a.score);
      ++nPushed;
      if (nPushed >= maxCands)
        break;
    }
    while (ei < extraScore.size() && nPushed < maxCands) {
      o.src.push_back(-100 - int(ei));
      o.hit.push_back(0);
      o.score.push_back(extraScore[ei]);
      ++nPushed;
      ++ei;
    }
    return o;
  }

  struct TieCase {
    md::CandOption opts[md::kMaxOptsPerSeed];
    int nOpts, nExtra, nCands;
    float extra[md::kMaxExtrasPerSeed];
    float best;
  };
  struct TieOut {
    int nOut, nUpd, bestSrc;
    int src[md::kMaxCandsPerSeed], hit[md::kMaxCandsPerSeed], upd[md::kMaxCandsPerSeed], updHit[md::kMaxCandsPerSeed];
    float score[md::kMaxCandsPerSeed];
    float bestScore;
  };

  class KernelTies {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, const TieCase* cases, TieOut* outs, int n) const {
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        const TieCase& c = cases[i];
        md::CandBook cin[md::kMaxCandsPerSeed];
        float pt[md::kMaxCandsPerSeed];
        for (int k = 0; k < md::kMaxCandsPerSeed; ++k) {
          cin[k] = md::CandBook{};
          cin[k].lastHitIdx = k;
          cin[k].overlaps.reset();
          pt[k] = 1.f;
        }
        md::CandExtra ext[md::kMaxExtrasPerSeed];
        for (int e = 0; e < c.nExtra; ++e) {
          ext[e].book = md::CandBook{};
          ext[e].book.score = c.extra[e];
          ext[e].book.lastHitIdx = 1000 + e;
          ext[e].stateSrc = 0;
        }
        md::CandBook best{};
        best.score = c.best;
        int8_t bestValid = 1;
        int32_t bestSrc = -1;
        md::HoTNode hots[64];
        int32_t nHots = 8;
        md::CandBook out[md::kMaxCandsPerSeed];
        int32_t outSrc[md::kMaxCandsPerSeed], nOut = 0, nUpd = 0, nOvl = 0;
        md::CandUpdate upd[md::kMaxCandsPerSeed], ovl[md::kMaxCandsPerSeed];
        uint32_t ovf = 0;
        md::SeedSelIO io{cin, pt, c.nCands, ext, c.nExtra, c.opts, c.nOpts, md::kFinding, &best, &bestValid, &bestSrc,
                         hots, 0, 64, &nHots, out, outSrc, &nOut, upd, &nUpd, ovl, &nOvl, &ovf};
        md::SeedSelParams p{3, 5, -1.f, false};
        md::selectSeedCandidates(p, io);
        TieOut& o = outs[i];
        o.nOut = nOut;
        for (int k = 0; k < nOut; ++k) {
          const int last = out[k].lastHitIdx;
          if (last >= 1000) {
            o.src[k] = -100 - (last - 1000);
            o.hit[k] = 0;
          } else {
            o.src[k] = outSrc[k];
            o.hit[k] = hots[last].index;
          }
          o.score[k] = out[k].score;
        }
        o.nUpd = nUpd;
        for (int u = 0; u < nUpd; ++u) {
          o.upd[u] = upd[u].cand_idx;
          o.updHit[u] = upd[u].hit_idx;
        }
        o.bestScore = best.score;
        o.bestSrc = bestSrc;
      }
    }
  };

  Out toOut(const TieOut& t) {
    Out o;
    for (int k = 0; k < t.nOut; ++k) {
      o.src.push_back(t.src[k]);
      o.hit.push_back(t.hit[k]);
      o.score.push_back(t.score[k]);
    }
    for (int u = 0; u < t.nUpd; ++u) {
      o.upd.push_back(t.upd[u]);
      o.updHit.push_back(t.updHit[u]);
    }
    o.bestScore = t.bestScore;
    o.bestSrc = t.bestSrc;
    return o;
  }

}  // namespace

int main(int argc, char** argv) {
  const long nTrials = argc > 1 ? std::atol(argv[1]) : 200000;
  std::mt19937 rng(argc > 2 ? std::atoi(argv[2]) : 12345);
  long n16 = 0, ok16 = 0, nbig = 0, okbig = 0;
  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    printf("no device for backend %s, skipping\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
    return 0;
  }
  Queue queue(devices[0]);
  printf("backend %s, device %s\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE), alpaka::getName(devices[0]).c_str());
  auto hcases = cms::alpakatools::make_host_buffer<TieCase[]>(queue, nTrials);
  auto houts = cms::alpakatools::make_host_buffer<TieOut[]>(queue, nTrials);
  std::vector<std::vector<md::CandOption>> allOpts(nTrials);
  std::vector<std::vector<float>> allExtras(nTrials);
  for (long t = 0; t < nTrials; ++t) {
    const int nCands = 1 + rng() % 5;
    const int nLevels = 1 + rng() % 6;  // few distinct score values -> many exact ties
    std::vector<md::CandOption> opts;
    int hitId = 100;
    for (int ic = 0; ic < nCands; ++ic) {
      const int nh = rng() % 7;
      for (int j = 0; j < nh; ++j) {
        md::CandOption o;
        std::memset(&o, 0, sizeof(o));
        o.trkIdx = ic;
        o.hitIdx = hitId++;
        o.module = 0;
        o.score = float(rng() % nLevels);
        opts.push_back(o);
      }
      md::CandOption o;
      std::memset(&o, 0, sizeof(o));
      o.trkIdx = ic;
      o.hitIdx = (rng() % 3 == 0) ? -2 : -1;
      o.score = float(rng() % nLevels) - 0.5f * (rng() % 2);
      opts.push_back(o);
    }
    std::shuffle(opts.begin(), opts.end(), rng);  // arbitrary stock input order
    std::vector<float> extras;
    const int ne = rng() % 3;
    for (int e = 0; e < ne; ++e)
      extras.push_back(float(rng() % nLevels));
    std::sort(extras.begin(), extras.end(), [](float a, float b) { return a > b; });  // stock: extras sorted
    TieCase& c = hcases[t];
    std::memset(&c, 0, sizeof(c));
    for (size_t k = 0; k < opts.size(); ++k)
      c.opts[k] = opts[k];
    c.nOpts = opts.size();
    c.nExtra = ne;
    for (int e = 0; e < ne; ++e)
      c.extra[e] = extras[e];
    c.best = float(rng() % nLevels) - 1.f;
    c.nCands = nCands;
    allOpts[t] = opts;
    allExtras[t] = extras;
  }
  auto dcases = cms::alpakatools::make_device_buffer<TieCase[]>(queue, nTrials);
  auto douts = cms::alpakatools::make_device_buffer<TieOut[]>(queue, nTrials);
  alpaka::memcpy(queue, dcases, hcases);
  alpaka::exec<Acc1D>(queue,
                      cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nTrials, 128), 128),
                      KernelTies{},
                      dcases.data(),
                      douts.data(),
                      int(nTrials));
  alpaka::memcpy(queue, houts, douts);
  alpaka::wait(queue);
  for (long t = 0; t < nTrials; ++t) {
    const Out a = stockSelect(allOpts[t], allExtras[t], hcases[t].best, 5);
    const Out b = toOut(houts[t]);
    if (allOpts[t].size() <= 16) {
      ++n16;
      ok16 += (a == b);
    } else {
      ++nbig;
      okbig += (a == b);
    }
  }
  printf("RESULT tie study: n<=16 lists %ld identical %ld ; n>16 lists %ld identical %ld (%.2f%%)\n",
         n16,
         ok16,
         nbig,
         okbig,
         nbig ? 100.0 * okbig / nbig : 100.0);
  return (ok16 == n16) ? 0 : 1;  // n > 16 tie order not reproduced (D-ties)
}
