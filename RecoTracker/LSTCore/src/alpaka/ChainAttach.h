#ifndef RecoTracker_LSTCore_src_alpaka_ChainAttach_h
#define RecoTracker_LSTCore_src_alpaka_ChainAttach_h

#include <bit>
#include <numbers>
#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"
#include "HeterogeneousCore/AlpakaMath/interface/deltaPhi.h"

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

// Pixel-seed attach, stage A: match pixel line segments (the inner-tracker seeds) to outer-tracker
// chains and grant each chain at most one seed, producing pixel-seeded track candidates.
//
// Consumes the accepted chains with their fit and gate quantities, and the pixel seeds with their
// hits. Produces the per-chain grant (chains.attachPls / attachLogit), the seed ownership map
// plsOwned, and the filtered (chain, seed) pairs the bare-chain cross-clean resolves later.
// A pixel seed is owned by at most ONE target across BOTH attach stages: this file is stage A
// (chain targets), ChainAttachT3.h is stage B (bare-triplet targets) and depends on that invariant.
//
// Stages:
//   ChainAttachPlsPre        per-seed hoisted record
//   ChainTargetFlags + prefix + compaction: the accepted, long enough, IP-compatible chains, in
//                            accepted order
//   ChainAttachTargetPre     per-target record
//   ChainAttachGridBounds / GridCount / GridScatter   the geometric prefilter grid
//   ChainAttachScore         candidate walk, the pair features, the attach head, best per target
//   ChainAttachArgmax / Resolve / OwnerHits / SeedConflicts / SeedDedup / Publish: one seed one
//                            owner, then the seed-family dedup
//   ChainTCKeepSuppress + prefix + gather/scatter: retirement of the carried pixel-only rows the
//                            attach supersedes (the row-type upgrade itself lives in
//                            ChainArbitrate.h ChainEmitTCs)
//
// THE GRID, AND WHY ITS CANDIDATE SET IS A SUPERSET OF THE EXHAUSTIVE ONE
//
// The exhaustive candidate search is `for each target, for each seed: evalPair` -- about
// 2.5k x 18.4k = 4.6e7 probes per event at PU200, of which 0.28% survive. The grid replaces that
// inner scan and nothing else: the predicate is still evaluated exactly, on exactly the same
// arithmetic, for every candidate the grid returns. The claim below is that every pair passing the
// predicate shares a grid cell with its target, so the walk can miss nothing.
//
// The predicate is two one-dimensional windows,
//     |seed.tanLambda - target.tanLambda| < attachPrefDTanL
//   AND
//     |wrap(phiDir(target.rtInner) - target.chordPhi)| < attachPrefDPhi,
// where phiDir(r) is the direction of motion of the seed helix where it crosses radius r outbound.
// The grid is keyed on exactly the three quantities the windows are written in: the target's
// rtInner, its tanLambda and its chordPhi.
//
// (1) tanLambda. Cells of width attachPrefDTanL over [-30, 30], clamped at both ends. A seed
//     occupies the single cell of its own tanLambda; a target scans every cell overlapping its
//     window, so a pair inside the window shares a scanned cell. Clamping is safe: two clamped
//     values share the edge cell, and a value clamped to within one window width of an unclamped
//     one lands in the same or the neighbouring cell, which is scanned too.
//
// (2) r. Fixed 16 cm bins, used ONLY as a bucketing device. For each bin the kernel measures the
//     min and max rtInner of the targets that actually landed in it, so the bin EDGES never enter
//     the argument -- the interval a seed is scattered against is the exact hull of the targets it
//     will be compared with in that bin. (In the barrel that hull is a couple of centimetres wide.)
//
// (3) phi. For one seed and r bin j the kernel computes the exact RANGE of phiDir over
//     [rMin_j, rMax_j] and scatters the seed into every phi cell that range touches; a target scans
//     every cell overlapping its own chordPhi window. Since target.rtInner lies in
//     [rMin_j, rMax_j], phiDir(rtInner) lies in the scattered range, so the seed occupies the cell
//     containing it; and if the pair passes the phi window then that same value lies in the scanned
//     interval, so the cell is also scanned. Superset, with no slack term to bound.
//
// The range in (3) is computed from the two bin endpoints alone, with no sampling, because phiDir
// is MONOTONE in r: the outbound crossing branch is fixed by the maximum-radial selection and is
// never data-dependent, so the traversal is a monotone arc of at most half a turn whose extremes
// are its endpoints. Two edge cases are handled explicitly -- a bin that leaves the seed's
// reachable annulus has no crossing and falls back to the seed phi, which is added to the occupied
// cells too, and kAttachPhiPad (1e-3 rad against a 0.4 rad window) absorbs float rounding between
// this endpoint evaluation and the decision path's own atan2.
//
// ChainAttachAudit at the bottom of this file is the standing check on all of the above: it
// replays the exhaustive scan and counts the pairs the grid failed to offer, which must be zero.
//
// ChainAttachAudit below is the standing check of the construction: it replays the exhaustive scan
// and counts the pairs the grid failed to offer, which must be zero.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chainattach {
    constexpr float kEps = 1e-6f;
    // Radius reported for a straight (zero-curvature) target. 1e5 cm is far outside the detector,
    // so the residual feature built from it saturates instead of overflowing to an infinity.
    constexpr float kStraightRadius = 1e5f;
    // Seed-family dedup scratch. The table holds one entry per (pixel hit index, kept owner) pair;
    // at PU200 that is a few hundred owners x at most 4 hits, so a 16k-slot open-addressed table
    // never exceeds a quarter load. A power of two, so the probe wraps with a mask.
    constexpr uint32_t kSeedHashSlots = 16384u;
    constexpr uint32_t kSeedHashEmpty = 0xFFFFFFFFu;
    // Written over a REVOKED owner's key by the serial dedup pass. It is not the empty sentinel, so
    // it does not truncate a probe run, and it is not a hit index (hitsBase.idxs() numbering is far
    // below it), so it never matches a lookup key. The table's MEMBERSHIP after that pass is
    // therefore exactly the surviving owners' entries, which is what stage B reads.
    constexpr uint32_t kSeedHashTomb = 0xFFFFFFFEu;
    // Probe-length stop for the concurrent insert, standing in for a serial insert's running
    // load-factor test. A few hundred owners x at most 4 hits in 16k slots is under a quarter load,
    // so this can only fire far outside the design point.
    constexpr uint32_t kSeedHashMaxProbe = 256u;
    constexpr int kSeedDedupSeenMax = 64;
    // The two attach stages share ONE table, and the stage-B greedy has to know which entries are
    // "earlier" than the owner it is deciding. An entry's owner tag is `kSeedOwnerStageA + i` in
    // stage A and `kSeedOwnerStageB + i` in stage B, so a plain `tag < myTag` is exactly "already
    // decided": every stage-A entry precedes every stage-B one (stage A is final before stage B
    // starts) and inside stage B it reduces to a comparison of owner indices. The bias is the top
    // bit, which a stage-A owner index -- a few hundred accepted chains -- cannot reach, so the
    // tags of the two stages cannot alias.
    constexpr uint32_t kSeedOwnerStageA = 0u;
    constexpr uint32_t kSeedOwnerStageB = 0x80000000u;
    // The bits of ChainAttachSeedConflicts' per-owner word (see that kernel for what they mean).
    constexpr uint32_t kSeedContPartner = 1u;       // some other owner shares >= 2 pixel hit rows
    constexpr uint32_t kSeedContEarlierStage = 2u;  // ... and one of them is final (earlier stage)
    constexpr uint32_t kSeedContDead = 4u;          // the greedy's own "revoked" marker
    // Diagnostic counters (never read by a decision):
    //   0 grid entries   1 candidates iterated   2 pairs scored   3 per-target picks
    //   4 attached after contention   5 dedup revocations   6 carried rows retired
    //   7 seed-dedup owner-slot overflows   9 dedup logit-tie census
    //  10 grid candidates skipped as a repeat of the same seed inside one target's cell walk
    //  11 cross-clean pass-1 buffer overflow census
    //  12 owners a STRICTLY EARLIER-STAGE partner revokes outright (stage B only -- see
    //     ChainAttachSeedConflicts; unreachable in stage A, whose owner tag bias is zero)
    //  13 contentious owners (the ones the serial greedy actually visits)
    //  14 max table entries one owner's keys resolve to (the kSeedDedupSeenMax headroom witness)
    //  15 conflict-prefilter disagreements with the brute-force pair scan (LST_CHAIN_RD_AUDIT only)
    // NOTE the two stats layouts are DELIBERATELY aligned on slots 7 and 12-14: ChainAttachOwnerHits
    // and ChainAttachSeedConflicts are shared by both attach stages, so a slot they write has to
    // mean the same thing in chainattach::kStats and in chainattacht3::kStats.
    constexpr uint32_t kStats = 16u;
  }  // namespace chainattach

  // A target's circle radius from its signed curvature. A straight target (curvature at or below the
  // epsilon) reads the cap instead of overflowing, so the inward-residual feature built from this
  // radius stays finite everywhere.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float chainAttachRadiusOf(TAcc const& acc, float fitKappa) {
    float const absKappa = alpaka::math::abs(acc, fitKappa);
    return (absKappa > chainattach::kEps) ? (1.f / absKappa) : chainattach::kStraightRadius;
  }

  // Per-input preprocessing of the attach head for ONE input index: sanitize, then the generated
  // header's optional log10(1 + x), clip and standardize, in that order.
  //
  // THIS IS THE SINGLE DEFINITION OF THAT CHAIN, and it must stay that way. Fourteen of the pair
  // features are pure per-seed or per-target quantities, so their preprocessed value is a property
  // of the seed or of the target and is computed ONCE in the pre-record kernels instead of ~150
  // times per seed in the scoring loop. Both the hoisted and the on-the-fly half of the input
  // vector go through this same template with the same compile-time index, which is what stops the
  // two halves from drifting apart or away from the head's training-time preprocessing.
  template <int kInputIndex, typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float attachStdz(TAcc const& acc, float rawValue) {
    static_assert(kInputIndex >= 0 && kInputIndex < dnn::attachmlp::kInput,
                  "attach feature index outside the head input");
    float value = chainSanitize(rawValue);
    if (dnn::attachmlp::kLog10p1[kInputIndex])
      value = alpaka::math::log10(acc, 1.f + value);
    value = alpaka::math::min(
        acc, alpaka::math::max(acc, value, dnn::attachmlp::kClipLo[kInputIndex]), dnn::attachmlp::kClipHi[kInputIndex]);
    return (value - dnn::attachmlp::kFeatMean[kInputIndex]) / dnn::attachmlp::kFeatStd[kInputIndex];
  }

  // Fibonacci hash of a pixel hit index onto the seed-dedup scratch table.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t attachSeedHash(uint32_t hitIndex) {
    uint32_t hash = hitIndex * 2654435761u;
    hash ^= hash >> 15;
    return hash & (chainattach::kSeedHashSlots - 1u);
  }

  // "no pair yet" sentinel. A finite value rather than -infinity, because -ffinite-math-only makes
  // infinities fair game for the optimizer; it is never compared against anything except behind the
  // `bestPls < 0` / `tgtPls < 0` guards that precede it.
  constexpr float kAttachNoLogit = -1e30f;

  // Per-seed hoisted record, and also the GRID ITEM type: the scatter writes the whole record into
  // the cell, so the candidate walk reads one contiguous array instead of chasing 18.4k scattered
  // 72-byte records through the cache. That locality is the single biggest term in the attach wall
  // time.
  //
  // The seven pair-invariant seed features (log10Pt, ptErrRel, etaErr, charge, isQuad, log10R,
  // deltaPhi) are stored ALREADY PREPROCESSED in seedInputs[0..6] rather than raw -- nothing else
  // reads them -- so the record does not grow and the scoring loop assembles those with a copy.
  struct AttachPlsPre {
    uint32_t seedRow;     // the pixel line segment row this record belongs to
    float seedInputs[7];  // head inputs 0..6, preprocessed (attachStdz<0..6>)
    float tanLambda;      // pz / max(pt, eps)
    float kappaSigned;    // rotSign / max(circleRadius, eps)
    float rotSign;        // -charge
    float phi;            // the phiDir fallback direction == the seed phi
    float centreX, centreY, circleRadius, centreDist;
    float innerHitZ, innerHitRt;
    // The seed's innermost anchor hit, for head input 21 (its signed residual to the TARGET's
    // circle). innerHitRt above is already built from these two, so they cost no extra load.
    float innerHitX, innerHitY;
    // Per-seed quantities resolved once here so the pair loop pays one register read instead of an
    // eta-band lookup and a branch:
    //   eta        the seed eta
    //   attachThr  the delivery margin for this seed's |eta| band
    //   xcThr      the cross-clean bar for the same band
    //   isQuad     whether the seed is a quadruplet, which the cross-clean requires
    float eta;
    float attachThr;
    float xcThr;
    uint8_t isQuad;
    uint16_t phiMask;  // the phi-cell set this copy was scattered under; 0 in the per-seed array
  };

  // One filtered (chain, seed) pair of the bare-chain cross-clean, appended at attach-scoring time
  // by pass 1 (logit at or above the |seed eta|-banded xcTheta). Pass 2 resolves it after delivery:
  // the chain must have emitted a SEEDLESS track candidate and the seed must still be unowned.
  struct ChainXcPair {
    uint32_t chain;
    uint32_t plsRow;
  };

  // MEASUREMENT ONLY -- the LST_CHAIN_PAIR_DUMP attach pair row: the on-policy training rows for
  // the attach head. One record per SCORED pair, i.e. per pair that survived the accept test and
  // was therefore pushed through the head. Nothing in the algorithm reads it back; the scorers
  // write it only when the row pointer is non-null, which happens only when the environment
  // variable names an output file.
  //
  //   stage  0 chain target at or above the attach layer floor (the delivery-eligible stage-A list)
  //          1 bare-triplet target (stage B, ChainAttachT3Score)
  //          2 chain target from the auxiliary 4-layer tail of the stage-A launch -- score-only
  //            targets, outside the stage-A census, kept apart so a trainer can drop or keep them
  //            deliberately
  //   target stage 0 / 2: the CHAIN index, the row LST_CHAIN_CHAIN_DUMP enumerates;
  //          stage 1: the sparse triplet index
  //   pls    the pLS row. The pre-record carries no seed index, so the join to the ntuple goes
  //          through the pLS collection order.
  //   x      the standardized head inputs AS CONSUMED, read back out of the batch stage after any
  //          per-kind overwrite (stage B rewrites the target-kind input); de-standardize offline
  //          with the constants in AttachNetworkWeights.h
  //   logit  the head output for exactly that x
  struct ChainAttachPairRow {
    uint32_t stage;
    uint32_t target;
    uint32_t pls;
    float logit;
    float x[kAttachFeatures];
  };

  // Control block of the pair dump: [0] the append cursor (may run past the capacity), [1] the
  // number of rows dropped because it did.
  static constexpr uint32_t kAttachPairCtl = 2;

  // Deterministic, LABEL-FREE and SCORE-FREE keep test for the pair dump. keep == 1 keeps every
  // pair; otherwise one pair in keep survives, chosen by a hash of the two identities the record
  // itself carries, so the decision is reproducible bit-for-bit across reruns AND verifiable
  // offline from the dump alone.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool attachPairKeep(uint32_t target, uint32_t pls, uint32_t keep) {
    if (keep <= 1u)
      return true;
    return (((target * 2654435761u) ^ (pls * 40503u)) % keep) == 0u;
  }

  // The resolved stage-A attach length floor: a configured override, else the compiled-in default.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE int chainAttachMinLayers(ChainConfig const& config) {
    return (config.t4AttachMinLayers > 0) ? config.t4AttachMinLayers : kAttachMinLayers;
  }

  // Per-target record. Both target kinds fill it: chains here, bare triplets in ChainAttachT3.h.
  // fitKappa and tanLambda are kept RAW as well as preprocessed, because the per-pair features
  // dKappa and dTanLambda are differences against the seed and must be formed before standardizing.
  struct AttachTargetPre {
    float rtInner, zInner, chordPhi, tanLambda;
    float fitKappa;
    float targetInputs[7];  // head inputs 7..13, preprocessed (attachStdz<7..13>)
    float rotSign, centerX, centerY;
    // 1/|fitKappa| clamped to kStraightRadius: the target's own circle radius, consumed by head
    // input 21 (the seed's inward rphi residual against it).
    float radius;
    // The chain row for a chain target; the sparse triplet index for a bare-triplet target.
    uint32_t chain;
    uint8_t centerValid;
  };

  // The direction of motion of the seed helix where it crosses the given radius outbound. This is the
  // quantity the phi window is written in and the one the grid needs to be monotone in radius (file
  // header, point 3). Both circle intersections are formed and the outbound one -- the larger
  // radial component -- is taken; a radius the helix never reaches falls back to the seed phi.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE float attachPhiDirAt(TAcc const& acc, AttachPlsPre const& seed, float radius) {
    float phiDir = seed.phi;
    if (seed.centreDist > chainattach::kEps) {
      float const chordOffset =
          (seed.centreDist * seed.centreDist + radius * radius - seed.circleRadius * seed.circleRadius) /
          (2.f * seed.centreDist);
      float const halfChordSq = radius * radius - chordOffset * chordOffset;
      if (halfChordSq >= 0.f) {
        float const halfChord = alpaka::math::sqrt(acc, halfChordSq);
        float const centreDirX = seed.centreX / seed.centreDist, centreDirY = seed.centreY / seed.centreDist;
        float bestTangentX = 0.f, bestTangentY = 0.f, bestRadialComponent = 0.f;
        bool haveBest = false;
        for (int branchSign = -1; branchSign <= 1; branchSign += 2) {
          float const crossX = chordOffset * centreDirX - static_cast<float>(branchSign) * halfChord * centreDirY;
          float const crossY = chordOffset * centreDirY + static_cast<float>(branchSign) * halfChord * centreDirX;
          float const tangentX = seed.rotSign * (-(crossY - seed.centreY));
          float const tangentY = seed.rotSign * (crossX - seed.centreX);
          float const radialComponent = tangentX * crossX + tangentY * crossY;
          if (!haveBest || radialComponent > bestRadialComponent) {
            haveBest = true;
            bestRadialComponent = radialComponent;
            bestTangentX = tangentX;
            bestTangentY = tangentY;
          }
        }
        phiDir = alpaka::math::atan2(acc, bestTangentY, bestTangentX);
      }
    }
    return phiDir;
  }

  // Grid axis helpers. All three clamp into range rather than wrapping or rejecting; the file
  // header's superset argument covers the clamped end cells.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE int attachRBin(float rt) {
    int binIdx = static_cast<int>(rt / kAttachRBinWidth);
    if (binIdx < 0)
      binIdx = 0;
    if (binIdx >= kAttachRBins)
      binIdx = kAttachRBins - 1;
    return binIdx;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE int attachTanLBin(float tanLambda, float width) {
    float const scaled = (tanLambda - kAttachTanLLo) / width;
    int binIdx = (scaled < 0.f) ? -1 : static_cast<int>(scaled);
    if (binIdx < 0)
      binIdx = 0;
    if (binIdx >= kAttachTanLBins)
      binIdx = kAttachTanLBins - 1;
    return binIdx;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE int attachPhiBin(float phi) {
    // phi is folded into [0, 2 pi) first so the bin index needs no negative-floor special case.
    float const twoPi = 2.f * std::numbers::pi_v<float>;
    float wrapped = phi;
    while (wrapped < 0.f)
      wrapped += twoPi;
    while (wrapped >= twoPi)
      wrapped -= twoPi;
    int binIdx = static_cast<int>(wrapped * (static_cast<float>(kAttachPhiBins) / twoPi));
    if (binIdx < 0)
      binIdx = 0;
    if (binIdx >= kAttachPhiBins)
      binIdx = kAttachPhiBins - 1;
    return binIdx;
  }

  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t attachCellId(int rBin, int tanLambdaBin, int phiBin) {
    return static_cast<uint32_t>((rBin * kAttachTanLBins + tanLambdaBin) * kAttachPhiBins + phiBin);
  }

  static constexpr uint32_t kAttachCells = static_cast<uint32_t>(kAttachRBins) * kAttachTanLBins * kAttachPhiBins;

  // The phi-cell set of one seed fits in a 16-bit word (kAttachPhiBins == 16); this is its mask.
  static_assert(kAttachPhiBins <= 16, "the phi-cell set is carried in a uint16_t");
  static constexpr uint32_t kAttachPhiCellMask = (kAttachPhiBins >= 32) ? 0xFFFFFFFFu : ((1u << kAttachPhiBins) - 1u);

  // How many surviving pairs the host backends evaluate through the attach head at a time. 24
  // hidden units x 16 lanes is 384 accumulators, which the AVX-512 register file plus L1 absorbs;
  // the device path uses 1 (see ChainAttachScore).
  static constexpr int kAttachScoreBatch = 16;

  // How many DEVICE threads cooperate on one target's candidate walk (see ChainAttachScore).
  // A multiple of 32 puts whole warps on one target, so the cell-bound loads are warp-uniform and
  // the lanes read consecutive 96-byte grid items. Host backends always use 1 (one thread per
  // target, the serial walk), so this constant never reaches the CPU path.
  //
  // 64 is the measured optimum of the stage: past it the per-thread fixed cost -- the target record
  // and the cell-bound walk, both paid by every slice -- outweighs the extra parallelism.
  static constexpr uint32_t kAttachScoreSlices = 64u;

  // The 16-bit set of phi cells the seed `seed` can serve for r bin [rtMin, rtMax]. Returns 0 when the
  // bin holds no target. Building a MASK rather than emitting intervals absorbs the overlap between
  // the arc and the fallback direction for free, so the count pass and the scatter pass agree by
  // construction.
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t
  attachPhiCellMask(TAcc const& acc, AttachPlsPre const& seed, float rtMin, float rtMax, float phiPad) {
    uint32_t mask = 0u;
    if (!(rtMax >= rtMin))
      return 0u;

    if (!(seed.centreDist > chainattach::kEps)) {
      // Degenerate centre: the direction is the seed phi at every radius.
      mask |= (1u << attachPhiBin(seed.phi));
      return mask;
    }

    // h^2 at the two bin ends decides whether the fallback branch is reachable inside the bin.
    auto halfChordSqAt = [&](float radius) {
      float const chordOffset =
          (seed.centreDist * seed.centreDist + radius * radius - seed.circleRadius * seed.circleRadius) /
          (2.f * seed.centreDist);
      return radius * radius - chordOffset * chordOffset;
    };
    bool const fallbackReachable = (halfChordSqAt(rtMin) < 0.f) || (halfChordSqAt(rtMax) < 0.f);
    if (fallbackReachable)
      mask |= (1u << attachPhiBin(seed.phi));

    // Clip the bin to the reachable annulus [|d - R|, d + R] and take the monotone arc endpoints.
    float const annulusMin = alpaka::math::abs(acc, seed.centreDist - seed.circleRadius);
    float const annulusMax = seed.centreDist + seed.circleRadius;
    float const clipMin = (rtMin > annulusMin) ? rtMin : annulusMin;
    float const clipMax = (rtMax < annulusMax) ? rtMax : annulusMax;
    if (clipMin <= clipMax) {
      float const dirAtLo = attachPhiDirAt(acc, seed, clipMin);
      float const dirAtHi = attachPhiDirAt(acc, seed, clipMax);
      // Both endpoints sit on one monotone arc of at most half a turn, so the shorter signed
      // difference IS the traversed arc.
      float const delta = cms::alpakatools::reducePhiRange(acc, dirAtHi - dirAtLo);
      float phiLo = (delta >= 0.f) ? dirAtLo : dirAtHi;
      float const span = alpaka::math::abs(acc, delta);
      phiLo -= phiPad;
      float const phiHi = phiLo + span + 2.f * phiPad;
      // The occupied cells are exactly bin(phiLo) .. bin(phiHi) walking forward; the arc is at most
      // half a turn plus two pads, so the walk can never lap the ring.
      int const firstBin = attachPhiBin(phiLo), lastBin = attachPhiBin(phiHi);
      int nCells = lastBin - firstBin;
      if (nCells < 0)
        nCells += kAttachPhiBins;
      ++nCells;
      if (nCells > kAttachPhiBins)
        nCells = kAttachPhiBins;
      for (int k = 0; k < nCells; ++k)
        mask |= (1u << ((firstBin + k) % kAttachPhiBins));
    }
    return mask;
  }

  // The per-seed record: everything about a pixel line segment the pair loop needs, resolved once.
  struct ChainAttachPlsPre {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  PixelSeedsConst pixelSeeds,
                                  PixelSegmentsConst pixelSegments,
                                  AttachPlsPre* outRecords,
                                  uint32_t nPls,
                                  ChainConfig config) const {
      for (uint32_t seedIdx : cms::alpakatools::uniform_elements(acc, nPls)) {
        AttachPlsPre record;
        float const pt = alpaka::math::max(acc, pixelSeeds.ptIn()[seedIdx], float{chainattach::kEps});
        float const charge = static_cast<float>(pixelSeeds.charge()[seedIdx]);
        float const circleRadius =
            alpaka::math::max(acc, pixelSegments.circleRadius()[seedIdx], float{chainattach::kEps});
        record.seedInputs[0] = attachStdz<0>(acc, alpaka::math::log10(acc, pt));
        record.seedInputs[1] = attachStdz<1>(acc, pixelSeeds.ptErr()[seedIdx] / pt);
        record.seedInputs[2] = attachStdz<2>(acc, pixelSeeds.etaErr()[seedIdx]);
        record.seedInputs[3] = attachStdz<3>(acc, charge);
        record.seedInputs[4] = attachStdz<4>(acc, pixelSeeds.isQuad()[seedIdx] ? 1.f : 0.f);
        record.seedInputs[5] = attachStdz<5>(acc, alpaka::math::log10(acc, circleRadius));
        record.seedInputs[6] = attachStdz<6>(acc, pixelSeeds.deltaPhi()[seedIdx]);
        record.tanLambda = pixelSeeds.pz()[seedIdx] / pt;
        record.rotSign = (charge > 0.f) ? -1.f : 1.f;
        record.kappaSigned = record.rotSign / circleRadius;
        record.phi = pixelSeeds.phi()[seedIdx];
        // The delivery margin and the cross-clean bar are both banded on the seed |eta| at the same
        // two boundaries; resolving them here keeps eta out of the pair loop entirely.
        record.eta = pixelSeeds.eta()[seedIdx];
        float const absEta = alpaka::math::abs(acc, record.eta);
        record.attachThr =
            (absEta < 1.1f) ? config.attachTheta : ((absEta < 1.7f) ? config.attachThetaT : config.attachThetaE);
        record.xcThr = (absEta < 1.1f) ? config.xcTheta : ((absEta < 1.7f) ? config.xcThetaT : config.xcThetaE);
        // Region-conditioned relaxation of the cross-clean bar. The duplicate seeds this bar exists
        // to retire sit almost entirely at |eta| >= 1.1 and low pt, so relaxing it globally spends
        // efficiency in the barrel and at high pt where it retires nothing. The relaxation is a
        // continuous linear ramp in pt -- full at pt -> 0, zero at the ceiling -- so the rule has no
        // pt cliff, and the |eta| boundary is one xcThr already switches on, so it adds no new
        // discontinuity. Conditioning is on the seed's own |eta| and pt and on nothing else.
        // dupXcDelta <= 0 disables it, leaving the barrel and the pt >= dupXcPtMax population
        // untouched by construction.
        if (config.dupXcDelta > 0.f && absEta >= 1.1f && pt < config.dupXcPtMax) {
          float const ramp = 1.f - pt / config.dupXcPtMax;
          record.xcThr -= config.dupXcDelta * ramp;
        }
        record.isQuad = pixelSeeds.isQuad()[seedIdx] ? 1u : 0u;
        record.centreX = pixelSegments.circleCenterX()[seedIdx];
        record.centreY = pixelSegments.circleCenterY()[seedIdx];
        record.circleRadius = circleRadius;
        record.centreDist = alpaka::math::sqrt(acc, record.centreX * record.centreX + record.centreY * record.centreY);
        record.innerHitZ = pixelSeeds.hit0Z()[seedIdx];
        float const hitX = pixelSeeds.hit0X()[seedIdx], hitY = pixelSeeds.hit0Y()[seedIdx];
        record.innerHitRt = alpaka::math::sqrt(acc, hitX * hitX + hitY * hitY);
        record.innerHitX = hitX;
        record.innerHitY = hitY;
        record.seedRow = seedIdx;
        record.phiMask = 0u;  // only the scattered copies carry a cell set
        outRecords[seedIdx] = record;
      }
    }
  };

  // The per-target record for a CHAIN target: its inner anchor, its chord direction, its rz slope
  // and its circle fit, plus the seven target-side head inputs preprocessed.
  //
  // Every accumulation is in double and the anchor coordinates are promoted before any
  // differencing, matching the fit convention used elsewhere in the chain pipeline. The cumulative
  // chord length is re-accumulated sequentially in each pass instead of being staged in an array:
  // same sequence of partial sums, no storage.
  struct ChainAttachTargetPre {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  MiniDoubletsConst miniDoublets,
                                  ChainItemsConst items,
                                  ChainsConst chains,
                                  uint32_t const* targets,
                                  uint32_t nTargets,
                                  AttachTargetPre* outRecords) const {
      for (uint32_t targetIdx : cms::alpakatools::uniform_elements(acc, nTargets)) {
        uint32_t const chainIdx = targets[targetIdx];
        AttachTargetPre record;
        record.chain = chainIdx;
        record.rtInner = 0.f;
        record.zInner = 0.f;
        record.chordPhi = 0.f;
        record.tanLambda = 0.f;
        record.centerX = 0.f;
        record.centerY = 0.f;
        record.centerValid = 0u;

        uint32_t const mdBase = 3u * chains.nodeOffset()[chainIdx];
        int const nMiniDoublets = chains.nMDs()[chainIdx];

        if (nMiniDoublets >= 1) {
          uint32_t const mdInner = items.mdItems()[mdBase];
          double const innerAnchorX = miniDoublets.anchorX()[mdInner], innerAnchorY = miniDoublets.anchorY()[mdInner];
          record.rtInner =
              static_cast<float>(alpaka::math::sqrt(acc, innerAnchorX * innerAnchorX + innerAnchorY * innerAnchorY));
          record.zInner = miniDoublets.anchorZ()[mdInner];
          if (nMiniDoublets >= 2) {
            uint32_t const mdSecond = items.mdItems()[mdBase + 1];
            record.chordPhi = static_cast<float>(
                alpaka::math::atan2(acc,
                                    static_cast<double>(miniDoublets.anchorY()[mdSecond]) - innerAnchorY,
                                    static_cast<double>(miniDoublets.anchorX()[mdSecond]) - innerAnchorX));
          }

          // rz straight-line fit, slope only: the target tanLambda.
          {
            double pathMean = 0.0, zMean = 0.0;
            double pathLength = 0.0, prevX = innerAnchorX, prevY = innerAnchorY;
            for (int k = 0; k < nMiniDoublets; ++k) {
              if (k > 0) {
                uint32_t const mdIdx = items.mdItems()[mdBase + k];
                double const deltaX = static_cast<double>(miniDoublets.anchorX()[mdIdx]) - prevX;
                double const deltaY = static_cast<double>(miniDoublets.anchorY()[mdIdx]) - prevY;
                pathLength += alpaka::math::sqrt(acc, deltaX * deltaX + deltaY * deltaY);
                prevX = miniDoublets.anchorX()[mdIdx];
                prevY = miniDoublets.anchorY()[mdIdx];
              }
              pathMean += pathLength;
              zMean += miniDoublets.anchorZ()[items.mdItems()[mdBase + k]];
            }
            pathMean /= nMiniDoublets;
            zMean /= nMiniDoublets;
            double sumPathSq = 0.0, sumPathZ = 0.0;
            pathLength = 0.0;
            prevX = innerAnchorX;
            prevY = innerAnchorY;
            for (int k = 0; k < nMiniDoublets; ++k) {
              if (k > 0) {
                uint32_t const mdIdx = items.mdItems()[mdBase + k];
                double const deltaX = static_cast<double>(miniDoublets.anchorX()[mdIdx]) - prevX;
                double const deltaY = static_cast<double>(miniDoublets.anchorY()[mdIdx]) - prevY;
                pathLength += alpaka::math::sqrt(acc, deltaX * deltaX + deltaY * deltaY);
                prevX = miniDoublets.anchorX()[mdIdx];
                prevY = miniDoublets.anchorY()[mdIdx];
              }
              double const pathResidual = pathLength - pathMean;
              sumPathSq += pathResidual * pathResidual;
              sumPathZ += pathResidual * (miniDoublets.anchorZ()[items.mdItems()[mdBase + k]] - zMean);
            }
            if (sumPathSq > 1e-12)
              record.tanLambda = static_cast<float>(sumPathZ / sumPathSq);
          }

          // Kasa circle centre, feeding the centre-distance feature (head input 18). Same algorithm
          // as the chain feature builder, without the radius.
          if (nMiniDoublets >= 3) {
            double xMean = 0.0, yMean = 0.0;
            for (int k = 0; k < nMiniDoublets; ++k) {
              xMean += miniDoublets.anchorX()[items.mdItems()[mdBase + k]];
              yMean += miniDoublets.anchorY()[items.mdItems()[mdBase + k]];
            }
            xMean /= nMiniDoublets;
            yMean /= nMiniDoublets;
            double sumUU = 0.0, sumVV = 0.0, sumUV = 0.0, sumUW = 0.0, sumVW = 0.0;
            for (int k = 0; k < nMiniDoublets; ++k) {
              double const uOffset = miniDoublets.anchorX()[items.mdItems()[mdBase + k]] - xMean;
              double const vOffset = miniDoublets.anchorY()[items.mdItems()[mdBase + k]] - yMean;
              double const uvSquared = uOffset * uOffset + vOffset * vOffset;
              sumUU += uOffset * uOffset;
              sumVV += vOffset * vOffset;
              sumUV += uOffset * vOffset;
              sumUW += uOffset * uvSquared;
              sumVW += vOffset * uvSquared;
            }
            double const determinant = sumUU * sumVV - sumUV * sumUV;
            double const scale = sumUU + sumVV;
            if (determinant > 1e-12 * scale * scale) {
              double const centreOffsetU = (sumVV * (0.5 * sumUW) - sumUV * (0.5 * sumVW)) / determinant;
              double const centreOffsetV = (sumUU * (0.5 * sumVW) - sumUV * (0.5 * sumUW)) / determinant;
              record.centerX = static_cast<float>(xMean + centreOffsetU);
              record.centerY = static_cast<float>(yMean + centreOffsetV);
              record.centerValid = 1u;
            }
          }
        }

        record.fitKappa = chains.features()[chainIdx][7];
        record.rotSign = (record.fitKappa >= 0.f) ? 1.f : -1.f;
        record.radius = chainAttachRadiusOf(acc, record.fitKappa);
        record.targetInputs[0] = attachStdz<7>(acc, record.fitKappa);
        record.targetInputs[1] = attachStdz<8>(acc, record.tanLambda);
        record.targetInputs[2] = attachStdz<9>(acc, chains.features()[chainIdx][10]);  // innermostLayer
        record.targetInputs[3] = attachStdz<10>(acc, chains.features()[chainIdx][1]);  // nLayers
        // The three RAW gate logits, already stored per chain for the gate kills and the ordering
        // key, so they cost no new chain-side arithmetic. All three go to the head separately and
        // nothing reduces them by hand: any combination (a max, say) discards WHICH class won, and
        // that is precisely the information a prompt-versus-displaced decision runs on.
        record.targetInputs[4] = attachStdz<11>(acc, chains.zFake()[chainIdx]);
        record.targetInputs[5] = attachStdz<12>(acc, chains.zPrompt()[chainIdx]);
        record.targetInputs[6] = attachStdz<13>(acc, chains.zDisp()[chainIdx]);
        outRecords[targetIdx] = record;
      }
    }
  };

  // The measured radial hull of the targets, per r bin (file header, point 2). Storing the raw bit
  // patterns of these non-negative floats makes the integer atomicMin / atomicMax an exact float
  // min / max.
  //
  // BOTH target sets -- stage-A chain targets and stage-B bare-triplet targets -- are reduced in
  // ONE launch into ONE hull, their union. That is what lets a single grid serve both stages: a
  // wider hull only LOOSENS the phi interval each seed occupies, a looser interval yields a
  // SUPERSET of candidates, and both scorers re-filter with the same exact predicate before any
  // observable write. `tgtB` may be null, in which case only set A contributes.
  struct ChainAttachGridBounds {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachTargetPre const* tgtA,
                                  uint32_t nTargetsA,
                                  AttachTargetPre const* tgtB,
                                  uint32_t nTargetsB,
                                  uint32_t* rMinBits,
                                  uint32_t* rMaxBits) const {
      for (uint32_t flatIdx : cms::alpakatools::uniform_elements(acc, nTargetsA + nTargetsB)) {
        float const rt = (flatIdx < nTargetsA) ? tgtA[flatIdx].rtInner : tgtB[flatIdx - nTargetsA].rtInner;
        int const rBin = attachRBin(rt);
        uint32_t const bits = std::bit_cast<uint32_t>(rt);
        // The read-first filter is exact -- atomicMin/Max are idempotent and a value that does not
        // improve the running extremum cannot change the final one -- and it keeps a few thousand
        // threads off the same three or four occupied bins.
        if (bits < rMinBits[rBin])
          alpaka::atomicMin(acc, &rMinBits[rBin], bits, alpaka::hierarchy::Threads{});
        if (bits > rMaxBits[rBin])
          alpaka::atomicMax(acc, &rMaxBits[rBin], bits, alpaka::hierarchy::Threads{});
      }
    }
  };

  // The per-cell occupancy count. It shares its mask buffer with the scatter below, so the two
  // passes cannot disagree about which cells a seed occupies.
  //
  // `nRadialSlices` is a LAUNCH SHAPE knob and nothing else. The natural element of both passes is a
  // (seed, r bin) PAIR rather than a seed: the r bins of one seed are independent -- each computes
  // its own phi-cell mask from that bin's target hull and writes its own masks[] slot -- so
  // nRadialSlices == kAttachRBins sizes the launch to nPls * kAttachRBins elements instead of nPls, which is
  // what fills a device. Every nRadialSlices partitions the same (seed, r bin) set exactly once, both passes
  // write only masks[seed * kAttachRBins + rBin] (one owner per pair) and accumulate into counts[] /
  // cursor[] atomically, so the result is independent of the partition. nRadialSlices == 1 is the
  // serial-over-r form and is what the host backends use, one thread per seed.
  //
  // The gain is small: the scatter is WRITE-BANDWIDTH bound, not latency bound -- it copies a whole
  // 96-byte AttachPlsPre per grid entry to a scattered address -- so parallelism was never the
  // binding constraint here.
  struct ChainAttachGridCount {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachPlsPre const* seeds,
                                  uint32_t nPls,
                                  uint32_t const* rMinBits,
                                  uint32_t const* rMaxBits,
                                  uint16_t* masks,
                                  uint32_t* counts,
                                  uint32_t nRadialSlices,
                                  ChainConfig config) const {
      uint32_t const rStride = (nRadialSlices > 0u) ? nRadialSlices : 1u;
      for (uint32_t flatIdx : cms::alpakatools::uniform_elements(acc, nPls * rStride)) {
        uint32_t const seedIdx = (rStride == 1u) ? flatIdx : flatIdx / rStride;
        int const rBinStart = (rStride == 1u) ? 0 : static_cast<int>(flatIdx - seedIdx * rStride);
        int const tanLambdaBin = attachTanLBin(seeds[seedIdx].tanLambda, config.attachPrefDTanL);
        for (int rBin = rBinStart; rBin < kAttachRBins; rBin += static_cast<int>(rStride)) {
          uint32_t const loBits = rMinBits[rBin], hiBits = rMaxBits[rBin];
          if (loBits == 0xFFFFFFFFu)
            continue;  // no target landed in this bin
          float const rtMin = std::bit_cast<float>(loBits);
          float const rtMax = std::bit_cast<float>(hiBits);
          uint32_t const mask = attachPhiCellMask(acc, seeds[seedIdx], rtMin, rtMax, kAttachPhiPad);
          masks[seedIdx * kAttachRBins + rBin] = static_cast<uint16_t>(mask);
          for (int phiBin = 0; phiBin < kAttachPhiBins; ++phiBin)
            if (mask & (1u << phiBin))
              alpaka::atomicAdd(
                  acc, &counts[attachCellId(rBin, tanLambdaBin, phiBin)], 1u, alpaka::hierarchy::Threads{});
        }
      }
    }
  };

  // The scatter: one copy of the seed record into every cell it occupies. Reads the masks the count
  // pass wrote, so the entry count per cell matches the offsets exactly.
  struct ChainAttachGridScatter {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachPlsPre const* seeds,
                                  uint32_t nPls,
                                  uint16_t const* masks,
                                  uint32_t const* offsets,
                                  uint32_t* cursor,
                                  AttachPlsPre* itemsOut,
                                  uint32_t nRadialSlices,
                                  ChainConfig config) const {
      uint32_t const rStride = (nRadialSlices > 0u) ? nRadialSlices : 1u;
      for (uint32_t flatIdx : cms::alpakatools::uniform_elements(acc, nPls * rStride)) {
        uint32_t const seedIdx = (rStride == 1u) ? flatIdx : flatIdx / rStride;
        int const rBinStart = (rStride == 1u) ? 0 : static_cast<int>(flatIdx - seedIdx * rStride);
        int const tanLambdaBin = attachTanLBin(seeds[seedIdx].tanLambda, config.attachPrefDTanL);
        for (int rBin = rBinStart; rBin < kAttachRBins; rBin += static_cast<int>(rStride)) {
          uint32_t const mask = masks[seedIdx * kAttachRBins + rBin];
          if (mask == 0u)
            continue;
          for (int phiBin = 0; phiBin < kAttachPhiBins; ++phiBin) {
            if (!(mask & (1u << phiBin)))
              continue;
            uint32_t const cell = attachCellId(rBin, tanLambdaBin, phiBin);
            uint32_t const slot = alpaka::atomicAdd(acc, &cursor[cell], 1u, alpaka::hierarchy::Threads{});
            AttachPlsPre& gridItem = itemsOut[offsets[cell] + slot];
            gridItem = seeds[seedIdx];
            // The whole phi-cell set this seed occupies for this r bin travels with the copy:
            // ChainAttachScore uses it to keep only the FIRST occurrence of the seed in a target's
            // cell walk.
            gridItem.phiMask = static_cast<uint16_t>(mask);
          }
        }
      }
    }
  };

  // The pair features, delivered ALREADY PREPROCESSED (see attachStdz). Returns false when the pair
  // fails the two analytic windows -- the exact predicate the grid returns a superset of, so this is
  // where every candidate the walk offers is accepted or rejected. `dTanL` is passed in because the
  // caller has already computed it for the cheap early exit.
  //
  // The outputs are written to featuresOut[i * featureStride], so the caller can stage a batch of pairs
  // TRANSPOSED (input-major) without a second pass.
  static_assert(dnn::attachmlp::kInput == kAttachFeatures,
                "AttachNetworkWeights.h input size does not match the pair-feature layout");
  template <typename TAcc>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool attachEvalPairX(TAcc const& acc,
                                                      AttachPlsPre const& seed,
                                                      AttachTargetPre const& target,
                                                      float dTanL,
                                                      ChainConfig const& config,
                                                      float* featuresOut,
                                                      int featureStride) {
    float const absDTanL = alpaka::math::abs(acc, dTanL);
    if (absDTanL >= config.attachPrefDTanL)
      return false;
    float const dPhi =
        cms::alpakatools::reducePhiRange(acc, attachPhiDirAt(acc, seed, target.rtInner) - target.chordPhi);
    if (!(alpaka::math::abs(acc, dPhi) < config.attachPrefDPhi))
      return false;

    float const chargeAgree = (seed.rotSign == target.rotSign) ? 1.f : 0.f;
    float const dKappa = seed.kappaSigned - target.fitKappa;
    float centerDist = 0.f;
    if (target.centerValid) {
      float const deltaCentreX = seed.centreX - target.centerX, deltaCentreY = seed.centreY - target.centerY;
      centerDist = alpaka::math::sqrt(acc, deltaCentreX * deltaCentreX + deltaCentreY * deltaCentreY);
    }
    float const zResid = seed.innerHitZ + seed.tanLambda * (target.rtInner - seed.innerHitRt) - target.zInner;

    // 0..6 per-seed and 7..13 per-target: hoisted into the pre-records, copied here.
    CMS_UNROLL_LOOP
    for (int i = 0; i < 7; ++i)
      featuresOut[i * featureStride] = seed.seedInputs[i];
    CMS_UNROLL_LOOP
    for (int i = 0; i < 7; ++i)
      featuresOut[(7 + i) * featureStride] = target.targetInputs[i];
    featuresOut[14 * featureStride] = attachStdz<14>(acc, chargeAgree);
    featuresOut[15 * featureStride] = attachStdz<15>(acc, dKappa);
    featuresOut[16 * featureStride] = attachStdz<16>(acc, dTanL);
    featuresOut[17 * featureStride] = attachStdz<17>(acc, dPhi);
    featuresOut[18 * featureStride] = attachStdz<18>(acc, centerDist);
    featuresOut[19 * featureStride] = attachStdz<19>(acc, zResid);
    // 20 targetType: 0 is the chain kind. ChainAttachT3.h overwrites this one input, and only this
    // one, for a bare-triplet target.
    featuresOut[20 * featureStride] = attachStdz<20>(acc, 0.f);
    // 21 rphiResidInwards: the seed's innermost anchor hit's signed residual to the TARGET's own
    // circle -- the content of LST's rPhiChiSquaredInwards, at one sqrt per pair. An invalid target
    // circle keeps the 0 flag value, exactly as the centre-distance input above does.
    float residInw = 0.f;
    if (target.centerValid) {
      float const hitToCentreX = seed.innerHitX - target.centerX;
      float const hitToCentreY = seed.innerHitY - target.centerY;
      residInw = alpaka::math::sqrt(acc, hitToCentreX * hitToCentreX + hitToCentreY * hitToCentreY) - target.radius;
    }
    featuresOut[21 * featureStride] = attachStdz<21>(acc, residInw);
    return true;
  }

  // The attach head's linear layer, unbatched. BIT-EXACTLY the shared NeuralNetwork.h linear_layer
  // -- each output accumulates the same products in the same j order -- but with the loops
  // interchanged so the vector unit runs across the OUTPUT index and the weight reads are
  // contiguous. The shared template is left untouched so no other network's code generation moves.
  // This is the form the device path uses.
  template <int IN_FEATURES, int OUT_FEATURES>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void attachLinear(float const (&input)[IN_FEATURES],
                                                   float (&output)[OUT_FEATURES],
                                                   float const (&weights)[IN_FEATURES][OUT_FEATURES],
                                                   float const (&biases)[OUT_FEATURES]) {
    CMS_UNROLL_LOOP
    for (int i = 0; i < OUT_FEATURES; ++i)
      output[i] = biases[i];
    for (int j = 0; j < IN_FEATURES; ++j) {
      float const inValue = input[j];
      CMS_UNROLL_LOOP
      for (int i = 0; i < OUT_FEATURES; ++i)
        output[i] += inValue * weights[j][i];
    }
  }

  // The attach head over a BATCH of kBatch pairs, on the shared batched primitives of ChainEdges.h
  // (which carry the bit-identity argument). kBatch == 1 is the unbatched form above and is what the
  // device path instantiates.
  template <int kBatch>
  ALPAKA_FN_ACC ALPAKA_FN_INLINE void attachHeadBatch(float const (&inputsTransposed)[dnn::attachmlp::kInput * kBatch],
                                                      float (&logits)[kBatch]) {
    constexpr int kInputs = dnn::attachmlp::kInput;
    constexpr int kHiddenUnits = dnn::attachmlp::kHidden;
    alignas(64) float hidden1[kHiddenUnits * kBatch];
    alignas(64) float hidden2[kHiddenUnits * kBatch];
    if constexpr (kBatch == 1) {
      attachLinear<kInputs, kHiddenUnits>(inputsTransposed, hidden1, dnn::attachmlp::wgt_l1, dnn::attachmlp::bias_l1);
      relu_activation<kHiddenUnits>(hidden1);
      attachLinear<kHiddenUnits, kHiddenUnits>(hidden1, hidden2, dnn::attachmlp::wgt_l2, dnn::attachmlp::bias_l2);
      relu_activation<kHiddenUnits>(hidden2);
      float logit = dnn::attachmlp::bias_out;
      for (int j = 0; j < kHiddenUnits; ++j)
        logit += hidden2[j] * dnn::attachmlp::wgt_out[j];
      logits[0] = logit;
    } else {
      chainLinearBatch<kInputs, kHiddenUnits, kBatch>(
          inputsTransposed, hidden1, dnn::attachmlp::wgt_l1, dnn::attachmlp::bias_l1);
      chainReluBatch<kHiddenUnits, kBatch>(hidden1);
      chainLinearBatch<kHiddenUnits, kHiddenUnits, kBatch>(
          hidden1, hidden2, dnn::attachmlp::wgt_l2, dnn::attachmlp::bias_l2);
      chainReluBatch<kHiddenUnits, kBatch>(hidden2);
      chainDotBatch<kHiddenUnits, kBatch>(hidden2, logits, dnn::attachmlp::wgt_out, dnn::attachmlp::bias_out);
    }
  }

  // The packed (logit, earlier position) argmax key of the attach contention, shared by both attach
  // stages. Ordering the packed key is exactly "higher logit wins; on an exact tie the earlier
  // position wins", so a contention can be resolved by a plain atomicMax with no ordering imposed
  // on the threads. -0.0f and +0.0f compare equal as floats but have different order keys, so the
  // sign of zero is canonicalised first: one instruction, and the question never arises.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint64_t attachContendKey(float logit, uint32_t position) {
    float const canon = (logit == 0.f) ? 0.f : logit;
    return (static_cast<uint64_t>(chainOrderFloat(canon)) << 32) | static_cast<uint64_t>(0xFFFFFFFFu - position);
  }

  // The candidate walk: for each target, gather its grid candidates, evaluate the exact predicate,
  // score the survivors through the attach head and keep the best. The auxiliary 4-layer targets
  // ride in the same launch; see the is4L comment in the target loop for what they may write.
  //
  // The per-target pick is the maximum logit, with the LOWEST seed row winning a tie. The grid
  // returns candidates in an arbitrary order, so that rule is spelled out rather than left to the
  // iteration order.
  //
  // plsBest is the per-seed maximum over every SCORED pair, before any threshold: it is the input
  // to the carried-row retirement predicate downstream. Being a max, the atomic is
  // order-independent and the result is the same on every backend.
  //
  // Two shape parameters make this kernel fit its backend without changing what it computes: kBatch
  // batches pairs through the head (host only) and nSlices splits one target's walk over several
  // threads (device only). Both are argued at their use sites below.
  struct ChainAttachScore {
    template <typename TAcc>
    ALPAKA_FN_ACC void operator()(TAcc const& acc,
                                  AttachPlsPre const* seeds,
                                  AttachTargetPre const* targets,
                                  uint32_t nTargets,
                                  uint32_t nTgtAll,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  uint64_t* tgtKey,
                                  uint64_t* tgtKeyPre,
                                  uint32_t* plsBest,
                                  ChainXcPair* xcPairs,
                                  uint32_t* xcCursor,
                                  uint32_t xcCap,
                                  uint32_t* stats,
                                  // MEASUREMENT ONLY, all four inert when pairRows == nullptr, which
                                  // is the case unless LST_CHAIN_PAIR_DUMP names a file (the same
                                  // discipline as the edge head's LST_CHAIN_FEAT_DUMP tap).
                                  ChainAttachPairRow* pairRows,
                                  uint32_t* pairCtl,
                                  uint32_t pairCap,
                                  uint32_t pairKeep,
                                  uint32_t nSlices,
                                  ChainConfig config) const {
      constexpr bool kHost = cms::alpakatools::requires_single_thread_per_block_v<TAcc>;
      constexpr int kBatch = kHost ? kAttachScoreBatch : 1;
      constexpr int kInputs = dnn::attachmlp::kInput;
      uint32_t const sliceCount = kHost ? 1u : ((nSlices > 0u) ? nSlices : 1u);

      alignas(64) float inputsTransposed[kInputs * kBatch];
      int32_t batchRow[kBatch];
      AttachPlsPre const* batchSeed[kBatch];
      float logits[kBatch];
      for (int i = 0; i < kInputs * kBatch; ++i)
        inputsTransposed[i] = 0.f;  // the tail lanes of a partial batch are evaluated and discarded

      // SLICING (device only). One thread per target is ~1080 threads at PU200: a few dozen warps,
      // no latency hiding, and each thread walks ~333 candidates and runs ~125 head evaluations
      // serially. So sliceCount threads share one target, the thread with index `slice` taking
      // items base+slice, base+slice+sliceCount, ... of every scanned cell. Every candidate is
      // still visited exactly once by exactly one thread -- nothing is re-walked -- so the census
      // is unchanged term by term and only the per-target argmax has to be reduced across slices.
      // The launch is therefore over (target, slice) pairs, with the target class still decided by
      // POSITION in the target list.
      for (uint32_t flatIdx : cms::alpakatools::uniform_elements(acc, nTgtAll * sliceCount)) {
        uint32_t targetIdx, slice;
        if constexpr (kHost) {
          targetIdx = flatIdx;
          slice = 0u;
        } else {
          targetIdx = flatIdx / sliceCount;
          slice = flatIdx - targetIdx * sliceCount;
        }
        // Positions [nTargets, nTgtAll) are the auxiliary 4-LAYER target list, which exists only to
        // feed the cross-clean pass-1 append: it writes no plsBest, no tgtKey and no census, so the
        // grants and the delivery are bit-identical with and without it. The two classes are
        // disjoint and contiguous by construction -- ChainTargetFlags keeps nLayers at or above the
        // attach floor, ChainAttachSelectAux keeps nLayers == 4 -- so position IS class and no chain
        // row has to be re-read to tell them apart. They ride in THIS launch rather than a second
        // one because the append reads nothing the contention stage produces.
        bool const is4L = (targetIdx >= nTargets);
        AttachTargetPre const target = targets[targetIdx];
        int32_t bestPls = -1;
        float bestLogit = kAttachNoLogit;
        // MUTUAL-BEST RETIREMENT. The PRE-THRESHOLD argmax of the same walk: the seed this target
        // likes best whether or not the pair reaches the delivery margin. Retiring a carried pixel
        // row is a strictly weaker claim than delivering a track ("this seed's track is already
        // this chain's track"), which is why the evidence for it may live below that margin; the
        // mutual test in ChainAttachUnpackBest is what keeps it safe. Inert unless
        // config.dupMutualDelta >= 0.
        int32_t bestPrePls = -1;
        float bestPreLogit = kAttachNoLogit;
        float bestPreThr = 0.f;
        uint32_t nCand = 0, nScored = 0, nDup = 0;
        int nStaged = 0;

        // BATCHING (host backends only, kBatch > 1). The head is a narrow MLP with a serial dependency
        // down each unit's accumulation, so a single pair leaves the vector unit mostly idle;
        // survivors are staged transposed into inputsTransposed and evaluated kBatch at a time, which interleaves kBatch
        // independent accumulator chains without touching the per-pair operation order (see
        // attachHeadBatch). Retiring a batch reduces its pairs in staging order, which is immaterial
        // because both reductions below are order-independent maxima and the cross-clean append is
        // a set insertion resolved order-independently in pass 2.
        auto flush = [&]() {
          attachHeadBatch<kBatch>(inputsTransposed, logits);
          for (int batchIdx = 0; batchIdx < nStaged; ++batchIdx) {
            float const logit = logits[batchIdx];
            int32_t const seedIdx = batchRow[batchIdx];
            AttachPlsPre const& seed = *batchSeed[batchIdx];
            // MEASUREMENT ONLY: the on-policy pair row. Emitted BEFORE any verdict so the row set
            // is the SCORED stream, not the delivered one, and with no dependence on the logit
            // beyond copying it out.
            if (pairRows != nullptr && attachPairKeep(target.chain, static_cast<uint32_t>(seedIdx), pairKeep)) {
              uint32_t const slot = alpaka::atomicAdd(acc, pairCtl, 1u, alpaka::hierarchy::Threads{});
              if (slot < pairCap) {
                ChainAttachPairRow& row = pairRows[slot];
                row.stage = is4L ? 2u : 0u;
                row.target = target.chain;
                row.pls = static_cast<uint32_t>(seedIdx);
                row.logit = logit;
                for (int i = 0; i < kInputs; ++i)
                  row.x[i] = inputsTransposed[i * kBatch + batchIdx];
              } else {
                alpaka::atomicAdd(acc, pairCtl + 1u, 1u, alpaka::hierarchy::Threads{});
              }
            }
            if (!is4L)
              alpaka::atomicMax(
                  acc, &plsBest[static_cast<uint32_t>(seedIdx)], chainOrderFloat(logit), alpaka::hierarchy::Threads{});
            // Bare-chain cross-clean, pass 1: the filtered (chain, seed) compaction of the
            // scored-pair stream, thresholded on the |seed eta|-banded xcTheta and on NO geometric
            // window. A dR window would have to be taken against the chain's overall direction,
            // which for a chain spanning several layers is not where the seed's helix points, and
            // it vetoes pairs the head has already scored as matches. Ordering: BEFORE the delivery
            // threshold, so the cross-clean sees sub-margin pairs too (xcThr sits below attachThr).
            if (xcPairs != nullptr && seed.isQuad != 0u && logit >= seed.xcThr) {
              uint32_t const slot = alpaka::atomicAdd(acc, xcCursor, 1u, alpaka::hierarchy::Threads{});
              if (slot < xcCap) {
                xcPairs[slot].chain = target.chain;
                xcPairs[slot].plsRow = static_cast<uint32_t>(seedIdx);
              } else {
                alpaka::atomicAdd(acc, &stats[11], 1u, alpaka::hierarchy::Threads{});  // overflow census
              }
            }
            if (!is4L && config.dupMutualDelta >= 0.f &&
                (bestPrePls < 0 || logit > bestPreLogit || (logit == bestPreLogit && seedIdx < bestPrePls))) {
              bestPrePls = seedIdx;
              bestPreLogit = logit;
              bestPreThr = seed.attachThr;  // this pair's OWN banded margin, for the floor below
            }
            if (logit < seed.attachThr)
              continue;  // the eta-banded delivery margin
            if (bestPls < 0 || logit > bestLogit || (logit == bestLogit && seedIdx < bestPls)) {
              bestPls = seedIdx;
              bestLogit = logit;
            }
          }
          nScored += static_cast<uint32_t>(nStaged);
          nStaged = 0;
        };

        int const rBin = attachRBin(target.rtInner);
        int const tanLambdaBinLo = attachTanLBin(target.tanLambda - config.attachPrefDTanL, config.attachPrefDTanL);
        int const tanLambdaBinHi = attachTanLBin(target.tanLambda + config.attachPrefDTanL, config.attachPrefDTanL);
        int const phiBinLo = attachPhiBin(target.chordPhi - config.attachPrefDPhi);
        int const phiBinHi = attachPhiBin(target.chordPhi + config.attachPrefDPhi);
        int nPhiBinsScanned = phiBinHi - phiBinLo;
        if (nPhiBinsScanned < 0)
          nPhiBinsScanned += kAttachPhiBins;
        ++nPhiBinsScanned;
        if (nPhiBinsScanned > kAttachPhiBins)
          nPhiBinsScanned = kAttachPhiBins;

        for (int tanLambdaBin = tanLambdaBinLo; tanLambdaBin <= tanLambdaBinHi; ++tanLambdaBin) {
          for (int phiStep = 0; phiStep < nPhiBinsScanned; ++phiStep) {
            int const phiBin = (phiBinLo + phiStep) % kAttachPhiBins;
            // Bits of the seed mask below the current walk position, i.e. the cells of THIS scan
            // that were visited earlier and would already have supplied the same seed.
            uint32_t const earlier = (1u << phiStep) - 1u;
            uint32_t const cell = attachCellId(rBin, tanLambdaBin, phiBin);
            uint32_t const cellBegin = offsets[cell], cellEnd = offsets[cell + 1u];
            for (uint32_t itemIdx = cellBegin + slice; itemIdx < cellEnd; itemIdx += sliceCount) {
              AttachPlsPre const& seed = items[itemIdx];
              uint32_t const seedIdx = seed.seedRow;
              ++nCand;
              // DUPLICATE SUPPRESSION. A seed occupies every phi cell its direction range touches,
              // so a target scanning nPhiBinsScanned cells meets the same seed up to nPhiBinsScanned times. Re-scoring it
              // cannot change anything -- same logit, a max reduction and an argmax under a strict
              // total order -- so only its FIRST occurrence in the walk is kept. The test is exact
              // and assumes no geometry: the item copy carries the phi-cell MASK it was scattered
              // under, so the walk position of the seed's lowest scanned cell is read straight off
              // that mask. The r bin and the tanLambda bin are single-valued over one scan, so phi
              // is the only axis that can repeat. Running the test BEFORE the predicate is what
              // makes a duplicate cost neither an atan2 nor a head evaluation.
              uint32_t const seedMask = seed.phiMask;
              uint32_t const rotatedMask =
                  ((seedMask >> phiBinLo) | (seedMask << (kAttachPhiBins - phiBinLo))) & (kAttachPhiCellMask);
              if ((rotatedMask & earlier) != 0u) {
                ++nDup;
                continue;
              }
              float const dTanL = seed.tanLambda - target.tanLambda;
              if (!attachEvalPairX(acc, seed, target, dTanL, config, inputsTransposed + nStaged, kBatch))
                continue;
              batchRow[nStaged] = static_cast<int32_t>(seedIdx);
              batchSeed[nStaged] = &seed;
              ++nStaged;
              if (nStaged == kBatch)
                flush();
            }
          }
        }
        if (nStaged > 0)
          flush();

        if (is4L)
          continue;  // score-only: no delivery verdict, and out of the census
        // One packed-key atomicMax per slice retires that slice's local argmax; since the key
        // encodes the full tie-break order, the winner is the same as the serial walk's. Blocks{}
        // hierarchy: slices of one target can land in different blocks.
        if (bestPls >= 0)
          alpaka::atomicMax(acc,
                            &tgtKey[targetIdx],
                            attachContendKey(bestLogit, static_cast<uint32_t>(bestPls)),
                            alpaka::hierarchy::Blocks{});
        // The same packed-key reduction for the pre-threshold pick, with the FLOOR applied to each
        // slice's own local winner: a slice whose local best is below its floor writes nothing, and
        // the global argmax (at or above every local best) still wins the atomicMax if it passed.
        if (tgtKeyPre != nullptr && config.dupMutualDelta >= 0.f && bestPrePls >= 0 &&
            bestPreLogit >= bestPreThr - config.dupMutualDelta)
          alpaka::atomicMax(acc,
                            &tgtKeyPre[targetIdx],
                            attachContendKey(bestPreLogit, static_cast<uint32_t>(bestPrePls)),
                            alpaka::hierarchy::Blocks{});
        alpaka::atomicAdd(acc, &stats[1], nCand, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[2], nScored, alpaka::hierarchy::Threads{});
        alpaka::atomicAdd(acc, &stats[10], nDup, alpaka::hierarchy::Threads{});
      }
    }
  };

  // The per-target winner, read back out of the packed argmax key the slices of the scorer reduce
  // into. chainUnorderFloat is the exact inverse of chainOrderFloat, so tgtLogit is bit for bit the
  // logit the head produced -- which matters, because ChainAttachResolve re-forms the key from it
  // and compares for equality.
  //
  // Key 0 means "no pair reached the delivery margin": chainOrderFloat returns 0 only for the bit
  // pattern 0xFFFFFFFF, a negative NaN, which no logit can be.
  //
  // It also owns the two per-TARGET censuses a sliced scorer cannot count itself (each slice would
  // count its own share): stats[3], targets that made a pick, and, when tgtScored is given,
  // stats[8], targets that scored at least one pair. Stage A passes nullptr for the latter.
  struct ChainAttachUnpackBest {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint64_t const* tgtKey,
                                  uint32_t const* tgtScored,
                                  int32_t* tgtPls,
                                  float* tgtLogit,
                                  uint32_t nTargets,
                                  uint32_t* stats,
                                  uint64_t const* tgtKeyPre = nullptr,
                                  uint32_t const* plsBest = nullptr,
                                  uint8_t* plsMutual = nullptr,
                                  uint32_t nPls = 0u) const {
      for (uint32_t targetIdx : cms::alpakatools::uniform_elements(acc, nTargets)) {
        // MUTUAL BEST, decided here because both halves are final: tgtKeyPre[targetIdx] is THIS
        // target's pre-threshold argmax pair, and plsBest[seedIdx] is the highest logit any target
        // scored for that seed, both in the same chainOrderFloat encoding, so the equality is an
        // exact integer comparison. A mutual pair says "each is the other's best", which no
        // threshold can express -- and a threshold is what lets in a seed whose own track lies
        // somewhere else entirely.
        if (plsMutual != nullptr && tgtKeyPre != nullptr && plsBest != nullptr) {
          uint64_t const preKey = tgtKeyPre[targetIdx];
          if (preKey != 0u) {
            uint32_t const seedIdx = 0xFFFFFFFFu - static_cast<uint32_t>(preKey & 0xFFFFFFFFu);
            uint32_t const orderedLogit = static_cast<uint32_t>(preKey >> 32);
            if (seedIdx < nPls && plsBest[seedIdx] == orderedLogit)
              plsMutual[seedIdx] = 1u;
          }
        }
        uint64_t const winnerKey = tgtKey[targetIdx];
        if (winnerKey == 0u) {
          tgtPls[targetIdx] = -1;
          tgtLogit[targetIdx] = kAttachNoLogit;
        } else {
          tgtPls[targetIdx] = static_cast<int32_t>(0xFFFFFFFFu - static_cast<uint32_t>(winnerKey & 0xFFFFFFFFu));
          tgtLogit[targetIdx] = chainUnorderFloat(static_cast<uint32_t>(winnerKey >> 32));
          alpaka::atomicAdd(acc, &stats[3], 1u, alpaka::hierarchy::Threads{});
        }
        if (tgtScored != nullptr && tgtScored[targetIdx] > 0u)
          alpaka::atomicAdd(acc, &stats[8], 1u, alpaka::hierarchy::Threads{});
      }
    }
  };

  // The auxiliary 4-layer target list: accepted chains with nLayers == 4, appended AFTER the
  // stage-A targets in the combined array. Serial because it appends in accepted order and the
  // append position must not race the stage-A count.
  struct ChainAttachSelectAux {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t const* nTargets5,
                                  uint32_t* targets,
                                  uint32_t* nTargetsAllOut,
                                  ChainConfig config) const {
      if (!cms::alpakatools::once_per_grid(acc))
        return;
      uint32_t const nAcc = chains.nAccepted();
      uint32_t nOut = *nTargets5;
      // When the attach floor is at or below 4, the 4-layer chains that clear the stage-A dcaXY
      // gate are ALREADY real targets in targets[0, nTargets5). Appending them again would score
      // them twice and give one chain two positions in the contention, so the auxiliary list keeps
      // exactly the 4-layer chains stage A did NOT take.
      bool const auxAll = chainAttachMinLayers(config) > 4;
      for (uint32_t acceptedIdx = 0; acceptedIdx < nAcc; ++acceptedIdx) {
        uint32_t const chainIdx = accepted[acceptedIdx];
        if (chains.nLayers()[chainIdx] != 4)
          continue;
        if (!auxAll && !(chains.dcaXY()[chainIdx] >= config.attachDcaMax))
          continue;
        targets[nOut++] = chainIdx;
      }
      *nTargetsAllOut = nOut;
    }
  };

  // The offline verification of the grid's superset property, compiled in but inert unless
  // LSTEvent::attachGridAudit is switched on by LST_CHAIN_ATTACH_AUDIT.
  //
  // It replays the EXHAUSTIVE scan -- every (target, seed) pair through the two windows -- and, for
  // every pair the scan accepts, searches that target's grid candidate list for the seed.
  // audit[3] must be 0 on every event before the grid may be trusted; audit[1] is the grid's probe
  // count, to be read against nTargets * nPls.
  //   audit[0] pairs accepted by the exhaustive scan
  //   audit[1] candidates the grid returns (including the duplicates a multi-cell scan produces)
  //   audit[2] grid candidates that pass the windows
  //   audit[3] scan-accepted pairs ABSENT from the grid   <-- must be zero
  struct ChainAttachAudit {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  AttachPlsPre const* seeds,
                                  uint32_t nPls,
                                  AttachTargetPre const* targets,
                                  uint32_t nTargets,
                                  uint32_t const* offsets,
                                  AttachPlsPre const* items,
                                  uint32_t* audit,
                                  ChainConfig config) const {
      for (uint32_t targetIdx : cms::alpakatools::uniform_elements(acc, nTargets)) {
        AttachTargetPre const target = targets[targetIdx];

        int const rBin = attachRBin(target.rtInner);
        int const tanLambdaBinLo = attachTanLBin(target.tanLambda - config.attachPrefDTanL, config.attachPrefDTanL);
        int const tanLambdaBinHi = attachTanLBin(target.tanLambda + config.attachPrefDTanL, config.attachPrefDTanL);
        int const phiBinLo = attachPhiBin(target.chordPhi - config.attachPrefDPhi);
        int const phiBinHi = attachPhiBin(target.chordPhi + config.attachPrefDPhi);
        int nPhiBinsScanned = phiBinHi - phiBinLo;
        if (nPhiBinsScanned < 0)
          nPhiBinsScanned += kAttachPhiBins;
        ++nPhiBinsScanned;
        if (nPhiBinsScanned > kAttachPhiBins)
          nPhiBinsScanned = kAttachPhiBins;

        uint32_t nCand = 0, nGridPass = 0;
        for (int tanLambdaBin = tanLambdaBinLo; tanLambdaBin <= tanLambdaBinHi; ++tanLambdaBin)
          for (int phiStep = 0; phiStep < nPhiBinsScanned; ++phiStep) {
            uint32_t const cell = attachCellId(rBin, tanLambdaBin, (phiBinLo + phiStep) % kAttachPhiBins);
            for (uint32_t itemIdx = offsets[cell]; itemIdx < offsets[cell + 1u]; ++itemIdx) {
              ++nCand;
              float features[kAttachFeatures];
              if (attachEvalPairX(
                      acc, items[itemIdx], target, items[itemIdx].tanLambda - target.tanLambda, config, features, 1))
                ++nGridPass;
            }
          }

        uint32_t nExact = 0, nMissing = 0;
        for (uint32_t seedIdx = 0; seedIdx < nPls; ++seedIdx) {
          float features[kAttachFeatures];
          if (!attachEvalPairX(
                  acc, seeds[seedIdx], target, seeds[seedIdx].tanLambda - target.tanLambda, config, features, 1))
            continue;
          ++nExact;
          bool found = false;
          for (int tanLambdaBin = tanLambdaBinLo; tanLambdaBin <= tanLambdaBinHi && !found; ++tanLambdaBin)
            for (int phiStep = 0; phiStep < nPhiBinsScanned && !found; ++phiStep) {
              uint32_t const cell = attachCellId(rBin, tanLambdaBin, (phiBinLo + phiStep) % kAttachPhiBins);
              for (uint32_t itemIdx = offsets[cell]; itemIdx < offsets[cell + 1u]; ++itemIdx)
                if (items[itemIdx].seedRow == seedIdx) {
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

  // Target selection, the attach contention, and the seed-family dedup.

  // keep[] for the stage-A target compaction: an accepted chain is a target if it is long enough
  // and IP-compatible. `nBound` may exceed the accepted count, so the tail is explicitly cleared.
  struct ChainTargetFlags {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* accepted,
                                  uint32_t nBound,
                                  uint32_t* keep,
                                  ChainConfig config) const {
      uint32_t const nAcc = chains.nAccepted();
      int const minLayers = chainAttachMinLayers(config);
      for (uint32_t acceptedIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        bool isTarget = false;
        if (acceptedIdx < nAcc) {
          uint32_t const chainIdx = accepted[acceptedIdx];
          isTarget = (chains.nLayers()[chainIdx] >= minLayers) && !(chains.dcaXY()[chainIdx] >= config.attachDcaMax);
        }
        keep[acceptedIdx] = isTarget ? 1u : 0u;
      }
    }
  };

  // The contention: a seed picked by several targets goes to the one with the strictly higher
  // logit, the earlier target position keeping an exact tie. That is an argmax over the positions
  // that picked the seed, and an argmax has no order, so it is a plain atomicMax over the packed
  // (logit, earlier position) key.
  struct ChainAttachArgmax {
    ALPAKA_FN_ACC void operator()(
        Acc1D const& acc, int32_t const* tgtPls, float const* tgtLogit, uint32_t nTargets, uint64_t* plsKey) const {
      for (uint32_t position : cms::alpakatools::uniform_elements(acc, nTargets)) {
        int32_t const seedIdx = tgtPls[position];
        if (seedIdx < 0)
          continue;
        alpaka::atomicMax(acc,
                          &plsKey[static_cast<uint32_t>(seedIdx)],
                          attachContendKey(tgtLogit[position], position),
                          alpaka::hierarchy::Blocks{});
      }
    }
  };

  // Both attach stages resolve their argmax with this kernel. `targets == nullptr` is stage B,
  // whose verdicts stay on the position arrays; stage A passes its target list and the winners are
  // published onto the chain rows as well, because its dedup pass and its delivery are chain-row
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
      for (uint32_t position : cms::alpakatools::uniform_elements(acc, nTargets)) {
        int32_t const seedIdx = tgtPls[position];
        if (seedIdx >= 0 && plsKey[static_cast<uint32_t>(seedIdx)] != attachContendKey(tgtLogit[position], position)) {
          tgtPls[position] = -1;
          tgtLogit[position] = kAttachNoLogit;
        }
        if (targets != nullptr) {
          uint32_t const chainIdx = targets[position];
          chains.attachPls()[chainIdx] = tgtPls[position];
          chains.attachLogit()[chainIdx] = tgtLogit[position];
        }
        keep[position] = (tgtPls[position] >= 0) ? 1u : 0u;
      }
    }
  };

  // The owner's DISTINCT pixel hit indices, staged so the dedup passes below touch a compact array
  // instead of chasing the hits SoA. Keyed by owner slot, not by seed row.
  //
  // Both stages stage their owners here. The owner list holds chain rows in stage A (whose grant
  // lives on the chain row, so `tgtPls == nullptr` means "read chains.attachPls()") and target
  // positions in stage B (`tgtPls` is its position-keyed grant array). Nothing else differs.
  //
  // NO DEFAULT ARGUMENTS, and every call site casts its nullptrs to the parameter type. Alpaka
  // deduces the kernel's argument PACK from the call, so a defaulted argument or a bare `nullptr`
  // makes the two stages instantiate the SAME kernel TWICE -- two device entry points for one
  // algorithm. Spelling every argument out collapses them to one.
  //
  // Both stages also build the dedup hash table here (`hashKey != nullptr`), concurrently, one
  // thread per owner. See ChainAttachSeedDedup for why building it for every owner up front --
  // rather than one kept owner at a time inside the serial walk -- leaves the same table.
  // `ownerTag` is the stage bias added to the owner index before it is stored
  // (chainattach::kSeedOwnerStageA / ...StageB), which is what turns "this entry belongs to an
  // already-decided owner" into a single comparison.
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
      uint32_t const nOwners = *nOwnersPtr;
      // Local copy: atomicCas takes its compare value by reference, and a constexpr at namespace
      // scope has no device-side storage to bind to.
      uint32_t const empty = chainattach::kSeedHashEmpty;
      for (uint32_t ownerIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (ownerIdx >= nOwners)
          continue;
        uint32_t const owner = owners[ownerIdx];
        int32_t const seedIdx = (tgtPls != nullptr) ? tgtPls[owner] : chains.attachPls()[owner];
        ownerPls[ownerIdx] = seedIdx;
        int nKeys = 0;
        if (seedIdx >= 0) {
          uint32_t const first = pixelSeeds.firstHit()[seedIdx];
          uint32_t const nSeedHits = static_cast<uint32_t>(pixelSeeds.nHits()[seedIdx]);
          uint32_t const nStored = nSeedHits < kMaxPLSHitsInHitsSoA ? nSeedHits : kMaxPLSHitsInHitsSoA;
          for (uint32_t k = 0; k < nStored; ++k) {
            uint32_t const hitIdx = first + k;
            if (hitIdx >= nHits)
              continue;
            if (hitsBase.detid()[hitIdx] != kPixelModuleId)
              continue;
            ownerHits[ownerIdx * kMaxPLSHitsInHitsSoA + nKeys] = hitsBase.idxs()[hitIdx];
            ++nKeys;
          }
        }
        ownerNHits[ownerIdx] = static_cast<uint8_t>(nKeys);

        if (hashKey == nullptr || seedIdx < 0)
          continue;
        // One entry per (owner, staged position), never folded, because the dup test downstream
        // COUNTS entries. A CAS that loses its slot moves on even when the resident key is our own,
        // which is what keeps the MULTISET of entries independent of the interleaving.
        for (int keyIdx = 0; keyIdx < nKeys; ++keyIdx) {
          uint32_t const hitKey = ownerHits[ownerIdx * kMaxPLSHitsInHitsSoA + keyIdx];
          uint32_t slot = attachSeedHash(hitKey);
          for (uint32_t probe = 0; probe < chainattach::kSeedHashMaxProbe; ++probe) {
            uint32_t const prev = alpaka::atomicCas(acc, &hashKey[slot], empty, hitKey, alpaka::hierarchy::Blocks{});
            if (prev == empty) {
              hashVal[slot] = seedIdx;
              hashOwner[slot] = ownerTag + ownerIdx;
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

  // How many entries of the dedup table, over ALL of one owner's staged keys, carry the owner tag
  // `wantOwner`. An entry of that owner sits under key k once per occurrence of k in its own staged
  // list, so the count is
  //   sum over our positions a of mult(keys[a], wantOwner's list)
  //     == sum over keys k of mult(k, us) * mult(k, wantOwner),
  // which is SYMMETRIC in the two owners. "Shares at least 2 pixel hit rows" is count >= 2, so the
  // partner relation is symmetric too -- which is what lets the prefilter below have each side of a
  // partner pair mark itself, with no thread writing another thread's flag.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE uint32_t attachSeedShared(
      uint32_t const* hashKey, uint32_t const* hashOwner, uint32_t const* keys, int nKeys, uint32_t wantOwner) {
    uint32_t count = 0;
    for (int keyIdx = 0; keyIdx < nKeys; ++keyIdx) {
      uint32_t slot = attachSeedHash(keys[keyIdx]);
      for (uint32_t entryKey = hashKey[slot]; entryKey != chainattach::kSeedHashEmpty; entryKey = hashKey[slot]) {
        if (entryKey == keys[keyIdx] && hashOwner[slot] == wantOwner)
          ++count;
        slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
      }
    }
    return count;
  }

  // The parallel prefilter of the seed-family dedup: one thread per owner, no order anywhere.
  //
  // Bit 0 (kSeedContPartner) is set iff SOME OTHER owner shares at least 2 pixel hit rows with this
  // one. The relation is symmetric (see attachSeedShared), so each side of a pair marks itself. The
  // serial greedy then only has to visit the flagged owners: an owner with no partner can neither
  // be revoked (a revocation needs a partner) nor cause one (same relation), so it is inert and
  // skipping it is not an approximation. At PU200 stage A flags a handful of a few hundred owners.
  //
  // Bit 1 (kSeedContEarlierStage) is set iff one of those partners belongs to an EARLIER STAGE --
  // an owner tag below `ownerTag`, i.e. a stage-A owner seen from stage B. Such a partner is final:
  // nothing the greedy does can revoke it, so bit 1 alone decides this owner and the greedy revokes
  // it WITHOUT walking the table at all. That is what makes the split pay in stage B, where about
  // four fifths of the owners are flagged. In stage A `ownerTag` is 0, no tag can be below it, and
  // the predicate below collapses to "some other owner".
  //
  // Bit 2 (kSeedContDead) is not written here: it is the greedy's own "already revoked" marker, and
  // it lives in this word so the serial residue needs no second array (see ChainAttachT3Dedup).
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
      uint32_t const nOwners = *nOwnersPtr;
      for (uint32_t ownerIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (ownerIdx >= nOwners)
          continue;
        cont[ownerIdx] = 0u;
        if (ownerPls[ownerIdx] < 0)
          continue;
        uint32_t const self = ownerTag + ownerIdx;
        uint32_t const* keys = ownerHits + static_cast<size_t>(ownerIdx) * kMaxPLSHitsInHitsSoA;
        int const nKeys = ownerNHits[ownerIdx];
        bool contentious = false;
        bool earlierStage = false;
        // nEnt is the number of table entries this owner's keys resolve to, and it bounds from
        // above the number of DISTINCT partner owners the greedy's `seen` array can hold. Reporting
        // its maximum is the standing evidence that the kSeedDedupSeenMax cap never truncates a
        // verdict.
        uint32_t nEnt = 0;
        for (int keyIdx = 0; keyIdx < nKeys; ++keyIdx) {
          uint32_t slot = attachSeedHash(keys[keyIdx]);
          for (uint32_t entryKey = hashKey[slot]; entryKey != chainattach::kSeedHashEmpty; entryKey = hashKey[slot]) {
            if (entryKey == keys[keyIdx]) {
              ++nEnt;
              uint32_t const entryOwner = hashOwner[slot];
              // Stop as soon as BOTH bits are settled: while only bit 1 is outstanding, nothing but
              // an earlier-stage tag can still change the answer. With ownerTag == 0 (stage A) the
              // `entryOwner < ownerTag` disjunct is always false and this is just "some other
              // owner, not yet flagged".
              bool const want = (entryOwner != self) && (!contentious || (!earlierStage && entryOwner < ownerTag));
              if (want && attachSeedShared(hashKey, hashOwner, keys, nKeys, entryOwner) >= 2u) {
                contentious = true;
                if (entryOwner < ownerTag)
                  earlierStage = true;
              }
            }
            slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
          }
        }
        alpaka::atomicMax(acc, &stats[14], nEnt, alpaka::hierarchy::Blocks{});
        if (contentious) {
          cont[ownerIdx] = chainattach::kSeedContPartner | (earlierStage ? chainattach::kSeedContEarlierStage : 0u);
          alpaka::atomicAdd(acc, &stats[13], 1u, alpaka::hierarchy::Blocks{});
          if (earlierStage)
            alpaka::atomicAdd(acc, &stats[12], 1u, alpaka::hierarchy::Blocks{});
        }
      }
    }
  };

  // Offline verification of the conflict prefilter, off unless LST_CHAIN_RD_AUDIT is set (same
  // shape as ChainAttachAudit). It recomputes the partner relation the brute-force way -- every
  // ordered owner pair, straight off the staged hit lists, no hash table anywhere -- and counts the
  // owners whose cont[] flag disagrees. The count must be zero: it is the standing assertion that
  // ChainAttachSeedConflicts flags a superset of the revocable owners, which is what lets the
  // greedy skip everything else.
  struct ChainAttachSeedAudit {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  uint32_t const* nOwnersPtr,
                                  uint32_t const* ownerHits,
                                  uint8_t const* ownerNHits,
                                  int32_t const* ownerPls,
                                  uint32_t const* cont,
                                  uint32_t nBound,
                                  uint32_t* stats) const {
      uint32_t const nOwners = *nOwnersPtr;
      for (uint32_t ownerIdx : cms::alpakatools::uniform_elements(acc, nBound)) {
        if (ownerIdx >= nOwners || ownerPls[ownerIdx] < 0)
          continue;
        uint32_t const* keys = ownerHits + static_cast<size_t>(ownerIdx) * kMaxPLSHitsInHitsSoA;
        int const nKeys = ownerNHits[ownerIdx];
        bool partner = false;
        for (uint32_t otherIdx = 0; otherIdx < nOwners && !partner; ++otherIdx) {
          if (otherIdx == ownerIdx || ownerPls[otherIdx] < 0)
            continue;
          uint32_t const* otherKeys = ownerHits + static_cast<size_t>(otherIdx) * kMaxPLSHitsInHitsSoA;
          int const nOtherKeys = ownerNHits[otherIdx];
          uint32_t shared = 0;
          for (int keyIdx = 0; keyIdx < nKeys; ++keyIdx)
            for (int otherKeyIdx = 0; otherKeyIdx < nOtherKeys; ++otherKeyIdx)
              if (keys[keyIdx] == otherKeys[otherKeyIdx])
                ++shared;
          if (shared >= 2u)
            partner = true;
        }
        if (partner != (cont[ownerIdx] != 0u))
          alpaka::atomicAdd(acc, &stats[15], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

  // The stage-A seed-family dedup: an owner is revoked iff an EARLIER owner that was itself kept
  // shares at least 2 pixel hit rows with it. The verdict genuinely depends on the order, so this
  // greedy cannot simply be run one thread per owner. It can be split, which is what the two
  // kernels above do:
  //
  //   (1) the table is built for EVERY owner up front, concurrently (ChainAttachOwnerHits). The
  //       insert is a linear-probe CAS that always claims a fresh slot, so the resulting MULTISET
  //       of (key, owner) entries does not depend on the interleaving; only the slot LAYOUT can
  //       differ from a serial insert, and no reader can observe that, because every lookup probes
  //       to the first empty slot in a later launch. Building it for owners that will later be
  //       revoked is what the tombstone below undoes.
  //   (2) the partner relation is symmetric and order-free (see attachSeedShared), so the set of
  //       owners that could POSSIBLY be revoked, or cause a revocation, is computed in parallel
  //       (ChainAttachSeedConflicts). Everything outside that set is inert.
  //
  // What is left here is the greedy over the flagged owners alone, and it reads the decision it
  // needs straight off the table: when it reaches an owner, the entries present are exactly those
  // of the owners still alive, and the scan below ignores every entry whose owner is not EARLIER.
  // That is the same read set an incremental walk would have, so the verdict is the same, with the
  // partner count exact rather than capped at kSeedDedupSeenMax distinct partners.
  //
  // A revoked owner's entries are TOMBSTONED, which is what keeps step (1) honest: after this
  // kernel the table holds exactly the surviving owners' entries, which is what stage B reuses, and
  // no later owner in this walk can see a revoked one either.
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

      for (uint32_t ownerIdx = 0; ownerIdx < nOwners; ++ownerIdx) {
        if (cont[ownerIdx] == 0u)  // no partner anywhere: cannot be revoked, cannot revoke
          continue;
        int32_t const seedIdx = ownerPls[ownerIdx];
        if (seedIdx < 0)
          continue;
        uint32_t const* keys = ownerHits + static_cast<size_t>(ownerIdx) * kMaxPLSHitsInHitsSoA;
        int const nKeys = ownerNHits[ownerIdx];

        bool isDup = false;
        for (int keyIdx = 0; keyIdx < nKeys && !isDup; ++keyIdx) {
          uint32_t slot = attachSeedHash(keys[keyIdx]);
          for (uint32_t entryKey = hashKey[slot]; entryKey != chainattach::kSeedHashEmpty; entryKey = hashKey[slot]) {
            if (entryKey == keys[keyIdx]) {
              uint32_t const entryOwner = hashOwner[slot];
              // Our own entries fail the test; a later owner has not been decided yet and cannot
              // revoke us; a revoked earlier owner is not in the table at all, its entries carrying
              // the tombstone key.
              if (entryOwner < ownerIdx && attachSeedShared(hashKey, hashOwner, keys, nKeys, entryOwner) >= 2u) {
                isDup = true;
                break;
              }
            }
            slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
          }
        }
        if (!isDup)
          continue;

        chains.attachPls()[owners[ownerIdx]] = -1;
        chains.attachLogit()[owners[ownerIdx]] = kAttachNoLogit;
        ++stats[5];
        for (int keyIdx = 0; keyIdx < nKeys; ++keyIdx) {
          uint32_t slot = attachSeedHash(keys[keyIdx]);
          for (uint32_t entryKey = hashKey[slot]; entryKey != chainattach::kSeedHashEmpty; entryKey = hashKey[slot]) {
            if (entryKey == keys[keyIdx] && hashOwner[slot] == ownerIdx) {
              hashKey[slot] = chainattach::kSeedHashTomb;
              break;  // one entry per staged position, so retire one per position
            }
            slot = (slot + 1u) & (chainattach::kSeedHashSlots - 1u);
          }
        }
      }
    }
  };

  // The surviving grants of EITHER stage, after that stage's dedup revocations, published into the
  // one-seed-one-owner authority array plsOwned. Both stages leave a revoked grant negative, and a
  // target that lost the contention is already negative from ChainAttachResolve, so one pass over
  // the position array covers exactly the survivors.
  //
  // `tgtPls == nullptr` is stage A, whose grant lives on the chain row (a chain can only carry one
  // if it is a target, so its target list is the index); stage B passes its position-keyed grant
  // array and leaves `targets` unused. Same nullptr switch as ChainAttachOwnerHits and
  // ChainAttachResolve. `stats == nullptr` suppresses the census.
  struct ChainAttachPublish {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ChainsConst chains,
                                  uint32_t const* targets,
                                  int32_t const* tgtPls,
                                  uint32_t nTargets,
                                  uint8_t* plsOwned,
                                  uint32_t* stats) const {
      for (uint32_t position : cms::alpakatools::uniform_elements(acc, nTargets)) {
        int32_t const seedIdx = (tgtPls != nullptr) ? tgtPls[position] : chains.attachPls()[targets[position]];
        if (seedIdx < 0)
          continue;
        plsOwned[static_cast<uint32_t>(seedIdx)] = 1u;
        if (stats != nullptr)
          alpaka::atomicAdd(acc, &stats[4], 1u, alpaka::hierarchy::Blocks{});
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
