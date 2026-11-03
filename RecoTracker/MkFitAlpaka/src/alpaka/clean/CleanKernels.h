#ifndef RecoTracker_MkFitAlpaka_src_alpaka_clean_CleanKernels_h
#define RecoTracker_MkFitAlpaka_src_alpaka_clean_CleanKernels_h

// Kernels of the device duplicate cleaner (StdSeq::clean_duplicates_sharedhits_pixelseed) and of the LST-step
// quality filter (phase1:qfilter_n_hits_pixseed + qfilter_nan_n_silly), with deterministic stable compaction.

#include <cstdint>

#include <Eigen/Core>

#include <alpaka/alpaka.hpp>

#include "DataFormats/Math/interface/choleskyInversion.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/MkFitAlpaka/interface/tracks/TrackSoA.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/clean/CleanFunctions.h"
#include "RecoTracker/MkFitAlpaka/src/alpaka/prop/PropagationMPlex.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::clean {

  using namespace ::mkfitdev::clean;
  using ::mkfitdev::TrackSoAConstView;
  using ::mkfitdev::TrackSoAView;

  // (phi, cot theta) cells. Cell widths >= the MkFitCore pre-cuts (|dphi| <= 0.37, |dcot theta| <= 0.37), so every pair
  // MkFitCore can flag lies in the same or an adjacent cell. phi is cyclic; cot theta outside the range is clamped into
  // the edge cells, which keeps the neighbour property. Tracks with a non-finite or absurd phi / cot theta are
  // "wildcards" and are paired with every track (MkFitCore would evaluate those pairs too).
  constexpr int kNPhiCells = 16;  // width 2 pi / 16 = 0.393
  constexpr int kNCtCells = 64;   // width 0.375 over [-12, 12)
  constexpr float kCtCellWidth = 0.375f;
  constexpr float kCtMin = -0.5f * kNCtCells * kCtCellWidth;
  constexpr int kNCells = kNPhiCells * kNCtCells;
  constexpr float kMaxBinnablePhi = 100.f;

  // counters (device int buffer)
  enum Counter : int {
    kCntWildcard = 0,
    kCntCellOverflow = 1,
    kCntEdges = 2,         // survivorsOnly: (winner, loser) pairs recorded
    kCntEdgeOverflow = 3,  // survivorsOnly: pairs beyond the edge capacity (then the any-winner flags are kept)
    kNCounters = 8
  };  // overflow must stay 0 (consistency check)

  // Exclusive prefix scan of in[0..n) into out[0..n], out[n] = total; one block, deterministic. n is read on device
  // from *nPtr (+ nExtra) so that no host synchronisation is needed.
  struct KernelScanOneBlock {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, const int* in, int* out, const int32_t* nPtr, int nConst) const {
      const int n = nPtr ? *nPtr : nConst;
      auto& partial = alpaka::declareSharedVar<int[256], __COUNTER__>(acc);  // <= 256 threads per block
      const int nThr = static_cast<int>(alpaka::getWorkDiv<alpaka::Block, alpaka::Threads>(acc)[0u]);
      const int tid = static_cast<int>(alpaka::getIdx<alpaka::Block, alpaka::Threads>(acc)[0u]);
      const int chunk = (n + nThr - 1) / nThr;
      const int b = tid * chunk;
      const int e = (b + chunk < n) ? b + chunk : n;
      int s = 0;
      for (int k = b; k < e; ++k)
        s += in[k];
      partial[tid] = s;
      alpaka::syncBlockThreads(acc);
      if (tid == 0) {
        int run = 0;
        for (int k = 0; k < nThr; ++k) {
          const int v = partial[k];
          partial[k] = run;
          run += v;
        }
        out[n] = run;
      }
      alpaka::syncBlockThreads(acc);
      int run = partial[tid];
      for (int k = b; k < e; ++k) {
        const int v = in[k];
        out[k] = run;
        run += v;
      }
    }
  };

  // Per track: cot theta (MkFitCore: 1.f / std::tan(theta)), hasPixel (pixelPriority), cell, clear flags, count cell
  // occupancy.
  struct KernelPrepare {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackSoAConstView trk,
                                  DupCleanParams p,
                                  float* ct,
                                  int8_t* hasPix,
                                  int* cell,
                                  int* flags,
                                  int* cellCount,
                                  int* wild,
                                  int* counters) const {
      const int n = trk.nTracks();
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        flags[i] = 0;
        const float c = 1.f / alpaka::math::tan(acc, trk[i].params().v[5]);
        ct[i] = c;
        hasPix[i] = (p.pixelPriority && hasPixelHits(trk, i, p)) ? 1 : 0;
        const float phi = trk[i].params().v[4];
        if (!isFiniteBits(c) || !isFiniteBits(phi) || alpaka::math::abs(acc, phi) > kMaxBinnablePhi) {
          cell[i] = -1;
          const int w = alpaka::atomicAdd(acc, &counters[kCntWildcard], 1, alpaka::hierarchy::Grids{});
          wild[w] = i;
          continue;
        }
        // full wrap into [-pi, pi) for binning only (the decision uses MkFitCore's squashPhiMinimal)
        float ph = phi - kTwoPI * alpaka::math::floor(acc, (phi + kPI) / kTwoPI);
        int ip = static_cast<int>((ph + kPI) * (kNPhiCells / kTwoPI));
        ip = ip < 0 ? 0 : (ip >= kNPhiCells ? kNPhiCells - 1 : ip);
        const float fc = (c - kCtMin) / kCtCellWidth;
        int ic = fc < 0.f ? 0 : (fc >= float(kNCtCells) ? kNCtCells - 1 : static_cast<int>(fc));
        const int cl = ic * kNPhiCells + ip;
        cell[i] = cl;
        alpaka::atomicAdd(acc, &cellCount[cl], 1, alpaka::hierarchy::Grids{});
      }
    }
  };

  // Fill cell contents (order inside a cell is irrelevant for the result).
  struct KernelFill {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackSoAConstView trk,
                                  const int* cell,
                                  const int* cellStart,
                                  int* cellCursor,
                                  int* cellContent,
                                  int* counters) const {
      const int n = trk.nTracks();
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        const int cl = cell[i];
        if (cl < 0)
          continue;
        const int pos = cellStart[cl] + alpaka::atomicAdd(acc, &cellCursor[cl], 1, alpaka::hierarchy::Grids{});
        if (pos < cellStart[cl + 1])
          cellContent[pos] = i;
        else
          alpaka::atomicAdd(acc, &counters[kCntCellOverflow], 1, alpaka::hierarchy::Grids{});
      }
    }
  };

  // Neighbour cell k (0..8) of cell cl, or -1 if outside the cot theta range.
  ALPAKA_FN_HOST_ACC inline int neighbourCell(int cl, int k) {
    const int ic = cl / kNPhiCells + (k / 3 - 1);
    if (ic < 0 || ic >= kNCtCells)
      return -1;
    const int ip = (cl % kNPhiCells + (k % 3 - 1) + kNPhiCells) % kNPhiCells;
    return ic * kNPhiCells + ip;
  }

  // Number of partner slots of each track: total occupancy of its 3x3 neighbourhood.
  struct KernelCountPairs {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, TrackSoAConstView trk, const int* cell, const int* cellStart, int* pairCount) const {
      const int n = trk.nTracks();
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        const int cl = cell[i];
        int s = 0;
        if (cl >= 0) {
          for (int k = 0; k < 9; ++k) {
            const int nc = neighbourCell(cl, k);
            if (nc >= 0)
              s += cellStart[nc + 1] - cellStart[nc];
          }
        }
        pairCount[i] = s;
      }
    }
  };

  // Same-particle decision (DupCleanParams::sameParticleChi2 > 0) for a pixel-seeded and a pixel-less fitted track
  // that share no hit and pass the cleaner's cot theta, phi and 1/pT pre-cuts: the pixel-less track is propagated as a
  // helix, with its covariance transported by the Jacobian, to the radius of the pixel track's state (its innermost
  // hit). The two states are compared in the position transverse to the pixel track's direction, q/pT, phi and theta,
  // with the sum of both covariances plus the multiple scattering in the layers between the two states (five degrees
  // of freedom). Below the cut they are one particle and the track with more found hits is kept, the pixel track on a
  // tie. Returns the index to flag, or -1. Reads only immutable inputs.
  ALPAKA_FN_ACC inline int sameParticlePairDecision(
      TrackSoAConstView t, const float* ct, const int8_t* hasPix, int i, int j, DupCleanParams const& p) {
    if (t[i].label() == t[j].label() || hasPix[i] == hasPix[j])
      return -1;
    if (std::abs(ct[j] - ct[i]) > kMaxdcth)
      return -1;
    if (std::abs(squashPhiMinimal(t[i].params().v[4] - t[j].params().v[4])) > kMaxdphi)
      return -1;
    if (std::abs(t[j].params().v[3] - t[i].params().v[3]) > kMaxd1pt)
      return -1;
    {
      const auto& h1 = t[i].hits().hot;
      const auto& h2 = t[j].hits().hot;
      const int n1 = t[i].nTotalHits(), n2 = t[j].nTotalHits();
      for (int a = 0; a < n1; ++a) {
        if (h1[a].index < 0)
          continue;
        const uint32_t key = hotKey(h1[a]);
        for (int b = 0; b < n2; ++b)
          if (hotKey(h2[b]) == key)
            return -1;
      }
    }
    const int ia = hasPix[i] ? i : j;  // pixel-seeded
    const int ib = ia == i ? j : i;    // pixel-less, propagated
    const float* pa = t[ia].params().v;

    MPlexLV<1> inPar, outPar;
    MPlexLS<1> inErr, outErr;
    MPlexQI<1> chg, fail;
    MPlexQF<1> rad;
    for (int k = 0; k < 6; ++k) {
      inPar(0, k, 0) = t[ib].params().v[k];
      for (int l = 0; l <= k; ++l)
        inErr(0, k, l) = t[ib].errors().v[::mkfitdev::symIdx6(k, l)];
    }
    chg(0, 0, 0) = t[ib].charge();
    rad(0, 0, 0) = hipo(pa[0], pa[1]);
    const PropagationFlags pf{};
    propagateHelixToRMPlex(inErr, inPar, chg, rad, outErr, outPar, fail, 1, pf);
    if (fail(0, 0, 0))
      return -1;

    // residual and covariance in (u1, u2, q/pT, phi, theta); u1, u2 span the plane transverse to the pixel track
    const double sT = std::sin(double(pa[5])), cT = std::cos(double(pa[5]));
    const double sP = std::sin(double(pa[4])), cP = std::cos(double(pa[4]));
    const double u[2][3] = {{-sP, cP, 0.}, {-cT * cP, -cT * sP, sT}};
    const double qa = t[ia].charge(), qb = t[ib].charge();
    double d[5];
    for (int m = 0; m < 2; ++m)
      d[m] = u[m][0] * (pa[0] - outPar(0, 0, 0)) + u[m][1] * (pa[1] - outPar(0, 1, 0)) +
             u[m][2] * (pa[2] - outPar(0, 2, 0));
    d[2] = qa * pa[3] - qb * outPar(0, 3, 0);
    d[3] = squashPhiMinimal(pa[4] - outPar(0, 4, 0));
    d[4] = pa[5] - outPar(0, 5, 0);
    // cov += A C A^T, A = the projection from (x, y, z, 1/pT, phi, theta): rows u1, u2 (position), q (1/pT), phi, theta
    Eigen::Matrix<double, 5, 5> cov = Eigen::Matrix<double, 5, 5>::Zero(), inv;
    auto addProjected = [&](auto const& c, double q) {
      double g[5][6];  // A C
      for (int l = 0; l < 6; ++l) {
        for (int m = 0; m < 2; ++m)
          g[m][l] = u[m][0] * c(0, l) + u[m][1] * c(1, l) + u[m][2] * c(2, l);
        g[2][l] = q * c(3, l);
        g[3][l] = c(4, l);
        g[4][l] = c(5, l);
      }
      for (int m = 0; m < 5; ++m) {
        for (int n = 0; n < 2; ++n)
          cov(m, n) += g[m][0] * u[n][0] + g[m][1] * u[n][1] + g[m][2] * u[n][2];
        cov(m, 2) += q * g[m][3];
        cov(m, 3) += g[m][4];
        cov(m, 4) += g[m][5];
      }
    };
    const auto& ea = t[ia].errors().v;
    addProjected([&](int k, int l) -> double { return ea[::mkfitdev::symIdx6(k, l)]; }, qa);
    addProjected([&](int k, int l) -> double { return outErr(0, k, l); }, qb);
    // Multiple scattering in the material-map layers crossed between the two states. The map is scanned along the
    // chord in steps of at most half a bin; a layer is entered where a bin with material follows one without, its
    // normal is radial if the step changed the r bin and along z otherwise. A layer entered within one bin (in r for a
    // radial normal, in z otherwise) of either end is that end's own layer, which its state already carries. A kink
    // with angle variance theta0^2 at path distance l from the compared point moves the backward extrapolation by l
    // times the kink transversely and its direction by minus the kink; theta0 and the pT dependence on theta are those
    // of applyMaterialEffects (Config::usePtMultScat).
    {
      auto const& mv = p.material;
      const double xs = t[ib].params().v[0], ys = t[ib].params().v[1], zs = t[ib].params().v[2];
      const double xe = outPar(0, 0, 0), ye = outPar(0, 1, 0), ze = outPar(0, 2, 0);
      const double len = std::sqrt((xs - xe) * (xs - xe) + (ys - ye) * (ys - ye) + (zs - ze) * (zs - ze));
      const double drBins = std::abs(std::sqrt(xs * xs + ys * ys) - std::sqrt(xe * xe + ye * ye)) * mv.facR;
      const double dzBins = std::abs(zs - ze) * mv.facZ;
      const int nSteps = mv.radl ? 2 * int(drBins > dzBins ? drBins : dzBins) + 2 : 0;
      const double ipt = t[ib].params().v[3];
      const double pTot = 1. / (ipt * sT);
      const double pz = pTot * cT;
      const double beta = pTot / std::sqrt(pTot * pTot + 0.140 * 0.140);
      const double dIptDTheta = -pz * ipt * ipt;
      const double sPx = (xs - xe) / len, sPy = (ys - ye) / len, sPz = (zs - ze) / len;  // unit chord, end to start
      const int endZ = mv.binZ(std::abs(ze)), endR = mv.binR(std::sqrt(xe * xe + ye * ye));
      const int startZ = mv.binZ(std::abs(zs)), startR = mv.binR(std::sqrt(xs * xs + ys * ys));
      int prevZ = endZ, prevR = endR;
      bool prevMat = false;
      for (int k = 1; k < nSteps; ++k) {
        const double l = len * k / nSteps;
        const double x = xe + l * sPx, y = ye + l * sPy, z = ze + l * sPz;
        const double r = std::sqrt(x * x + y * y);
        const int bz = mv.binZ(std::abs(z)), br = mv.binR(r);
        if (bz == prevZ && br == prevR)
          continue;
        float bbxi, radl;
        mv.materialChecked(std::abs(z), r, bbxi, radl);
        const bool mat = radl > 0.f;
        const bool radial = br != prevR;
        const bool ownLayer = radial ? (std::abs(br - endR) <= 1 || std::abs(br - startR) <= 1)
                                     : (std::abs(bz - endZ) <= 1 || std::abs(bz - startZ) <= 1);
        if (mat && !prevMat && !ownLayer) {
          // incidence on the layer: radial normal if the r bin changed, else the z axis
          const double cosInc = radial ? std::abs(sT * (cP * x + sP * y) / r) : std::abs(cT);
          const double radL = radl / (cosInc > 1e-3 ? cosInc : 1e-3);
          const double th0 = 0.0136 * (1. + 0.038 * std::log(radL)) / (beta * pTot);
          const double th02 = th0 * th0 * radL;
          const double v1[5] = {l, 0., 0., -1. / sT, 0.};
          const double v2[5] = {0., l, qb * dIptDTheta, 0., 1.};
          for (int m = 0; m < 5; ++m)
            for (int n = 0; n < 5; ++n)
              cov(m, n) += th02 * (v1[m] * v1[n] + v2[m] * v2[n]);
        }
        prevMat = mat;
        prevZ = bz;
        prevR = br;
      }
    }
    for (int m = 0; m < 5; ++m)
      if (!(cov(m, m) > 0.))
        return -1;
    math::cholesky::invert(cov, inv);
    double chi2 = 0.;
    for (int m = 0; m < 5; ++m)
      for (int n = 0; n < 5; ++n)
        chi2 += d[m] * inv(m, n) * d[n];
    if (!(chi2 >= 0. && chi2 < double(p.sameParticleChi2)))
      return -1;
    return t[ib].nFoundHits() > t[ia].nFoundHits() ? ia : ib;
  }

  ALPAKA_FN_ACC inline void applyPair(Acc1D const& acc,
                                      TrackSoAConstView trk,
                                      const float* ct,
                                      const int8_t* hasPix,
                                      int i,
                                      int j,
                                      DupCleanParams const& p,
                                      int* flags,
                                      int* counters,
                                      int* edges,
                                      int edgeCap) {
    const int loser = p.sameParticleChi2 > 0.f ? sameParticlePairDecision(trk, ct, hasPix, i, j, p)
                                               : dupPairDecision(trk, ct, hasPix, i, j, p);
    if (loser >= 0) {
      alpaka::atomicOr(acc, &flags[loser], 1, alpaka::hierarchy::Grids{});
      if (edges) {
        const int e = alpaka::atomicAdd(acc, &counters[kCntEdges], 1, alpaka::hierarchy::Blocks{});
        if (e < edgeCap) {
          edges[2 * e] = loser == i ? j : i;
          edges[2 * e + 1] = loser;
        } else
          alpaka::atomicAdd(acc, &counters[kCntEdgeOverflow], 1, alpaka::hierarchy::Blocks{});
      }
    }
  }

  // One thread per candidate pair: global pair index -> (track i, neighbour slot) -> partner j; pairs with j <= i
  // are skipped so each unordered pair is decided once, oriented as MkFitCore (itrack < jtrack).
  struct KernelPairs {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackSoAConstView trk,
                                  const float* ct,
                                  const int8_t* hasPix,
                                  const int* cell,
                                  const int* cellStart,
                                  const int* cellContent,
                                  const int* pairOffset,
                                  DupCleanParams p,
                                  int* flags,
                                  int* counters,
                                  int* edges,
                                  int edgeCap) const {
      const int n = trk.nTracks();
      const int nPairs = pairOffset[n];
      // Per-thread walk state. Consecutive pair indices of one thread (CPU backends: contiguous element ranges) are
      // advanced incrementally; otherwise (GPU grid stride) the track is found by binary search. Both give the same
      // (i, j) for a given pair index.
      int i = -1, k = 0, s = 0, gPrev = -2;
      for (int32_t g : cms::alpakatools::uniform_elements(acc, nPairs)) {
        if (g == gPrev + 1 && i >= 0) {
          if (g < pairOffset[i + 1]) {
            ++s;  // next slot of the same track
          } else {
            while (pairOffset[i + 1] <= g)  // next track with slots
              ++i;
            k = 0;
            s = g - pairOffset[i];
          }
        } else {
          // last i with pairOffset[i] <= g (binary search; pairOffset is non-decreasing)
          int lo = 0, hi = n - 1;
          while (lo < hi) {
            const int mid = (lo + hi + 1) >> 1;
            if (pairOffset[mid] <= g)
              lo = mid;
            else
              hi = mid - 1;
          }
          i = lo;
          k = 0;
          s = g - pairOffset[i];
        }
        gPrev = g;
        const int cl = cell[i];
        // walk the neighbour cells from k: find the cell holding slot s
        int nc = neighbourCell(cl, k);
        int sz = nc >= 0 ? cellStart[nc + 1] - cellStart[nc] : 0;
        while (s >= sz) {
          s -= sz;
          ++k;
          nc = neighbourCell(cl, k);
          sz = nc >= 0 ? cellStart[nc + 1] - cellStart[nc] : 0;
        }
        const int j = cellContent[cellStart[nc] + s];
        if (j <= i)
          continue;
        applyPair(acc, trk, ct, hasPix, i, j, p, flags, counters, edges, edgeCap);
      }
    }
  };

  // Wildcard tracks against every other track (expected to be empty on real events; counted).
  struct KernelWildcardPairs {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackSoAConstView trk,
                                  const float* ct,
                                  const int8_t* hasPix,
                                  const int* wild,
                                  DupCleanParams p,
                                  int* flags,
                                  int* counters,
                                  int* edges,
                                  int edgeCap) const {
      const int n = trk.nTracks();
      const int nw = counters[kCntWildcard];
      for (int32_t g : cms::alpakatools::uniform_elements(acc, nw * n)) {
        const int w = wild[g / n];
        const int t = g % n;
        if (t == w)
          continue;
        applyPair(acc, trk, ct, hasPix, w < t ? w : t, w < t ? t : w, p, flags, counters, edges, edgeCap);
      }
    }
  };

  // survivorsOnly: re-decide the losers on the recorded (winner, loser) pairs so that only kept tracks remove others.
  // The winner order (pixel priority, then score, then index) is total, so the pairs form a DAG and the fixed point
  // is the greedy result; each round settles every loser whose winners are all settled. One block.
  // st: 0 kept, 1 removed, 2 undecided; kw / uw: loser has a kept / an undecided winner in this round.
  struct KernelSurvivorsOnly {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackSoAConstView trk,
                                  int* flags,
                                  const int* counters,
                                  const int* edges,
                                  int* st,
                                  int* kw,
                                  int* uw) const {
      if (counters[kCntEdgeOverflow] > 0)
        return;  // keep the any-winner flags
      const int n = trk.nTracks();
      const int ne = counters[kCntEdges];
      const int nThr = static_cast<int>(alpaka::getWorkDiv<alpaka::Block, alpaka::Threads>(acc)[0u]);
      const int tid = static_cast<int>(alpaka::getIdx<alpaka::Block, alpaka::Threads>(acc)[0u]);
      auto& changed = alpaka::declareSharedVar<int, __COUNTER__>(acc);
      for (int i = tid; i < n; i += nThr) {
        st[i] = flags[i] ? 2 : 0;
        kw[i] = 0;
        uw[i] = 0;
      }
      alpaka::syncBlockThreads(acc);
      for (int round = 0; round < n + 1; ++round) {
        if (tid == 0)
          changed = 0;
        for (int e = tid; e < ne; e += nThr) {
          const int w = edges[2 * e], l = edges[2 * e + 1];
          if (st[l] == 2) {
            if (st[w] == 0)
              kw[l] = 1;
            else if (st[w] == 2)
              uw[l] = 1;
          }
        }
        alpaka::syncBlockThreads(acc);
        for (int e = tid; e < ne; e += nThr) {
          const int l = edges[2 * e + 1];
          if (st[l] == 2 && (kw[l] || !uw[l])) {
            st[l] = kw[l] ? 1 : 0;
            changed = 1;
          }
        }
        alpaka::syncBlockThreads(acc);
        for (int e = tid; e < ne; e += nThr) {
          const int l = edges[2 * e + 1];
          kw[l] = 0;
          uw[l] = 0;
        }
        const int c = changed;
        alpaka::syncBlockThreads(acc);
        if (!c)
          break;
      }
      for (int i = tid; i < n; i += nThr)
        flags[i] = st[i] != 0 ? 1 : 0;  // undecided (not reached) counts as removed
    }
  };

  // duplicate column from the flags; keep = !duplicate
  struct KernelWriteDupFlags {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, TrackSoAView trk, const int* flags, int* keep) const {
      const int n = trk.nTracks();
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        trk[i].duplicate() = flags[i] ? 1 : 0;
        keep[i] = flags[i] ? 0 : 1;
      }
    }
  };

  // LST-step candidate filter on finished tracks: keep = qfilter_n_hits_pixseed && !hasNanNSillyValues
  struct KernelFilterTracks {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, TrackSoAConstView trk, int minHitsQF, int* keep) const {
      const int n = trk.nTracks();
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        keep[i] = lstStepCandFilter(trk[i].nFoundHits(), trk[i].errors().v, minHitsQF) ? 1 : 0;
      }
    }
  };

  // Fitted tracks the output conversion keeps: a fit with a non-finite or negative chi2 has failed
  struct KernelFilterFitted {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, TrackSoAConstView trk, int* keep) const {
      const int n = trk.nTracks();
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        const float c = trk[i].chi2();
        keep[i] = (isFiniteBits(c) && c >= 0.f) ? 1 : 0;
      }
    }
  };

  // Stable compaction of rows with keep != 0 (std::remove_if / filter_comb_cands order); dest from the exclusive scan.
  struct KernelCompact {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, TrackSoAConstView in, TrackSoAView out, const int* keep, const int* dest) const {
      const int n = in.nTracks();
      if (cms::alpakatools::once_per_grid(acc)) {
        out.nTracks() = dest[n];
        out.nOverflowTracks() = in.nOverflowTracks();
        out.nOverflowHits() = in.nOverflowHits();
      }
      for (int32_t i : cms::alpakatools::uniform_elements(acc, n)) {
        if (!keep[i])
          continue;
        const int d = dest[i];
        out[d].params() = in[i].params();
        out[d].errors() = in[i].errors();
        out[d].charge() = in[i].charge();
        out[d].chi2() = in[i].chi2();
        out[d].score() = in[i].score();
        out[d].label() = in[i].label();
        out[d].nTotalHits() = in[i].nTotalHits();
        out[d].nFoundHits() = in[i].nFoundHits();
        out[d].nSeedHits() = in[i].nSeedHits();
        out[d].etaRegion() = in[i].etaRegion();
        out[d].algorithm() = in[i].algorithm();
        out[d].nOverlaps() = in[i].nOverlaps();
        out[d].duplicate() = in[i].duplicate();
        out[d].outer() = in[i].outer();
        const int nh = in[i].nTotalHits();
        for (int h = 0; h < nh; ++h)
          out[d].hits().hot[h] = in[i].hits().hot[h];
      }
    }
  };

  // ---- Building blocks for filter_comb_cands on the candidate store of the building ----

  // filter_comb_cands(filter, attempt_all_cands = true) per seed: index of the first candidate (front first, then
  // 1, 2, ... in stored order) that passes the LST-step filter, or -1 (seed removed). nFound(j), err(j) are callables.
  template <typename TNFound, typename TErr>
  ALPAKA_FN_HOST_ACC inline int firstPassingCand(int nCands, TNFound nFound, TErr err, int minHitsQF) {
    for (int j = 0; j < nCands; ++j)
      if (lstStepCandFilter(nFound(j), err(j), minHitsQF))
        return j;
    return -1;
  }

  // New region separators after the stable compaction (m_seedEtaSeparators update of filter_comb_cands):
  // newSep[r] = number of passing seeds before the old separator = dest[oldSep[r]] of the exclusive scan.
  struct KernelNewSeparators {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, const int* dest, const int* oldSep, int* newSep, int nRegions) const {
      for (int32_t r : cms::alpakatools::uniform_elements(acc, nRegions))
        newSep[r] = dest[oldSep[r]];
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::clean

#endif
