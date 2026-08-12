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
#include "ChainWeld.h"
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
//   K8-0b ChainTargetFlags + prefix + compaction: the accepted, nLayers >= 5,
//         dca-eligible chains IN K9 ORDER
//   K8-0c ChainAttachTargetPre     per-target record               (makeChainPre + makeChainPreGeom)
//   K8a   ChainAttachGridBounds / GridCount / GridPrefix / GridScatter   the grid prefilter
//   K8b   ChainAttachScore         candidate iteration + 19 features + the r2 head, best per target
//   K8c   ChainAttachArgmax / Resolve / OwnerHits / SeedDedup / Publish:
//         one pLS one owner, then the -RD seed-family dedup
//   K8d   ChainTCKeepSuppress + prefix + gather/scatter: the contention / -RPS
//         retirement of carried pixel rows
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
    // Radius a straight (zero-curvature) target reports to head input 19; 1e5 cm is far outside
    // the detector, so the residual it produces is a saturated flag rather than an infinity.
    constexpr float kStraightRadius = 1e5f;
    constexpr float kPi = 3.14159265358979323846f;
    // -RD seed-family dedup scratch. The table holds one entry per (pixel hit index, kept owner)
    // pair; at PU200 that is a few hundred owners x <= 4 hits, so a 16k-slot open-addressed table
    // never exceeds a quarter load. Power of two: the probe wraps with a mask.
    constexpr uint32_t kSeedHashSlots = 16384u;
    constexpr uint32_t kSeedHashEmpty = 0xFFFFFFFFu;
    // A REVOKED owner's entry, written over its key by the serial pass below. It is not the empty
    // sentinel, so it does not truncate a probe run, and it is not a hit index (hitsBase.idxs()
    // numbering is far below it), so it never matches a lookup key: the table's MEMBERSHIP after
    // the pass is exactly the surviving owners' entries, which is what stage B's -RDT reads.
    constexpr uint32_t kSeedHashTomb = 0xFFFFFFFEu;
    // Probe-length stop for the concurrent insert, the analogue of chainxc::kHitHashMaxProbe: it
    // stands in for the serial form's running load-factor test. A few hundred owners x <= 4 hits
    // in 16k slots is under a quarter load, so it can only fire far outside the design point.
    constexpr uint32_t kSeedHashMaxProbe = 256u;
    constexpr int kSeedDedupSeenMax = 64;
    // The two attach stages share ONE table, and the stage-B (-RDT) greedy has to know which
    // entries are "earlier" than the owner it is deciding. An entry's owner tag is therefore
    // `kSeedOwnerStageA + i` in stage A and `kSeedOwnerStageB + i` in stage B, so a plain
    // `tag < myTag` is exactly "already decided": every stage-A entry precedes every stage-B one
    // (stage A is final before stage B starts) and inside stage B it reduces to `j < i`. The bias
    // is the top bit, so the stage-A owner index -- a few hundred accepted chains -- cannot reach
    // it and the tags cannot alias.
    constexpr uint32_t kSeedOwnerStageA = 0u;
    constexpr uint32_t kSeedOwnerStageB = 0x80000000u;
    // The bits of ChainAttachSeedConflicts' per-owner word (see that kernel for what they mean).
    constexpr uint32_t kSeedContPartner = 1u;       // some other owner shares >= 2 pixel hit rows
    constexpr uint32_t kSeedContEarlierStage = 2u;  // ... and one of them is final (earlier stage)
    constexpr uint32_t kSeedContDead = 4u;          // the greedy's own "revoked" marker
    // Diagnostic counters (never read by a decision):
    //   0 grid entries   1 candidates iterated   2 pairs scored   3 per-target picks
    //   4 attached after contention   5 -RD revocations   6 carried rows retired
    //   7 seed-dedup owner-slot overflows   9 -RD logit-tie census
    //  10 grid candidates skipped as a repeat of the same pLS inside one target's cell walk
    //  11 -XC pass-1 buffer overflow census
    //  12 owners a STRICTLY EARLIER-STAGE partner revokes outright (stage B only -- see
    //     ChainAttachSeedConflicts; unreachable in stage A, whose owner tag bias is zero)
    //  13 -RD contentious owners (the ones the serial greedy actually visits)
    //  14 -RD max table entries one owner's keys resolve to (the kSeedDedupSeenMax headroom witness)
    //  15 -RD prefilter disagreements with the brute-force pair scan (LST_CHAIN_RD_AUDIT only)
    // NOTE the two stats layouts are DELIBERATELY aligned on slots 7 and 12-14: ChainAttachOwnerHits
    // and ChainAttachSeedConflicts are shared by both attach stages, so the slot they write has to
    // mean the same thing in chainattach::kStats and in chainattacht3::kStats.
    constexpr uint32_t kStats = 16u;
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

  // A target's circle radius from its signed curvature. A straight target (kappa at or below the
  // epsilon) reads the cap instead of overflowing, so head input 19 stays finite everywhere.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainAttachRadiusOf(float fitKappa) {
    float const a = attachFabs(fitKappa);
    return (a > chainattach::kEps) ? (1.f / a) : chainattach::kStraightRadius;
  }

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
    uint32_t row;       // the pLS row this record belongs to
    float xs[7];        // head inputs 0..6, preprocessed (attachStdz<0..6>)
    float tanLambda;    // pz / max(pt, eps)
    float kappaSigned;  // rotSign / max(circleRadius, eps)
    float rotSign;      // -charge
    float phi;          // the dPhi fallback direction == the SEED phi (pixelSeeds.phi())
    float cx, cy, r, d;
    float hit0z, rt0;
    // The seed's innermost anchor hit, for head input 19 (rphiResidInwards): its residual to the
    // TARGET's circle. rt0 above is already built from these two, so they cost no extra load.
    float hit0x, hit0y;
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
    float xs[7];  // head inputs 7..13, preprocessed (attachStdz<7..13>)
    float rotSign, centerX, centerY;
    // U5 DELETION (2026-08-07): `float tcEta, tcPhi;` lived here to feed the -XC bare-chain arm's
    // pass-1 dR^2 window. That window was dropped in a2f81bb ("Drop the bare-chain crossclean's dR
    // window") and -CCS, tcEta's last reader, was deleted by T1 in round 1, so BOTH FIELDS WERE
    // WRITE-ONLY: three write sites (ChainAttach.h twice, ChainAttachT3.h once) and ZERO readers in
    // the whole tree. T1 flagged this at FINDINGS_GPU.md [T1 11:15] item 7 and deliberately left it
    // to keep its measured binary identical; nobody picked it up. Removing them also removes the
    // three-level dependent chase that computed them (nodeItems -> tripletIndex -> chainNodeMDs ->
    // chainT3Eta / chainHitPhi) once per target.
    // NOT to be confused with the ChainsSoA::tcEta / tcPhi COLUMNS, which are LIVE
    // (written in ChainArbitrate.h:306-307 by ChainEmitTCs).
    // radius = 1/|fitKappa| clamped to kStraightRadius, the target's own circle radius. Consumed
    // by head input 19 (rphiResidInwards); a bare-T3 target fills it the same way.
    float radius;
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

  static constexpr uint32_t kAttachCells = static_cast<uint32_t>(kAttachRBins) * kAttachTanLBins * kAttachPhiBins;

  // The phi-cell set of one pLS fits in a 16-bit word (kAttachPhiBins == 16); this is its mask.
  static_assert(kAttachPhiBins <= 16, "the phi-cell set is carried in a uint16_t");
  static constexpr uint32_t kAttachPhiCellMask = (kAttachPhiBins >= 32) ? 0xFFFFFFFFu : ((1u << kAttachPhiBins) - 1u);

  // How many surviving pairs the host backends evaluate through the r2 head at a time. 24 hidden
  // units x 16 lanes is 384 accumulators, which the AVX-512 register file plus L1 absorbs; the
  // device path uses 1 (see ChainAttachScore).
  static constexpr int kAttachScoreBatch = 16;

  // How many DEVICE threads cooperate on one target's candidate walk (see ChainAttachScore).
  // A multiple of 32 puts whole warps on one target, so the cell-bound loads are warp-uniform and
  // the lanes read consecutive 96-byte grid items. Host backends always use 1 (one thread per
  // target, the serial walk), so this constant never reaches the CPU path.
  //
  // 64 is measured (tag T2P2, 30 evt, stage-A `score` ms): nS=1 1.440 | 32 0.069 | 64 0.053 |
  // 128 0.066 -- past 64 the per-thread fixed cost (the target record plus the cell-bound walk)
  // starts to outweigh the extra parallelism.
  static constexpr uint32_t kAttachScoreSlices = 64u;

  // The 16-bit set of phi cells pLS `pls` can serve for r bin [rLo, rHi]. Returns 0 when the bin
  // holds no target. Building a MASK rather than emitting intervals removes the arc / fallback
  // overlap for free, so the count pass and the scatter pass agree by construction.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t
  attachPhiCellMask(TAcc const& acc, AttachPlsPre const& pls, float rLo, float rHi, float pad) {
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
        o.hit0x = hx;
        o.hit0y = hy;
        o.row = p;
        o.phiMask = 0u;  // only the scattered copies carry a cell set
        out[p] = o;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8-0c. The per-target record: prototype/PixelAttach.cc makeChainPreGeom + makeChainPre.
  // Every accumulation is in double exactly as the reference does it, and the anchor coordinates
  // are promoted to double BEFORE any differencing (the P2.2 ported-fit rule). The reference stages
  // the cumulative chord lengths in an sArc vector; the same sequential accumulation is replayed in
  // each pass here, which reproduces every partial sum bit for bit without an array.
  struct ChainAttachTargetPre {
    // U5: `SegmentsConst segments`, `TripletsConst triplets` and `ChainNodesConst nodes` are gone
    // with the tcEta / tcPhi block above -- they had NO other use in this kernel. Each was a whole
    // SoA ConstView in the kernel's parameter pack, so the deletion also shrinks cmem[0]
    // (a free ptxas receipt that the change is really in the device image).
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst mds,
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
        // The target's own circle radius (head input 19). A straight target reads the cap rather
        // than overflowing.
        o.radius = chainAttachRadiusOf(o.fitKappa);
        o.xs[0] = attachStdz<7>(acc, o.fitKappa);
        o.xs[1] = attachStdz<8>(acc, o.tanLambda);
        o.xs[2] = attachStdz<9>(acc, chains.features()[c][10]);  // innermostLayer
        o.xs[3] = attachStdz<10>(acc, chains.features()[c][1]);  // nLayers
        // S3: the THREE RAW 3-class gate logits, already stored per chain for the -G 6 kills
        // and the K9 order key, so this costs no new chain-side arithmetic. They replaced the
        // DELETED 2-class chainmlp logit and the head was retrained on them; nothing reduces or
        // combines them by hand (max() would discard which class won, which is precisely the
        // defect that made gateLogit2 score .336 -- inverted -- on displaced-vs-prompt).
        o.xs[4] = attachStdz<11>(acc, chains.zFake()[c]);
        o.xs[5] = attachStdz<12>(acc, chains.zPrompt()[c]);
        o.xs[6] = attachStdz<13>(acc, chains.zDisp()[c]);
        out[t] = o;
      }
    }
  };

  // ------------------------------------------------------------------------------------------
  // K8a-1. The measured radial hull of the targets per r bin. Storing the raw bit patterns of the
  // non-negative floats makes the integer atomicMin / atomicMax an exact float min / max.
  //
  // It takes BOTH target sets (stage-A chain targets and stage-B bare-T3 targets) in ONE launch and
  // reduces them into ONE hull -- the UNION. That is what lets a single grid serve both stages: the
  // hull only ever LOOSENS the phi interval each pLS occupies, and a looser interval yields a
  // SUPERSET of candidates which both scorers re-filter with the same exact analytic predicate
  // before any observable write. Same argument already blessed in-tree for the aux 4-layer targets
  // joining the stage-A hull. `tgtB` may be null (then only set A contributes).
  struct ChainAttachGridBounds {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachTargetPre const* tgtA,
                                  uint32_t nA,
                                  AttachTargetPre const* tgtB,
                                  uint32_t nB,
                                  uint32_t* rMinBits,
                                  uint32_t* rMaxBits) const {
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nA + nB)) {
        float const rt = (i < nA) ? tgtA[i].rtInner : tgtB[i - nA].rtInner;
        int const rb = attachRBin(rt);
        uint32_t const bits = std::bit_cast<uint32_t>(rt);
        // The read-first filter is exact -- atomicMin/Max are idempotent and a value that does not
        // improve the running extremum cannot change the final one -- and it keeps a few thousand
        // threads off the same three or four occupied bins.
        if (bits < rMinBits[rb])
          alpaka::atomicMin(acc, &rMinBits[rb], bits, alpaka::hierarchy::Threads{});
        if (bits > rMaxBits[rb])
          alpaka::atomicMax(acc, &rMaxBits[rb], bits, alpaka::hierarchy::Threads{});
      }
    }
  };

  // K8a-2 / K8a-4. Count and scatter share one mask buffer, so they cannot disagree.
  //
  // LAUNCH SHAPE (the reason `nR` exists). The natural element of both passes is a (pLS, r bin)
  // PAIR, not a pLS: the nine r bins of one pLS are completely independent -- each computes its own
  // phi-cell mask from that bin's target hull and writes its own masks[] slot -- so folding them
  // into one thread throws away a factor of kAttachRBins of parallelism, and the launch was the
  // fixed 80 x 256 flat work division regardless of the element count. With nR == kAttachRBins
  // there are nPls * 9 ~ 211k elements and the launch is sized to them, which fills the machine.
  // MEASURED WORTH, so nobody over-reads the paragraph above: only -0.032 ms of the 0.339 ms these
  // two passes cost (broker tag U4P1), and the curve is already flat at nR = 3. The scatter is
  // WRITE-BANDWIDTH bound, not latency bound -- it copies a whole 96-byte AttachPlsPre per grid
  // entry to a scattered address -- so parallelism was never the binding constraint here. The real
  // win in this area was removing one of the two grids entirely (see buildAttachGrid).
  // nR == 1 recovers the original serial-over-r form verbatim and is what the host backends use
  // (one thread per pLS, the r loop hoisting the pLS record and its tanLambda bin). Every nR
  // partitions the same (pLS, r bin) set exactly once, and both passes only ever write
  // masks[p * kAttachRBins + rb] (one owner per pair) and atomically accumulate into counts[] /
  // cursor[], so the result is independent of the partition.
  struct ChainAttachGridCount {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachPlsPre const* pls,
                                  uint32_t nPls,
                                  uint32_t const* rMinBits,
                                  uint32_t const* rMaxBits,
                                  uint16_t* masks,
                                  uint32_t* counts,
                                  uint32_t nR,
                                  ChainConfig cfg) const {
      uint32_t const nRs = (nR > 0u) ? nR : 1u;
      for (uint32_t gi : cms::alpakatools::uniform_elements(acc, nPls * nRs)) {
        uint32_t const p = (nRs == 1u) ? gi : gi / nRs;
        int const rb0 = (nRs == 1u) ? 0 : static_cast<int>(gi - p * nRs);
        int const tb = attachTanLBin(pls[p].tanLambda, cfg.attachPrefDTanL);
        for (int rb = rb0; rb < kAttachRBins; rb += static_cast<int>(nRs)) {
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
                                  uint32_t nR,
                                  ChainConfig cfg) const {
      uint32_t const nRs = (nR > 0u) ? nR : 1u;
      for (uint32_t gi : cms::alpakatools::uniform_elements(acc, nPls * nRs)) {
        uint32_t const p = (nRs == 1u) ? gi : gi / nRs;
        int const rb0 = (nRs == 1u) ? 0 : static_cast<int>(gi - p * nRs);
        int const tb = attachTanLBin(pls[p].tanLambda, cfg.attachPrefDTanL);
        for (int rb = rb0; rb < kAttachRBins; rb += static_cast<int>(nRs)) {
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
  // The 22 outputs are written to xOut[i * xStride], so the caller can stage a batch of pairs
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

    // 0..6 per-pLS and 7..13 per-target: hoisted into the pre-records, copied here.
    CMS_UNROLL_LOOP
    for (int i = 0; i < 7; ++i)
      xOut[i * xStride] = pls.xs[i];
    CMS_UNROLL_LOOP
    for (int i = 0; i < 7; ++i)
      xOut[(7 + i) * xStride] = cp.xs[i];
    xOut[14 * xStride] = attachStdz<14>(acc, chargeAgree);
    xOut[15 * xStride] = attachStdz<15>(acc, dKappa);
    xOut[16 * xStride] = attachStdz<16>(acc, dTanL);
    xOut[17 * xStride] = attachStdz<17>(acc, dPhi);
    xOut[18 * xStride] = attachStdz<18>(acc, centerDist);
    xOut[19 * xStride] = attachStdz<19>(acc, zResid);
    // targetType: chain target (-RT3 0, so the bare-T3 kind does not exist)
    xOut[20 * xStride] = attachStdz<20>(acc, 0.f);
    // 21 rphiResidInwards: the SEED's innermost anchor hit's signed residual to the TARGET's own
    // circle -- LST's rPhiChiSquaredInwards in content (PixelTriplet.h:330-339), one sqrt per pair.
    // An invalid target circle keeps the 0 flag value, exactly as slot 16 does.
    float residInw = 0.f;
    if (cp.centerValid) {
      float const rdx = pls.hit0x - cp.centerX;
      float const rdy = pls.hit0y - cp.centerY;
      residInw = alpaka::math::sqrt(acc, rdx * rdx + rdy * rdy) - cp.radius;
    }
    xOut[21 * xStride] = attachStdz<21>(acc, residInw);
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


  // The packed (logit, earlier position) argmax key of the attach contention (stage A's parallel
  // form below and the stage-B kernels of ChainAttachT3.h share it).
  // -0.0f and +0.0f compare equal as floats but have different order keys, so the packed argmax
  // would rank them. Every logit that reaches it is >= cfg.attachTheta, but canonicalising costs
  // one instruction and removes the question entirely.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint64_t attachContendKey(float logit, uint32_t pos) {
    float const v = (logit == 0.f) ? 0.f : logit;
    return (static_cast<uint64_t>(chainOrderFloat(v)) << 32) | static_cast<uint64_t>(0xFFFFFFFFu - pos);
  }

  // ------------------------------------------------------------------------------------------
  // K8b / the per-target half of K8c. One thread per target: gather the grid candidates, evaluate
  // the exact predicate, score the survivors and keep the best.
  //
  // It carries the aux 4-layer (-XC4) targets in the same launch as the stage-A 5+ ones; see the
  // is4L comment in the target loop for why they belong here and what they are allowed to write.
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
  //
  // P2.6c, DEVICE PARALLELISM (this is the whole GPU cost of the stage). One thread per target is
  // nTargets = ~1080 threads at PU200: 34 warps for a 142-SM device, five of the eighty blocks the
  // launch asks for, and no latency hiding whatsoever, while each thread walks ~333 candidates and
  // runs ~125 head evaluations serially. So the walk is SLICED: nS threads share one target, thread
  // `sl` taking items b+sl, b+sl+nS, ... of every scanned cell. Every candidate is still visited
  // exactly once by exactly one thread -- nothing is re-walked, which is what made the reverted
  // per-(target,phase) split slower -- and the census (cand / dup / scored) is unchanged term by
  // term. The per-target argmax becomes an atomicMax on the packed attachContendKey, which is the
  // SAME strict total order the serial `lo > best || (lo == best && p < best)` implements, so the
  // pick is bit-identical; ChainAttachUnpackBest turns the key back into (tgtPls, tgtLogit).
  // nS == 1 on host backends, where this is the previous code verbatim.
  struct ChainAttachScore {
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(TAcc const& acc,
                                  AttachPlsPre const* pls,
                                  AttachTargetPre const* tgt,
                                  uint32_t nTargets,
                                  uint32_t nTgtAll,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  uint64_t* tgtKey,
                                  uint32_t* plsBest,
                                  ChainXcPair* xcPairs,
                                  uint32_t* xcCursor,
                                  uint32_t xcCap,
                                  uint32_t* stats,
                                  uint32_t nSlices,
                                  ChainConfig cfg) const {
      constexpr bool kHost = cms::alpakatools::requires_single_thread_per_block_v<TAcc>;
      constexpr int kB = kHost ? kAttachScoreBatch : 1;
      constexpr int kIn = dnn::attachmlp::kInput;
      uint32_t const nS = kHost ? 1u : ((nSlices > 0u) ? nSlices : 1u);

      alignas(64) float xT[kIn * kB];
      int32_t rowB[kB];
      AttachPlsPre const* ppB[kB];
      float logits[kB];
      for (int i = 0; i < kIn * kB; ++i)
        xT[i] = 0.f;  // the tail lanes of a partial batch are evaluated and discarded

      // MERGED (T1 x T2): T1 widened this launch to carry the aux 4-layer targets; T2 sliced each
      // target's candidate walk nS ways. The two are orthogonal -- one extends the target list, the
      // other partitions the work per target -- so the launch is nTgtAll * nS and the slice index is
      // recovered the same way, with the -XC4 class still decided by POSITION in the target list.
      for (uint32_t gi : cms::alpakatools::uniform_elements(acc, nTgtAll * nS)) {
        uint32_t t, sl;
        if constexpr (kHost) {
          t = gi;
          sl = 0u;
        } else {
          t = gi / nS;
          sl = gi - t * nS;
        }
        // A15 -XC4. Positions [nTargets, nTgtAll) are the aux 4-LAYER target list, which exists
        // only to feed the -XC pass-1 append: it writes no plsBest, no tgtKey and no census, so
        // -RPSA and the delivery are bit-identical with and without it (invariant I5). The two
        // classes are disjoint and contiguous by construction -- ChainTargetFlags keeps
        // nLayers >= kAttachMinLayers and ChainAttachSelectAux keeps nLayers == 4 -- so the
        // position is the class and no chain row has to be re-read to tell them apart.
        // They ride in THIS launch rather than a second one because the -XC4 append reads nothing
        // the contention stage produces: the whole grant map is irrelevant to it.
        bool const is4L = (t >= nTargets);
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
            if (!is4L)
              alpaka::atomicMax(
                  acc, &plsBest[static_cast<uint32_t>(p)], chainOrderFloat(lo), alpaka::hierarchy::Threads{});
            // -XC bare-chain arm, pass 1 (A15): the filtered (chain, seed) compaction of the
            // scored-pair stream. Threshold on the |seed eta|-banded xcTheta ONLY -- no geometric
            // window. LST's T5 arm gated its embedding test with a dR^2 < 0.02 window on the
            // TC CENTROID direction, which for a bare chain is the mean of hits spanning several
            // layers and is not where the seed's helix points; the window was therefore vetoing
            // pairs the head had already scored as matches. Dropping it and re-fitting the three
            // bars buys dup barrel -.0046 / transition -.0027 at eff -.0005, displaced bit-flat
            // (977 evt, d3_ref/r_E1_977 vs r_BASE977). Ordering: BEFORE the delivery threshold
            // (the crossclean sees sub-margin pairs; xcThr sits below attachThr).
            if (xcPairs != nullptr && pq.isQuad != 0u && lo >= pq.xcThr) {
              uint32_t const slot = alpaka::atomicAdd(acc, xcCursor, 1u, alpaka::hierarchy::Threads{});
              if (slot < xcCap) {
                xcPairs[slot].chain = cp.chain;
                xcPairs[slot].pls = static_cast<uint32_t>(p);
              } else {
                alpaka::atomicAdd(acc, &stats[11], 1u, alpaka::hierarchy::Threads{});  // overflow census
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
            for (uint32_t i = b + sl; i < e; i += nS) {
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

        if (is4L)
          continue;  // score-only: no delivery verdict and out of the census, as in invariant I5
        // One packed-key atomicMax per slice retires the slice's local argmax. Blocks{} hierarchy:
        // slices of one target can land in different blocks when nS does not divide the block size.
        if (bestPls >= 0)
          alpaka::atomicMax(acc,
                            &tgtKey[t],
                            attachContendKey(bestLogit, static_cast<uint32_t>(bestPls)),
                            alpaka::hierarchy::Blocks{});
        alpaka::atomicAdd(acc, &stats[1], nCand, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[2], nScored, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[10], nDup, alpaka::hierarchy::Threads{});
      }
    }
  };

  // The per-target winner, read back out of the packed argmax key the slices of ChainAttachScore
  // reduce into. chainUnorderFloat is the exact inverse of chainOrderFloat, so tgtLogit is the same
  // float the serial scan produced -- which matters, because ChainAttachResolve re-forms the key
  // from it and compares for equality.
  //
  // Key 0 means "no pair reached the delivery margin": chainOrderFloat is 0 only for the single
  // bit pattern 0xFFFFFFFF (a negative NaN), which no logit can be.
  //
  // It also owns the two per-TARGET censuses that a sliced scorer can no longer count itself,
  // because each slice would count its own share: stats[3] (targets that made a pick) and, when
  // tgtScored is given, stats[8] (targets that scored at least one pair -- stage B's counter;
  // stage A passes nullptr).
  struct ChainAttachUnpackBest {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint64_t const* tgtKey,
                                  uint32_t const* tgtScored,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  uint32_t nTargets,
                                  uint32_t* stats) const {
      for (uint32_t t : cms::alpakatools::uniform_elements(acc, nTargets)) {
        uint64_t const k = tgtKey[t];
        if (k == 0u) {
          tgtPls[t] = -1;
          tgtLogit[t] = kAttachNoLogit;
        } else {
          tgtPls[t] = static_cast<int32_t>(0xFFFFFFFFu - static_cast<uint32_t>(k & 0xFFFFFFFFu));
          tgtLogit[t] = chainUnorderFloat(static_cast<uint32_t>(k >> 32));
          alpaka::atomicAdd(acc, &stats[3], 1u, alpaka::hierarchy::Threads{});
        }
        if (tgtScored != nullptr && tgtScored[t] > 0u)
          alpaka::atomicAdd(acc, &stats[8], 1u, alpaka::hierarchy::Threads{});
      }
    }
  };

  // The aux 4-layer target list: accepted chains with nLayers == 4, appended AFTER the stage-A
  // (5+) targets in the combined array. Serial because it reads the K9 accepted order (order is
  // immaterial for -XC4, but the append position must not race the stage-A count).
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


  // ==========================================================================================
  // Multi-kernel forms of the K8 target-list, contention and -RD dedup stages (was ChainParallel.h).

  // ==========================================================================================
  // K8. The attach contention and the -RD seed-family dedup.
  // (attachContendKey, the packed argmax key, lives in ChainAttach.h next to chainOrderFloat:
  // the stage-B kernels of ChainAttachT3.h share it.)

  struct ChainTargetFlags {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t nBound,
                                  uint32_t* keep,
                                  ChainConfig cfg) const {
      uint32_t const nAcc = chains.nAccepted();
      for (uint32_t ai : cms::alpakatools::uniform_elements(acc, nBound)) {
        bool k = false;
        if (ai < nAcc) {
          uint32_t const c = accepted[ai];
          k = (chains.nLayers()[c] >= kAttachMinLayers) && !(chains.dcaXY()[c] >= cfg.attachDcaMax);
        }
        keep[ai] = k ? 1u : 0u;
      }
    }
  };

  // K8c contention. The reference walks the target positions and lets a later target take a
  // contested pLS only on a STRICTLY higher logit -- which is the argmax over the positions that
  // picked that pLS, with the EARLIER position keeping an exact tie. An argmax has no order, so it
  // is a plain atomicMax over the packed (logit, ~position) key.
  struct ChainAttachArgmax {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, int32_t const* tgtPls, float const* tgtLogit, uint32_t nTargets, uint64_t* plsKey) const {
      for (uint32_t pos : cms::alpakatools::uniform_elements(acc, nTargets)) {
        int32_t const p = tgtPls[pos];
        if (p < 0)
          continue;
        alpaka::atomicMax(
            acc, &plsKey[static_cast<uint32_t>(p)], attachContendKey(tgtLogit[pos], pos), alpaka::hierarchy::Blocks{});
      }
    }
  };

  // Both attach stages resolve their argmax with this kernel. `targets == nullptr` is stage B,
  // whose verdicts stay on the position arrays; stage A passes its target list and the winners are
  // published onto the chain rows as well, because its -RD pass and its delivery are chain-row
  // keyed.
  struct ChainAttachResolve {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  Chains chains,
                                  uint32_t const* targets,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  uint32_t nTargets,
                                  uint64_t const* plsKey,
                                  uint32_t* keep) const {
      for (uint32_t pos : cms::alpakatools::uniform_elements(acc, nTargets)) {
        int32_t const p = tgtPls[pos];
        if (p >= 0 && plsKey[static_cast<uint32_t>(p)] != attachContendKey(tgtLogit[pos], pos)) {
          tgtPls[pos] = -1;
          tgtLogit[pos] = kAttachNoLogit;
        }
        if (targets != nullptr) {
          uint32_t const c = targets[pos];
          chains.attachPls()[c] = tgtPls[pos];
          chains.attachLogit()[c] = tgtLogit[pos];
        }
        keep[pos] = (tgtPls[pos] >= 0) ? 1u : 0u;
      }
    }
  };

  // (The -RD rank kernel is gone: per the zero-sorts directive the dedup walk visits owners in
  // the gather's ascending-position order -- simp change 3, NONEXACT, n300-gated. The serial
  // walk below consumes the gathered list directly.)

  // The pLS's DISTINCT pixel hit indices, staged so the dedup passes below touch a compact array
  // instead of chasing the hits SoA. Keyed by owner slot, not by pLS row.
  //
  // Both stages stage their owners here. The owner list holds chain rows in stage A (its grant
  // lives on the chain row, so `tgtPls == nullptr` reads chains.attachPls()) and target positions
  // in stage B (`tgtPls` is its position-keyed grant array). Nothing else differs.
  //
  // NO DEFAULT ARGUMENTS, and every call site casts its nullptrs to the parameter type. alpaka
  // deduces the kernel's argument PACK from the call, so a defaulted argument or a bare `nullptr`
  // makes the two stages instantiate the SAME kernel TWICE -- two ptxas entries for one algorithm.
  // Spelling every argument out collapses them to one. (This deletes one instantiation the baseline
  // already carried: its stage-A call passed `Dn` for tgtPls and its stage-B call passed `Pi`.)
  //
  // BOTH stages also build the -RD table here (`hashKey != nullptr`), concurrently, one thread per
  // owner. See ChainAttachSeedDedup for why building it for every owner up front -- rather than
  // one kept owner at a time in the serial walk -- leaves the same table. `ownerTag` is the stage
  // bias added to the owner index before it is stored (chainattach::kSeedOwnerStageA / ...StageB),
  // which is what makes "this entry belongs to an already-decided owner" a single comparison.
  struct ChainAttachOwnerHits {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  HitsBaseConst hitsBase,
                                  ChainsConst chains,
                                  int32_t const* tgtPls,
                                  uint32_t const* owners,
                                  uint32_t const* nOwnersPtr,
                                  uint32_t nBound,
                                  uint32_t nHits,
                                  uint32_t* ownerHits,
                                  uint8_t* ownerNHits,
                                  int32_t* ownerPls,
                                  uint32_t* hashKey,
                                  int32_t* hashVal,
                                  uint32_t* hashOwner,
                                  uint32_t* stats,
                                  uint32_t ownerTag) const {
      uint32_t const n = *nOwnersPtr;
      // Local copy: atomicCas takes its compare value by reference, and a constexpr in namespace
      // scope has no device-side storage to bind to.
      uint32_t const empty = chainattach::kSeedHashEmpty;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n)
          continue;
        uint32_t const c = owners[i];
        int32_t const p = (tgtPls != nullptr) ? tgtPls[c] : chains.attachPls()[c];
        ownerPls[i] = p;
        int nh = 0;
        if (p >= 0) {
          uint32_t const first = pixelSeeds.firstHit()[p];
          uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[p]);
          uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
          for (uint32_t k = 0; k < nStored; ++k) {
            uint32_t const h = first + k;
            if (h >= nHits)
              continue;
            if (hitsBase.detid()[h] != kPixelModuleId)
              continue;
            ownerHits[i * kMaxPLSHitsInHitsSoA + nh] = hitsBase.idxs()[h];
            ++nh;
          }
        }
        ownerNHits[i] = static_cast<uint8_t>(nh);

        if (hashKey == nullptr || p < 0)
          continue;
        // One entry per (owner, staged position), never folded: the serial form inserted one entry
        // per position too, and stage B's dup test counts entries. A CAS that loses the slot moves
        // on even when the resident key is our own -- that is what keeps the MULTISET of entries
        // the same as the serial insert's.
        for (int a = 0; a < nh; ++a) {
          uint32_t const key = ownerHits[i * kMaxPLSHitsInHitsSoA + a];
          uint32_t slot = attachSeedHash(key);
          for (uint32_t probe = 0; probe < chainattach::kSeedHashMaxProbe; ++probe) {
            uint32_t const prev = alpaka::atomicCas(acc, &hashKey[slot], empty, key, alpaka::hierarchy::Blocks{});
            if (prev == empty) {
              hashVal[slot] = p;
              hashOwner[slot] = ownerTag + i;
              break;
            }
            slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
            if (probe + 1u == chainattach::kSeedHashMaxProbe)
              alpaka::atomicAdd(acc, &stats[7], 1u, alpaka::hierarchy::Blocks{});  // never at PU200
          }
        }
      }
    }
  };

  // How many entries of the -RD table, over ALL of owner i's staged keys, carry owner TAG j. This
  // is exactly the quantity the serial form's `seen` array tested: an entry of j sits under key k
  // once per occurrence of k in j's staged list, so the count is
  //   sum over i's positions a of (multiplicity of ownerHits[i][a] in j's list)
  //     == sum over keys k of mult(k, i) * mult(k, j),
  // which is SYMMETRIC in i and j. "shares >= 2 pixel hit rows" is count >= 2.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t attachSeedShared(uint32_t const* hashKey,
                                                           uint32_t const* hashOwner,
                                                           uint32_t const* g,
                                                           int nh,
                                                           uint32_t j) {
    uint32_t cnt = 0;
    for (int b = 0; b < nh; ++b) {
      uint32_t slot = attachSeedHash(g[b]);
      for (uint32_t key = hashKey[slot]; key != chainattach::kSeedHashEmpty; key = hashKey[slot]) {
        if (key == g[b] && hashOwner[slot] == j)
          ++cnt;
        slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
      }
    }
    return cnt;
  }

  // K8c-1, the parallel prefilter of the -RD dedup: one thread per owner, no order anywhere.
  //
  // `cont[i]` bit 0 is set iff SOME OTHER owner shares >= 2 pixel hit rows with owner i. Because the
  // relation is symmetric (see attachSeedShared) each side of a pair marks itself, so no thread
  // writes another thread's flag. The serial greedy below then only has to visit the flagged
  // owners: an owner with no partner can neither be revoked (a revocation needs a partner) nor
  // cause one (the partner relation is the same relation), so it is inert and skipping it is not
  // an approximation. At PU200 the flagged set is a handful of a few hundred owners.
  //
  // `cont[i]` bit 1 (kSeedContEarlierStage) is set iff one of those partners belongs to an EARLIER
  // STAGE -- an owner tag below `ownerTag`, i.e. a stage-A owner seen from stage B. Such a partner
  // is final: it cannot be revoked by anything the greedy does, so bit 1 alone decides owner i and
  // the greedy revokes it WITHOUT walking the table at all. This is what makes the split pay in
  // stage B, where the flagged fraction is ~4/5 of the owners rather than stage A's 4 of 716.
  // In stage A `ownerTag` is 0, no tag can be below it, and the predicate below collapses to the
  // original `!contentious && j != i` character for character.
  //
  // Bit 2 (kSeedContDead) is not written here: it is the greedy's own "already revoked" marker, and
  // it lives in this word so the residue needs no second array (see ChainAttachT3Dedup).
  struct ChainAttachSeedConflicts {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t const* nOwnersPtr,
                                  uint32_t const* ownerHits,
                                  uint8_t const* ownerNHits,
                                  int32_t const* ownerPls,
                                  uint32_t const* hashKey,
                                  uint32_t const* hashOwner,
                                  uint32_t nBound,
                                  uint32_t* cont,
                                  uint32_t* stats,
                                  uint32_t ownerTag) const {
      uint32_t const n = *nOwnersPtr;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n)
          continue;
        cont[i] = 0u;
        if (ownerPls[i] < 0)
          continue;
        uint32_t const self = ownerTag + i;
        uint32_t const* g = ownerHits + static_cast<size_t>(i) * kMaxPLSHitsInHitsSoA;
        int const nh = ownerNHits[i];
        bool contentious = false;
        bool earlierStage = false;
        // nEnt is the number of table entries owner i's keys resolve to. The retired serial walk
        // recorded one `seen` slot per DISTINCT owner among a SUBSET of these (only the kept
        // earlier owners were in its table), so nEnt bounds its nSeen from above: reporting the
        // maximum is the standing evidence that its kSeedDedupSeenMax cap never truncated a
        // verdict, which is the one place the two forms could have disagreed.
        uint32_t nEnt = 0;
        for (int a = 0; a < nh; ++a) {
          uint32_t slot = attachSeedHash(g[a]);
          for (uint32_t key = hashKey[slot]; key != chainattach::kSeedHashEmpty; key = hashKey[slot]) {
            if (key == g[a]) {
              ++nEnt;
              uint32_t const j = hashOwner[slot];
              // Stop as soon as BOTH bits are settled; while only bit 1 is outstanding, only an
              // earlier-stage tag can still change the answer. With ownerTag == 0 (stage A) the
              // `j < ownerTag` disjunct is false, so this is `!contentious && j != i` exactly.
              bool const want = (j != self) && (!contentious || (!earlierStage && j < ownerTag));
              if (want && attachSeedShared(hashKey, hashOwner, g, nh, j) >= 2u) {
                contentious = true;
                if (j < ownerTag)
                  earlierStage = true;
              }
            }
            slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
          }
        }
        alpaka::atomicMax(acc, &stats[14], nEnt, alpaka::hierarchy::Blocks{});
        if (contentious) {
          cont[i] = chainattach::kSeedContPartner | (earlierStage ? chainattach::kSeedContEarlierStage : 0u);
          alpaka::atomicAdd(acc, &stats[13], 1u, alpaka::hierarchy::Blocks{});
          if (earlierStage)
            alpaka::atomicAdd(acc, &stats[12], 1u, alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

  // OFFLINE PREFILTER VERIFICATION, off unless LST_CHAIN_RD_AUDIT is set (the parity-protocol
  // sidecar, same shape as ChainAttachAudit). It recomputes the partner relation the brute-force
  // way -- every ordered owner pair, straight off the staged hit lists, no hash table anywhere --
  // and counts the owners whose cont[] flag disagrees. The count must be zero: it is the assertion
  // that ChainAttachSeedConflicts flags a superset of the revocable owners, which is what lets the
  // greedy skip the rest.
  struct ChainAttachSeedAudit {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t const* nOwnersPtr,
                                  uint32_t const* ownerHits,
                                  uint8_t const* ownerNHits,
                                  int32_t const* ownerPls,
                                  uint32_t const* cont,
                                  uint32_t nBound,
                                  uint32_t* stats) const {
      uint32_t const n = *nOwnersPtr;
      for (uint32_t i : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (i >= n || ownerPls[i] < 0)
          continue;
        uint32_t const* gi = ownerHits + static_cast<size_t>(i) * kMaxPLSHitsInHitsSoA;
        int const nhi = ownerNHits[i];
        bool partner = false;
        for (uint32_t j = 0; j < n && !partner; ++j) {
          if (j == i || ownerPls[j] < 0)
            continue;
          uint32_t const* gj = ownerHits + static_cast<size_t>(j) * kMaxPLSHitsInHitsSoA;
          int const nhj = ownerNHits[j];
          uint32_t shared = 0;
          for (int a = 0; a < nhi; ++a)
            for (int b = 0; b < nhj; ++b)
              if (gi[a] == gj[b])
                ++shared;
          if (shared >= 2u)
            partner = true;
        }
        if (partner != (cont[i] != 0u))
          alpaka::atomicAdd(acc, &stats[15], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  // K8c-2, the -RD seed-family dedup. It is a greedy over the gathered owner order -- an owner is
  // revoked iff an EARLIER owner that was itself kept shares >= 2 pixel hit rows with it -- so the
  // decision genuinely depends on the order, and unlike ChainXcAnchorHits it cannot simply be run
  // one thread per owner. What it can be is split, which is what the two kernels above do:
  //
  //   (1) the table is built for EVERY owner up front, concurrently (ChainAttachOwnerHits). The
  //       insert is a linear-probe CAS that always claims a fresh slot, so the resulting MULTISET
  //       of (key, owner) entries is interleaving-independent; only the slot LAYOUT can differ from
  //       a serial insert, and no reader observes it, because every lookup probes to the first
  //       empty slot in a later launch. Building it for owners that will later be revoked is what
  //       the tombstone below undoes.
  //   (2) the partner relation -- shares >= 2 hit rows -- is symmetric and order-free, so the set
  //       of owners that could POSSIBLY be revoked (or cause a revocation) is computed in parallel
  //       (ChainAttachSeedConflicts). Everything outside it is inert.
  //
  // What is left here is the greedy over just the flagged owners, and it reads the decision it
  // needs straight off the table: at owner i the entries present are exactly the entries of the
  // owners that are still alive, and the scan below ignores every entry whose owner is not EARLIER
  // than i. That is the same read set the old walk had (which held only kept earlier owners),
  // so the verdict is the same -- with the difference that the count is exact rather than capped
  // at kSeedDedupSeenMax distinct partners.
  //
  // A revoked owner's entries are TOMBSTONED, which is what keeps step (1) honest: after this
  // kernel the table holds exactly the surviving owners' entries, which is what stage B's -RDT
  // reuses, and no later owner in this walk can see a revoked one either.
  struct ChainAttachSeedDedup {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  Chains chains,
                                  uint32_t const* owners,
                                  uint32_t const* nOwnersPtr,
                                  uint32_t const* ownerHits,
                                  uint8_t const* ownerNHits,
                                  int32_t const* ownerPls,
                                  uint32_t const* cont,
                                  uint32_t* hashKey,
                                  uint32_t const* hashOwner,
                                  uint32_t* stats) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nOwners = *nOwnersPtr;

      for (uint32_t i = 0; i < nOwners; ++i) {
        if (cont[i] == 0u)  // no partner anywhere: cannot be revoked, cannot revoke
          continue;
        int32_t const p = ownerPls[i];
        if (p < 0)
          continue;
        uint32_t const* g = ownerHits + static_cast<size_t>(i) * kMaxPLSHitsInHitsSoA;
        int const nh = ownerNHits[i];

        bool dup = false;
        for (int a = 0; a < nh && !dup; ++a) {
          uint32_t slot = attachSeedHash(g[a]);
          for (uint32_t key = hashKey[slot]; key != chainattach::kSeedHashEmpty; key = hashKey[slot]) {
            if (key == g[a]) {
              uint32_t const j = hashOwner[slot];
              // j == i is our own entry; j > i has not been decided yet and cannot revoke us; a
              // revoked j is not here at all (its entries carry the tombstone key).
              if (j < i && attachSeedShared(hashKey, hashOwner, g, nh, j) >= 2u) {
                dup = true;
                break;
              }
            }
            slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
          }
        }
        if (!dup)
          continue;

        chains.attachPls()[owners[i]] = -1;
        chains.attachLogit()[owners[i]] = kAttachNoLogit;
        ++stats[5];
        for (int a = 0; a < nh; ++a) {
          uint32_t slot = attachSeedHash(g[a]);
          for (uint32_t key = hashKey[slot]; key != chainattach::kSeedHashEmpty; key = hashKey[slot]) {
            if (key == g[a] && hashOwner[slot] == i) {
              hashKey[slot] = chainattach::kSeedHashTomb;
              break;  // one entry per staged position, so retire one per position
            }
            slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
          }
        }
      }
    }
  };

  // The surviving grants of EITHER stage, after that stage's -RD/-RDT revocations: the
  // one-pLS-one-owner authority array plsOwned. Both stages leave "revoked" as a negative grant, and
  // a losing target is already negative from ChainAttachResolve, so one pass over the position array
  // covers exactly the survivors.
  //
  // `tgtPls == nullptr` is stage A, whose grant lives on the chain row (a chain can only carry one
  // if it is a target, so its target list is the index); stage B passes its position-keyed grant
  // array and `targets` is unused. Same nullptr switch as ChainAttachOwnerHits and
  // ChainAttachResolve. `stats == nullptr` suppresses the census.
  struct ChainAttachPublish {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* targets,
                                  int32_t const* tgtPls,
                                  uint32_t nTargets,
                                  uint8_t* plsOwned,
                                  uint32_t* stats) const {
      for (uint32_t pos : cms::alpakatools::uniform_elements(acc, nTargets)) {
        int32_t const p = (tgtPls != nullptr) ? tgtPls[pos] : chains.attachPls()[targets[pos]];
        if (p < 0)
          continue;
        plsOwned[static_cast<uint32_t>(p)] = 1u;
        if (stats != nullptr)
          alpaka::atomicAdd(acc, &stats[4], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
