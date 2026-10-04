// Stage-B prototype kernels (lane lstin, round 6): see MkFitAlpakaLstInputKernels.h and doc/lstin.txt.
#include <Eigen/Core>  // before any SoA header (Eigen columns of TracksSoA)
#include <cmath>
#include <numbers>

#include <alpaka/alpaka.hpp>

#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "RecoTracker/LSTCore/interface/Common.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/DeviceHitInput.h"
#include "RecoTracker/MkFitAlpaka/interface/math/TkBfield.h"

#include "MkFitAlpakaLstInputKernels.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstin {

  using namespace cms::alpakatools;
  using ::mkfitdev::lstin::kMaxTrackHits;
  using ::mkfitdev::lstin::kNoKey;

  namespace {
    constexpr uint32_t kChunk = 256;  // tracks per scan chunk

    // ROOT::Math::VectorUtil::DeltaPhi(v1, v2) = phi(v2) - phi(v1), wrapped to (-pi, pi]
    ALPAKA_FN_ACC ALPAKA_FN_INLINE float deltaPhiRoot(float phi1, float phi2) {
      float d = phi2 - phi1;
      if (d > float(M_PI))
        d -= float(2.0 * M_PI);
      else if (d <= -float(M_PI))
        d += float(2.0 * M_PI);
      return d;
    }

    // ROOT Eta_FromRhoZ for rho > 0
    ALPAKA_FN_ACC ALPAKA_FN_INLINE float etaRoot(float rho, float z) {
      const float zs = z / rho;
      return std::log(zs + std::sqrt(zs * zs + 1.f));
    }

    struct KernelPLSCompute {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    ::reco::TrackBlocksConstView tracks,
                                    uint32_t maxTracks,
                                    ::reco::TrackingBlocksSoAConstView hits,
                                    uint32_t const* pixKey,
                                    uint32_t const* otKey,
                                    uint32_t const* otDetId,
                                    uint16_t const* otClust,
                                    int32_t const* seedOfTrack,
                                    Params p,
                                    PLS* scratch,
                                    uint32_t* counts) const {
        auto const tk = tracks.tracks();
        auto const th = tracks.trackHits();
        auto const hs = hits.trackingHits();
        const int nT = tk.nTracks();
        if (once_per_grid(acc))
          counts[2] = nT;
        for (uint32_t t : uniform_elements(acc, maxTracks)) {
          PLS& o = scratch[t];
          o.pass = 0;
          o.nToSoA = 0;
          if (int(t) >= nT)
            continue;
          if (static_cast<int>(tk[t].quality()) < p.minQuality)
            continue;
          int32_t seedIdx = int32_t(t);
          if (p.nSeedMap > 0) {  // only tracks with a host seed (the converter needs it)
            seedIdx = t < p.nSeedMap ? seedOfTrack[t] : -1;
            if (seedIdx < 0)
              continue;
          }
          const uint32_t start = t == 0 ? 0 : tk[t - 1].hitOffsets();
          const uint32_t end = tk[t].hitOffsets();
          const uint32_t nh = end - start;
          if (nh > uint32_t(kMaxTrackHits) || nh < 3) {
            alpaka::atomicAdd(acc, &counts[3], 1u, alpaka::hierarchy::Blocks{});
            continue;
          }
          const auto st = tk[t].state();
          const auto cv = tk[t].covariance();
          const float phi0 = st(0), tip = st(1), qpt = st(2), cot = st(3), zip = st(4);
          if (!(std::isfinite(phi0) && std::isfinite(tip) && std::isfinite(qpt) && std::isfinite(cot) &&
                std::isfinite(zip) && qpt != 0.f)) {
            alpaka::atomicAdd(acc, &counts[5], 1u, alpaka::hierarchy::Blocks{});
            continue;
          }
          const int q = qpt > 0.f ? 1 : -1;
          const float ptFit = 1.f / std::abs(qpt);  // Patatrack's pT (field at the origin): the radius is R = ptFit / k
          const float sphi = std::sin(phi0), cphi = std::cos(phi0);

          // hits in Patatrack order (inside-out; the host seed sorts by radius: checked by the comparator)
          uint32_t key[4] = {kNoKey, kNoKey, kNoKey, kNoKey};
          bool isOT[4] = {false, false, false, false};
          uint8_t bits = 0;
          const uint32_t nToBits = nh < ::lst::kMaxPLSHitBitsInHitsSoA ? nh : ::lst::kMaxPLSHitBitsInHitsSoA;
          float lx = 0.f, ly = 0.f, lz = 0.f;
          float bzSum = 0.f;
          for (uint32_t i = 0; i < nh; ++i) {
            const uint32_t id = th[start + i].id();
            if (p.ptFieldCorrection)
              bzSum +=
                  ::mkfitdev::field::tkBz(hs[id].xGlobal() + p.bsx, hs[id].yGlobal() + p.bsy, hs[id].zGlobal() + p.bsz);
            const bool ot = id >= p.nPixelSoA;
            uint32_t k = kNoKey;
            if (!ot)
              k = id < p.nPixKeys ? pixKey[id] : kNoKey;
            else
              k = (id - p.nPixelSoA) < p.nOTSoA ? otKey[id - p.nPixelSoA] : kNoKey;
            if (k == kNoKey)
              alpaka::atomicAdd(acc, &counts[4], 1u, alpaka::hierarchy::Blocks{});
            // slot of this hit among (0, 1, 2, last); hits 0..2 and the last one
            const int slot = i < 3 ? int(i) : (i + 1 == nh ? 3 : -1);
            if (slot == 0) {
              key[0] = k;
              isOT[0] = ot;
            } else if (slot == 1) {
              key[1] = k;
              isOT[1] = ot;
            } else if (slot == 2) {
              key[2] = k;
              isOT[2] = ot;
            } else if (slot == 3) {
              key[3] = k;
              isOT[3] = ot;
            }
            // hitDetBits: bit iSH for iSH < nToBits, the last bit is the last hit
            if (i + 1 < nToBits)
              bits |= uint8_t(ot) << i;
            if (i + 1 == nh)
              bits |= uint8_t(ot) << (nToBits - 1);
            if (i + 1 == nh) {
              lx = hs[id].xGlobal() + p.bsx;
              ly = hs[id].yGlobal() + p.bsy;
              lz = hs[id].zGlobal() + p.bsz;
            }
          }
          if (nh == 3) {  // with 3 hits slot 2 is also the last; slot 3 unused
            key[3] = kNoKey;
          }
          // momentum scale: 1, or <Bz>_hits / Bz(0) (switch ptFieldCorrection, off = round 7)
          const float pt = p.ptFieldCorrection ? ptFit * (bzSum / float(nh)) / p.bz0 : ptFit;

          // PCA (perigee of the Patatrack fit, beam-spot frame): position bs + (tip sin, -tip cos, zip)
          const float x0 = p.bsx + tip * sphi;
          const float y0 = p.bsy - tip * cphi;
          const float px0 = pt * cphi, py0 = pt * sphi, pz0 = pt * cot;
          const float dxy = -tip;
          const float dz = zip;

          // momentum at the outermost hit: uniform-field helix (Patatrack's field) from the PCA
          const float R = ptFit / p.k;
          const float cx = x0 + q * R * sphi;
          const float cy = y0 - q * R * cphi;
          const float lxHit = lx, lyHit = ly, lzHit = lz;
          if (p.pseudoLH == 1) {
            // r3LH = the helix point at the hit's transverse radius rL (circle-circle intersection nearest to the hit;
            // no intersection: the hit projected radially onto the helix circle), z from the helix arc length
            const float rL2 = lx * lx + ly * ly;
            const float d = std::sqrt(cx * cx + cy * cy);
            float hx = 0.f, hy = 0.f;
            const float a = d > 0.f ? (rL2 - R * R + d * d) / (2.f * d) : 0.f;
            const float h2 = rL2 - a * a;
            if (d > 0.f && h2 >= 0.f) {
              const float h = std::sqrt(h2), ux = cx / d, uy = cy / d;
              const float x1 = a * ux - h * uy, y1 = a * uy + h * ux;
              const float x2 = a * ux + h * uy, y2 = a * uy - h * ux;
              const bool first = (x1 - lx) * (x1 - lx) + (y1 - ly) * (y1 - ly) <= (x2 - lx) * (x2 - lx) + (y2 - ly) * (y2 - ly);
              hx = first ? x1 : x2;
              hy = first ? y1 : y2;
            } else {
              const float ex = lx - cx, ey = ly - cy;
              const float en = std::sqrt(ex * ex + ey * ey);
              hx = cx + R * ex / en;
              hy = cy + R * ey / en;
            }
            // transverse arc from the PCA, positive along the momentum (q > 0 turns clockwise)
            const float ax = x0 - cx, ay = y0 - cy, bx = hx - cx, by = hy - cy;
            const float dphi = std::atan2(ax * by - ay * bx, ax * bx + ay * by);
            const float s = -float(q) * R * dphi;
            lz = p.bsz + zip + s * cot;
            lx = hx;
            ly = hy;
          }
          const float ddx = lx - cx, ddy = ly - cy;
          const float dn = std::sqrt(ddx * ddx + ddy * ddy);
          const float pxL = pt * (q * ddy / dn);
          const float pyL = pt * (-q * ddx / dn);
          const float pzL = pz0;

          // PCA used for the pseudo-hits / dxy / dz / superbin (pcaAnchor 1: the helix through the outermost hit with
          // the tangent there, back to its point of closest approach to the beam line, uniform field)
          float xa = x0, ya = y0, za = p.bsz + zip, cpa = cphi, spa = sphi;
          if (p.pcaAnchor == 1) {
            const float hx = lxHit - cx, hy = lyHit - cy;
            const float hn = std::sqrt(hx * hx + hy * hy);
            const float cxa = lxHit - R * hx / hn, cya = lyHit - R * hy / hn;  // centre: the circle through the hit
            const float bx = p.bsx - cxa, by = p.bsy - cya;
            const float bn = std::sqrt(bx * bx + by * by);
            const float ux = bx / bn, uy = by / bn;
            xa = cxa + R * ux;
            ya = cya + R * uy;
            cpa = float(q) * uy;  // tangent at the PCA (direction of motion)
            spa = -float(q) * ux;
            const float ex = lxHit - cxa, ey = lyHit - cya;
            const float dphiA = std::atan2(ux * ey - uy * ex, ux * ex + uy * ey);
            za = lzHit + float(q) * R * dphiA * cot;  // z at the hit minus (arc length s = -q R dphi) x cot
          }
          const float ptIn = std::sqrt(pxL * pxL + pyL * pyL);
          // perigee errors (rho, theta) of the Patatrack covariance, then the host formulas of LSTInputProducer
          const float s2 = 1.f / (1.f + cot * cot);
          const float varRho = p.k * p.k * cv(9);
          const float covRT = p.k * s2 * cv(10);
          const float varTh = s2 * s2 * cv(12);
          const float pmag = pt * std::sqrt(1.f + cot * cot);
          const float qf = float(q);
          const float ptErr = std::sqrt(pt * pt * pmag * pmag / (qf * qf) * varRho +
                                        2.f * std::sqrt(pmag * pmag * pt * pt) / qf * pz0 * covRT + pz0 * pz0 * varTh);
          const float etaErr = std::sqrt(varTh) * pmag / pt;

          if (!(ptIn > p.ptCut - 2 * ptErr))
            continue;
          const float phiL = std::atan2(pyL, pxL);
          const float dPhi = deltaPhiRoot(phiL, std::atan2(ly, lx));
          int8_t pixtype;
          if (ptIn >= 2.0f)
            pixtype = ::lst::PixelType::kHighPt;
          else if (ptIn >= (p.ptCut - 2 * ptErr) and ptIn < 2.0f)
            pixtype = dPhi >= 0 ? ::lst::PixelType::kLowPtPosCurv : ::lst::PixelType::kLowPtNegCurv;
          else
            continue;

          // ::lst::calculateR3FromPCA (on the PCA chosen by pcaAnchor; 0 = Patatrack's, the round 7-9 values)
          const float pxA = p.pcaAnchor == 1 ? pt * cpa : px0, pyA = p.pcaAnchor == 1 ? pt * spa : py0;
          const float dxyA =
              p.pcaAnchor == 1 ? (-(xa - p.bsx) * pyA + (ya - p.bsy) * pxA) / std::sqrt(pxA * pxA + pyA * pyA) : dxy;
          const float dzA = p.pcaAnchor == 1 ? za - p.bsz : dz;
          const float ptP = std::sqrt(pxA * pxA + pyA * pyA);
          const float pP = std::sqrt(pxA * pxA + pyA * pyA + pz0 * pz0);
          const float vz = dzA * ptP * ptP / pP / pP;
          const float vx = -dxyA * pyA / ptP - pxA / pP * pz0 / pP * dzA;
          const float vy = dxyA * pxA / ptP - pyA / pP * pz0 / pP * dzA;
          const float etaP = etaRoot(ptP, pz0);
          const float phiP = std::atan2(pyA, pxA);

          o.pass = 1;
          o.seedIdx = seedIdx;
          o.charge = q;
          o.nHits = nh;
          o.nToSoA = nh < ::lst::kMaxPLSHitsInHitsSoA ? nh : ::lst::kMaxPLSHitsInHitsSoA;
          o.hitDetBits = bits;
          o.isQuad = nh > 3;
          o.pixelType = pixtype;
          o.ptIn = ptIn;
          o.ptErr = ptErr;
          o.px = pxL;
          o.py = pyL;
          o.pz = pzL;
          o.etaErr = etaErr;
          o.eta = etaRoot(ptIn, pzL);
          o.phi = phiL;
          o.deltaPhi = dPhi;
          // pseudo-hits as ::lst::prepareInput
          o.x[0] = vx;
          o.y[0] = vy;
          o.z[0] = vz;
          o.x[1] = ptP;
          o.y[1] = etaP;
          o.z[1] = phiP;
          o.x[2] = lx;
          o.y[2] = ly;
          o.z[2] = lz;
          o.x[3] = lx;
          o.y[3] = dxyA;
          o.z[3] = dzA;
          // detid / cluster size of the hits stored: iSH -> iH (the last slot is the last hit)
          for (int s = 0; s < 4; ++s) {
            const int src = (s + 1 == int(o.nToSoA)) ? (nh > 3 ? 3 : 2) : s;
            const uint32_t k = key[src];
            const bool ot = isOT[src];
            o.detid[s] = ot ? (k != kNoKey ? otDetId[k] : 0u) : ::lst::kPixelModuleId;
            o.clust[s] = ot ? (k != kNoKey ? otClust[k] : uint16_t(0)) : uint16_t(1);
          }
          o.idx[0] = key[0];
          o.idx[1] = key[1];
          o.idx[2] = key[2];
          o.idx[3] = nh > 3 ? key[3] : kNoKey;
          // superbin, with the host's mixed float/double expressions
          const float neta = 25.f, nphi = 72.f, nz = 25.f;
          const int etabin = (etaP + 2.6) / ((2 * 2.6) / neta);
          const int phibin = (phiP + std::numbers::pi_v<float>) / ((2. * std::numbers::pi_v<float>) / nphi);
          const float dzc = dzA < -30.f ? -30.f : (dzA > 30.f ? 30.f : dzA);
          const int dzbin = (dzc + 30) / (2 * 30 / nz);
          o.superbin = (nz * nphi) * etabin + (nz)*phibin + dzbin;
        }
      }
    };

    // scan: per chunk of kChunk tracks the pLS and pseudo-hit counts, a serial scan over chunks, then per-track offsets
    struct KernelChunkCount {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    PLS const* scratch,
                                    uint32_t maxTracks,
                                    uint32_t nChunks,
                                    uint32_t* chunkP,
                                    uint32_t* chunkH) const {
        for (uint32_t c : uniform_elements(acc, nChunks)) {
          uint32_t np = 0, nhs = 0;
          const uint32_t e = (c + 1) * kChunk < maxTracks ? (c + 1) * kChunk : maxTracks;
          for (uint32_t t = c * kChunk; t < e; ++t)
            if (scratch[t].pass) {
              ++np;
              nhs += scratch[t].nToSoA;
            }
          chunkP[c] = np;
          chunkH[c] = nhs;
        }
      }
    };

    struct KernelChunkScan {
      ALPAKA_FN_ACC void operator()(
          Acc1D const& acc, uint32_t nChunks, uint32_t* chunkP, uint32_t* chunkH, uint32_t* counts) const {
        if (once_per_grid(acc)) {
          uint32_t sp = 0, sh = 0;
          for (uint32_t c = 0; c < nChunks; ++c) {
            const uint32_t a = chunkP[c], b = chunkH[c];
            chunkP[c] = sp;
            chunkH[c] = sh;
            sp += a;
            sh += b;
          }
          counts[0] = sp;
          counts[1] = sh;
        }
      }
    };

    struct KernelChunkOffsets {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    PLS const* scratch,
                                    uint32_t maxTracks,
                                    uint32_t nChunks,
                                    uint32_t const* chunkP,
                                    uint32_t const* chunkH,
                                    uint32_t* pIdx,
                                    uint32_t* hOff) const {
        for (uint32_t c : uniform_elements(acc, nChunks)) {
          uint32_t np = chunkP[c], nhs = chunkH[c];
          const uint32_t e = (c + 1) * kChunk < maxTracks ? (c + 1) * kChunk : maxTracks;
          for (uint32_t t = c * kChunk; t < e; ++t) {
            pIdx[t] = np;
            hOff[t] = nhs;
            if (scratch[t].pass) {
              ++np;
              nhs += scratch[t].nToSoA;
            }
          }
        }
      }
    };

    template <::mkfitdev::Contract M>
    struct KernelFillOT {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    ::lst::LSTInputView out,
                                    OTModule const* modules,
                                    int32_t const* otModule,
                                    uint32_t const* otDetId,
                                    float const* otLx,
                                    float const* otLy,
                                    uint16_t const* otClust,
                                    uint32_t nOT) const {
        auto hits = out.hits();
        if (once_per_grid(acc))
          hits.nHitsOT() = nOT;
        for (uint32_t i : uniform_elements(acc, nOT)) {
          OTModule const& m = modules[otModule[i]];
          float g[3];
          ::mkfitdev::localToGlobal<M>(m, otLx[i], otLy[i], g);
          hits[i].xs() = g[0];
          hits[i].ys() = g[1];
          hits[i].zs() = g[2];
          hits[i].detid() = otDetId[i];
          hits[i].clustsize() = otClust[i];
        }
      }
    };

    // round 9: OT hits from the device OT rechit SoA (row = cluster key = legacy rechit index)
    struct KernelFillOTSoA {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    ::lst::LSTInputView out,
                                    ::mkfitdev::OTRecHitSoA::ConstView ot,
                                    uint32_t nOT) const {
        auto hits = out.hits();
        if (once_per_grid(acc))
          hits.nHitsOT() = nOT;
        for (uint32_t i : uniform_elements(acc, nOT)) {
          hits[i].xs() = ot[i].gx();
          hits[i].ys() = ot[i].gy();
          hits[i].zs() = ot[i].gz();
          hits[i].detid() = ot[i].detId();
          hits[i].clustsize() = ot[i].clustSize();
        }
      }
    };

    struct KernelFillPLS {
      ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                    ::lst::LSTInputView out,
                                    uint32_t maxTracks,
                                    uint32_t nPLSCap,
                                    uint32_t nOT,
                                    PLS const* scratch,
                                    uint32_t const* pIdx,
                                    uint32_t const* hOff) const {
        auto hits = out.hits();
        auto ps = out.pixelSeeds();
        for (uint32_t t : uniform_elements(acc, maxTracks)) {
          PLS const& o = scratch[t];
          if (!o.pass)
            continue;
          const uint32_t h0 = nOT + hOff[t];
          for (uint32_t s = 0; s < o.nToSoA; ++s) {
            hits[h0 + s].xs() = o.x[s];
            hits[h0 + s].ys() = o.y[s];
            hits[h0 + s].zs() = o.z[s];
            hits[h0 + s].detid() = o.detid[s];
            hits[h0 + s].clustsize() = o.clust[s];
            // first three hit keys + the last one (3-hit seed: hit 2 is the last); O10-1 (#280): pLS rows only
            out.hitsIT()[hOff[t] + s].idxs() = o.idx[s];
          }
          const uint32_t r = pIdx[t];
          if (r >= nPLSCap)
            continue;
          ps[r].firstHit() = h0;
          ps[r].nHits() = o.nHits;
          ps[r].hitDetBits() = o.hitDetBits;
          ps[r].deltaPhi() = o.deltaPhi;
          ps[r].seedIdx() = o.seedIdx;
          ps[r].charge() = o.charge;
          ps[r].superbin() = o.superbin;
          ps[r].pixelType() = static_cast<::lst::PixelType>(o.pixelType);
          ps[r].isQuad() = o.isQuad;
          ps[r].ptIn() = o.ptIn;
          ps[r].ptErr() = o.ptErr;
          ps[r].px() = o.px;
          ps[r].py() = o.py;
          ps[r].pz() = o.pz;
          ps[r].etaErr() = o.etaErr;
          ps[r].eta() = o.eta;
          ps[r].phi() = o.phi;
        }
      }
    };
  }  // namespace

  void launchPLS(Queue& queue,
                 ::reco::TrackBlocksConstView tracks,
                 uint32_t maxTracks,
                 ::reco::TrackingBlocksSoAConstView hits,
                 uint32_t const* pixKey,
                 uint32_t const* otKey,
                 uint32_t const* otDetId,
                 uint16_t const* otClust,
                 int32_t const* seedOfTrack,
                 Params p,
                 PLS* scratch,
                 uint32_t* pIdx,
                 uint32_t* hOff,
                 uint32_t* counts) {
    if (maxTracks == 0)
      return;
    constexpr uint32_t kBlock = 128;
    alpaka::exec<Acc1D>(queue,
                        make_workdiv<Acc1D>(divide_up_by(maxTracks, kBlock), kBlock),
                        KernelPLSCompute{},
                        tracks,
                        maxTracks,
                        hits,
                        pixKey,
                        otKey,
                        otDetId,
                        otClust,
                        seedOfTrack,
                        p,
                        scratch,
                        counts);
    const uint32_t nChunks = divide_up_by(maxTracks, kChunk);
    // chunk sums: counts[kNCounts .. kNCounts + 2 nChunks) (the producer sizes the counts buffer)
    uint32_t* chunkP = counts + ::mkfitdev::lstin::kNCounts;
    uint32_t* chunkH = chunkP + nChunks;
    alpaka::exec<Acc1D>(queue,
                        make_workdiv<Acc1D>(divide_up_by(nChunks, 64u), 64u),
                        KernelChunkCount{},
                        scratch,
                        maxTracks,
                        nChunks,
                        chunkP,
                        chunkH);
    alpaka::exec<Acc1D>(queue, make_workdiv<Acc1D>(1, 1), KernelChunkScan{}, nChunks, chunkP, chunkH, counts);
    alpaka::exec<Acc1D>(queue,
                        make_workdiv<Acc1D>(divide_up_by(nChunks, 64u), 64u),
                        KernelChunkOffsets{},
                        scratch,
                        maxTracks,
                        nChunks,
                        chunkP,
                        chunkH,
                        pIdx,
                        hOff);
  }

  void launchFill(Queue& queue,
                  ::lst::LSTInputView out,
                  uint32_t maxTracks,
                  uint32_t nPLSCap,
                  OTModule const* modules,
                  int32_t const* otModule,
                  uint32_t const* otDetId,
                  float const* otLx,
                  float const* otLy,
                  uint16_t const* otClust,
                  Params p,
                  PLS const* scratch,
                  uint32_t const* pIdx,
                  uint32_t const* hOff,
                  uint32_t const* counts) {
    constexpr uint32_t kBlock = 128;
    const uint32_t nOT = p.nOT;
    auto wd = make_workdiv<Acc1D>(divide_up_by(nOT > 0 ? nOT : 1u, kBlock), kBlock);
    if (p.contract == 0)
      alpaka::exec<Acc1D>(queue,
                          wd,
                          KernelFillOT<::mkfitdev::Contract::kFuseSecond>{},
                          out,
                          modules,
                          otModule,
                          otDetId,
                          otLx,
                          otLy,
                          otClust,
                          nOT);
    else if (p.contract == 2)
      alpaka::exec<Acc1D>(queue,
                          wd,
                          KernelFillOT<::mkfitdev::Contract::kNoFuse>{},
                          out,
                          modules,
                          otModule,
                          otDetId,
                          otLx,
                          otLy,
                          otClust,
                          nOT);
    else
      alpaka::exec<Acc1D>(queue,
                          wd,
                          KernelFillOT<::mkfitdev::Contract::kFuseFirst>{},
                          out,
                          modules,
                          otModule,
                          otDetId,
                          otLx,
                          otLy,
                          otClust,
                          nOT);
    if (maxTracks > 0)
      alpaka::exec<Acc1D>(queue,
                          make_workdiv<Acc1D>(divide_up_by(maxTracks, kBlock), kBlock),
                          KernelFillPLS{},
                          out,
                          maxTracks,
                          nPLSCap,
                          nOT,
                          scratch,
                          pIdx,
                          hOff);
    (void)counts;
  }

  void launchFillSoA(Queue& queue,
                     ::lst::LSTInputView out,
                     uint32_t maxTracks,
                     uint32_t nPLSCap,
                     ::mkfitdev::OTRecHitSoA::ConstView ot,
                     Params p,
                     PLS const* scratch,
                     uint32_t const* pIdx,
                     uint32_t const* hOff) {
    constexpr uint32_t kBlock = 128;
    const uint32_t nOT = p.nOT;
    alpaka::exec<Acc1D>(
        queue, make_workdiv<Acc1D>(divide_up_by(nOT > 0 ? nOT : 1u, kBlock), kBlock), KernelFillOTSoA{}, out, ot, nOT);
    if (maxTracks > 0)
      alpaka::exec<Acc1D>(queue,
                          make_workdiv<Acc1D>(divide_up_by(maxTracks, kBlock), kBlock),
                          KernelFillPLS{},
                          out,
                          maxTracks,
                          nPLSCap,
                          nOT,
                          scratch,
                          pIdx,
                          hOff);
  }

  void launchFillOTSoAOnly(Queue& queue, ::lst::LSTInputView out, ::mkfitdev::OTRecHitSoA::ConstView ot, uint32_t nOT) {
    constexpr uint32_t kBlock = 128;
    alpaka::exec<Acc1D>(
        queue, make_workdiv<Acc1D>(divide_up_by(nOT > 0 ? nOT : 1u, kBlock), kBlock), KernelFillOTSoA{}, out, ot, nOT);
  }

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstin
