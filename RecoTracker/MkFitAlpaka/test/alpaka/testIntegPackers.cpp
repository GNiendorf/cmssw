// integ: MPlex <-> SoA packers (src/alpaka/Packers.h) and the PropagationFlags adapter vs STOCK mkFit.
// Stock side: mkfit::Hit / mkfit::Track objects copied into STOCK Matriplex (vendored verbatim, mplex_stock/) exactly as
// MkFinder/MkFitter copy_in do; port side: the same objects written into the port SoAs with the package's own
// converters (TrackSoAMkFitConversion.h; HitSoA convention of the hits lane) and loaded by the packers.
// Every Matriplex element must be bitwise equal, for every slot n of an N = NN (stock CPU width) Matriplex.
// Also: round trips (load -> store) are identities, the module-plane packer follows packModuleNormDirPnt, and the
// PropagationFlags adapter maps each PropagationConfig member to the same-named flags. Exit 0 = PASS.

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "mplex_stock/Matrix.h"  // verbatim stock RecoTracker/MkFitCore/src/Matrix.h (mplex_stock/README)
#include "RecoTracker/MkFitCore/interface/Hit.h"
#include "RecoTracker/MkFitCore/interface/Track.h"

#include "DataFormats/Portable/interface/PortableHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESLayouts.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsHostCollections.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAHostCollection.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoAMkFitConversion.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/Packers.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/PropagationFlagsAdapter.h"

namespace {
  constexpr int NS = mkfit::NN;  // stock Matriplex width of this build
  int nFail = 0;
  long nChecked = 0;

  template <typename A, typename B>
  void cmpArrays(const char* what, const A* a, const B* b, int n) {
    for (int i = 0; i < n; ++i) {
      ++nChecked;
      if (std::memcmp(&a[i], &b[i], sizeof(A)) != 0) {
        if (nFail < 20)
          std::printf("FAIL %s element %d: stock %.9g port %.9g\n", what, i, double(a[i]), double(b[i]));
        ++nFail;
      }
    }
  }
}  // namespace

int main() {
  static_assert(sizeof(float) == 4);
  std::mt19937 rng(12345);
  std::uniform_real_distribution<float> u(-50.f, 50.f), ue(1e-6f, 1e-2f);
  constexpr int nObj = 4096;

  // ---------------- hits: MkFinder m_msErr.copyIn(n, hit.errArray()); m_msPar.copyIn(n, hit.posArray())
  std::vector<mkfit::Hit> hits;
  ::mkfitdev::HitsHostCollection hitsH(nObj);
  auto hv = hitsH.view();
  for (int i = 0; i < nObj; ++i) {
    mkfit::SVector3 p(u(rng), u(rng), u(rng));
    mkfit::SMatrixSym33 e;
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c <= r; ++c)
        e(r, c) = (r == c) ? ue(rng) : 0.1f * ue(rng);
    hits.emplace_back(p, e);
    const mkfit::Hit& h = hits.back();
    // HitSoA convention (hits lane, HitSoA.h): position as Hit, e.. = SMatrixSym33 packed (00, 10, 11, 20, 21, 22)
    hv[i].x() = h.x();
    hv[i].y() = h.y();
    hv[i].z() = h.z();
    hv[i].e00() = h.error()(0, 0);
    hv[i].e10() = h.error()(1, 0);
    hv[i].e11() = h.error()(1, 1);
    hv[i].e20() = h.error()(2, 0);
    hv[i].e21() = h.error()(2, 1);
    hv[i].e22() = h.error()(2, 2);
  }
  for (int b = 0; b < nObj; b += NS) {
    mkfit::MPlexHS sErr;
    mkfit::MPlexHV sPar;
    ::mkfitdev::MPlexHS<NS> pErr;
    ::mkfitdev::MPlexHV<NS> pPar;
    for (int n = 0; n < NS; ++n) {
      sErr.copyIn(n, hits[b + n].errArray());
      sPar.copyIn(n, hits[b + n].posArray());
      ::mkfitdev::pack::loadHit<NS>(hitsH.const_view(), b + n, n, pErr, pPar);
    }
    cmpArrays("hit err", sErr.fArray, pErr.fArray, 6 * NS);
    cmpArrays("hit par", sPar.fArray, pPar.fArray, 3 * NS);
  }

  // ---------------- tracks: MkFinder/MkFitter copy_in(Track): m_Err, m_Par, m_Chg, m_Chi2
  std::vector<mkfit::Track> trks;
  ::mkfitdev::TrackSoAHostCollection trkH(nObj), trkOut(nObj);
  for (int i = 0; i < nObj; ++i) {
    mkfit::SMatrixSym66 e;
    for (int r = 0; r < 6; ++r)
      for (int c = 0; c <= r; ++c)
        e(r, c) = (r == c) ? ue(rng) : 0.1f * ue(rng);
    mkfit::SVector3 pos(u(rng), u(rng), u(rng)), mom(u(rng), u(rng), u(rng));
    trks.emplace_back(i % 2 ? 1 : -1, pos, mom, e, ue(rng) * 1000.f);
    ::mkfitdev::trackToSoA(trks.back(), trkH.view(), i);
  }
  for (int b = 0; b < nObj; b += NS) {
    mkfit::MPlexLS sErr;
    mkfit::MPlexLV sPar;
    mkfit::MPlexQI sChg;
    mkfit::MPlexQF sChi2;
    ::mkfitdev::MPlexLS<NS> pErr;
    ::mkfitdev::MPlexLV<NS> pPar;
    ::mkfitdev::MPlexQI<NS> pChg;
    ::mkfitdev::MPlexQF<NS> pChi2;
    for (int n = 0; n < NS; ++n) {
      const mkfit::Track& t = trks[b + n];
      sErr.copyIn(n, t.errors().Array());
      sPar.copyIn(n, t.parameters().Array());
      sChg(n, 0, 0) = t.charge();
      sChi2(n, 0, 0) = t.chi2();
      ::mkfitdev::pack::loadTrack<NS>(trkH.const_view(), b + n, n, pErr, pPar, pChg, pChi2);
    }
    cmpArrays("trk err", sErr.fArray, pErr.fArray, 21 * NS);
    cmpArrays("trk par", sPar.fArray, pPar.fArray, 6 * NS);
    cmpArrays("trk chg", sChg.fArray, pChg.fArray, NS);
    cmpArrays("trk chi2", sChi2.fArray, pChi2.fArray, NS);
    // round trip: store into a second SoA, rows must equal the input rows
    for (int n = 0; n < NS; ++n)
      ::mkfitdev::pack::storeTrack<NS>(pErr, pPar, pChg, pChi2, n, trkOut.view(), b + n);
    for (int n = 0; n < NS; ++n) {
      cmpArrays("trk rt err", trkH.const_view()[b + n].errors().v, trkOut.const_view()[b + n].errors().v, 21);
      cmpArrays("trk rt par", trkH.const_view()[b + n].params().v, trkOut.const_view()[b + n].params().v, 6);
    }

    // ---------------- candidates: copy_in(TrackCand) of the same state must give the same Matriplex contents
    ::mkfitdev::MPlexLS<NS> cErr;
    ::mkfitdev::MPlexLV<NS> cPar;
    ::mkfitdev::MPlexQI<NS> cChg;
    ::mkfitdev::CandState cs[NS], back[NS];
    for (int n = 0; n < NS; ++n) {
      const mkfit::Track& t = trks[b + n];
      std::memcpy(cs[n].err, t.errors().Array(), sizeof(cs[n].err));
      std::memcpy(cs[n].par, t.parameters().Array(), sizeof(cs[n].par));
      cs[n].charge = t.charge();
      ::mkfitdev::pack::loadCandState<NS>(cs[n], n, cErr, cPar, cChg);
    }
    cmpArrays("cand err", sErr.fArray, cErr.fArray, 21 * NS);
    cmpArrays("cand par", sPar.fArray, cPar.fArray, 6 * NS);
    cmpArrays("cand chg", sChg.fArray, cChg.fArray, NS);
    for (int n = 0; n < NS; ++n) {
      back[n] = cs[n];
      std::memset(back[n].err, 0, sizeof(back[n].err));
      std::memset(back[n].par, 0, sizeof(back[n].par));
      back[n].charge = 0;
      ::mkfitdev::pack::storeCandState<NS>(cErr, cPar, cChg, n, back[n]);
      cmpArrays("cand rt err", cs[n].err, back[n].err, 21);
      cmpArrays("cand rt par", cs[n].par, back[n].par, 6);
      cmpArrays("cand rt chg", &cs[n].charge, &back[n].charge, 1);
    }
  }

  // ---------------- module plane (packModuleNormDirPnt: norm = zdir, dir = xdir, pnt = pos of module_info(sid))
  {
    constexpr int nL = 2, nM = 7;
    PortableHostCollection<::mkfitdev::LayerInfoSoA> lay(nL);
    PortableHostCollection<::mkfitdev::ModuleInfoSoA> mod(nM);
    lay.view()[0].module_begin() = 0;
    lay.view()[1].module_begin() = 3;
    for (int m = 0; m < nM; ++m) {
      auto r = mod.view()[m];
      r.zdir_x() = 1 + m, r.zdir_y() = 2 + m, r.zdir_z() = 3 + m;
      r.xdir_x() = 11 + m, r.xdir_y() = 12 + m, r.xdir_z() = 13 + m;
      r.pos_x() = 21 + m, r.pos_y() = 22 + m, r.pos_z() = 23 + m;
    }
    ::mkfitdev::ESView es{};
    es.layers = lay.const_view();
    es.modules = mod.const_view();
    ::mkfitdev::MPlexHV<NS> nrm, dir, pnt;
    for (int sid = 0; sid < 4; ++sid) {
      const int n = sid % NS;
      ::mkfitdev::pack::loadModulePlane<NS>(es, 1, ::mkfitdev::hitpack::pack(sid, 7, 3, 2), n, nrm, dir, pnt);
      const int m = 3 + sid;
      const float expN[3] = {float(1 + m), float(2 + m), float(3 + m)};
      const float expD[3] = {float(11 + m), float(12 + m), float(13 + m)};
      const float expP[3] = {float(21 + m), float(22 + m), float(23 + m)};
      for (int i = 0; i < 3; ++i) {
        cmpArrays("plane norm", &expN[i], &nrm(n, i, 0), 1);
        cmpArrays("plane dir", &expD[i], &dir(n, i, 0), 1);
        cmpArrays("plane pnt", &expP[i], &pnt(n, i, 0), 1);
      }
      ::mkfitdev::pack::zeroModulePlane<NS>(n, nrm, dir, pnt);
      const float z = 0.f;
      for (int i = 0; i < 3; ++i) {
        cmpArrays("plane zero", &z, &nrm(n, i, 0), 1);
        cmpArrays("plane zero", &z, &dir(n, i, 0), 1);
        cmpArrays("plane zero", &z, &pnt(n, i, 0), 1);
      }
    }
  }

  // ---------------- PropagationFlags adapter: each stage picks its own PropagationConfig member
  {
    using namespace ::mkfitdev::prop;
    ::mkfitdev::ESConfig cfg{};
    auto set = [](::mkfitdev::PropFlags& f, int k) {
      f.use_param_b_field = k & 1;
      f.apply_material = k & 2;
      f.copy_input_state_on_fail = k & 4;
    };
    set(cfg.prop_config.finding_inter_layer_pflags, 1);
    set(cfg.prop_config.finding_intra_layer_pflags, 2);
    set(cfg.prop_config.backward_fit_pflags, 3);
    set(cfg.prop_config.forward_fit_pflags, 4);
    set(cfg.prop_config.seed_fit_pflags, 5);
    set(cfg.prop_config.pca_prop_pflags, 6);
    float bb[2] = {1.f, 2.f}, rl[2] = {3.f, 4.f};
    ::mkfitdev::ESView es{};
    es.config = &cfg;
    es.material = ::mkfitdev::MaterialView{bb, rl, 1, 2, 0.5f, 0.25f};
    const PropStage st[6] = {PropStage::FindingInterLayer,
                             PropStage::FindingIntraLayer,
                             PropStage::BackwardFit,
                             PropStage::ForwardFit,
                             PropStage::SeedFit,
                             PropStage::PcaProp};
    for (int s = 0; s < 6; ++s) {
      const PropagationFlags pf = propagationFlags(es, st[s]);
      const int k = s + 1;
      const bool ok = pf.use_param_b_field == bool(k & 1) && pf.apply_material == bool(k & 2) &&
                      pf.copy_input_state_on_fail == bool(k & 4) && pf.material.bbxi == bb && pf.material.radl == rl &&
                      pf.material.nBinsZ == 1 && pf.material.nBinsR == 2 && pf.material.facZ == 0.5f &&
                      pf.material.facR == 0.25f;
      ++nChecked;
      if (!ok) {
        std::printf("FAIL PropagationFlags adapter stage %d\n", s);
        ++nFail;
      }
    }
  }

  std::printf("testIntegPackers: N=%d, %ld element checks, %d failures -> %s\n",
              NS,
              nChecked,
              nFail,
              nFail ? "FAIL" : "PASS");
  return nFail ? 1 : 0;
}
