#ifndef RecoTracker_LSTCore_src_alpaka_ChainAttach_h
#define RecoTracker_LSTCore_src_alpaka_ChainAttach_h

#include <bit>
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainNodesSoA.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"
#include "RecoTracker/LSTCore/interface/LSTInputSoA.h"
#include "RecoTracker/LSTCore/interface/MiniDoubletsSoA.h"
#include "RecoTracker/LSTCore/interface/SegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TripletsSoA.h"
#include "RecoTracker/LSTCore/interface/ObjectRangesSoA.h"
#include "RecoTracker/LSTCore/interface/PixelSegmentsSoA.h"
#include "RecoTracker/LSTCore/interface/TrackCandidatesSoA.h"

#include "AttachNetworkWeights.h"
#include "ChainGate.h"
#include "NeuralNetwork.h"

// K8 pixel attach, phase P2.4 of standalone/prototype/P2_PORT_MAP.md.
//
// Reference implementation: prototype/PixelAttach.{h,cc} (the pre-records, the analytic prefilter
// and the 19 pair features), prototype/AttachInference.cc (the r2 head), prototype/AttachDelivery.cc
// (gaStageChains + resolveContention) and the -A 4 blocks of prototype/main.cc (target selection,
// -RD seed dedup, the -RPS / contention suppression of carried rows, the in-place type-7 upgrade),
// all at the M19 frozen flags. -RT3 is 0 in the freeze, so the bare-T3 target universe (stage B)
// does not exist here; every target is an accepted chain, and feature 18 targetType is the
// structural constant 0.
//
// Stages:
//   K8-0  ChainAttachPlsPre        per-pLS hoisted record          (PixelAttach.cc makePlsPre)
//   K8-0b ChainAttachSelectTargets the accepted, nLayers >= 5, dca-eligible chains IN K9 ORDER
//   K8-0c ChainAttachTargetPre     per-target record               (makeChainPre + makeChainPreGeom)
//   K8a   ChainAttachGridBounds / GridCount / GridPrefix / GridScatter   the grid prefilter
//   K8b   ChainAttachScore         candidate iteration + 19 features + the r2 head, best per target
//   K8c   ChainAttachContend       one pLS one owner, then the -RD seed-family dedup
//   K8d   ChainSuppressCarriedTCs  the contention / -RPS retirement of carried pixel rows
//         (the type-7 upgrade itself lives in ChainArbitrate.h's ChainEmitTCs)
//
// =====================================================================================
// THE GRID (K8a) -- what it replaces and why its candidate set is provably a SUPERSET
// =====================================================================================
// prototype/PixelAttach.cc:456-495 is `for each target { for (int p = 0; p < nPls; ++p) evalPair }`
// -- about 2.5k x 18.4k = 4.6e7 probes per event, 201.8 ms, 0.28% survival. The grid replaces the
// inner scan; it changes NOTHING about the predicate, which is still evaluated exactly, on exactly
// the same arithmetic, for every candidate the grid returns.
//
// The frozen predicate (evalPair) is
//     |pls.tanLambda - tgt.tanLambda| < prefDTanL (0.6)
//   AND
//     |wrap(phiDir_p(tgt.rtInner) - tgt.chordPhi)| < prefDPhi (0.4)
// where phiDir_p(r) is the direction of motion of the pLS helix where it crosses radius r
// outbound. Both windows are one-dimensional, so the grid is keyed on exactly the three
// quantities they are written in: the target's rtInner, its tanLambda and its chordPhi.
//
// (1) tanLambda axis. Cells of width prefDTanL over [-30, 30], clamped at both ends. A pLS is
//     scattered into the single cell of its own tanLambda; a target scans every cell that overlaps
//     [tanL - prefDTanL, tanL + prefDTanL]. Any pair inside the window therefore shares a scanned
//     cell. Clamping is safe because two clamped values are both in the edge cell, and a clamped
//     value within 0.6 of an unclamped one puts the unclamped one in the same or the neighbouring
//     cell, which is scanned.
//
// (2) r axis. Fixed 16 cm bins, used ONLY as a bucketing device. For every bin the kernel measures
//     the MIN and MAX rtInner of the targets that actually landed in it, so the bin EDGES never
//     enter the argument -- the interval a pLS is scattered against is the exact hull of the
//     targets it will be compared with. (In the barrel this collapses to a couple of centimetres.)
//
// (3) phi axis. For pLS p and r bin j, the kernel computes the exact RANGE of phiDir_p over
//     [rMin_j, rMax_j] and scatters p into every phi cell that range touches. A target scans every
//     cell overlapping [chordPhi - prefDPhi, chordPhi + prefDPhi]. Because rtInner in [rMin_j,
//     rMax_j], phiDir_p(rtInner) is inside the scattered range, so p occupies the cell containing
//     phiDir_p(rtInner); and if the pair passes the window then phiDir_p(rtInner) is inside the
//     scanned interval, so that cell is scanned. SUPERSET, with no slack term to bound.
//
// The range is exact because phiDir_p is MONOTONE in r. Writing C for the pLS circle centre,
// d = |C|, R its radius and s its rotation sense, the crossing point at radius r is
// P = a u + sgn h u_perp with u = C/d, a = (d^2 + r^2 - R^2) / 2d, h = sqrt(r^2 - a^2); the
// reference picks the branch with t.P > 0, and t.P = -s sgn d h, so the branch is ALWAYS
// sgn = -s (never data-dependent). Then phiDir = phi_u + beta + s pi/2 with
// beta = atan2(sgn h, a - d), and
//     d beta / d r = -sgn r / (d h) = s r / (d h),
// which has a constant sign. Moreover sgn h = -s h keeps beta in [-pi, 0] for s = +1 and in
// [0, pi] for s = -1, so the traversal is a monotone arc of at most half a turn and its extremes
// are its endpoints -- no wrap-around bookkeeping is needed. Where the bin leaves the reachable
// annulus [|d - R|, d + R] the reference falls back to phiDir = pls.phi; that case is detected by
// h^2 < 0 at a bin endpoint and the fallback direction is added to the occupied cells as well.
// kAttachPhiPad covers float rounding between this endpoint evaluation and the decision path's own
// atan2; it is 1e-3 rad against a 0.4 rad window.
//
// The offline verification of this construction (grid candidate set vs the exhaustive analytic
// scan, per event) is the LST_CHAIN_ATTACH_AUDIT sidecar in LSTEvent.dev.cc.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainattach {
    // prototype/PixelAttach.cc anonymous namespace.
    constexpr float kEps = 1e-6f;
    constexpr float kPi = 3.14159265358979323846f;
    // -RD seed-family dedup scratch. The table holds one entry per (pixel hit index, kept owner)
    // pair; at PU200 that is a few hundred owners x <= 4 hits, so a 16k-slot open-addressed table
    // never exceeds a quarter load. Power of two: the probe wraps with a mask.
    constexpr uint32_t kSeedHashSlots = 16384u;
    constexpr uint32_t kSeedHashEmpty = 0xFFFFFFFFu;
    constexpr int kSeedDedupSeenMax = 64;
    // Diagnostic counters (never read by a decision):
    //   0 grid entries   1 candidates iterated   2 pairs scored   3 per-target picks
    //   4 attached after contention   5 -RD revocations   6 carried rows retired
    //   7 seed-dedup owner-slot overflows   9 -RD logit-tie census
    //  10 grid candidates skipped as a repeat of the same pLS inside one target's cell walk
    //  11 -XC pass-1 buffer overflow census   12 -CCS suppressed chains
    constexpr uint32_t kStats = 14u;
  }  // namespace chainattach

  // prototype/PixelAttach.cc wrapPhi is chainWrapPhi verbatim (same float pi, same while loops);
  // sanitizeOne is the Features.cc convention, tested on the bit pattern because the standalone
  // device library is built with -Ofast and would fold std::isnan / std::isinf to constants.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float attachSanitize(float x) {
    if (chainIsNan(x))
      return 0.f;
    if (chainIsInf(x))
      return (x > 0.f) ? 1e12f : -1e12f;
    return x;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE float attachFabs(float v) { return (v < 0.f) ? -v : v; }

  // The frozen per-input preprocessing of the r2 head for ONE input index: the Features.cc sanitize
  // followed by the generated header's optional log10(1 + x), clip and standardize, in that order.
  //
  // P2.6b. This is the SINGLE definition of that chain. Twelve of the nineteen pair features are
  // pure per-pLS or per-target quantities, so their preprocessed value is a property of the pLS or
  // of the target and is computed ONCE, in the pre-record kernels, instead of ~150 times per pLS in
  // the scoring loop (that included the head's one log10). The per-pair features go through the same
  // function template, with the same compile-time index, so the hoisted and the on-the-fly halves
  // of the input vector cannot drift from one another or from the frozen contract.
  template <int I, typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float attachStdz(TAcc const& acc, float raw) {
    static_assert(I >= 0 && I < dnn::attachmlp::kInput, "attach feature index outside the head input");
    float v = attachSanitize(raw);
    if (dnn::attachmlp::kLog10p1[I])
      v = alpaka::math::log10(acc, 1.f + v);
    v = chainMinf(chainMaxf(v, dnn::attachmlp::kClipLo[I]), dnn::attachmlp::kClipHi[I]);
    return (v - dnn::attachmlp::kFeatMean[I]) / dnn::attachmlp::kFeatStd[I];
  }

  // Fibonacci hash of a hit index onto the -RD scratch table.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t attachSeedHash(uint32_t key) {
    uint32_t h = key * 2654435761u;
    h ^= h >> 15;
    return h & (chainattach::kSeedHashSlots - 1u);
  }

  // "no pair yet" sentinel. The reference uses -infinity; a finite value is used here because
  // -ffinite-math-only makes infinities fair game for the optimizer, and it is never compared
  // against anything except through the `bestPls < 0` / `tgtPls < 0` guards that precede it.
  constexpr float kAttachNoLogit = -1e30f;

  // ------------------------------------------------------------------------------------------
  // Per-pLS hoisted record, field for field prototype/PixelAttach.cc PlsPre.
  // Also the GRID ITEM type: the scatter writes the whole record into the cell, so the candidate
  // loop reads one contiguous array instead of chasing 18.4k scattered 72-byte records through the
  // cache. That is the single biggest term in the attach wall time.
  //
  // P2.6b: the seven pair-invariant pLS features (log10Pt, ptErrRel, etaErr, charge, isQuad,
  // log10R, plsDeltaPhi) are stored ALREADY PREPROCESSED in xs[0..6] instead of raw -- they are
  // read by nothing else -- so the record does not grow and the scoring loop assembles those
  // inputs with a copy. phiMask is the 16-bit phi-cell set this copy of the record was scattered
  // under, which is what makes the multi-cell duplicate suppression in K8b exact.
  struct AttachPlsPre {
    uint32_t row;  // the pLS row this record belongs to
    float xs[7];   // head inputs 0..6, preprocessed (attachStdz<0..6>)
    float tanLambda;    // pz / max(pt, eps)
    float kappaSigned;  // rotSign / max(circleRadius, eps)
    float rotSign;      // -charge
    float phi;          // the dPhi fallback direction == the SEED phi (pixelSeeds.phi())
    float cx, cy, r, d;
    float hit0z, rt0;
    // A11 / A15 per-seed resolved quantities, computed once here so the scoring loop pays one
    // register read per pair instead of a band lookup:
    //   eta        the SEED eta (pixelSeeds.eta()) -- the -XC dR^2 candidate side
    //   attachThr  the banded -a / -a2 / -a3 delivery margin for this seed's |eta| band
    //   xcThr      the banded -XCT / -XCT2 / -XCT3 crossclean bar for the same band
    //   isQuad     pixelSeeds.isQuad(), the -XC candidacy requirement
    float eta;
    float attachThr;
    float xcThr;
    uint8_t isQuad;
    uint16_t phiMask;  // set by the grid scatter; 0 in the per-pLS array
  };

  // One filtered (chain, seed) pair of the -XC bare-chain arm, appended at attach-scoring time
  // (SPEC_XC section 5.2 pass 1: logit >= the |seed eta|-banded xcTheta AND dR^2 < xcDR2Chain
  // against the chain's innermost-T3 direction). Pass 2 resolves it after delivery: the chain must
  // have emitted a SEEDLESS TC and the seed must still be unowned.
  struct ChainXcPair {
    uint32_t chain;
    uint32_t pls;
  };

  // Per-target record, prototype/PixelAttach.cc TargetPre restricted to the chain kind.
  // xs[0..4] are head inputs 7..11 preprocessed; fitKappa and tanLambda stay raw as well because
  // the per-pair features dKappa and dTanLambda are built from them.
  struct AttachTargetPre {
    float rtInner, zInner, chordPhi, tanLambda;
    float fitKappa;
    float xs[5];  // head inputs 7..11, preprocessed (attachStdz<7..11>)
    float rotSign, centerX, centerY;
    // The emitted-TC direction of a CHAIN target: eta/phi of the innermost member T3, exactly the
    // values K10 gives the TC. Exact at scoring time because extendMode = 1 (outer end only) never
    // changes the innermost member. Consumed by the -XC bare-chain arm's pass-1 dR^2 window; unused
    // (0) for bare-T3 targets.
    float tcEta, tcPhi;
    uint32_t chain;
    uint8_t centerValid;
  };

  // ------------------------------------------------------------------------------------------
  // The direction of motion of the pLS helix where it crosses radius R1 outbound, EXACTLY as
  // prototype/PixelAttach.cc dPhiAtInnermost computes it (same float operations, same order, same
  // two-branch max-radial selection, same fallback).
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float attachPhiDirAt(TAcc const& acc, AttachPlsPre const& pls, float R1) {
    float phiDir = pls.phi;
    if (pls.d > chainattach::kEps) {
      float const a = (pls.d * pls.d + R1 * R1 - pls.r * pls.r) / (2.f * pls.d);
      float const h2 = R1 * R1 - a * a;
      if (h2 >= 0.f) {
        float const h = alpaka::math::sqrt(acc, h2);
        float const ux = pls.cx / pls.d, uy = pls.cy / pls.d;
        float bestTx = 0.f, bestTy = 0.f, bestRadial = 0.f;
        bool haveBest = false;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
          float const px = a * ux - static_cast<float>(sgn) * h * uy;
          float const py = a * uy + static_cast<float>(sgn) * h * ux;
          float const tx = pls.rotSign * (-(py - pls.cy));
          float const ty = pls.rotSign * (px - pls.cx);
          float const radial = tx * px + ty * py;
          if (!haveBest || radial > bestRadial) {
            haveBest = true;
            bestRadial = radial;
            bestTx = tx;
            bestTy = ty;
          }
        }
        phiDir = alpaka::math::atan2(acc, bestTy, bestTx);
      }
    }
    return phiDir;
  }

  // ------------------------------------------------------------------------------------------
  // Grid axis helpers. All three are pure functions of the frozen window widths.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE int attachRBin(float rt) {
    int b = static_cast<int>(rt / kAttachRBinWidth);
    if (b < 0)
      b = 0;
    if (b >= kAttachRBins)
      b = kAttachRBins - 1;
    return b;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE int attachTanLBin(float t, float width) {
    float const f = (t - kAttachTanLLo) / width;
    int b = (f < 0.f) ? -1 : static_cast<int>(f);
    if (b < 0)
      b = 0;
    if (b >= kAttachTanLBins)
      b = kAttachTanLBins - 1;
    return b;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE int attachPhiBin(float phi) {
    // phi is folded into [0, 2 pi) first so the bin index needs no negative-floor special case.
    float const twoPi = 2.f * chainattach::kPi;
    float x = phi;
    while (x < 0.f)
      x += twoPi;
    while (x >= twoPi)
      x -= twoPi;
    int b = static_cast<int>(x * (static_cast<float>(kAttachPhiBins) / twoPi));
    if (b < 0)
      b = 0;
    if (b >= kAttachPhiBins)
      b = kAttachPhiBins - 1;
    return b;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t attachCellId(int rb, int tb, int pb) {
    return static_cast<uint32_t>((rb * kAttachTanLBins + tb) * kAttachPhiBins + pb);
  }

  static constexpr uint32_t kAttachCells =
      static_cast<uint32_t>(kAttachRBins) * kAttachTanLBins * kAttachPhiBins;

  // The phi-cell set of one pLS fits in a 16-bit word (kAttachPhiBins == 16); this is its mask.
  static_assert(kAttachPhiBins <= 16, "the phi-cell set is carried in a uint16_t");
  static constexpr uint32_t kAttachPhiCellMask = (kAttachPhiBins >= 32) ? 0xFFFFFFFFu
                                                                       : ((1u << kAttachPhiBins) - 1u);

  // How many surviving pairs the host backends evaluate through the r2 head at a time. 24 hidden
  // units x 16 lanes is 384 accumulators, which the AVX-512 register file plus L1 absorbs; the
  // device path uses 1 (see ChainAttachScore).
  static constexpr int kAttachScoreBatch = 16;

  // The 16-bit set of phi cells pLS `pls` can serve for r bin [rLo, rHi]. Returns 0 when the bin
  // holds no target. Building a MASK rather than emitting intervals removes the arc / fallback
  // overlap for free, so the count pass and the scatter pass agree by construction.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t attachPhiCellMask(
      TAcc const& acc, AttachPlsPre const& pls, float rLo, float rHi, float pad) {
    uint32_t mask = 0u;
    if (!(rHi >= rLo))
      return 0u;

    if (!(pls.d > chainattach::kEps)) {
      // Degenerate centre: the reference returns pls.phi at every radius.
      mask |= (1u << attachPhiBin(pls.phi));
      return mask;
    }

    // h^2 at the two bin ends decides whether the fallback branch is reachable inside the bin.
    auto h2At = [&](float R1) {
      float const a = (pls.d * pls.d + R1 * R1 - pls.r * pls.r) / (2.f * pls.d);
      return R1 * R1 - a * a;
    };
    bool const fallbackReachable = (h2At(rLo) < 0.f) || (h2At(rHi) < 0.f);
    if (fallbackReachable)
      mask |= (1u << attachPhiBin(pls.phi));

    // Clip the bin to the reachable annulus [|d - R|, d + R] and take the monotone arc endpoints.
    float const annLo = attachFabs(pls.d - pls.r);
    float const annHi = pls.d + pls.r;
    float const cLo = (rLo > annLo) ? rLo : annLo;
    float const cHi = (rHi < annHi) ? rHi : annHi;
    if (cLo <= cHi) {
      float const p0 = attachPhiDirAt(acc, pls, cLo);
      float const p1 = attachPhiDirAt(acc, pls, cHi);
      // Both endpoints sit on one monotone arc of at most half a turn, so the shorter signed
      // difference IS the traversed arc.
      float const delta = chainWrapPhi(p1 - p0);
      float lo = (delta >= 0.f) ? p0 : p1;
      float const span = attachFabs(delta);
      lo -= pad;
      float const hi = lo + span + 2.f * pad;
      // The occupied cells are exactly bin(lo) .. bin(hi) walking forward; the arc is at most half
      // a turn plus two pads, so the walk can never lap the ring.
      int const bLo = attachPhiBin(lo), bHi = attachPhiBin(hi);
      int nCells = bHi - bLo;
      if (nCells < 0)
        nCells += kAttachPhiBins;
      ++nCells;
      if (nCells > kAttachPhiBins)
        nCells = kAttachPhiBins;
      for (int k = 0; k < nCells; ++k)
        mask |= (1u << ((bLo + k) % kAttachPhiBins));
    }
    return mask;
  }

  // ------------------------------------------------------------------------------------------
  // K8-0. The per-pLS record.
  struct ChainAttachPlsPre {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  PixelSegmentsConst pixelSegments,
                                  AttachPlsPre* out,
                                  uint32_t nPls,
                                  ChainConfig cfg) const {
      for (uint32_t p : cms::alpakatools::uniform_elements(acc, nPls)) {
        AttachPlsPre o;
        float const pt = chainMaxf(pixelSeeds.ptIn()[p], chainattach::kEps);
        float const charge = static_cast<float>(pixelSeeds.charge()[p]);
        float const r = chainMaxf(pixelSegments.circleRadius()[p], chainattach::kEps);
        o.xs[0] = attachStdz<0>(acc, alpaka::math::log10(acc, pt));
        o.xs[1] = attachStdz<1>(acc, pixelSeeds.ptErr()[p] / pt);
        o.xs[2] = attachStdz<2>(acc, pixelSeeds.etaErr()[p]);
        o.xs[3] = attachStdz<3>(acc, charge);
        o.xs[4] = attachStdz<4>(acc, pixelSeeds.isQuad()[p] ? 1.f : 0.f);
        o.xs[5] = attachStdz<5>(acc, alpaka::math::log10(acc, r));
        o.xs[6] = attachStdz<6>(acc, pixelSeeds.deltaPhi()[p]);
        o.tanLambda = pixelSeeds.pz()[p] / pt;
        o.rotSign = (charge > 0.f) ? -1.f : 1.f;
        o.kappaSigned = o.rotSign / r;
        o.phi = pixelSeeds.phi()[p];
        // The banded per-seed thresholds (A11 -a bands, A15 -XCT bands), both on the SEED |eta| at
        // the literal 1.1 / 1.7 boundaries the reference hardcodes (AttachDelivery.cc:95,
        // main.cc:5023-5024). Resolved once per seed so the pair loop never branches on eta.
        o.eta = pixelSeeds.eta()[p];
        float const ae = attachFabs(o.eta);
        o.attachThr = (ae < 1.1f) ? cfg.attachTheta : ((ae < 1.7f) ? cfg.attachThetaT : cfg.attachThetaE);
        o.xcThr = (ae < 1.1f) ? cfg.xcTheta : ((ae < 1.7f) ? cfg.xcThetaT : cfg.xcThetaE);
        o.isQuad = pixelSeeds.isQuad()[p] ? 1u : 0u;
        o.cx = pixelSegments.circleCenterX()[p];
        o.cy = pixelSegments.circleCenterY()[p];
        o.r = r;
        o.d = alpaka::math::sqrt(acc, o.cx * o.cx + o.cy * o.cy);
        o.hit0z = pixelSeeds.hit0Z()[p];
        float const hx = pixelSeeds.hit0X()[p], hy = pixelSeeds.hit0Y()[p];
        o.rt0 = alpaka::math::sqrt(acc, hx * hx + hy * hy);
        o.row = p;
        o.phiMask = 0u;  // only the scattered copies carry a cell set
        out[p] = o;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8-0b. The target list.
  //
  // prototype/main.cc, -A 4 stage A: `for (int c : accepted) { if nLayers < 5 continue; if
  // chainDca(c) >= dcaAttach4 continue; thetaPass.push_back(c); }`. The ORDER is the K9 accepted
  // (best-first) order and it is load-bearing -- the contention tie-break is "the earlier target
  // position keeps the pLS" -- so this is a serial filtered copy of a serial array.
  struct ChainAttachSelectTargets {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t* targets,
                                  uint32_t* nTargetsOut,
                                  ChainConfig cfg) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nAcc = chains.nAccepted();
      uint32_t n = 0;
      for (uint32_t ai = 0; ai < nAcc; ++ai) {
        uint32_t const c = accepted[ai];
        if (chains.nLayers()[c] < kAttachMinLayers)
          continue;
        if (chains.dcaXY()[c] >= cfg.attachDcaMax)
          continue;
        targets[n++] = c;
      }
      *nTargetsOut = n;
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8-0c. The per-target record: prototype/PixelAttach.cc makeChainPreGeom + makeChainPre.
  // Every accumulation is in double exactly as the reference does it, and the anchor coordinates
  // are promoted to double BEFORE any differencing (the P2.2 ported-fit rule). The reference stages
  // the cumulative chord lengths in an sArc vector; the same sequential accumulation is replayed in
  // each pass here, which reproduces every partial sum bit for bit without an array.
  struct ChainAttachTargetPre {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainItemsConst items,
                                  ChainsConst chains,
                                  uint32_t const* targets,
                                  uint32_t nTargets,
                                  AttachTargetPre* out) const {
      for (uint32_t t : cms::alpakatools::uniform_elements(acc, nTargets)) {
        uint32_t const c = targets[t];
        AttachTargetPre o;
        o.chain = c;
        o.rtInner = 0.f;
        o.zInner = 0.f;
        o.chordPhi = 0.f;
        o.tanLambda = 0.f;
        o.centerX = 0.f;
        o.centerY = 0.f;
        o.centerValid = 0u;
        o.tcEta = 0.f;
        o.tcPhi = 0.f;

        // The K10 TC direction (innermost member T3's eta/phi), for the -XC bare-chain arm's
        // pass-1 dR^2 window. Byte-identical to what ChainEmitTCs later stores in tcEta / tcPhi,
        // because outer-only extension never changes the innermost member.
        if (chains.nNodes()[c] > 0) {
          uint32_t const t3In = nodes.tripletIndex()[items.nodeItems()[chains.nodeOffset()[c]]];
          unsigned int em0, em1, em2;
          chainNodeMDs(triplets, segments, t3In, em0, em1, em2);
          o.tcEta = chainT3Eta(acc, mds, em2);
          o.tcPhi = chainHitPhi(acc, mds.anchorX()[em0], mds.anchorY()[em0]);
        }

        uint32_t const mdBase = 3u * chains.nodeOffset()[c];
        int const nMD = chains.nMDs()[c];

        if (nMD >= 1) {
          uint32_t const mdIn = items.mdItems()[mdBase];
          double const x0 = mds.anchorX()[mdIn], y0 = mds.anchorY()[mdIn];
          o.rtInner = static_cast<float>(alpaka::math::sqrt(acc, x0 * x0 + y0 * y0));
          o.zInner = mds.anchorZ()[mdIn];
          if (nMD >= 2) {
            uint32_t const md1 = items.mdItems()[mdBase + 1];
            o.chordPhi = static_cast<float>(alpaka::math::atan2(
                acc, static_cast<double>(mds.anchorY()[md1]) - y0, static_cast<double>(mds.anchorX()[md1]) - x0));
          }

          // rz straight-line fit, slope only (mirrors the cf[6] fit).
          {
            double sbar = 0.0, zbar = 0.0;
            double s = 0.0, prevX = x0, prevY = y0;
            for (int k = 0; k < nMD; ++k) {
              if (k > 0) {
                uint32_t const md = items.mdItems()[mdBase + k];
                double const dx = static_cast<double>(mds.anchorX()[md]) - prevX;
                double const dy = static_cast<double>(mds.anchorY()[md]) - prevY;
                s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
                prevX = mds.anchorX()[md];
                prevY = mds.anchorY()[md];
              }
              sbar += s;
              zbar += mds.anchorZ()[items.mdItems()[mdBase + k]];
            }
            sbar /= nMD;
            zbar /= nMD;
            double Sss = 0.0, Ssz = 0.0;
            s = 0.0;
            prevX = x0;
            prevY = y0;
            for (int k = 0; k < nMD; ++k) {
              if (k > 0) {
                uint32_t const md = items.mdItems()[mdBase + k];
                double const dx = static_cast<double>(mds.anchorX()[md]) - prevX;
                double const dy = static_cast<double>(mds.anchorY()[md]) - prevY;
                s += alpaka::math::sqrt(acc, dx * dx + dy * dy);
                prevX = mds.anchorX()[md];
                prevY = mds.anchorY()[md];
              }
              double const ds = s - sbar;
              Sss += ds * ds;
              Ssz += ds * (mds.anchorZ()[items.mdItems()[mdBase + k]] - zbar);
            }
            if (Sss > 1e-12)
              o.tanLambda = static_cast<float>(Ssz / Sss);
          }

          // Kasa circle centre (feature 16), the ChainFeatures.cc algorithm without the radius.
          if (nMD >= 3) {
            double xbar = 0.0, ybar = 0.0;
            for (int k = 0; k < nMD; ++k) {
              xbar += mds.anchorX()[items.mdItems()[mdBase + k]];
              ybar += mds.anchorY()[items.mdItems()[mdBase + k]];
            }
            xbar /= nMD;
            ybar /= nMD;
            double Suu = 0.0, Svv = 0.0, Suv = 0.0, Suw = 0.0, Svw = 0.0;
            for (int k = 0; k < nMD; ++k) {
              double const u = mds.anchorX()[items.mdItems()[mdBase + k]] - xbar;
              double const v = mds.anchorY()[items.mdItems()[mdBase + k]] - ybar;
              double const w = u * u + v * v;
              Suu += u * u;
              Svv += v * v;
              Suv += u * v;
              Suw += u * w;
              Svw += v * w;
            }
            double const det = Suu * Svv - Suv * Suv;
            double const scale = Suu + Svv;
            if (det > 1e-12 * scale * scale) {
              double const uc = (Svv * (0.5 * Suw) - Suv * (0.5 * Svw)) / det;
              double const vc = (Suu * (0.5 * Svw) - Suv * (0.5 * Suw)) / det;
              o.centerX = static_cast<float>(xbar + uc);
              o.centerY = static_cast<float>(ybar + vc);
              o.centerValid = 1u;
            }
          }
        }

        o.fitKappa = chains.features()[c][7];
        o.rotSign = (o.fitKappa >= 0.f) ? 1.f : -1.f;
        o.xs[0] = attachStdz<7>(acc, o.fitKappa);
        o.xs[1] = attachStdz<8>(acc, o.tanLambda);
        o.xs[2] = attachStdz<9>(acc, chains.features()[c][10]);   // innermostLayer
        o.xs[3] = attachStdz<10>(acc, chains.features()[c][1]);   // nLayers
        o.xs[4] = attachStdz<11>(acc, chains.gateLogit2()[c]);    // chain gate logit
        out[t] = o;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8a-1. The measured radial hull of the targets per r bin. Storing the raw bit patterns of the
  // non-negative floats makes the integer atomicMin / atomicMax an exact float min / max.
  struct ChainAttachGridBounds {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachTargetPre const* tgt,
                                  uint32_t nTargets,
                                  uint32_t* rMinBits,
                                  uint32_t* rMaxBits) const {
      for (uint32_t t : cms::alpakatools::uniform_elements(acc, nTargets)) {
        float const rt = tgt[t].rtInner;
        int const rb = attachRBin(rt);
        uint32_t const bits = std::bit_cast<uint32_t>(rt);
        alpaka::atomicMin(acc, &rMinBits[rb], bits, alpaka::hierarchy::Threads{});
        alpaka::atomicMax(acc, &rMaxBits[rb], bits, alpaka::hierarchy::Threads{});
      }
    }
  };

  // K8a-2 / K8a-4. Count and scatter share one mask buffer, so they cannot disagree.
  struct ChainAttachGridCount {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachPlsPre const* pls,
                                  uint32_t nPls,
                                  uint32_t const* rMinBits,
                                  uint32_t const* rMaxBits,
                                  uint16_t* masks,
                                  uint32_t* counts,
                                  ChainConfig cfg) const {
      for (uint32_t p : cms::alpakatools::uniform_elements(acc, nPls)) {
        int const tb = attachTanLBin(pls[p].tanLambda, cfg.attachPrefDTanL);
        for (int rb = 0; rb < kAttachRBins; ++rb) {
          uint32_t const loBits = rMinBits[rb], hiBits = rMaxBits[rb];
          if (loBits == 0xFFFFFFFFu)
            continue;  // no target landed in this bin
          float const rLo = std::bit_cast<float>(loBits);
          float const rHi = std::bit_cast<float>(hiBits);
          uint32_t const mask = attachPhiCellMask(acc, pls[p], rLo, rHi, kAttachPhiPad);
          masks[p * kAttachRBins + rb] = static_cast<uint16_t>(mask);
          for (int pb = 0; pb < kAttachPhiBins; ++pb)
            if (mask & (1u << pb))
              alpaka::atomicAdd(acc, &counts[attachCellId(rb, tb, pb)], 1u, alpaka::hierarchy::Threads{});
        }
      }
    }
  };

  struct ChainAttachGridScatter {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachPlsPre const* pls,
                                  uint32_t nPls,
                                  uint16_t const* masks,
                                  uint32_t const* offsets,
                                  uint32_t* cursor,
                                  AttachPlsPre* itemsOut,
                                  ChainConfig cfg) const {
      for (uint32_t p : cms::alpakatools::uniform_elements(acc, nPls)) {
        int const tb = attachTanLBin(pls[p].tanLambda, cfg.attachPrefDTanL);
        for (int rb = 0; rb < kAttachRBins; ++rb) {
          uint32_t const mask = masks[p * kAttachRBins + rb];
          if (mask == 0u)
            continue;
          for (int pb = 0; pb < kAttachPhiBins; ++pb) {
            if (!(mask & (1u << pb)))
              continue;
            uint32_t const cell = attachCellId(rb, tb, pb);
            uint32_t const slot = alpaka::atomicAdd(acc, &cursor[cell], 1u, alpaka::hierarchy::Threads{});
            AttachPlsPre& dst = itemsOut[offsets[cell] + slot];
            dst = pls[p];
            // The whole phi-cell set this pLS occupies for this r bin travels with the copy: K8b
            // uses it to keep only the FIRST occurrence of the pLS in a target's cell walk.
            dst.phiMask = static_cast<uint16_t>(mask);
          }
        }
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // The 19 frozen pair features, prototype/PixelAttach.cc evalPair, delivered ALREADY PREPROCESSED
  // (see attachStdz). Returns false when the pair fails the analytic prefilter; the arithmetic, its
  // order and the sanitize pass are transcribed verbatim, so a candidate that reaches this function
  // is decided exactly as the reference decides it. `dTanL` is passed in because the caller has
  // already computed it for the cheap early exit.
  //
  // The 19 outputs are written to xOut[i * xStride], so the caller can stage a batch of pairs
  // TRANSPOSED (input-major) without a second pass. phiDirOut is the direction of motion at the
  // target radius, exported for the audit kernel.
  static_assert(dnn::attachmlp::kInput == kAttachFeatures,
                "AttachNetworkWeights.h input size does not match the frozen pair-feature layout");
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool attachEvalPairX(TAcc const& acc,
                                                      AttachPlsPre const& pls,
                                                      AttachTargetPre const& cp,
                                                      float dTanL,
                                                      ChainConfig const& cfg,
                                                      float* xOut,
                                                      int xStride) {
      float const absDTanL = attachFabs(dTanL);
      if (absDTanL >= cfg.attachPrefDTanL)
        return false;
      float const dPhi = chainWrapPhi(attachPhiDirAt(acc, pls, cp.rtInner) - cp.chordPhi);
      if (!(attachFabs(dPhi) < cfg.attachPrefDPhi))
        return false;

      float const chargeAgree = (pls.rotSign == cp.rotSign) ? 1.f : 0.f;
      float const dKappa = pls.kappaSigned - cp.fitKappa;
      float centerDist = 0.f;
      if (cp.centerValid) {
        float const dcx = pls.cx - cp.centerX, dcy = pls.cy - cp.centerY;
        centerDist = alpaka::math::sqrt(acc, dcx * dcx + dcy * dcy);
      }
      float const zResid = pls.hit0z + pls.tanLambda * (cp.rtInner - pls.rt0) - cp.zInner;

      // 0..6 per-pLS and 7..11 per-target: hoisted into the pre-records, copied here.
      CMS_UNROLL_LOOP
      for (int i = 0; i < 7; ++i)
        xOut[i * xStride] = pls.xs[i];
      CMS_UNROLL_LOOP
      for (int i = 0; i < 5; ++i)
        xOut[(7 + i) * xStride] = cp.xs[i];
      xOut[12 * xStride] = attachStdz<12>(acc, chargeAgree);
      xOut[13 * xStride] = attachStdz<13>(acc, dKappa);
      xOut[14 * xStride] = attachStdz<14>(acc, dTanL);
      xOut[15 * xStride] = attachStdz<15>(acc, dPhi);
      xOut[16 * xStride] = attachStdz<16>(acc, centerDist);
      xOut[17 * xStride] = attachStdz<17>(acc, zResid);
      // targetType: chain target (-RT3 0, so the bare-T3 kind does not exist)
      xOut[18 * xStride] = attachStdz<18>(acc, 0.f);
      return true;
  }

  // The r2 pair head's linear layer, unbatched. Mathematically and BIT-EXACTLY the shared
  // src/alpaka/NeuralNetwork.h linear_layer -- each output accumulates the same products in the
  // same j order -- but with the loops interchanged so the vector unit runs across the OUTPUT
  // index and the weight reads are contiguous. The shared template is left untouched so no other
  // network's code generation moves. This is the form the device path uses.
  template <int IN_FEATURES, int OUT_FEATURES>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void attachLinear(float const (&input)[IN_FEATURES],
                                                   float (&output)[OUT_FEATURES],
                                                   float const (&weights)[IN_FEATURES][OUT_FEATURES],
                                                   float const (&biases)[OUT_FEATURES]) {
    CMS_UNROLL_LOOP
    for (int i = 0; i < OUT_FEATURES; ++i)
      output[i] = biases[i];
    for (int j = 0; j < IN_FEATURES; ++j) {
      float const in = input[j];
      CMS_UNROLL_LOOP
      for (int i = 0; i < OUT_FEATURES; ++i)
        output[i] += in * weights[j][i];
    }
  }

  // The r2 pair head over a BATCH of B pairs, prototype/AttachInference.cc attachLogit, on the
  // shared batched primitives of ChainEdges.h (which carry the bit-identity argument). B == 1 is
  // the previous unbatched code verbatim and is what the device path instantiates.
  template <int B>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void attachHeadBatch(float const (&xT)[dnn::attachmlp::kInput * B],
                                                      float (&logits)[B]) {
    constexpr int kIn = dnn::attachmlp::kInput;
    constexpr int kH = dnn::attachmlp::kHidden;
    alignas(64) float h1[kH * B];
    alignas(64) float h2[kH * B];
    if constexpr (B == 1) {
      attachLinear<kIn, kH>(xT, h1, dnn::attachmlp::wgt_l1, dnn::attachmlp::bias_l1);
      relu_activation<kH>(h1);
      attachLinear<kH, kH>(h1, h2, dnn::attachmlp::wgt_l2, dnn::attachmlp::bias_l2);
      relu_activation<kH>(h2);
      float logit = dnn::attachmlp::bias_out;
      for (int j = 0; j < kH; ++j)
        logit += h2[j] * dnn::attachmlp::wgt_out[j];
      logits[0] = logit;
    } else {
      chainLinearBatch<kIn, kH, B>(xT, h1, dnn::attachmlp::wgt_l1, dnn::attachmlp::bias_l1);
      chainReluBatch<kH, B>(h1);
      chainLinearBatch<kH, kH, B>(h1, h2, dnn::attachmlp::wgt_l2, dnn::attachmlp::bias_l2);
      chainReluBatch<kH, B>(h2);
      chainDotBatch<kH, B>(h2, logits, dnn::attachmlp::wgt_out, dnn::attachmlp::bias_out);
    }
  }

  // Monotone float -> uint32 order key, used for the lock-free per-pLS best-logit reduction.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t attachOrderFloat(float v) {
    uint32_t const b = std::bit_cast<uint32_t>(v);
    return (b & 0x80000000u) ? ~b : (b | 0x80000000u);
  }

  // The packed (logit, earlier position) argmax key of the attach contention (stage A's parallel
  // form in ChainParallel.h and the stage-B kernels of ChainAttachT3.h share it).
  // -0.0f and +0.0f compare equal as floats but have different order keys, so the packed argmax
  // would rank them. Every logit that reaches it is >= cfg.attachTheta, but canonicalising costs
  // one instruction and removes the question entirely.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint64_t attachContendKey(float logit, uint32_t pos) {
    float const v = (logit == 0.f) ? 0.f : logit;
    return (static_cast<uint64_t>(attachOrderFloat(v)) << 32) | static_cast<uint64_t>(0xFFFFFFFFu - pos);
  }

  // ------------------------------------------------------------------------------------------
  // K8b / the per-target half of K8c. One thread per target: gather the grid candidates, evaluate
  // the exact predicate, score the survivors and keep the best.
  //
  // prototype/AttachDelivery.cc gaStageChains keeps `lo > tgtLogit` over pairs that arrive in
  // ASCENDING pLS row, i.e. the maximum logit with the LOWEST pLS row winning a tie. The grid
  // returns candidates in an arbitrary order, so that rule is written out explicitly here.
  //
  // plsBest is the per-pLS maximum over every SCORED pair before any threshold -- the -RPS
  // predicate's input. It is a max, so the atomic is order-independent and the result is
  // deterministic on every backend.
  //
  // P2.6b, two changes, both output-neutral:
  //
  // (1) DUPLICATE SUPPRESSION. A pLS occupies every phi cell its direction range touches, so a
  //     target that scans nPb cells sees the same pLS up to nPb times (1.20 candidates per unique
  //     pair, measured by the P2.4 audit). Scoring it twice cannot change anything -- the pair
  //     produces the same logit, the plsBest reduction is a max and the per-target pick is an
  //     argmax under a strict total order -- so only the FIRST occurrence in the walk is kept. The
  //     test is exact and carries no geometric assumption: the item copy carries the phi-cell MASK
  //     it was scattered under, and the walk position of the pLS's lowest scanned cell is read off
  //     that mask directly. Both the r bin and the tanLambda bin are single-valued over a scan (the
  //     target's own r bin; the pLS's own tanLambda bin), so the phi walk is the only axis that can
  //     repeat. The check runs BEFORE the predicate, so the duplicate costs neither an atan2 nor a
  //     head evaluation.
  //
  // (2) BATCHING (host backends only, kB > 1). The head is 19->24->24->1 with a serial dependency
  //     down each unit's accumulation; one pair leaves the vector unit mostly idle. Survivors are
  //     staged transposed into xT and evaluated kB at a time, which interleaves kB independent
  //     accumulator chains without touching the per-pair operation order (see attachHeadBatch).
  //     The device path takes kB == 1 and is unchanged.
  struct ChainAttachScore {
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(TAcc const& acc,
                                  AttachPlsPre const* pls,
                                  AttachTargetPre const* tgt,
                                  uint32_t nTargets,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  uint32_t* plsBest,
                                  ChainXcPair* xcPairs,
                                  uint32_t* xcCursor,
                                  uint32_t xcCap,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      constexpr int kB = cms::alpakatools::requires_single_thread_per_block_v<TAcc> ? kAttachScoreBatch : 1;
      constexpr int kIn = dnn::attachmlp::kInput;

      alignas(64) float xT[kIn * kB];
      int32_t rowB[kB];
      AttachPlsPre const* ppB[kB];
      float logits[kB];
      for (int i = 0; i < kIn * kB; ++i)
        xT[i] = 0.f;  // the tail lanes of a partial batch are evaluated and discarded

      for (uint32_t t : cms::alpakatools::uniform_elements(acc, nTargets)) {
        AttachTargetPre const cp = tgt[t];
        int32_t bestPls = -1;
        float bestLogit = kAttachNoLogit;
        uint32_t nCand = 0, nScored = 0, nDup = 0;
        int nb = 0;

        // Retire a staged batch: the pairs are reduced in staging order, which is immaterial
        // because both reductions below are order-independent maxima (and the -XC append is a
        // set insertion, resolved order-independently in pass 2).
        auto flush = [&]() {
          attachHeadBatch<kB>(xT, logits);
          for (int b = 0; b < nb; ++b) {
            float const lo = logits[b];
            int32_t const p = rowB[b];
            AttachPlsPre const& pq = *ppB[b];
            alpaka::atomicMax(
                acc, &plsBest[static_cast<uint32_t>(p)], attachOrderFloat(lo), alpaka::hierarchy::Threads{});
            // -XC bare-chain arm, pass 1 (A15): the filtered (chain, seed) compaction of the
            // scored-pair stream. Threshold on the |seed eta|-banded xcTheta, window on the
            // emitted-TC direction. Ordering: BEFORE the delivery threshold (the crossclean sees
            // sub-margin pairs; xcThr 3.5-3.75 sits well below attachThr 5.0-6.0).
            if (xcPairs != nullptr && pq.isQuad != 0u && lo >= pq.xcThr) {
              float const dEta = pq.eta - cp.tcEta;
              float const dPhi = chainWrapPhi(pq.phi - cp.tcPhi);
              if (dEta * dEta + dPhi * dPhi < cfg.xcDR2Chain) {
                uint32_t const slot = alpaka::atomicAdd(acc, xcCursor, 1u, alpaka::hierarchy::Threads{});
                if (slot < xcCap) {
                  xcPairs[slot].chain = cp.chain;
                  xcPairs[slot].pls = static_cast<uint32_t>(p);
                } else {
                  alpaka::atomicAdd(acc, &stats[11], 1u, alpaka::hierarchy::Threads{});  // overflow census
                }
              }
            }
            if (lo < pq.attachThr)
              continue;  // the banded -a / -a2 / -a3 delivery margin (A11)
            if (bestPls < 0 || lo > bestLogit || (lo == bestLogit && p < bestPls)) {
              bestPls = p;
              bestLogit = lo;
            }
          }
          nScored += static_cast<uint32_t>(nb);
          nb = 0;
        };

        int const rb = attachRBin(cp.rtInner);
        int const tbLo = attachTanLBin(cp.tanLambda - cfg.attachPrefDTanL, cfg.attachPrefDTanL);
        int const tbHi = attachTanLBin(cp.tanLambda + cfg.attachPrefDTanL, cfg.attachPrefDTanL);
        int const pbLo = attachPhiBin(cp.chordPhi - cfg.attachPrefDPhi);
        int const pbHi = attachPhiBin(cp.chordPhi + cfg.attachPrefDPhi);
        int nPb = pbHi - pbLo;
        if (nPb < 0)
          nPb += kAttachPhiBins;
        ++nPb;
        if (nPb > kAttachPhiBins)
          nPb = kAttachPhiBins;

        for (int tb = tbLo; tb <= tbHi; ++tb) {
          for (int k = 0; k < nPb; ++k) {
            int const pb = (pbLo + k) % kAttachPhiBins;
            // Bits of the pLS mask below the current walk position, i.e. the cells of THIS scan
            // that were visited earlier and would already have supplied the same pLS.
            uint32_t const earlier = (1u << k) - 1u;
            uint32_t const cell = attachCellId(rb, tb, pb);
            uint32_t const b = offsets[cell], e = offsets[cell + 1u];
            for (uint32_t i = b; i < e; ++i) {
              AttachPlsPre const& pp = items[i];
              uint32_t const p = pp.row;
              ++nCand;
              uint32_t const m = pp.phiMask;
              uint32_t const rot = ((m >> pbLo) | (m << (kAttachPhiBins - pbLo))) & (kAttachPhiCellMask);
              if ((rot & earlier) != 0u) {
                ++nDup;
                continue;
              }
              float const dTanL = pp.tanLambda - cp.tanLambda;
              if (!attachEvalPairX(acc, pp, cp, dTanL, cfg, xT + nb, kB))
                continue;
              rowB[nb] = static_cast<int32_t>(p);
              ppB[nb] = &pp;
              ++nb;
              if (nb == kB)
                flush();
            }
          }
        }
        if (nb > 0)
          flush();

        tgtPls[t] = bestPls;
        tgtLogit[t] = bestLogit;
        alpaka::atomicAdd(acc, &stats[1], nCand, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[2], nScored, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[10], nDup, alpaka::hierarchy::Threads{});
        if (bestPls >= 0)
          alpaka::atomicAdd(acc, &stats[3], 1u, alpaka::hierarchy::Threads{});
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // A14 -CCS support: the inverted stage-A grant map, pLS row -> owning chain row or -1.
  // Single-valued by the one-pLS-one-owner invariant, so no atomics. Run AFTER the contention and
  // the -RD dedup are final (chains.attachPls() is the post-dedup grant).
  struct ChainBuildPlsOwnerChain {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc, ChainsConst chains, int32_t* plsOwnerChain) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());
      for (uint32_t c : cms::alpakatools::uniform_elements(acc, nChains)) {
        int32_t const p = chains.attachPls()[c];
        if (p >= 0)
          plsOwnerChain[p] = static_cast<int32_t>(c);
      }
    }
  };

  // A14 -CCS / A15 -XC4: the RESTRICTED second scoring pass (SPEC_CCS_MR section 1.8 -- one
  // inverted map plus one restricted pass; the reference's pair log is never materialised).
  //
  // Targets are ALL entries of the combined pre-record array (the stage-A 5+ list followed by the
  // aux 4-layer accepted list); the kernel walks the same grid with the same exact predicate:
  //   - a BARE target (attachPls < 0, which is every 4-layer chain) accumulates
  //     ccsLoserLogit = max head logit over its prefiltered pairs whose pLS is owned by a
  //     DIFFERENT chain -- threshold-free and contention-free, exactly the reference's map;
  //   - a 4-LAYER target additionally evaluates EVERY prefiltered pair (this is its only scoring
  //     pass -- the -XC4 score-only extension) and appends the -XC pass-1 candidates for it. It
  //     writes NOTHING else: not tgtPls, not plsBest -- so -RPSA is bit-identical with and without
  //     the 4-layer pass (invariant I5).
  //   - a 5+ target evaluates the head ONLY for owned-pLS pairs (its full pair set was already
  //     scored and -XC-appended by ChainAttachScore).
  // At the end the -CCS verdict is applied directly: a bare chain whose loser max reaches the
  // band bar (on the emitted-TC |eta|, literal 1.1 / 1.7, per-band 1e9 = OFF with NO fallback)
  // gets kChainFlagCcsSuppressed, which K10 row assignment honours.
  struct ChainAttachCcsScore {
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(TAcc const& acc,
                                  Chains chains,
                                  AttachTargetPre const* tgt,
                                  uint32_t nTgtAll,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  int32_t const* plsOwnerChain,
                                  ChainXcPair* xcPairs,
                                  uint32_t* xcCursor,
                                  uint32_t xcCap,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      constexpr int kB = cms::alpakatools::requires_single_thread_per_block_v<TAcc> ? kAttachScoreBatch : 1;
      constexpr int kIn = dnn::attachmlp::kInput;

      alignas(64) float xT[kIn * kB];
      int32_t rowB[kB];
      AttachPlsPre const* ppB[kB];
      float logits[kB];
      for (int i = 0; i < kIn * kB; ++i)
        xT[i] = 0.f;

      for (uint32_t t : cms::alpakatools::uniform_elements(acc, nTgtAll)) {
        AttachTargetPre const cp = tgt[t];
        uint32_t const c = cp.chain;
        bool const is4L = chains.nLayers()[c] < 5;
        bool const bare = chains.attachPls()[c] < 0;
        if (!is4L && !bare)
          continue;  // attached 5+ chain: not CCS-eligible and already fully scored

        float loser = kAttachNoLogit;
        int nb = 0;

        auto flush = [&]() {
          attachHeadBatch<kB>(xT, logits);
          for (int b = 0; b < nb; ++b) {
            float const lo = logits[b];
            int32_t const p = rowB[b];
            AttachPlsPre const& pq = *ppB[b];
            if (is4L && xcPairs != nullptr && pq.isQuad != 0u && lo >= pq.xcThr) {
              float const dEta = pq.eta - cp.tcEta;
              float const dPhi = chainWrapPhi(pq.phi - cp.tcPhi);
              if (dEta * dEta + dPhi * dPhi < cfg.xcDR2Chain) {
                uint32_t const slot = alpaka::atomicAdd(acc, xcCursor, 1u, alpaka::hierarchy::Threads{});
                if (slot < xcCap) {
                  xcPairs[slot].chain = c;
                  xcPairs[slot].pls = static_cast<uint32_t>(p);
                } else {
                  alpaka::atomicAdd(acc, &stats[11], 1u, alpaka::hierarchy::Threads{});
                }
              }
            }
            int32_t const oc = plsOwnerChain[p];
            if (oc >= 0 && oc != static_cast<int32_t>(c) && lo > loser)
              loser = lo;
          }
          nb = 0;
        };

        int const rb = attachRBin(cp.rtInner);
        int const tbLo = attachTanLBin(cp.tanLambda - cfg.attachPrefDTanL, cfg.attachPrefDTanL);
        int const tbHi = attachTanLBin(cp.tanLambda + cfg.attachPrefDTanL, cfg.attachPrefDTanL);
        int const pbLo = attachPhiBin(cp.chordPhi - cfg.attachPrefDPhi);
        int const pbHi = attachPhiBin(cp.chordPhi + cfg.attachPrefDPhi);
        int nPb = pbHi - pbLo;
        if (nPb < 0)
          nPb += kAttachPhiBins;
        ++nPb;
        if (nPb > kAttachPhiBins)
          nPb = kAttachPhiBins;

        for (int tb = tbLo; tb <= tbHi; ++tb) {
          for (int k = 0; k < nPb; ++k) {
            int const pb = (pbLo + k) % kAttachPhiBins;
            uint32_t const earlier = (1u << k) - 1u;
            uint32_t const cell = attachCellId(rb, tb, pb);
            uint32_t const b = offsets[cell], e = offsets[cell + 1u];
            for (uint32_t i = b; i < e; ++i) {
              AttachPlsPre const& pp = items[i];
              uint32_t const m = pp.phiMask;
              uint32_t const rot = ((m >> pbLo) | (m << (kAttachPhiBins - pbLo))) & (kAttachPhiCellMask);
              if ((rot & earlier) != 0u)
                continue;  // multi-cell duplicate: same suppression as the delivery scorer
              int32_t const oc = plsOwnerChain[pp.row];
              // A 5+ target only needs the owned-pLS pairs (the restricted pass); a 4-layer target
              // needs every pair (this is its -XC4 score-only enumeration).
              if (!is4L && oc < 0)
                continue;
              float const dTanL = pp.tanLambda - cp.tanLambda;
              if (!attachEvalPairX(acc, pp, cp, dTanL, cfg, xT + nb, kB))
                continue;
              rowB[nb] = static_cast<int32_t>(pp.row);
              ppB[nb] = &pp;
              ++nb;
              if (nb == kB)
                flush();
            }
          }
        }
        if (nb > 0)
          flush();

        // The -CCS verdict: bare targets only, band on the emitted-TC |eta|, >= comparison,
        // per-band 1e9 = OFF with no cross-band fallback (SPEC_CCS_MR 1.1 / 1.5).
        if (bare) {
          float const ae = attachFabs(cp.tcEta);
          float const bar = (ae < 1.1f) ? cfg.ccsTheta : ((ae < 1.7f) ? cfg.ccsThetaT : cfg.ccsThetaE);
          if (bar < 1e8f && loser >= bar) {
            chains.flags()[c] = chains.flags()[c] | kChainFlagCcsSuppressed;
            alpaka::atomicAdd(acc, &stats[12], 1u, alpaka::hierarchy::Threads{});
          }
        }
      }
    }
  };

  // The aux 4-layer target list: accepted chains with nLayers == 4, appended AFTER the stage-A
  // (5+) targets in the combined array. Serial because it reads the K9 accepted order (order is
  // immaterial for -CCS / -XC4, but the append position must not race the stage-A count).
  struct ChainAttachSelectAux {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t const* nTargets5,
                                  uint32_t* targets,
                                  uint32_t* nTargetsAllOut) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nAcc = chains.nAccepted();
      uint32_t n = *nTargets5;
      for (uint32_t ai = 0; ai < nAcc; ++ai) {
        uint32_t const c = accepted[ai];
        if (chains.nLayers()[c] != 4)
          continue;
        targets[n++] = c;
      }
      *nTargetsAllOut = n;
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8c. ONE pLS, ONE OWNER, then the -RD seed-family dedup. Both are order-dependent greedy rules
  // over a few hundred entries, so both run in one serial kernel.
  //
  // Contention (prototype/AttachDelivery.cc resolveContention): walk the target positions in K9
  // order, a later target takes a contested pLS only on a STRICTLY higher logit. That is the
  // argmax with "the earlier position keeps the tie".
  //
  // -RD (prototype/main.cc): visit the owners ordered by (logit descending, CHAIN ROW ascending)
  // and revoke an owner whose pLS shares >= 2 pixel hit rows with an already-kept owner's pLS. The
  // reference gets that order from std::sort with a strict total order, so a selection pass over
  // the same keys yields the identical sequence; no sort primitive is used anywhere in this phase.
  struct ChainAttachContend {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  HitsBaseConst hitsBase,
                                  Chains chains,
                                  uint32_t const* targets,
                                  uint32_t nTargets,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  int32_t* plsOwnerPos,
                                  uint8_t* plsOwned,
                                  uint32_t nPls,
                                  uint32_t* hashKey,
                                  int32_t* hashVal,
                                  uint32_t nHits,
                                  uint32_t* order,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;

      for (uint32_t p = 0; p < nPls; ++p) {
        plsOwnerPos[p] = -1;
        plsOwned[p] = 0u;
      }

      for (uint32_t pos = 0; pos < nTargets; ++pos) {
        int32_t const p = tgtPls[pos];
        if (p < 0)
          continue;
        int32_t const prev = plsOwnerPos[p];
        if (prev < 0) {
          plsOwnerPos[p] = static_cast<int32_t>(pos);
          continue;
        }
        if (tgtLogit[pos] > tgtLogit[prev]) {
          tgtPls[prev] = -1;
          tgtLogit[prev] = kAttachNoLogit;
          plsOwnerPos[p] = static_cast<int32_t>(pos);
        } else {
          tgtPls[pos] = -1;
          tgtLogit[pos] = kAttachNoLogit;
        }
      }

      // Publish onto the chain rows: the -RD pass and the delivery are both chain-row keyed.
      for (uint32_t pos = 0; pos < nTargets; ++pos) {
        uint32_t const c = targets[pos];
        chains.attachPls()[c] = tgtPls[pos];
        chains.attachLogit()[c] = tgtLogit[pos];
      }

      uint32_t nOwners = 0;
      for (uint32_t pos = 0; pos < nTargets; ++pos)
        if (tgtPls[pos] >= 0)
          order[nOwners++] = targets[pos];

      if (cfg.attachSeedDedup && nOwners > 0) {
        // Selection pass: (logit desc, chain row asc). `order` starts chain-row ascending because
        // the K9 accepted order is not, so the second key is applied explicitly. The reference gets
        // the same sequence from std::sort over the same strict total order.
        //
        // P2.5 classification: the chain row IS a volatile numbering, so an exact attachLogit tie
        // here is decided by a run-dependent quantity. stats[9] ("tieRD" in the [CHAIN K8] line)
        // counts every such tie comparison: 4 over 20 CUDA events, none on 18 of the 20, i.e.
        // ~0.2 per event against ~1.6e5 comparisons. It is exercised, but the fix was MEASURED and
        // is not free: substituting chains.stableKey() for the row here costs ~0.7 ms/event on
        // CUDA, because this kernel runs single-threaded on the device and the compiler speculates
        // the extra global load on every one of the O(nOwners^2) comparisons, tie or not. Under the
        // "no timing spent on determinism" rule that buys back nothing measurable -- the GPU
        // run-to-run TC identity is the same either way (99.67% vs 99.68%, both set by LST's own
        // upstream pT3 instability) -- so the volatile key stays and the residual is accepted and
        // recorded here. The stableKey column exists; swapping it in is a one-line change if the
        // trade is ever judged worthwhile.
        // The incumbent's logit is a loop invariant between swaps and is carried in a register,
        // which is why the comparison is cheaper here than the two loads the reference does.
        for (uint32_t i = 0; i < nOwners; ++i) {
          uint32_t best = i;
          float lb = chains.attachLogit()[order[best]];
          for (uint32_t j = i + 1; j < nOwners; ++j) {
            float const lj = chains.attachLogit()[order[j]];
            bool const jFirst = (lj != lb) ? (lj > lb) : (order[j] < order[best]);
            if (lj == lb)
              ++stats[9];  // tie census; this kernel is once_per_grid, so no atomic is needed
            if (jFirst) {
              best = j;
              lb = lj;
            }
          }
          uint32_t const tmp = order[i];
          order[i] = order[best];
          order[best] = tmp;
        }

        // hit2kept: an open-addressed (pixel hit index -> kept pLS row) multimap. The reference's
        // std::unordered_map is keyed on the TRACKING-NTUPLE hit index, not on the hits-SoA row --
        // two pixel seeds each own their own SoA rows and only ever collide through hitsBase.idxs().
        for (uint32_t k = 0; k < chainattach::kSeedHashSlots; ++k)
          hashKey[k] = chainattach::kSeedHashEmpty;
        uint32_t nIns = 0;

        for (uint32_t i = 0; i < nOwners; ++i) {
          uint32_t const c = order[i];
          int32_t const p = chains.attachPls()[c];
          if (p < 0)
            continue;

          // The pLS's DISTINCT pixel hit indices: the seed's rows in the hits SoA, minus any that
          // are outer-tracker (the reference keeps only the see_hitType == Pixel entries).
          uint32_t g[kMaxPLSHitsInHitsSoA];
          int nh = 0;
          uint32_t const first = pixelSeeds.firstHit()[p];
          uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[p]);
          uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
          for (uint32_t k = 0; k < nStored; ++k) {
            uint32_t const h = first + k;
            if (h >= nHits)
              continue;
            if (hitsBase.detid()[h] != kPixelModuleId)
              continue;
            g[nh++] = hitsBase.idxs()[h];
          }

          // "shares >= 2 hit rows with an already-kept owner" <=> some kept pLS shows up under two
          // DIFFERENT hits of p (the rows are distinct and a key holds each owner at most once).
          bool dup = false;
          int32_t seen[chainattach::kSeedDedupSeenMax];
          int nSeen = 0;
          for (int a = 0; a < nh && !dup; ++a) {
            uint32_t slot = attachSeedHash(g[a]);
            while (hashKey[slot] != chainattach::kSeedHashEmpty) {
              if (hashKey[slot] == g[a]) {
                int32_t const q = hashVal[slot];
                for (int u = 0; u < nSeen; ++u)
                  if (seen[u] == q) {
                    dup = true;
                    break;
                  }
                if (dup)
                  break;
                if (nSeen < chainattach::kSeedDedupSeenMax)
                  seen[nSeen++] = q;
              }
              slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
            }
          }

          if (dup) {
            chains.attachPls()[c] = -1;
            chains.attachLogit()[c] = kAttachNoLogit;
            alpaka::atomicAdd(acc, &stats[5], 1u, alpaka::hierarchy::Threads{});
            continue;
          }
          for (int a = 0; a < nh; ++a) {
            if (nIns + 1u >= chainattach::kSeedHashSlots / 2u) {
              alpaka::atomicAdd(acc, &stats[7], 1u, alpaka::hierarchy::Threads{});
              break;  // never reached at PU200 (a few hundred owners x <= 4 hits)
            }
            uint32_t slot = attachSeedHash(g[a]);
            while (hashKey[slot] != chainattach::kSeedHashEmpty)
              slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
            hashKey[slot] = g[a];
            hashVal[slot] = p;
            ++nIns;
          }
        }
      }

      uint32_t nAttached = 0;
      for (uint32_t pos = 0; pos < nTargets; ++pos) {
        uint32_t const c = targets[pos];
        int32_t const p = chains.attachPls()[c];
        if (p < 0)
          continue;
        plsOwned[p] = 1u;
        ++nAttached;
      }
      chains.nAttached() = nAttached;
      alpaka::atomicAdd(acc, &stats[4], nAttached, alpaka::hierarchy::Threads{});
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8d. The contention / -RPS / -XC retirement of the carried bare-pLS rows -- the FINAL pass of
  // the ON-state, run after the chain rows and the stage-B type-5 rows have been appended.
  //
  // prototype/main.cc m16RefreshSupp + the -XC channel: a carried type-8 row goes when
  //   (a) its seed now has an outer-tracker owner (a chain upgrade or a stage-B delivery),
  //   (b) under -RPS, its seed had a scored pair at or above the class RETIREMENT bar but is not
  //       the owner -- TWO separate bars on TWO separate evidence arrays: the chain evidence
  //       against rpsThetaChain (-RPSA 5.5) and the bare-T3 evidence against attachThetaT3
  //       (-AT3 6.0). The single-array shift trick of the measurement probe is gone: it was exact
  //       only while rpsThetaChain == attachTheta, which the A11 retune broke. -CCR 2's erasure of
  //       the T3 evidence is visible here by construction (the sweep runs first).
  //   (c) the ported seed crossclean retired it (xcRetired).
  // Carried pT5 / pT3 rows no longer exist (replacePT5 / replacePT3 dropped them wholesale), so
  // the pixel-collection row -> pLS mapping is gone with them. The chain-emitted rows occupy the
  // LAST nChainTCs positions and are kept verbatim (stable compaction preserves the row-range
  // contract isChainTCRow relies on); every per-class counter is recounted from the kept rows.
  struct ChainSuppressCarriedTCs {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  TrackCandidatesBase candsBase,
                                  TrackCandidatesExtended candsExtended,
                                  ChainsConst chains,
                                  uint8_t const* plsOwned,
                                  uint32_t const* plsBestChain,
                                  uint32_t const* plsBestT3,
                                  uint8_t const* xcRetired,
                                  uint32_t nPls,
                                  uint32_t* stats,
                                  ChainConfig cfg) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const keyChain = attachOrderFloat(cfg.rpsThetaChain);
      uint32_t const keyT3 = attachOrderFloat(cfg.attachThetaT3);

      uint32_t const nIn = candsBase.nTrackCandidates();
      uint32_t const nChainRows = chains.nChainTCs();
      uint32_t const boundary = (nChainRows <= nIn) ? (nIn - nChainRows) : 0u;
      uint32_t w = 0, nKeptPT5 = 0, nKeptPT3 = 0, nKeptPLS = 0, nKeptT5 = 0, nKeptT4 = 0;
      for (uint32_t r = 0; r < nIn; ++r) {
        LSTObjType const ty = candsBase.trackCandidateType()[r];
        bool drop = false;
        if (r < boundary && ty == LSTObjType::pLS) {
          int32_t const p = static_cast<int32_t>(candsExtended.directObjectIndices()[r]);
          if (p >= 0 && static_cast<uint32_t>(p) < nPls) {
            drop = plsOwned[p] != 0u;
            if (cfg.attachSuppressBarePLS)
              drop = drop || (plsBestChain[p] >= keyChain) || (plsBestT3[p] >= keyT3);
            drop = drop || (xcRetired[p] != 0u);
          }
        }
        if (drop) {
          alpaka::atomicAdd(acc, &stats[6], 1u, alpaka::hierarchy::Threads{});
          continue;
        }

        if (w != r) {
          candsBase.trackCandidateType()[w] = candsBase.trackCandidateType()[r];
          candsBase.pixelSeedIndex()[w] = candsBase.pixelSeedIndex()[r];
          candsExtended.directObjectIndices()[w] = candsExtended.directObjectIndices()[r];
          candsExtended.objectIndices()[w][0] = candsExtended.objectIndices()[r][0];
          candsExtended.objectIndices()[w][1] = candsExtended.objectIndices()[r][1];
          for (int s = 0; s < Params_TC::kLayers; ++s) {
            candsExtended.logicalLayers()[w][s] = candsExtended.logicalLayers()[r][s];
            candsExtended.lowerModuleIndices()[w][s] = candsExtended.lowerModuleIndices()[r][s];
            candsBase.hitIndices()[w][s][0] = candsBase.hitIndices()[r][s][0];
            candsBase.hitIndices()[w][s][1] = candsBase.hitIndices()[r][s][1];
          }
        }
        if (ty == LSTObjType::pT5)
          ++nKeptPT5;
        else if (ty == LSTObjType::pT3)
          ++nKeptPT3;
        else if (ty == LSTObjType::pLS)
          ++nKeptPLS;
        else if (ty == LSTObjType::T5)
          ++nKeptT5;
        else if (ty == LSTObjType::T4)
          ++nKeptT4;
        ++w;
      }
      candsBase.nTrackCandidates() = w;
      // Recount every class from the kept rows: the carried classes and the chain-row classes
      // (pT5 upgrades, bare T5 / T4 chains, stage-B pT3 rows) land in one consistent census.
      candsExtended.nTrackCandidatespT5() = nKeptPT5;
      candsExtended.nTrackCandidatespT3() = nKeptPT3;
      candsExtended.nTrackCandidatespLS() = nKeptPLS;
      candsExtended.nTrackCandidatesT5() = nKeptT5;
      candsExtended.nTrackCandidatesT4() = nKeptT4;
    }
  };

  // ------------------------------------------------------------------------------------------
  // The offline grid verification of port map section 1.1, compiled in but inert unless
  // LSTEvent::attachGridAudit is switched on by LST_CHAIN_ATTACH_AUDIT.
  //
  // It replays the EXHAUSTIVE scan the prototype runs -- every (target, pLS) pair through the two
  // frozen windows -- and, for every pair the scan accepts, searches the grid's candidate list for
  // that target. `missing` must be 0 on every event before the grid may be trusted; `cand` is the
  // grid's probe count, to be read against nTargets * nPls.
  //   audit[0] pairs accepted by the exhaustive scan
  //   audit[1] candidates the grid returns (with the duplicates a multi-cell scan produces)
  //   audit[2] grid candidates that pass the windows
  //   audit[3] scan-accepted pairs ABSENT from the grid   <-- must be zero
  struct ChainAttachAudit {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachPlsPre const* pls,
                                  uint32_t nPls,
                                  AttachTargetPre const* tgt,
                                  uint32_t nTargets,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  uint32_t* audit,
                                  ChainConfig cfg) const {
      for (uint32_t t : cms::alpakatools::uniform_elements(acc, nTargets)) {
        AttachTargetPre const cp = tgt[t];

        int const rb = attachRBin(cp.rtInner);
        int const tbLo = attachTanLBin(cp.tanLambda - cfg.attachPrefDTanL, cfg.attachPrefDTanL);
        int const tbHi = attachTanLBin(cp.tanLambda + cfg.attachPrefDTanL, cfg.attachPrefDTanL);
        int const pbLo = attachPhiBin(cp.chordPhi - cfg.attachPrefDPhi);
        int const pbHi = attachPhiBin(cp.chordPhi + cfg.attachPrefDPhi);
        int nPb = pbHi - pbLo;
        if (nPb < 0)
          nPb += kAttachPhiBins;
        ++nPb;
        if (nPb > kAttachPhiBins)
          nPb = kAttachPhiBins;

        uint32_t nCand = 0, nGridPass = 0;
        for (int tb = tbLo; tb <= tbHi; ++tb)
          for (int k = 0; k < nPb; ++k) {
            uint32_t const cell = attachCellId(rb, tb, (pbLo + k) % kAttachPhiBins);
            for (uint32_t i = offsets[cell]; i < offsets[cell + 1u]; ++i) {
              ++nCand;
              float f[kAttachFeatures];
              if (attachEvalPairX(acc, items[i], cp, items[i].tanLambda - cp.tanLambda, cfg, f, 1))
                ++nGridPass;
            }
          }

        uint32_t nExact = 0, nMissing = 0;
        for (uint32_t p = 0; p < nPls; ++p) {
          float f[kAttachFeatures];
          if (!attachEvalPairX(acc, pls[p], cp, pls[p].tanLambda - cp.tanLambda, cfg, f, 1))
            continue;
          ++nExact;
          bool found = false;
          for (int tb = tbLo; tb <= tbHi && !found; ++tb)
            for (int k = 0; k < nPb && !found; ++k) {
              uint32_t const cell = attachCellId(rb, tb, (pbLo + k) % kAttachPhiBins);
              for (uint32_t i = offsets[cell]; i < offsets[cell + 1u]; ++i)
                if (items[i].row == p) {
                  found = true;
                  break;
                }
            }
          if (!found)
            ++nMissing;
        }

        alpaka::atomicAdd(acc, &audit[0], nExact, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &audit[1], nCand, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &audit[2], nGridPass, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &audit[3], nMissing, alpaka::hierarchy::Threads{});
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
