#ifndef RecoTracker_MkFitAlpaka_src_alpaka_select_SeedOverlapKernel_h
#define RecoTracker_MkFitAlpaka_src_alpaka_select_SeedOverlapKernel_h

// Overlap hits on the outer-tracker layers a seed already spans. The forward search starts after the seed's last hit,
// so the clone engine never visits the seed's own layers, and a seed that carries one hit per layer (the LST seeds)
// misses the second-module (overlap) hit there that the building adds on every layer it searches itself.
// Thread per seed row of a seed with a pixel track (pT3, pT5): the pixel seed's state (on its last hit, the seed hit
// before the first outer-tracker one) is walked outward over the seed's outer-tracker hits (Kalman propagation with the
// inter-layer flags and update on each hit). On every outer-tracker seed layer with a single seed hit, the hits of that
// layer on other modules inside a window of the updated state (selectHitIndicesV2's, with a phi tolerance from the
// chi2 cut) are tested as the building tests a hit (cluster size, chi2 from the state to the hit's module plane with
// the intra-layer flags under the dynamic chi2 cut, strip q compatibility); the best one is the overlap hit. Overlap
// hits enter the seed's hit list in front of their seed hit (the last hit, the pickup layer and the seed state stay as
// they are); they are not used in the seed state. maxOTHits >= 0: only the first maxOTHits outer-tracker seed hits.

#include <cmath>
#include <cstdint>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandEngineTypes.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESView.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/LayerOfHitsAccess.h"
#include "RecoTracker/MkFitAlpaka/interface/math/MathUtils.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/SeedSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/seeds/alpaka/LstSeedFit.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/Packers.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/PropagationFlagsAdapter.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/engine/EngineHitChecks.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/prop/KalmanUtilsMPlex.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/select/SelectHitIndicesV2.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::select {

  class KernelSeedOverlapHits {
  public:
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ::mkfitdev::SeedSoAView seeds,
                                  int32_t n,
                                  ::mkfitdev::ESView es,
                                  ::mkfitdev::LayerSoA::ConstView eohLayers,
                                  ::mkfitdev::BinnedHitSoA::ConstView eohBinned,
                                  ::mkfitdev::BinSoA::ConstView eohBins,
                                  ::mkfitdev::HitSoA::ConstView hits,
                                  ::mkfitdev::EngineHitInputs in,
                                  ::mkfitdev::EngineIterParams ip,
                                  ::mkfitdev::lstseeds::PixelSeedRows pix,
                                  int maxOTHits) const {
      namespace pack = ::mkfitdev::pack;
      namespace hitpack = ::mkfitdev::hitpack;
      using ::mkfitdev::kMaxSeedHits;
      const auto pfInter = ::mkfitdev::prop::propagationFlags(es, ::mkfitdev::prop::PropStage::FindingInterLayer);
      const auto pfIntra = ::mkfitdev::prop::propagationFlags(es, ::mkfitdev::prop::PropStage::FindingIntraLayer);
      if (pix.pixIdx == nullptr)
        return;
      for (int32_t s : cms::alpakatools::uniform_elements(acc, n)) {
        const int32_t e = pix.pixIdx[s];
        const int nh = seeds[s].nHits();
        if (e < 0 || e >= pix.nStates || pix.states[e].charge() == 0 || nh < 2 || seeds[s].charge() == 0)
          continue;
        const ::mkfitdev::HitOnTrack* hot = seeds[s].hits().hot;
        int firstOT = -1;
        for (int k = 0; k < nh && firstOT < 0; ++k)
          if (hot[k].index >= 0 && !in.layers[hot[k].layer].isPixel)
            firstOT = k;
        if (firstOT < 1)
          continue;
        // a seed whose device fit failed (non-finite state) is removed later: left as it is
        bool ok = true;
        for (int k = 0; k < 21; ++k)
          ok = ok && ::mkfitdev::isFinite(seeds[s].errors().v[k]);
        MPlexLS<1> err;
        MPlexLV<1> par;
        MPlexQI<1> chg;
        for (int k = 0; k < 6; ++k) {
          par.At(0, k, 0) = pix.states[e].params().v[k];
          ok = ok && ::mkfitdev::isFinite(par.At(0, k, 0));
        }
        for (int k = 0; k < 21; ++k) {
          err.fArray[k] = pix.states[e].errors().v[k];
          ok = ok && ::mkfitdev::isFinite(err.fArray[k]);
        }
        if (!ok)
          continue;
        chg.At(0, 0, 0) = pix.states[e].charge();

        int32_t ovl[kMaxSeedHits];
        for (int k = 0; k < kMaxSeedHits; ++k)
          ovl[k] = -1;
        int nOvl = 0;
        const int endOT = (maxOTHits >= 0 && firstOT + maxOTHits < nh) ? firstOT + maxOTHits : nh;
        for (int k = firstOT; k < endOT; ++k) {
          const ::mkfitdev::HitOnTrack h = hot[k];
          if (h.index < 0)
            continue;
          const int layer = h.layer;
          const int row = pack::hitRow(in, layer, h.index);
          MPlexHS<1> msErr;
          MPlexHV<1> msPar, nrm, dir, pnt;
          pack::loadHit<1>(in, row, 0, msErr, msPar);
          pack::loadModulePlane<1>(in, layer, row, 0, nrm, dir, pnt);
          {
            MPlexLS<1> outErr;
            MPlexLV<1> outPar;
            MPlexQI<1> fail;
            fail.At(0, 0, 0) = 0;
            kalmanPropagateAndUpdatePlane(
                err, par, chg, msErr, msPar, nrm, dir, pnt, outErr, outPar, fail, 1, pfInter, true);
            bool good = fail.At(0, 0, 0) == 0;
            for (int i = 0; i < 6; ++i)
              good = good && ::mkfitdev::isFinite(outPar.At(0, i, 0));
            if (!good)
              break;
            err = outErr;
            par = outPar;
          }
          const ::mkfitdev::EngineLayerParams lp = in.layers[layer];
          if (lp.isPixel)
            continue;
          bool single = true;
          for (int j = 0; j < nh; ++j)
            single = single && (j == k || hot[j].layer != layer || hot[j].index < 0);
          if (!single)
            continue;
          if (!(std::abs(1.f / par.At(0, 3, 0)) > ip.pTCutOverlap))
            continue;

          ::mkfitdev::CandPropState ps;
          for (int i = 0; i < 6; ++i)
            ps.par[i] = par.At(0, i, 0);
          for (int i = 0; i < 21; ++i)
            ps.err[i] = err.fArray[i];
          ps.charge = chg.At(0, 0, 0);
          const float maxChi2 = engineDynamicChi2Cut(acc, lp, ps, ip.chi2CutMin);

          // hit window: the bin range and the per-hit mini propagation of selectHitIndicesV2, with the phi tolerance
          // set by the chi2 cut (sqrt of the cut times the phi sigma of state and seed hit together) in place of the
          // building's fixed margins: a hit whose phi residual alone exceeds it cannot pass the cut
          const auto li = es.layers[layer];
          const bool isBarrel = lp.isBarrel;
          const float lim1 = isBarrel ? li.rin() : li.zmin();
          const float lim2 = isBarrel ? li.rout() : li.zmax();
          const int charge = chg.At(0, 0, 0);
          BinsWindow<1> w;
          w.compute(par.fArray, err.fArray, &charge, isBarrel, &lim1, &lim2);
          auto phiVar = [](float x, float y, float exx, float exy, float eyy) {
            const float r2 = x * x + y * y;
            return (y * y * exx - 2.f * x * y * exy + x * x * eyy) / (r2 * r2);
          };
          const float varState =
              phiVar(par.At(0, 0, 0), par.At(0, 1, 0), err.At(0, 0, 0), err.At(0, 1, 0), err.At(0, 1, 1));
          const float varHit =
              phiVar(msPar.At(0, 0, 0), msPar.At(0, 1, 0), msErr.At(0, 0, 0), msErr.At(0, 1, 0), msErr.At(0, 1, 1));
          const float dphiCut = std::sqrt(maxChi2 * (varState + varHit));
          const ::mkfitdev::LayerOfHitsAccess L{eohLayers, eohBinned, eohBins, layer};
          Bins B;
          B.dphi_track = w.dphi_track[0];
          B.dq_track = w.dq_track[0];
          B.q_c = w.q_c[0];
          B.p1 = L.phiBinChecked(w.pmin[0] - dphiCut);
          B.p2 = L.phiMaskApply(L.phiBin(w.pmax[0] + dphiCut) + 1);
          B.q0 = L.qBinChecked(B.q_c);
          B.q1 = L.qBinChecked(w.qmin[0] - B.dq_track - Q_BIN_EXTRA_FAC * 0.5f * li.q_bin());
          B.q2 = L.qBinChecked(w.qmax[0] + B.dq_track + Q_BIN_EXTRA_FAC * 0.5f * li.q_bin()) + 1;
          SelResult res;
          if (!selectWsr(0, layer, isBarrel, es, B, res))
            continue;
          // the hits on other modules with the smallest phi residuals, kept as selectHitIndicesV2 keeps them; a hit
          // outside the window's phi range, or on the seed hit's module, is skipped before its propagation
          const uint32_t module0 = hitpack::detIDinLayer(in.packed[row]);
          const float phiSpan = ::mkfitdev::squashPhiGeneral(w.pmax[0] - w.pmin[0]);
          const float phiMid = w.pmin[0] + 0.5f * phiSpan;
          const float phiHalf = 0.5f * std::abs(phiSpan) + dphiCut;
          const mp::InitialState mpIs(par.fArray, charge);
          SelHeap heap;
          for (bidx_t qi = B.q1; qi != B.q2; ++qi) {
            for (bidx_t pi = B.p1; pi != B.p2; pi = L.phiMaskApply(pi + 1)) {
              const uint32_t content = L.binContent(pi, qi);
              const uint32_t hbeg = ::mkfitdev::binFirst(content);
              const uint32_t hend = hbeg + ::mkfitdev::binCount(content);
              for (uint32_t hb = hbeg; hb < hend; ++hb) {
                if (!(::mkfitdev::cdist(std::abs(L.hit_phi(hb) - phiMid)) < phiHalf))
                  continue;
                const int hbRow = pack::hitRow(in, layer, int(L.getOriginalHitIndex(hb)));
                if (hitpack::detIDinLayer(in.packed[hbRow]) == module0)
                  continue;
                float score;
                unsigned int hiOrig;
                if (!selectHitScore(
                        mpIs, isBarrel, li.module_begin(), B.dq_track, dphiCut, es, L, hits, hb, score, hiOrig))
                  continue;
                if (heap.n < NEW_MAX_HIT) {
                  heap.push({score, hiOrig});
                } else if (score < heap.top().score) {
                  heap.pop();
                  heap.push({score, hiOrig});
                }
              }
            }
          }
          float bestChi2 = maxChi2;
          int best = -1;
          for (int ih = 0; ih < heap.n; ++ih) {
            const int hi = int(heap.a[ih].hit_index);
            const int row2 = pack::hitRow(in, layer, hi);
            const uint32_t packed = in.packed[row2];
            if (int(hitpack::spanRows(packed)) >= ip.maxClusterSize)
              continue;
            MPlexHS<1> msErr2;
            MPlexHV<1> msPar2, nrm2, dir2, pnt2;
            pack::loadHit<1>(in, row2, 0, msErr2, msPar2);
            pack::loadModulePlane<1>(in, layer, row2, 0, nrm2, dir2, pnt2);
            MPlexQF<1> outChi2;
            MPlexLV<1> propPar;
            MPlexQI<1> fail;
            outChi2.At(0, 0, 0) = 0.f;
            fail.At(0, 0, 0) = 0;
            kalmanPropagateAndComputeChi2Plane(
                err, par, chg, msErr2, msPar2, nrm2, dir2, pnt2, outChi2, propPar, fail, 1, pfIntra, true);
            const float chi2 = std::abs(outChi2.At(0, 0, 0));
            if (fail.At(0, 0, 0) != 0 || !(chi2 < bestChi2))
              continue;
            if (!engineIsStripQCompatible(acc,
                                          lp.isBarrel,
                                          ps.err,
                                          propPar.At(0, 0, 0),
                                          propPar.At(0, 1, 0),
                                          propPar.At(0, 2, 0),
                                          msErr2,
                                          msPar2))
              continue;
            bestChi2 = chi2;
            best = hi;
          }
          if (best >= 0) {
            ovl[k] = best;
            ++nOvl;
          }
        }
        if (nOvl == 0)
          continue;

        // inner layers first while the hit list has room; the seed hits are always kept
        ::mkfitdev::HitOnTrack out[kMaxSeedHits];
        int room = kMaxSeedHits - nh;
        int m = 0;
        for (int k = 0; k < nh; ++k) {
          if (ovl[k] >= 0 && room > 0) {
            out[m].index = ovl[k];
            out[m].layer = hot[k].layer;
            ++m;
            --room;
          }
          out[m++] = hot[k];
        }
        for (int k = 0; k < m; ++k)
          seeds[s].hits().hot[k] = out[k];
        seeds[s].nHits() = int16_t(m);
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::select

#endif
