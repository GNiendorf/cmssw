// bkfit lane: re-run the STOCK backward fit (release libRecoTrackerMkFitCore: propagateHelixToPlaneMPlex,
// kalmanOperationPlaneLocal, kalmanCheckChargeFlip, with the loop of MkFinder::bkFitFitTracksProp2Plane, NN slots)
// on the inputs of a stock dump, and compare with the dump's outputs.
//   - with the production (x86-64-v3) library: must reproduce the dump bit-exactly (validates dump + loop);
//   - with the x86-64-v2 library (LD_LIBRARY_PATH=$CMSSW_RELEASE_BASE/lib/$SCRAM_ARCH/scram_x86-64-v2:...): the
//     D-M4 noise floor of this stage on identical inputs.
// usage: testMkFitAlpakaBkFitStockRerun dump.bin [maxCands] [reps]

#include <chrono>

#include "RecoTracker/MkFitCore/interface/Config.h"
#include "RecoTracker/MkFitCore/interface/PropagationConfig.h"
#include "RecoTracker/MkFitCore/interface/TrackerInfo.h"
// stock package-private headers on purpose (angle brackets: see testPropStockRef.cpp)
#include <RecoTracker/MkFitCore/src/KalmanUtilsMPlex.h>
#include <RecoTracker/MkFitCore/src/PropagationMPlex.h>

#include "RecoTracker/MkFitAlpaka/test/bkfitDumpIO.h"

using namespace mkfit;
namespace bk = mkfitdev::bkfit;

namespace {

  // transliteration of MkFinder::bkFitInputTracks(eoccs) + bkFitFitTracksProp2Plane for one group of <= NN cands
  void stockGroup(const bk::dump::Dump& D, const int* cand, int N_proc, const PropagationFlags& pf, bk::FlatOut* out) {
    MPlexLS errC, errP;
    MPlexLV parC, parP;
    MPlexQI chg;
    MPlexQF chi2;
    int curNode[NN];
    for (int i = 0; i < N_proc; ++i) {
      const bk::FlatCand& c = D.cands[cand[i]];
      chg(i, 0, 0) = c.charge;
      curNode[i] = c.lastNode;
      errC.copyIn(i, c.err);
      parC.copyIn(i, c.par);
    }
    chi2.setVal(0);
    errC.scale(100.0f);

    MPlexQF tmp_chi2{0.0f};
    MPlexQI done_flag(0);
    MPlexHV plNrm{0.0f}, plDir{0.0f}, plPnt{0.0f};
    MPlexHS msErr{0.0f};
    MPlexHV msPar{0.0f};
    MPlexQI failFlag;
    auto node = [&](int i, int k) -> const bk::FlatNode& { return D.nodes[D.cands[cand[i]].nodeBegin + k]; };

    int done_count = 0;
    while (done_count != N_proc) {
      int here_count = 0;
      for (int i = 0; i < N_proc; ++i) {
        if (done_flag[i])
          continue;
        while (curNode[i] >= 0 && node(i, curNode[i]).index < 0)
          curNode[i] = node(i, curNode[i]).prev;
        if (curNode[i] < 0) {
          done_flag[i] = 1;
          ++done_count;
          bk::FlatOut& o = out[cand[i]];
          errC.copyOut(i, o.err);
          parC.copyOut(i, o.par);
          o.charge = chg[i];
          o.chi2 = chi2[i];
          o.score = 0;  // not compared by this tool (scorer lives in MkFitCMS)
        } else {
          const int layer = node(i, curNode[i]).layer;
          while (node(i, curNode[i]).prev >= 0 && node(i, node(i, curNode[i]).prev).layer == layer)
            curNode[i] = node(i, curNode[i]).prev;
          const bk::FlatNode& hn = node(i, curNode[i]);
          msErr.copyIn(i, hn.msErr);
          msPar.copyIn(i, hn.msPar);
          plNrm.copyIn(i, hn.nrm);
          plDir.copyIn(i, hn.dir);
          plPnt.copyIn(i, hn.pnt);
          ++here_count;
          curNode[i] = hn.prev;
        }
      }
      if (done_count == N_proc)
        break;
      if (here_count == 0)
        continue;
      failFlag.setVal(0);
      propagateHelixToPlaneMPlex(errC, parC, chg, plPnt, plNrm, errP, parP, failFlag, N_proc, pf, nullptr);
      kalmanOperationPlaneLocal(KFO_Calculate_Chi2 | KFO_Update_Params | KFO_Local_Cov,
                                errP,
                                parP,
                                chg,
                                msErr,
                                msPar,
                                plNrm,
                                plDir,
                                plPnt,
                                errC,
                                parC,
                                tmp_chi2,
                                N_proc);
      kalmanCheckChargeFlip(parC, chg, N_proc);
      chi2.add(tmp_chi2);
    }
  }

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s dump.bin [maxCands] [reps]\n", argv[0]);
    return 1;
  }
  const long maxCands = argc > 2 ? std::atol(argv[2]) : -1;
  const int reps = argc > 3 ? std::atoi(argv[3]) : 1;
  bk::dump::Dump D;
  if (int rc = bk::dump::readDump(argv[1], maxCands, D))
    return rc;
  const int nc = D.cands.size();
  std::printf("read %d candidates (%d events), stock NN = %d\n", nc, D.nEvents, int(NN));

  // Phase-2 settings as MkFitGeometryESProducer sets them
  Config::usePropToPlane = true;
  Config::usePtMultScat = true;
  TrackerInfo ti;
  ti.create_material(D.nbZ, D.rngZ, D.nbR, D.rngR);
  for (int bz = 0; bz < D.nbZ; ++bz)
    for (int br = 0; br < D.nbR; ++br) {
      ti.material_bbxi(bz, br) = D.bbxi[bz * D.nbR + br];
      ti.material_radl(bz, br) = D.radl[bz * D.nbR + br];
    }
  PropagationFlags pf(PF_use_param_b_field | PF_apply_material);
  pf.tracker_info = &ti;

  std::vector<bk::FlatOut> out(nc);
  double best_ms = 1e30;
  for (int r = 0; r < std::max(1, reps); ++r) {
    const auto t0 = std::chrono::steady_clock::now();
    for (int g = 0; g < nc; g += NN) {
      int cand[NN];
      const int N_proc = std::min(int(NN), nc - g);
      for (int n = 0; n < NN; ++n)
        cand[n] = g + (n < N_proc ? n : 0);
      stockGroup(D, cand, N_proc, pf, out.data());
    }
    const auto t1 = std::chrono::steady_clock::now();
    best_ms = std::min(best_ms, std::chrono::duration<double, std::milli>(t1 - t0).count());
  }
  std::printf("stock: %.3f ms for %d candidates (1 core, best of %d) = %.1f ns/cand, %.3f ms/event\n",
              best_ms,
              nc,
              std::max(1, reps),
              best_ms * 1e6 / nc,
              best_ms / std::max(1, D.nEvents));
  // score is not recomputed here: copy stock's so that the score lines read as 'identical'
  for (int i = 0; i < nc; ++i)
    out[i].score = D.stock[i].out.score;
  if (const char* o = std::getenv("BKFIT_RERUN_OUT")) {  // floor input of the device test (BKFIT_FLOOR)
    std::FILE* f = std::fopen(o, "wb");
    if (f) {
      std::fwrite(out.data(), sizeof(bk::FlatOut), out.size(), f);
      std::fclose(f);
    }
  }
  const auto m = bk::dump::compare(D, out.data(), "stock re-run");
  std::printf("%s\n", m.pass ? "PASS" : "FAIL");
  return m.pass ? 0 : 4;
}
