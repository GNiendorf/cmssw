#ifndef RecoTracker_LSTCore_src_alpaka_OTPull_h
#define RecoTracker_LSTCore_src_alpaka_OTPull_h

#include <bit>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ModulesSoA.h"
#include "RecoTracker/LSTCore/interface/ObjectRangesSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"
#include "RecoTracker/LSTCore/interface/TrackCandidatesSoA.h"
#include "RecoTracker/LSTCore/interface/OTPull.h"
#include "RecoTracker/LSTCore/interface/OTPullCellsSoA.h"
#include "RecoTracker/LSTCore/interface/LSTInputSoA.h"
#include "TrackCandidate.h"
#include "TripletAccessors.h"

// OT-seeded lookup: an unused OT T3 starting at barrel OT layer 1-2, with no hit in any track candidate, takes the
// IT cell that best fits its circle and r-z line; mutual best pairs become OT-only (T4-typed) track candidates.
namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  ALPAKA_FN_ACC ALPAKA_FN_INLINE unsigned long long otpullKey(float chi2, unsigned int idx) {
    return (static_cast<unsigned long long>(std::bit_cast<uint32_t>(chi2)) << 32) | idx;
  }

  constexpr uint32_t kOTPullEmpty = 0xffffffffu;
  constexpr uint32_t kOTPullLastHit = 0x80000000u;

  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t otpullHashSlot(uint32_t key, uint32_t mask) {
    return (key * 2654435761u) & mask;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE int otpullHashFind(const uint32_t* keys, uint32_t mask, uint32_t key) {
    uint32_t slot = otpullHashSlot(key, mask);
    while (true) {
      const uint32_t k = keys[slot];
      if (k == key)
        return static_cast<int>(slot);
      if (k == kOTPullEmpty)
        return -1;
      slot = (slot + 1) & mask;
    }
  }

  // pLS pixel hits (original cluster keys) into an open-addressing set; value = seed index, bit 31 = the seed's last hit
  struct OTPullHashPLSHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  HitsBaseConst hits,
                                  PixelSeedsConst seeds,
                                  HitsITConst hitsIT,
                                  int mode,
                                  uint32_t* keys,
                                  uint32_t* vals,
                                  uint32_t mask) const {
      const unsigned int nHitsOT = hits.nHitsOT();
      const unsigned int nHitsIT = hitsIT.metadata().size();
      for (unsigned int s : cms::alpakatools::uniform_elements(acc, seeds.metadata().size())) {
        const unsigned int nh = seeds.nHits()[s];
        const unsigned int nStored = nh < kMaxPLSHitsInHitsSoA ? nh : kMaxPLSHitsInHitsSoA;
        for (unsigned int k = 0; k < nStored; ++k) {
          const bool last = (k + 1 == nStored);
          if (mode == 1 && !last)
            continue;
          const unsigned int bit = last ? (nh < 8u ? nh : 8u) - 1 : k;
          if ((seeds.hitDetBits()[s] >> bit) & 1)
            continue;  // OT hit
          const unsigned int ih = seeds.firstHit()[s] + k;
          if (ih < nHitsOT || ih - nHitsOT >= nHitsIT)
            continue;
          const uint32_t key = hitsIT.idxs()[ih - nHitsOT];
          if (key == kOTPullEmpty)
            continue;
          const uint32_t empty = kOTPullEmpty;
          uint32_t slot = otpullHashSlot(key, mask);
          while (true) {
            const uint32_t prev = alpaka::atomicCas(acc, &keys[slot], empty, key, alpaka::hierarchy::Blocks{});
            if (prev == empty || prev == key) {
              if (last)
                vals[slot] = s | kOTPullLastHit;  // diagnostics only
              break;
            }
            slot = (slot + 1) & mask;
          }
        }
      }
    }
  };

  // drop the cells sharing a hit with a pLS; bin the others by outer-hit radial shell x phi
  struct OTPullBinCells {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ::lst::OTPullCellsSoA::ConstView cells,
                                  HitsBaseConst hits,
                                  PixelSeedsConst seeds,
                                  OTPullParams p,
                                  const uint32_t* keys,
                                  const uint32_t* vals,
                                  uint32_t mask,
                                  int* bin,
                                  uint32_t* binCount,
                                  unsigned int* counters) const {
      constexpr float kPi = static_cast<float>(M_PI);
      for (unsigned int c : cms::alpakatools::uniform_elements(acc, cells.metadata().size())) {
        bin[c] = -1;
        if (p.plsMatch > 0) {
          const int slot[2] = {otpullHashFind(keys, mask, cells.hi()[c]), otpullHashFind(keys, mask, cells.ho()[c])};
          if (slot[0] >= 0 || slot[1] >= 0) {
            alpaka::atomicAdd(acc, &counters[4], 1u, alpaka::hierarchy::Blocks{});
            // index-space check: a pLS last hit sits at the seed state position (third stored entry of the seed)
            for (int side = 0; side < 2; ++side) {
              if (slot[side] < 0 || !(vals[slot[side]] & kOTPullLastHit))
                continue;
              const unsigned int ih = seeds.firstHit()[vals[slot[side]] & ~kOTPullLastHit] + 2;
              const float dx = (side ? cells.xo()[c] : cells.xi()[c]) - hits.xs()[ih];
              const float dy = (side ? cells.yo()[c] : cells.yi()[c]) - hits.ys()[ih];
              const float d2 = dx * dx + dy * dy;
              alpaka::atomicAdd(
                  acc, &counters[d2 < 2.5e-5f ? 5 : (d2 < 2.5e-3f ? 6 : 7)], 1u, alpaka::hierarchy::Blocks{});
            }
            continue;
          }
        }
        const float xo = cells.xo()[c], yo = cells.yo()[c];
        const float phi = alpaka::math::atan2(acc, yo, xo);
        int b = static_cast<int>(alpaka::math::floor(acc, (phi + kPi) / (2.f * kPi) * p.nPhiBins));
        b = b < 0 ? 0 : (b >= p.nPhiBins ? p.nPhiBins - 1 : b);
        int sh = static_cast<int>(alpaka::math::sqrt(acc, xo * xo + yo * yo) / p.shellWidth);
        sh = sh >= p.nShells ? p.nShells - 1 : sh;
        bin[c] = sh * p.nPhiBins + b;
        alpaka::atomicAdd(acc, &binCount[bin[c]], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  struct OTPullPlaceCells {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ::lst::OTPullCellsSoA::ConstView cells,
                                  const int* bin,
                                  uint32_t* fill,
                                  float* out,
                                  unsigned int* counters) const {
      for (unsigned int c : cms::alpakatools::uniform_elements(acc, cells.metadata().size())) {
        if (bin[c] < 0)
          continue;
        const uint32_t d = alpaka::atomicAdd(acc, &fill[bin[c]], 1u, alpaka::hierarchy::Blocks{});
        float* o = out + 6 * d;
        o[0] = cells.xi()[c];
        o[1] = cells.yi()[c];
        o[2] = cells.zi()[c];
        o[3] = cells.xo()[c];
        o[4] = cells.yo()[c];
        o[5] = cells.zo()[c];
        alpaka::atomicAdd(acc, &counters[8], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  struct OTPullMarkTCHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBaseConst cands,
                                  uint8_t* used,
                                  unsigned int nHits) const {
      for (unsigned int r : cms::alpakatools::uniform_elements(acc, cands.nTrackCandidates())) {
        for (int slot = 0; slot < Params_TC::kLayers; ++slot)
          for (int hs = 0; hs < Params_TC::kHitsPerLayer; ++hs) {
            const unsigned int k = cands.hitIndices()[r][slot][hs];
            if (k != kTCEmptyHitIdx && k < nHits)
              used[k] = 1;
          }
      }
    }
  };

  struct OTPullSearch {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  ObjectRangesConst ranges,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  TripletsOccupancyConst tripletsOcc,
                                  uint16_t nLowerModules,
                                  unsigned int nTripletSlots,
                                  const uint8_t* used,
                                  unsigned int nHits,
                                  const float* cells,
                                  const uint32_t* binStart,
                                  OTPullParams p,
                                  int* bestCell,
                                  float* bestChi2,
                                  unsigned long long* cellBest,
                                  unsigned int* counters) const {
      constexpr float kPi = static_cast<float>(M_PI);
      const float sqc = alpaka::math::sqrt(acc, p.chi2Cut);
      // one thread per triplet slot; a slot counts if it is a filled triplet of a barrel layer 1-2 module
      // (an unfilled slot fails the range test whatever its stale module index)
      for (unsigned int t : cms::alpakatools::uniform_elements(acc, nTripletSlots)) {
        {
          const uint16_t lm = triplets.lowerModuleIndices()[t][0];
          if (lm >= nLowerModules || modules.subdets()[lm] != Barrel ||
              (modules.layers()[lm] != 1 && modules.layers()[lm] != 2))
            continue;
          const int off = ranges.tripletModuleIndices()[lm];
          if (off < 0 || t < static_cast<unsigned int>(off) || t - off >= tripletsOcc.nTriplets()[lm])
            continue;
        }
        if (triplets.partOfPT5()[t] || triplets.partOfT5()[t] || triplets.partOfPT3()[t])
          continue;
        const unsigned int s0 = triplets.segmentIndices()[t][0], s1 = triplets.segmentIndices()[t][1];
        const unsigned int md[3] = {
            segments.mdIndices()[s0][0], segments.mdIndices()[s0][1], segments.mdIndices()[s1][1]};
        bool free = true;
        for (int k = 0; k < 3; ++k) {
          const unsigned int h0 = mds.anchorHitIndices()[md[k]], h1 = mds.outerHitIndices()[md[k]];
          free = free && !(h0 < nHits && used[h0]) && !(h1 < nHits && used[h1]);
        }
        if (!free)
          continue;
        alpaka::atomicAdd(acc, &counters[0], 1u, alpaka::hierarchy::Blocks{});
        float x[3], y[3], z[3], r[3];
        for (int k = 0; k < 3; ++k) {
          x[k] = mds.anchorX()[md[k]];
          y[k] = mds.anchorY()[md[k]];
          z[k] = mds.anchorZ()[md[k]];
          r[k] = mds.anchorRt()[md[k]];
        }
        const float cx = triplets.centerX()[t], cy = triplets.centerY()[t], rad = triplets.radius()[t];
        const float slope = (z[2] - z[0]) / (r[2] - r[0]);
        const float pt = rad / kR1GeVf;
        int pb = 0;
        for (int i = 0; i < OTPullParams::kNPtBins - 1; ++i)
          pb += (pt >= p.ptEdges[i]);
        const float sDo = p.sigma[0][pb], sDz = p.sigma[1][pb], sDa = p.sigma[2][pb], sDs = p.sigma[3][pb],
                    sJ = p.sigma[4][pb];
        const float phi0 = alpaka::math::atan2(acc, y[0], x[0]);
        const float bw = 2.f * kPi / p.nPhiBins;
        const int b0 = static_cast<int>(alpaka::math::floor(acc, (phi0 - p.phiWindow + kPi) / bw));
        const int b1 = static_cast<int>(alpaka::math::floor(acc, (phi0 + p.phiWindow + kPi) / bw));
        float best = p.chi2Cut;
        int bc = -1;
        unsigned int ntest = 0;
        // Per radial shell [ra, rb) of the cell outer hit, the hits within w of the circle sit at phi_c +- alpha with
        // cos(alpha) = (r^2 + d^2 - R'^2) / (2 r d), r in [ra, rb), R' in [R - w, R + w]: only those bins (+-1) are
        // scanned, inside the same +-phiWindow bins as the plain scan, so the candidate set passing the cuts is unchanged.
        // double precision: d^2 - R'^2 cancels for large radii
        const double wDo = sqc * sDo;
        const double dc = alpaka::math::sqrt(acc, double(cx) * cx + double(cy) * cy);
        const float phic = alpaka::math::atan2(acc, cy, cx);
        const double rlo = alpaka::math::max(acc, rad - wDo, 0.), rhi = rad + wDo;
        // (d - R')(d + R') written out to keep the difference exact enough
        auto fcos = [&](double r, double rr) { return (r * r + (dc - rr) * (dc + rr)) / (2. * r * dc); };
        for (int sh = 0; sh < p.nShells; ++sh) {
          const double ra = alpaka::math::max(acc, double(sh) * p.shellWidth, 1e-3);
          const double rb = (sh == p.nShells - 1) ? 1e3 : double(sh + 1) * p.shellWidth;
          const double fmax = alpaka::math::max(acc, fcos(ra, rlo), fcos(rb, rlo));
          double fmin = alpaka::math::min(acc, fcos(ra, rhi), fcos(rb, rhi));
          const double k2 = (dc - rhi) * (dc + rhi);
          if (k2 > 0.) {
            const double rs = alpaka::math::sqrt(acc, k2);
            if (rs > ra && rs < rb)
              fmin = alpaka::math::min(acc, fmin, fcos(rs, rhi));
          }
          if (fmin > 1. || fmax < -1.)
            continue;
          const float aLo = static_cast<float>(alpaka::math::acos(acc, alpaka::math::min(acc, fmax, 1.)));
          const float aHi = static_cast<float>(alpaka::math::acos(acc, alpaka::math::max(acc, fmin, -1.)));
          int lo[2], hi[2];
          for (int side = 0; side < 2; ++side) {
            float plo = side == 0 ? phic + aLo : phic - aHi;
            float phi = side == 0 ? phic + aHi : phic - aLo;
            const float sft = 2.f * kPi * alpaka::math::round(acc, (phi0 - 0.5f * (plo + phi)) / (2.f * kPi));
            lo[side] =
                alpaka::math::max(acc, static_cast<int>(alpaka::math::floor(acc, (plo + sft + kPi) / bw)) - 1, b0);
            hi[side] =
                alpaka::math::min(acc, static_cast<int>(alpaka::math::floor(acc, (phi + sft + kPi) / bw)) + 1, b1);
          }
          int nr = 0;
          int rlo2[2], rhi2[2];
          if (lo[0] <= hi[0] && lo[1] <= hi[1] && lo[1] <= hi[0] + 1 && lo[0] <= hi[1] + 1) {
            rlo2[0] = alpaka::math::min(acc, lo[0], lo[1]);
            rhi2[0] = alpaka::math::max(acc, hi[0], hi[1]);
            nr = 1;
          } else {
            for (int side = 0; side < 2; ++side)
              if (lo[side] <= hi[side]) {
                rlo2[nr] = lo[side];
                rhi2[nr] = hi[side];
                ++nr;
              }
          }
          for (int ir = 0; ir < nr; ++ir) {
            for (int bb = rlo2[ir]; bb <= rhi2[ir]; ++bb) {
              const int b = sh * p.nPhiBins + ((bb % p.nPhiBins) + p.nPhiBins) % p.nPhiBins;
              for (uint32_t c = binStart[b]; c < binStart[b + 1]; ++c) {
                const float* cc = cells + 6 * c;
                const float xi = cc[0], yi = cc[1], zi = cc[2], xo = cc[3], yo = cc[4], zo = cc[5];
                const float dO = alpaka::math::sqrt(acc, (xo - cx) * (xo - cx) + (yo - cy) * (yo - cy)) - rad;
                if (alpaka::math::abs(acc, dO) > sqc * sDo)
                  continue;
                const float ro = alpaka::math::sqrt(acc, xo * xo + yo * yo);
                const float dZ = zo - (z[0] + slope * (ro - r[0]));
                if (alpaka::math::abs(acc, dZ) > sqc * sDz)
                  continue;
                ++ntest;
                const float ri = alpaka::math::sqrt(acc, xi * xi + yi * yi);
                // direction of the cell against the circle tangent at its midpoint (oriented towards the T3)
                const float mx = 0.5f * (xi + xo), my = 0.5f * (yi + yo);
                float tx = -(my - cy), ty = mx - cx;
                if (tx * (x[0] - mx) + ty * (y[0] - my) < 0.f) {
                  tx = -tx;
                  ty = -ty;
                }
                const float vx = xo - xi, vy = yo - yi;
                const float dA = alpaka::math::atan2(acc, vx * ty - vy * tx, vx * tx + vy * ty);
                const float dS = (zo - zi) / (ro - ri) - slope;
                // joint algebraic circle fit of the 2 cell hits and the 3 T3 anchors: rms of the radial residuals
                const double px[5] = {xi, xo, x[0], x[1], x[2]}, py[5] = {yi, yo, y[0], y[1], y[2]};
                double sxx = 0, sxy = 0, syy = 0, sx = 0, sy = 0, sxb = 0, syb = 0, sb = 0;
                for (int k = 0; k < 5; ++k) {
                  const double bk = -(px[k] * px[k] + py[k] * py[k]);
                  sxx += px[k] * px[k];
                  sxy += px[k] * py[k];
                  syy += py[k] * py[k];
                  sx += px[k];
                  sy += py[k];
                  sxb += px[k] * bk;
                  syb += py[k] * bk;
                  sb += bk;
                }
                // normal equations [sxx sxy sx; sxy syy sy; sx sy 5] (D, E, F) = (sxb, syb, sb), Cramer's rule
                const double det = sxx * (syy * 5 - sy * sy) - sxy * (sxy * 5 - sy * sx) + sx * (sxy * sy - syy * sx);
                if (det == 0)
                  continue;
                const double dD = sxb * (syy * 5 - sy * sy) - sxy * (syb * 5 - sy * sb) + sx * (syb * sy - syy * sb);
                const double dE = sxx * (syb * 5 - sb * sy) - sxb * (sxy * 5 - sy * sx) + sx * (sxy * sb - syb * sx);
                const double dF =
                    sxx * (syy * sb - syb * sy) - sxy * (sxy * sb - syb * sx) + sxb * (sxy * sy - syy * sx);
                const double jcx = -0.5 * dD / det, jcy = -0.5 * dE / det;
                const double jr2 = jcx * jcx + jcy * jcy - dF / det;
                const double jr = jr2 > 0 ? alpaka::math::sqrt(acc, jr2) : 0.;
                double ss = 0;
                for (int k = 0; k < 5; ++k) {
                  const double d =
                      alpaka::math::sqrt(acc, (px[k] - jcx) * (px[k] - jcx) + (py[k] - jcy) * (py[k] - jcy)) - jr;
                  ss += d * d;
                }
                const float jrms = static_cast<float>(alpaka::math::sqrt(acc, ss / 5));
                const float chi2 = (dO / sDo) * (dO / sDo) + (dZ / sDz) * (dZ / sDz) + (dA / sDa) * (dA / sDa) +
                                   (dS / sDs) * (dS / sDs) + (jrms / sJ) * (jrms / sJ);
                if (chi2 < best) {
                  best = chi2;
                  bc = static_cast<int>(c);
                }
              }
            }
          }
        }
        alpaka::atomicAdd(acc, &counters[1], ntest, alpaka::hierarchy::Blocks{});
        if (bc >= 0) {
          bestCell[t] = bc;
          bestChi2[t] = best;
          alpaka::atomicMin(acc, &cellBest[bc], otpullKey(best, t), alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

  // mutual best (T3 <-> cell) pairs claim their OT anchor hits, so that no two new candidates share an MD
  struct OTPullClaimHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  const int* bestCell,
                                  const float* bestChi2,
                                  const unsigned long long* cellBest,
                                  unsigned int nTriplets,
                                  unsigned long long* hitBest,
                                  unsigned int nHits) const {
      for (unsigned int t : cms::alpakatools::uniform_elements(acc, nTriplets)) {
        const int c = bestCell[t];
        if (c < 0 || cellBest[c] != otpullKey(bestChi2[t], t))
          continue;
        const unsigned int s0 = triplets.segmentIndices()[t][0], s1 = triplets.segmentIndices()[t][1];
        const unsigned int md[3] = {
            segments.mdIndices()[s0][0], segments.mdIndices()[s0][1], segments.mdIndices()[s1][1]};
        for (int k = 0; k < 3; ++k) {
          const unsigned int h = mds.anchorHitIndices()[md[k]];
          if (h < nHits)
            alpaka::atomicMin(acc, &hitBest[h], otpullKey(bestChi2[t], t), alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

  struct OTPullAddTC {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  const int* bestCell,
                                  const float* bestChi2,
                                  const unsigned long long* cellBest,
                                  const unsigned long long* hitBest,
                                  unsigned int nTriplets,
                                  unsigned int nHits,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  unsigned int nAllocated,
                                  unsigned int* counters) const {
      for (unsigned int t : cms::alpakatools::uniform_elements(acc, nTriplets)) {
        const int c = bestCell[t];
        const unsigned long long key = otpullKey(bestChi2[t], t);
        if (c < 0 || cellBest[c] != key)
          continue;
        const unsigned int s0 = triplets.segmentIndices()[t][0], s1 = triplets.segmentIndices()[t][1];
        const unsigned int md[3] = {
            segments.mdIndices()[s0][0], segments.mdIndices()[s0][1], segments.mdIndices()[s1][1]};
        bool own = true;
        for (int k = 0; k < 3; ++k) {
          const unsigned int h = mds.anchorHitIndices()[md[k]];
          own = own && (h >= nHits || hitBest[h] == key);
        }
        if (!own)
          continue;
        const unsigned int idx = alpaka::atomicAdd(acc, &candsBase.nTrackCandidates(), 1u, alpaka::hierarchy::Blocks{});
        if (idx >= nAllocated) {
          alpaka::atomicSub(acc, &candsBase.nTrackCandidates(), 1u, alpaka::hierarchy::Blocks{});
          alpaka::atomicAdd(acc, &counters[3], 1u, alpaka::hierarchy::Blocks{});
          continue;
        }
        alpaka::atomicAdd(acc, &counters[2], 1u, alpaka::hierarchy::Blocks{});
        uint8_t ll[Params_T4::kLayers];
        uint16_t lmods[Params_T4::kLayers];
        unsigned int hits[2 * Params_T4::kLayers];
        for (int k = 0; k < Params_T4::kLayers; ++k) {
          ll[k] = 0;
          lmods[k] = kTCEmptyLowerModule;
          hits[2 * k] = kTCEmptyHitIdx;
          hits[2 * k + 1] = kTCEmptyHitIdx;
        }
        for (int k = 0; k < 3; ++k) {
          const uint16_t lm = triplets.lowerModuleIndices()[t][k];
          ll[k] = getLogicalLayer(modules, lm);
          lmods[k] = lm;
          hits[2 * k] = mds.anchorHitIndices()[md[k]];
          hits[2 * k + 1] = mds.outerHitIndices()[md[k]];
        }
        addTrackCandidateToMemory(candsBase,
                                  candsExtended,
                                  LSTObjType::T4,
                                  t,
                                  t,
                                  ll,
                                  lmods,
                                  hits,
                                  -1,
                                  triplets.centerX()[t],
                                  triplets.centerY()[t],
                                  triplets.radius()[t],
                                  idx,
                                  t);
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
