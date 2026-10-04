// bkfit lane: the device backward fit vs STOCK on real events.
// Input: a dump written by the instrumented private stock MkFitCore (MKFIT_BKFIT_DUMP, hook in MkBuilder::fit_cands,
// see doc/bkfit.txt): per best candidate the backward-fit input (state, counters, compacted HoT chain with hits and
// modules resolved) and stock's output (state, charge, chi2, score). The port runs through the exported entry point
// backwardFitFlat (src/alpaka/bkfit/BkFitLaunch.h); no kernel is instantiated here.
// usage: testMkFitAlpakaBkFit<Backend> dump.bin [maxCands] [reps]
//   env BKFIT_DIFFS=<file>: write per-candidate max relative differences (text) for offline study.
// Exit 0 = PASS: charge agreement >= 99.9%, no finiteness flips beyond 0.01%, >= 99% of candidates within 1e-3, and
// the production entry point within rounding level of the flat path (see below).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <optional>
#include <type_traits>
#include <vector>

#include <alpaka/alpaka.hpp>

#include "FWCore/Utilities/interface/stringize.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/devices.h"
#include "HeterogeneousCore/AlpakaInterface/interface/memory.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/bkfit/BkFitKernel.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/alpaka/CandsDeviceCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/matriplex/MatriplexBackend.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/bkfit/BkFitLaunch.h"
#include "RecoTracker/MkFitAlpaka/test/bkfitDumpIO.h"

using namespace ALPAKA_ACCELERATOR_NAMESPACE;
using namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev;
namespace bk = ::mkfitdev::bkfit;

// Test-only kernel: the scorer on the device (getScoreCand of interface/cands/CandTypes.h, as BkFitKernel calls it
// through bkFitScore) on stock's OUTPUT chi2 / pT; must reproduce stock's score.
struct ScoreCheckKernel {
  template <typename TAcc>
  ALPAKA_FN_ACC void operator()(
      TAcc const& acc, const bk::FlatCand* cands, const bk::FlatOut* stockOut, float* score, int n) const {
    for (int i : cms::alpakatools::uniform_elements(acc, n)) {
      ::mkfitdev::CandBook b{};
      b.nFound = cands[i].nFound;
      b.nMissing = cands[i].nMissing;
      b.nOverlap = cands[i].nOverlap;
      b.nInsideMinusOne = cands[i].nInsideMinusOne;
      b.nTailMinusOne = cands[i].nTailMinusOne;
      b.score = 0.f;
      bkFitScore(b, stockOut[i].chi2, stockOut[i].par[3]);
      score[i] = b.score;
    }
  }
};

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s dump.bin [maxCands] [reps]\n", argv[0]);
    return 1;
  }
  const long maxCands = argc > 2 ? std::atol(argv[2]) : -1;
  const int reps = argc > 3 ? std::atoi(argv[3]) : 1;

  ::mkfitdev::bkfit::dump::Dump D;
  if (int rc = ::mkfitdev::bkfit::dump::readDump(argv[1], maxCands, D))
    return rc;
  const auto& cands = D.cands;
  const auto& nodes = D.nodes;
  const int nbZ = D.nbZ, nbR = D.nbR;
  const float rngZ = D.rngZ, rngR = D.rngR;
  const auto& bbxi = D.bbxi;
  const auto& radl = D.radl;
  const int nEvents = D.nEvents;
  const long nInvalidNodes = D.nInvalidNodes, nSameLayerRuns = D.nSameLayerRuns;
  const int nc = cands.size();
  const int nn = nodes.size();
  std::printf("read %d candidates (%d events), %d nodes (%ld invalid, %ld same-layer hit pairs), material %dx%d\n",
              nc,
              nEvents,
              nn,
              nInvalidNodes,
              nSameLayerRuns,
              nbZ,
              nbR);
  if (nc == 0 || nbZ == 0)
    return 3;

  auto const& devices = cms::alpakatools::devices<Platform>();
  if (devices.empty()) {
    std::printf("No devices available for the %s backend, skipping.\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
    return 0;
  }
  Queue queue(devices[0]);
  std::printf("backend %s, N (tracks per thread) = %d\n", EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE), int(kNN));

  const int nmat = nbZ * nbR;
  auto h_bbxi = cms::alpakatools::make_host_buffer<float[]>(queue, nmat);
  auto h_radl = cms::alpakatools::make_host_buffer<float[]>(queue, nmat);
  std::copy(bbxi.begin(), bbxi.end(), h_bbxi.data());
  std::copy(radl.begin(), radl.end(), h_radl.data());
  auto d_bbxi = cms::alpakatools::make_device_buffer<float[]>(queue, nmat);
  auto d_radl = cms::alpakatools::make_device_buffer<float[]>(queue, nmat);
  alpaka::memcpy(queue, d_bbxi, h_bbxi);
  alpaka::memcpy(queue, d_radl, h_radl);
  // TrackerInfo::create_material: m_mat_fac_z = nBinZ / m_mat_range_z
  ::mkfitdev::MaterialView mv{d_bbxi.data(), d_radl.data(), nbZ, nbR, nbZ / rngZ, nbR / rngR};
  // LST step: backward_fit_pflags = PF_use_param_b_field | PF_apply_material (MkFitGeometryESProducer.cc:625-635)
  ::mkfitdev::prop::PropagationFlags pf(::mkfitdev::prop::PF_use_param_b_field | ::mkfitdev::prop::PF_apply_material,
                                        mv);

  auto h_c = cms::alpakatools::make_host_buffer<bk::FlatCand[]>(queue, nc);
  auto h_n = cms::alpakatools::make_host_buffer<bk::FlatNode[]>(queue, nn);
  auto h_o = cms::alpakatools::make_host_buffer<bk::FlatOut[]>(queue, nc);
  std::copy(cands.begin(), cands.end(), h_c.data());
  std::copy(nodes.begin(), nodes.end(), h_n.data());
  auto d_c = cms::alpakatools::make_device_buffer<bk::FlatCand[]>(queue, nc);
  auto d_n = cms::alpakatools::make_device_buffer<bk::FlatNode[]>(queue, nn);
  auto d_o = cms::alpakatools::make_device_buffer<bk::FlatOut[]>(queue, nc);
  alpaka::memcpy(queue, d_c, h_c);
  alpaka::memcpy(queue, d_n, h_n);
  alpaka::memset(queue, d_o, 0);
  alpaka::wait(queue);

  double best_ms = 1e30;
  for (int r = 0; r < std::max(1, reps); ++r) {
    const auto t0 = std::chrono::steady_clock::now();
    backwardFitFlat(queue, d_c.data(), d_n.data(), d_o.data(), pf, nc);
    alpaka::wait(queue);
    const auto t1 = std::chrono::steady_clock::now();
    best_ms = std::min(best_ms, std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  alpaka::memcpy(queue, h_o, d_o);
  alpaka::wait(queue);
  std::printf("kernel: %.3f ms for %d candidates (best of %d) = %.1f ns/cand, %.3f ms/event\n",
              best_ms,
              nc,
              std::max(1, reps),
              best_ms * 1e6 / nc,
              best_ms / std::max(1, nEvents));

  const auto mPort =
      ::mkfitdev::bkfit::dump::compare(D, h_o.data(), "port " EDM_STRINGIZE(ALPAKA_ACCELERATOR_NAMESPACE));
  bool pass = mPort.pass;
  bool scorerOk = true;
  // The scorer and counters on their own: getScoreCand on stock's OUTPUT chi2 and pT must give stock's score.
  {
    long same = 0;
    for (int i = 0; i < nc; ++i) {
      ::mkfitdev::CandBook b{};
      b.nFound = cands[i].nFound;
      b.nMissing = cands[i].nMissing;
      b.nOverlap = cands[i].nOverlap;
      b.nInsideMinusOne = cands[i].nInsideMinusOne;
      b.nTailMinusOne = cands[i].nTailMinusOne;
      b.chi2 = D.stock[i].out.chi2;
      const float sc = ::mkfitdev::getScoreCand(b, std::abs(1.f / D.stock[i].out.par[3]));
      same += std::memcmp(&sc, &D.stock[i].out.score, 4) == 0;
    }
    std::printf("scorer check, host (stock output chi2/pT -> score): bit-identical %ld/%d\n", same, nc);
    // the same on the device
    auto h_so = cms::alpakatools::make_host_buffer<bk::FlatOut[]>(queue, nc);
    for (int i = 0; i < nc; ++i)
      h_so[i] = D.stock[i].out;
    auto d_so = cms::alpakatools::make_device_buffer<bk::FlatOut[]>(queue, nc);
    auto d_sc = cms::alpakatools::make_device_buffer<float[]>(queue, nc);
    auto h_sc = cms::alpakatools::make_host_buffer<float[]>(queue, nc);
    alpaka::memcpy(queue, d_so, h_so);
    const int thr = cms::alpakatools::requires_single_thread_per_block_v<Acc1D> ? 1 : 128;
    alpaka::exec<Acc1D>(queue,
                        cms::alpakatools::make_workdiv<Acc1D>(cms::alpakatools::divide_up_by(nc, thr), thr),
                        ScoreCheckKernel{},
                        d_c.data(),
                        d_so.data(),
                        d_sc.data(),
                        nc);
    alpaka::memcpy(queue, h_sc, d_sc);
    alpaka::wait(queue);
    long sameDev = 0;
    for (int i = 0; i < nc; ++i)
      sameDev += std::memcmp(&h_sc[i], &D.stock[i].out.score, 4) == 0;
    std::printf("scorer check, device (stock output chi2/pT -> score): bit-identical %ld/%d\n", sameDev, nc);
    scorerOk = sameDev == nc;
  }
  // BKFIT_FLOOR=<rerun outputs (BKFIT_RERUN_OUT)>[:<more>...]: D-M4 decision. Round 7 (D7-a): the floor is the WORST of
  // several stock-vs-stock(v3 -Ofast) arms - v2 -Ofast, v3 without -Ofast (strict, as our GPU build), v3 with our CPU
  // flags (B6) - each re-run on the same dump (r7_fitgap/floors/isolated/bkfit/README.txt); a single file still works.
  std::optional<::mkfitdev::bkfit::dump::Metrics> mFloor;
  if (const char* ffs = std::getenv("BKFIT_FLOOR")) {
    std::string all(ffs);
    for (size_t b = 0; b <= all.size();) {
      const size_t e = std::min(all.find(':', b), all.size());
      const std::string ff = all.substr(b, e - b);
      b = e + 1;
      if (ff.empty())
        continue;
      std::vector<bk::FlatOut> fl(nc);
      std::FILE* f = std::fopen(ff.c_str(), "rb");
      if (!f || std::fread(fl.data(), sizeof(bk::FlatOut), nc, f) != size_t(nc)) {
        std::printf("cannot read %d floor records from %s\n", nc, ff.c_str());
        return 2;
      }
      std::fclose(f);
      const auto m = ::mkfitdev::bkfit::dump::compare(D, fl.data(), ("floor arm " + ff).c_str());
      mFloor = mFloor ? ::mkfitdev::bkfit::dump::worstOf(*mFloor, m) : m;
    }
    pass = ::mkfitdev::bkfit::dump::noWorseThanFloor(mPort, *mFloor, nc);
    std::printf("D-M4 (port vs stock(v3) no worse than the worst floor arm vs stock(v3)): %s\n", pass ? "PASS" : "FAIL");
  } else {
    std::printf("absolute rule (no floor given): %s\n", pass ? "PASS" : "FAIL");
  }

  // ---- production entry point (clone-engine SoAs + EngineHitInputs) on the same inputs, event by event ----
  // The hits/modules are laid out synthetically (every layer 'pixel', row = running node number, module row =
  // layer * 16384 + running number in the layer) so that the production IO reads exactly the numbers of the dump.
  // Criterion (round 6, lane cpu): rounding level, not bit identity. The two entry points are separate instantiations
  // of the same arithmetic; under the CPU backends' -Ofast flags (D5-a) the compiler vectorizes/contracts them
  // differently (CUDA stays bit-identical). The backward fit amplifies rounding (stock v2 vs v3: 1.3% of the
  // candidates beyond 1e-3 relative), so "rounding level" is the D-M4 rule itself: production vs flat must be no
  // worse than stock(x86-64-v2) vs stock(x86-64-v3) (BKFIT_FLOOR); without a floor, the absolute rule.
  long prodSame = 0, prodChecked = 0;
  std::vector<bk::FlatOut> prodOut(h_o.data(), h_o.data() + nc);  // events skipped below compare as identical
  int maxLayer = 0;
  for (const auto& fn : nodes)
    maxLayer = std::max(maxLayer, int(fn.layer));
  const int nLay = maxLayer + 1;
  constexpr int kModPerLayer = 1 << 14;  // detIDinLayer has 14 bits
  for (int c0 = 0; c0 < nc;) {
    int c1 = c0;
    while (c1 < nc && D.stock[c1].callSeq == D.stock[c0].callSeq)
      ++c1;
    const int ns = c1 - c0;
    int hps = 1, nValid = 0;
    for (int c = c0; c < c1; ++c) {
      hps = std::max(hps, int(cands[c].nNodes));
      for (int k = 0; k < cands[c].nNodes; ++k)
        nValid += nodes[cands[c].nodeBegin + k].index >= 0;
    }
    ::mkfitdev::SeedCandsHostCollection hSeeds(queue, ns);
    ::mkfitdev::CandSlotsHostCollection hSlots(queue, ns * ::mkfitdev::kSlotsPerSeed);
    ::mkfitdev::CandHotsHostCollection hHots(queue, ns * hps);
    std::memset(hSeeds.buffer().data(), 0, alpaka::getExtentProduct(hSeeds.buffer()));
    std::memset(hSlots.buffer().data(), 0, alpaka::getExtentProduct(hSlots.buffer()));
    std::memset(hHots.buffer().data(), 0, alpaka::getExtentProduct(hHots.buffer()));
    hSeeds.view().hotsPerSeed() = hps;
    const int nh = std::max(1, nValid);
    std::vector<float> hx(nh), hy(nh), hz(nh), he[6];
    for (auto& v : he)
      v.resize(nh);
    std::vector<uint32_t> hpacked(nh);
    std::vector<::mkfitdev::EngineModule> hmod(size_t(nLay) * kModPerLayer);
    std::vector<::mkfitdev::EngineLayerParams> hlay(nLay);
    std::vector<int> perLayer(nLay, 0);
    for (int l = 0; l < nLay; ++l) {
      hlay[l] = {};
      hlay[l].moduleBegin = l * kModPerLayer;
      hlay[l].isPixel = 1;
    }
    int row = 0;
    bool fits = true;
    for (int c = c0; c < c1; ++c) {
      const int s = c - c0;
      const auto& fc = cands[c];
      hSeeds.view()[s].nCands() = 1;
      hSeeds.view()[s].curBuf() = 0;
      hSeeds.view()[s].nHots() = fc.nNodes;
      auto sl = hSlots.view()[::mkfitdev::candSlotRow(s, 0, 0)];
      ::mkfitdev::CandState& st = sl.state();
      std::copy(fc.par, fc.par + 6, st.par);
      std::copy(fc.err, fc.err + 21, st.err);
      st.charge = fc.charge;
      ::mkfitdev::CandBook& bk = sl.book();
      bk.lastHitIdx = fc.lastNode;
      bk.nFound = fc.nFound;
      bk.nMissing = fc.nMissing;
      bk.nOverlap = fc.nOverlap;
      bk.nInsideMinusOne = fc.nInsideMinusOne;
      bk.nTailMinusOne = fc.nTailMinusOne;
      for (int k = 0; k < fc.nNodes; ++k) {
        const auto& fn = nodes[fc.nodeBegin + k];
        ::mkfitdev::HoTNode hn{fn.index, fn.layer, 0.f, fn.prev};
        if (fn.index >= 0) {
          const int m = perLayer[fn.layer]++;
          fits &= m < kModPerLayer;
          hn.index = row;
          hx[row] = fn.msPar[0];
          hy[row] = fn.msPar[1];
          hz[row] = fn.msPar[2];
          for (int e = 0; e < 6; ++e)
            he[e][row] = fn.msErr[e];
          hpacked[row] = ::mkfitdev::hitpack::pack(m & (kModPerLayer - 1), 0, 0, 0);
          auto& mod = hmod[size_t(fn.layer) * kModPerLayer + (m & (kModPerLayer - 1))];
          for (int i = 0; i < 3; ++i) {
            mod.nrm[i] = fn.nrm[i];
            mod.dir[i] = fn.dir[i];
            mod.pnt[i] = fn.pnt[i];
          }
          ++row;
        }
        hHots.view()[::mkfitdev::hotRow(s, k, hps)].node() = hn;
      }
    }
    if (!fits) {
      std::printf("production IO test: more than %d hits on one layer in event %d, skipped\n",
                  kModPerLayer,
                  D.stock[c0].callSeq);
      c0 = c1;
      continue;
    }
    auto toDev = [&](const auto& v) {
      using T = typename std::decay_t<decltype(v)>::value_type;
      auto h = cms::alpakatools::make_host_buffer<T[]>(queue, v.size());
      std::copy(v.begin(), v.end(), h.data());
      auto d = cms::alpakatools::make_device_buffer<T[]>(queue, v.size());
      alpaka::memcpy(queue, d, h);
      alpaka::wait(queue);
      return d;
    };
    auto dx = toDev(hx), dy = toDev(hy), dz = toDev(hz), d0 = toDev(he[0]), d1 = toDev(he[1]), d2 = toDev(he[2]),
         d3 = toDev(he[3]), d4 = toDev(he[4]), d5 = toDev(he[5]);
    auto dp = toDev(hpacked);
    auto dm = toDev(hmod);
    auto dl = toDev(hlay);
    ::mkfitdev::EngineHitInputs in{dx.data(),
                                   dy.data(),
                                   dz.data(),
                                   d0.data(),
                                   d1.data(),
                                   d2.data(),
                                   d3.data(),
                                   d4.data(),
                                   d5.data(),
                                   dp.data(),
                                   0u,
                                   dm.data(),
                                   dl.data()};
    SeedCandsDeviceCollection dSeeds(queue, ns);
    CandSlotsDeviceCollection dSlots(queue, ns * ::mkfitdev::kSlotsPerSeed);
    CandHotsDeviceCollection dHots(queue, ns * hps);
    alpaka::memcpy(queue, dSeeds.buffer(), hSeeds.buffer());
    alpaka::memcpy(queue, dSlots.buffer(), hSlots.buffer());
    alpaka::memcpy(queue, dHots.buffer(), hHots.buffer());
    backwardFit(queue, dSeeds.const_view(), dSlots.view(), dHots.view(), in, pf, ns, {});
    alpaka::memcpy(queue, hSlots.buffer(), dSlots.buffer());
    alpaka::wait(queue);
    for (int c = c0; c < c1; ++c) {
      const auto sl = hSlots.const_view()[::mkfitdev::candSlotRow(c - c0, 0, 0)];
      const bk::FlatOut& f = h_o[c];
      const bool same = std::memcmp(sl.state().par, f.par, sizeof(f.par)) == 0 &&
                        std::memcmp(sl.state().err, f.err, sizeof(f.err)) == 0 && sl.state().charge == f.charge &&
                        std::memcmp(&sl.book().chi2, &f.chi2, 4) == 0 &&
                        std::memcmp(&sl.book().score, &f.score, 4) == 0;
      prodSame += same;
      ++prodChecked;
      bk::FlatOut& p = prodOut[c];
      std::copy(sl.state().par, sl.state().par + 6, p.par);
      std::copy(sl.state().err, sl.state().err + 21, p.err);
      p.charge = sl.state().charge;
      p.chi2 = sl.book().chi2;
      p.score = sl.book().score;
    }
    c0 = c1;
  }
  std::printf("production entry point (engine SoAs + EngineHitInputs) vs flat path: bit-identical %ld/%ld\n",
              prodSame,
              prodChecked);
  bool prodOk = prodSame == prodChecked;
  if (!prodOk) {
    auto Dflat = D;  // the flat path's outputs as the reference
    for (int c = 0; c < nc; ++c)
      Dflat.stock[c].out = h_o[c];
    const auto mProd =
        ::mkfitdev::bkfit::dump::compare(Dflat, prodOut.data(), "production entry point (dump = flat path)");
    prodOk = mFloor ? ::mkfitdev::bkfit::dump::noWorseThanFloor(mProd, *mFloor, nc) : mProd.pass;
    std::printf("production vs flat, rounding level (%s): %s\n",
                mFloor ? "no worse than stock(v2) vs stock(v3)" : "absolute rule",
                prodOk ? "PASS" : "FAIL");
  }
  if (!scorerOk)
    std::printf("NOTE: the device scorer does not reproduce stock bit for bit (FMA contraction, see doc/bkfit.txt)\n");
  return (pass && prodOk) ? 0 : 4;
}
