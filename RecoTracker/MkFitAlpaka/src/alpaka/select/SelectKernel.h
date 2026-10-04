#ifndef RecoTracker_MkFitAlpaka_src_alpaka_select_SelectKernel_h
#define RecoTracker_MkFitAlpaka_src_alpaka_select_SelectKernel_h

// K2 of the clone engine (doc/clone_engine_design.txt): thread per listed candidate.
//   1. inter-layer propagation of the candidate state to the layer (stock MkBase::propagateTracksToR / ToZ with
//      prop_config.finding_inter_layer_pflags, after clearFailFlag)            -> PropStateSoA
//   2. MkFinder::selectHitIndicesV2 on the propagated state (iI = iP)          -> SelHitsSoA (hits, wsr, in_gap)
//   3. MkBuilder::find_tracks_handle_missed_layers decision                    -> SelHitsSoA.extra, wsr rewrite
// GPU backends: one candidate per thread (N = 1). CPU backends run step 1 on kNN = 8 candidates per Matriplex (stock
// NN): the Matriplex<8> R/Z propagators are bit-identical to stock (prop test), whatever the caller's inlining.

#include <limits>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandsSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/es/ESView.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/EventOfHitsSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/HitSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/LayerOfHitsAccess.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/PropagationFlagsAdapter.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/prop/PropagationMPlex.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/select/SelectHitIndicesV2.h"
#include "RecoTracker/MkFitAlpaka/interface/cands/CandEngineTypes.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/select/SelectSoA.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::select {

  // optional per-candidate diagnostics (test only): the stock Bins values
  struct SelDiag {
    float qc, dqTrack, dphiTrack;
    int32_t q0, q1, q2, p1, p2;
    float sp1[3], sp2[3];
    float qmin, qmax;
  };

  // es::PropFlags + material -> prop PropagationFlags: the shared adapter (review H5)
  ALPAKA_FN_HOST_ACC ALPAKA_FN_INLINE ::mkfitdev::prop::PropagationFlags interLayerFlags(const ::mkfitdev::ESView& es) {
    return ::mkfitdev::prop::propagationFlags(es, ::mkfitdev::prop::PropStage::FindingInterLayer);
  }

  struct KernelSelectHits {
    // CPU step 1: propagate nb <= N listed candidates of one kind (barrel: to R, endcap: to Z) in one width-N
    // Matriplex (rows past the count repeat its first row and are not stored) -> PropStateSoA.
    template <idx_t N>
    ALPAKA_FN_ACC ALPAKA_FN_INLINE static void propagateBatch(const int (&tb)[N],
                                                              int nb,
                                                              bool barrel,
                                                              ::mkfitdev::CandSlotsSoA::ConstView slots,
                                                              ::mkfitdev::SelListSoA::ConstView list,
                                                              const ::mkfitdev::ESView& es,
                                                              ::mkfitdev::PropStateSoA::View props,
                                                              const ::mkfitdev::prop::PropagationFlags& pflags) {
      int rows[N];
      int nr = 0;
      for (int j = 0; j < nb; ++j)
        if (es.isBarrel(list[tb[j]].layer()) == barrel)
          rows[nr++] = tb[j];
      if (nr == 0)
        return;
      MPlexLS<N> inErr, outErr;
      MPlexLV<N> inPar, outPar;
      MPlexQI<N> chg, fail;
      MPlexQF<N> msTarget;
      for (int n = 0; n < N; ++n) {
        const int i = rows[n < nr ? n : 0];
        const ::mkfitdev::CandState cs = slots[list[i].row()].state();
        for (int k = 0; k < 6; ++k)
          inPar(n, k, 0) = cs.par[k];
        for (int k = 0; k < 21; ++k)
          inErr.fArray[k * N + n] = cs.err[k];
        chg(n, 0, 0) = cs.charge;
        fail(n, 0, 0) = 0;  // clearFailFlag
        msTarget(n, 0, 0) = es.layers[list[i].layer()].propagate_to();
      }
      if (barrel)
        propagateHelixToRMPlex(inErr, inPar, chg, msTarget, outErr, outPar, fail, nr, pflags);
      else
        propagateHelixToZMPlex(inErr, inPar, chg, msTarget, outErr, outPar, fail, nr, pflags);
      for (int n = 0; n < nr; ++n) {
        ::mkfitdev::PropState& ps = props[rows[n]].ps();
        for (int k = 0; k < 6; ++k)
          ps.par[k] = outPar(n, k, 0);
        for (int k = 0; k < 21; ++k)
          ps.err[k] = outErr.fArray[k * N + n];
        ps.fail = fail(n, 0, 0);
      }
    }

    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ::mkfitdev::CandSlotsSoA::ConstView slots,
                                  ::mkfitdev::SelListSoA::ConstView list,
                                  ::mkfitdev::ESView es,
                                  ::mkfitdev::LayerSoA::ConstView eohLayers,
                                  ::mkfitdev::BinnedHitSoA::ConstView eohBinned,
                                  ::mkfitdev::BinSoA::ConstView eohBins,
                                  ::mkfitdev::HitSoA::ConstView hits,
                                  ::mkfitdev::PropStateSoA::View props,
                                  ::mkfitdev::SelHitsSoA::View sels,
                                  SelDiag* diag,
                                  ::mkfitdev::CandPropState* engProp,
                                  ::mkfitdev::CandSelHits* engSel) const {
      const int nList = list.n();
      const auto pflags = interLayerFlags(es);
      if constexpr (kNN > 1) {
        // CPU: step 1 for all candidates of this block first, kNN per Matriplex (stock NN)
        int tb[kNN];
        int nb = 0;
        for (int32_t i : cms::alpakatools::uniform_elements(acc, nList)) {
          tb[nb++] = i;
          if (nb == kNN) {
            propagateBatch<kNN>(tb, nb, true, slots, list, es, props, pflags);
            propagateBatch<kNN>(tb, nb, false, slots, list, es, props, pflags);
            nb = 0;
          }
        }
        if (nb > 0) {
          propagateBatch<kNN>(tb, nb, true, slots, list, es, props, pflags);
          propagateBatch<kNN>(tb, nb, false, slots, list, es, props, pflags);
        }
      }
      for (int32_t i : cms::alpakatools::uniform_elements(acc, nList)) {
        const ::mkfitdev::CandState cs = slots[list[i].row()].state();
        const int layer = list[i].layer();
        const bool isBarrel = es.isBarrel(layer);
        ::mkfitdev::PropState& ps = props[i].ps();

        // 1. propagate to the layer (N = 1; CPU backends did it above)
        if constexpr (kNN == 1) {
        MPlexLS<1> inErr, outErr;
        MPlexLV<1> inPar, outPar;
        MPlexQI<1> chg, fail;
        MPlexQF<1> msTarget;
        for (int k = 0; k < 6; ++k)
          inPar(0, k, 0) = cs.par[k];
        for (int k = 0; k < 21; ++k)
          inErr.fArray[k] = cs.err[k];
        chg(0, 0, 0) = cs.charge;
        fail(0, 0, 0) = 0;  // clearFailFlag
        msTarget(0, 0, 0) = es.layers[layer].propagate_to();
        if (isBarrel)
          propagateHelixToRMPlex(inErr, inPar, chg, msTarget, outErr, outPar, fail, 1, pflags);
        else
          propagateHelixToZMPlex(inErr, inPar, chg, msTarget, outErr, outPar, fail, 1, pflags);

        for (int k = 0; k < 6; ++k)
          ps.par[k] = outPar(0, k, 0);
        for (int k = 0; k < 21; ++k)
          ps.err[k] = outErr.fArray[k];
        ps.fail = fail(0, 0, 0);
        }

        // 2. selectHitIndicesV2
        const ::mkfitdev::LayerOfHitsAccess L{eohLayers, eohBinned, eohBins, layer};
        Bins B;
        SelResult res;
        selectHitIndicesV2(ps.par, ps.err, cs.charge, ps.fail, layer, es, L, hits, B, res);

        // 3. find_tracks_handle_missed_layers: ONE implementation, the engine's (K3b/K4 from wsrRaw; review H5).
        //    Select's own version runs only as a debug cross-check (diag != nullptr, the select stock check).
        ::mkfitdev::SelHits& sh = sels[i].sel();
        sh.wsrRaw = res.wsr;
        int wsr = res.wsr;
        sh.extra = diag ? handleMissedLayer(wsr, isBarrel, list[i].region()) : ::mkfitdev::kSelExtraNone;
        sh.wsr = wsr;
        sh.inGap = res.inGap;
        sh.nHits = res.nHits;
        for (int k = 0; k < ::mkfitdev::kMaxSelHits; ++k)
          sh.hits[k] = k < res.nHits ? res.hits[k] : -1;

        if (diag) {
          diag[i] = SelDiag{B.q_c,
                            B.dq_track,
                            B.dphi_track,
                            B.q0,
                            B.q1,
                            B.q2,
                            B.p1,
                            B.p2,
                            {B.sp1.x, B.sp1.y, B.sp1.z},
                            {B.sp2.x, B.sp2.y, B.sp2.z},
                            B.qmin,
                            B.qmax};
        }

        // 4. (round 4) K2 -> K3 straight into the clone engine's per-(seed, ic) rows: the former scatter kernel
        //    (KernelEngineScatterSelect, same values: raw WSR, the engine applies handle_missed_layers itself)
        if (engSel != nullptr) {
          const int row = list[i].row();
          const int s = row / ::mkfitdev::kSlotsPerSeed;
          const int ic = (row % ::mkfitdev::kSlotsPerSeed) % ::mkfitdev::kMaxCandsPerSeed;
          const int r = ::mkfitdev::selRow(s, ic);
          const ::mkfitdev::SelHits& shg = sels[i].sel();
          ::mkfitdev::CandSelHits& o = engSel[r];
          for (int k = 0; k < ::mkfitdev::kMaxHitsPerCand; ++k)
            o.hit[k] = shg.hits[k];
          o.n = shg.nHits;
          o.wsr = shg.wsrRaw;
          o.inGap = shg.inGap;
          const ::mkfitdev::PropState& psg = props[i].ps();
          ::mkfitdev::CandPropState& op = engProp[r];
          for (int k = 0; k < 6; ++k)
            op.par[k] = psg.par[k];
          for (int k = 0; k < 21; ++k)
            op.err[k] = psg.err[k];
          op.charge = cs.charge;
        }
      }
    }
  };

  //--------------------------------------------------------------------------------------------------------------
  // K2, GPU flavour (round 5, lane gpu): two kernels.
  //   K2a KernelSelectPrep  thread per candidate: inter-layer propagation, Bins, WSR (as KernelSelectHits, N = 1);
  //                         hand-off in SelHitsSoA::scan; candidates without a hit scan are finished here.
  //   K2b KernelSelectScan  kSelLanes lanes per candidate, kSelScanCands candidates per block: lane l takes the hits
  //                         at stock scan positions g = l, l + kSelLanes, ... (q bin outer, phi bin inner, binned
  //                         order: the stock loop order) and keeps a sorted best-6 by (ddphi, g); lane 0 merges the
  //                         lanes' lists into the 6 smallest (ddphi, g), best first.
  // Stock keeps the 6 smallest ddphi in a std::priority_queue with a strict '<' insert and drains it best first: the
  // same hits in the same order unless two contending ddphi are exactly equal (then stock's heap order decides).
  // Exact ties (D5-d: keep stock's order): a conservative detector flags every candidate where a tie could matter and
  // lane 0 then reruns the stock heap loop (selectScanHeap) for it: the result is stock's in every case.
  // ties != nullptr (validation): ties[0] += flagged candidates, ties[1] += candidates scanned.
  //--------------------------------------------------------------------------------------------------------------
  constexpr int kSelLanes = 16;
  constexpr int kSelScanCands = 8;
  constexpr int kSelScanBlock = kSelLanes * kSelScanCands;  // 128
  constexpr int kSelPrepBlock = 64;

  struct KernelSelectPrep {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ::mkfitdev::CandSlotsSoA::ConstView slots,
                                  ::mkfitdev::SelListSoA::ConstView list,
                                  ::mkfitdev::ESView es,
                                  ::mkfitdev::LayerSoA::ConstView eohLayers,
                                  ::mkfitdev::BinnedHitSoA::ConstView eohBinned,
                                  ::mkfitdev::BinSoA::ConstView eohBins,
                                  ::mkfitdev::PropStateSoA::View props,
                                  ::mkfitdev::SelHitsSoA::View sels,
                                  SelDiag* diag,
                                  ::mkfitdev::CandPropState* engProp,
                                  ::mkfitdev::CandSelHits* engSel) const {
      const int nList = list.n();
      const auto pflags = interLayerFlags(es);
      for (int32_t i : cms::alpakatools::uniform_elements(acc, nList)) {
        const ::mkfitdev::CandState cs = slots[list[i].row()].state();
        const int layer = list[i].layer();
        const bool isBarrel = es.isBarrel(layer);
        ::mkfitdev::PropState& ps = props[i].ps();
        {
          MPlexLS<1> inErr, outErr;
          MPlexLV<1> inPar, outPar;
          MPlexQI<1> chg, fail;
          MPlexQF<1> msTarget;
          for (int k = 0; k < 6; ++k)
            inPar(0, k, 0) = cs.par[k];
          for (int k = 0; k < 21; ++k)
            inErr.fArray[k] = cs.err[k];
          chg(0, 0, 0) = cs.charge;
          fail(0, 0, 0) = 0;  // clearFailFlag
          msTarget(0, 0, 0) = es.layers[layer].propagate_to();
          if (isBarrel)
            propagateHelixToRMPlex(inErr, inPar, chg, msTarget, outErr, outPar, fail, 1, pflags);
          else
            propagateHelixToZMPlex(inErr, inPar, chg, msTarget, outErr, outPar, fail, 1, pflags);
          for (int k = 0; k < 6; ++k)
            ps.par[k] = outPar(0, k, 0);
          for (int k = 0; k < 21; ++k)
            ps.err[k] = outErr.fArray[k];
          ps.fail = fail(0, 0, 0);
        }
        const ::mkfitdev::LayerOfHitsAccess L{eohLayers, eohBinned, eohBins, layer};
        Bins Bn;
        SelResult res;
        const bool scan = selectPrepare(ps.par, ps.err, cs.charge, ps.fail, layer, es, L, Bn, res);
        ::mkfitdev::SelHits& sh = sels[i].sel();
        sh.wsrRaw = res.wsr;
        int wsr = res.wsr;
        sh.extra = diag ? handleMissedLayer(wsr, isBarrel, list[i].region()) : ::mkfitdev::kSelExtraNone;
        sh.wsr = wsr;
        sh.inGap = res.inGap;
        ::mkfitdev::SelScan& sc = sels[i].scan();
        sc.q0 = Bn.q0;
        sc.q1 = Bn.q1;
        sc.q2 = Bn.q2;
        sc.p1 = Bn.p1;
        sc.p2 = Bn.p2;
        sc.scan = scan ? 1 : 0;
        sc.charge = cs.charge;
        sc.dqTrack = Bn.dq_track;
        sc.dphiCut = Bn.dphi_track + DDPHI_PRESEL_FAC * 0.0123f;
        if (diag) {
          diag[i] = SelDiag{Bn.q_c,
                            Bn.dq_track,
                            Bn.dphi_track,
                            Bn.q0,
                            Bn.q1,
                            Bn.q2,
                            Bn.p1,
                            Bn.p2,
                            {Bn.sp1.x, Bn.sp1.y, Bn.sp1.z},
                            {Bn.sp2.x, Bn.sp2.y, Bn.sp2.z},
                            Bn.qmin,
                            Bn.qmax};
        }
        const int row = list[i].row();
        const int s = row / ::mkfitdev::kSlotsPerSeed;
        const int ic = (row % ::mkfitdev::kSlotsPerSeed) % ::mkfitdev::kMaxCandsPerSeed;
        const int r = ::mkfitdev::selRow(s, ic);
        if (engProp != nullptr) {
          ::mkfitdev::CandPropState& op = engProp[r];
          for (int k = 0; k < 6; ++k)
            op.par[k] = ps.par[k];
          for (int k = 0; k < 21; ++k)
            op.err[k] = ps.err[k];
          op.charge = cs.charge;
        }
        if (!scan) {  // finished here (K2b skips it)
          sh.nHits = 0;
          for (int k = 0; k < ::mkfitdev::kMaxSelHits; ++k)
            sh.hits[k] = -1;
          if (engSel != nullptr) {
            ::mkfitdev::CandSelHits& o = engSel[r];
            for (int k = 0; k < ::mkfitdev::kMaxHitsPerCand; ++k)
              o.hit[k] = -1;
            o.n = 0;
            o.wsr = sh.wsrRaw;
            o.inGap = sh.inGap;
          }
        }
      }
    }
  };

  struct KernelSelectScan {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ::mkfitdev::SelListSoA::ConstView list,
                                  ::mkfitdev::ESView es,
                                  ::mkfitdev::LayerSoA::ConstView eohLayers,
                                  ::mkfitdev::BinnedHitSoA::ConstView eohBinned,
                                  ::mkfitdev::BinSoA::ConstView eohBins,
                                  ::mkfitdev::HitSoA::ConstView hits,
                                  ::mkfitdev::PropStateSoA::ConstView props,
                                  ::mkfitdev::SelHitsSoA::View sels,
                                  ::mkfitdev::CandSelHits* engSel,
                                  uint32_t* ties) const {
      constexpr int T = kSelLanes;
      constexpr int G = kSelScanCands;
      constexpr int K = NEW_MAX_HIT;
      constexpr float kInf = std::numeric_limits<float>::infinity();
      auto& pS = alpaka::declareSharedVar<float[G][T][K], __COUNTER__>(acc);
      auto& pG = alpaka::declareSharedVar<uint32_t[G][T][K], __COUNTER__>(acc);
      auto& pH = alpaka::declareSharedVar<uint32_t[G][T][K], __COUNTER__>(acc);
      auto& pRej = alpaka::declareSharedVar<float[G][T], __COUNTER__>(acc);
      auto& pHead = alpaka::declareSharedVar<uint8_t[G][T], __COUNTER__>(acc);

      const int tid = alpaka::getIdx<alpaka::Block, alpaka::Threads>(acc)[0u];
      const int bid = alpaka::getIdx<alpaka::Grid, alpaka::Blocks>(acc)[0u];
      const int nBlocks = alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0u];
      const int grp = tid / T;
      const int lane = tid % T;
      const int nList = list.n();

      for (int c0 = bid * G; c0 < nList; c0 += nBlocks * G) {  // block-uniform loop (syncs inside)
        const int i = c0 + grp;
        const bool active = i < nList && sels[i].scan().scan != 0;
        float s[K];
        uint32_t gg[K], hh[K];
#pragma unroll
        for (int k = 0; k < K; ++k) {
          s[k] = kInf;
          gg[k] = 0;
          hh[k] = 0;
        }
        float rej = kInf;
        bool gap = false;
        if (active) {
          const ::mkfitdev::SelScan sc = sels[i].scan();
          const int layer = list[i].layer();
          const bool isBarrel = es.isBarrel(layer);
          const ::mkfitdev::LayerOfHitsAccess L{eohLayers, eohBinned, eohBins, layer};
          const mp::InitialState mp_is(props[i].ps().par, sc.charge);
          const int moduleBegin = es.layers[layer].module_begin();
          const bidx_t q0 = sc.q0, q1 = sc.q1, q2 = sc.q2, p1 = sc.p1, p2 = sc.p2;
          // stock: in_gap if a visited bin of the central q bin is dead (lane 0)
          if (lane == 0 && static_cast<bidx_t>(q0 - q1) < static_cast<bidx_t>(q2 - q1)) {
            for (bidx_t pi = p1; pi != p2; pi = L.phiMaskApply(pi + 1))
              if (L.isBinDead(pi, q0) == true)
                gap = true;
          }
          // cursor over the stock bin order; this lane's hits are at scan positions lane + n * T
          bidx_t qi = q1, pi = p1;
          bool done = (qi == q2) || (p1 == p2);
          uint32_t gStart = 0, cnt = 0, hbeg = 0;
          if (!done) {
            const uint32_t content = L.binContent(pi, qi);
            hbeg = ::mkfitdev::binFirst(content);
            cnt = ::mkfitdev::binCount(content);
          }
          for (uint32_t g = lane;; g += T) {
            while (!done && g >= gStart + cnt) {
              gStart += cnt;
              pi = L.phiMaskApply(pi + 1);
              if (pi == p2) {
                pi = p1;
                ++qi;
                if (qi == q2) {
                  done = true;
                  break;
                }
              }
              const uint32_t content = L.binContent(pi, qi);
              hbeg = ::mkfitdev::binFirst(content);
              cnt = ::mkfitdev::binCount(content);
            }
            if (done)
              break;
            float sco;
            unsigned int ho;
            if (!selectHitScore(
                    mp_is, isBarrel, moduleBegin, sc.dqTrack, sc.dphiCut, es, L, hits, hbeg + (g - gStart), sco, ho))
              continue;
            if (sco < s[K - 1]) {
              float cs = sco;
              uint32_t cg = g, ch = ho;
#pragma unroll
              for (int k = 0; k < K; ++k) {
                if (cs < s[k] || (cs == s[k] && cg < gg[k])) {
                  const float ts = s[k];
                  const uint32_t tg = gg[k], th = hh[k];
                  s[k] = cs;
                  gg[k] = cg;
                  hh[k] = ch;
                  cs = ts;
                  cg = tg;
                  ch = th;
                }
              }
              if (cs < kInf)  // R5-M1: the score evicted from a full lane list (an exact tie with v6 is a contender)
                rej = cs < rej ? cs : rej;
            } else {
              rej = sco < rej ? sco : rej;  // a full lane list refused this score (>= its maximum)
            }
          }
        }
#pragma unroll
        for (int k = 0; k < K; ++k) {
          pS[grp][lane][k] = s[k];
          pG[grp][lane][k] = gg[k];
          pH[grp][lane][k] = hh[k];
        }
        pRej[grp][lane] = rej;
        pHead[grp][lane] = 0;
        alpaka::syncBlockThreads(acc);

        if (lane == 0 && active) {
          ::mkfitdev::SelHits& sh = sels[i].sel();
          int nOut = 0;
          bool tie = false;
          float last = kInf;
          for (int o = 0; o < K; ++o) {
            int best = -1;
            float bs = kInf;
            uint32_t bg = 0xffffffffu;
            for (int l = 0; l < T; ++l) {
              const int h = pHead[grp][l];
              if (h >= K)
                continue;
              const float sc = pS[grp][l][h];
              if (!(sc < kInf))
                continue;
              const uint32_t g = pG[grp][l][h];
              if (sc < bs || (sc == bs && g < bg)) {
                best = l;
                bs = sc;
                bg = g;
              }
            }
            if (best < 0)
              break;
            if (nOut > 0 && bs == last)
              tie = true;
            sh.hits[o] = pH[grp][best][pHead[grp][best]];
            pHead[grp][best] = pHead[grp][best] + 1;
            last = bs;
            ++nOut;
          }
          for (int o = nOut; o < K; ++o)
            sh.hits[o] = -1;
          sh.nHits = nOut;
          if (gap)
            sh.inGap = 1;
          // Exact-tie detector (conservative): equal ddphi inside the selection, a lane head left out at the 6th
          // score, or a lane that refused or evicted a score at or below the 6th score (every refused or evicted score
          // is >= v6, so this is exactly a contending tie; R5-M1 added the evictions). Any contending tie lands in one
          // of the three; without one the selection is the stock one.
          {
            const float s6 = nOut == K ? last : kInf;
            for (int l = 0; l < T; ++l) {
              const int h = pHead[grp][l];
              if (nOut == K && h < K && pS[grp][l][h] == s6)
                tie = true;
              if (pRej[grp][l] < kInf && pRej[grp][l] <= s6)
                tie = true;
            }
          }
          if (tie) {
            // D5-d: stock order on a tie - lane 0 reruns the stock loop with stock's heap for this candidate
            const ::mkfitdev::SelScan sc = sels[i].scan();
            const int layer = list[i].layer();
            const ::mkfitdev::LayerOfHitsAccess L{eohLayers, eohBinned, eohBins, layer};
            const mp::InitialState mp_is(props[i].ps().par, sc.charge);
            bool gapUnused = false;
            int n = 0;
            selectScanHeap(mp_is,
                           es.isBarrel(layer),
                           es.layers[layer].module_begin(),
                           sc.dqTrack,
                           sc.dphiCut,
                           es,
                           L,
                           hits,
                           sc.q0,
                           sc.q1,
                           sc.q2,
                           sc.p1,
                           sc.p2,
                           gapUnused,
                           sh.hits,
                           n);
            for (int o = n; o < K; ++o)
              sh.hits[o] = -1;
            sh.nHits = n;
          }
          if (ties != nullptr) {
            if (tie)
              alpaka::atomicAdd(acc, &ties[0], 1u, alpaka::hierarchy::Blocks{});
            alpaka::atomicAdd(acc, &ties[1], 1u, alpaka::hierarchy::Blocks{});
          }
          if (engSel != nullptr) {
            const int row = list[i].row();
            const int sd = row / ::mkfitdev::kSlotsPerSeed;
            const int ic = (row % ::mkfitdev::kSlotsPerSeed) % ::mkfitdev::kMaxCandsPerSeed;
            ::mkfitdev::CandSelHits& o = engSel[::mkfitdev::selRow(sd, ic)];
            for (int k = 0; k < ::mkfitdev::kMaxHitsPerCand; ++k)
              o.hit[k] = sh.hits[k];
            o.n = sh.nHits;
            o.wsr = sh.wsrRaw;
            o.inGap = sh.inGap;
          }
        }
        alpaka::syncBlockThreads(acc);
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::select

#endif
