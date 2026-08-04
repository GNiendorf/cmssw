#ifndef PROTOTYPE_PIXELATTACHCAND_H
#define PROTOTYPE_PIXELATTACHCAND_H

// ================== M20 (T3ATTACH-BUILD): CANDIDATE FINDING FOR THE GENERAL ATTACH =====
//
// The M16 general pLS->OT attach enumerates its candidate pairs with a FULL SCAN: every
// target x every pLS, rejected by the two analytic windows (|dTanLambda| < prefDTanL and
// |dPhiAtInnermost| < prefDPhi). That is affordable for the CHAIN target universe (a few
// thousand targets/evt) and NOT affordable for the BARE-T3 universe (~35k targets/evt x
// ~21k pLS = ~7e8 window evaluations/evt).
//
// This header adds the two candidate-finder alternatives the pT3-replacement campaign has
// to be able to compare on equal footing, WITHOUT changing the frozen physics:
//
//   kCandAnalytic (0)  the frozen full scan.                DEFAULT. Bit-identical.
//   kCandBinned   (1)  our own scalar binned prefilter -- the prototype form of
//                      production's invariant-keyed grid. pLS are binned by exactly the
//                      quantities the analytic windows are written in, so a target scans
//                      only the compatible bins. PROVABLE SUPERSET of the analytic
//                      candidate set (proof below), so the emitted pair list is IDENTICAL
//                      to mode 0's -- the audit checks that identity, event by event.
//   kCandMap      (2)  LST's pixel map used as a PREFILTER ONLY: a candidate-pair list
//                      dumped from production is read from a file and used INSTEAD of the
//                      built-in prefilter. No map port is needed to evaluate that option.
//
// ---------------------------------------------------------------------------------------
// THE BINNED PREFILTER, AND WHY IT IS A SUPERSET
//
// The two analytic windows are, for target T and pixel seed P,
//    W1  |tanLambda(P) - tanLambda(T)| < prefDTanL
//    W2  |wrap( phiDir(P, rtInner(T)) - chordPhi(T) )| < prefDPhi
// where phiDir(P, R) is the azimuth of the direction of motion of P's helix circle where
// it crosses radius R outward (PixelAttach.cc dPhiAtInnermost), and rtInner/chordPhi are
// the target's innermost anchor radius and innermost chord azimuth.
//
// Index axes: (rt bin of the TARGET) x (tanLambda bin of the SEED) x (phi bin).
//
// W1 axis. Seeds are binned by clamp(tanLambda, -T, +T); a target scans every bin between
//   bin(clamp(c_T - prefDTanL)) and bin(clamp(c_T + prefDTanL)), c_T = clamp(tanLambda(T)).
//   clamp to an interval is NON-EXPANSIVE, so |c_P - c_T| <= |tanLambda(P) - tanLambda(T)|;
//   any pair passing W1 therefore has c_P strictly inside (c_T - D, c_T + D), and bin() is
//   monotone, so its bin is inside the scanned range. Nothing that passes W1 is missed.
//
// W2 axis. phiDir(P, .) depends on the TARGET's radius, which is not a seed property --
//   hence the rt axis. For seed P and rt bin r = [rtLo, rtHi] we insert P into every phi
//   bin that the arc  I(P,r) = { phiDir(P,R) : R in [rtLo, rtHi] }  touches, padded by
//   candPhiPad. A target with rtInner in bin r scans the phi bins covering
//   [chordPhi - prefDPhi, chordPhi + prefDPhi]. If the pair passes W2 then
//   phiDir(P, rtInner) lies in BOTH sets, so the bin holding it is inserted AND scanned.
//
//   I(P,r) is computed from the ENDPOINTS because phiDir(P,R) is MONOTONE in R: writing
//   the seed circle as centre C (|C| = d), radius rho, the outward crossing at radius R is
//   at azimuth beta = -rotSign * acos((R^2 - d^2 - rho^2)/(2 d rho)) measured from C's
//   azimuth, and the direction of motion is that azimuth + rotSign*pi/2; acos(.) is
//   monotone in R, so phiDir is monotone in R and the endpoints bracket the arc exactly.
//   Two interior samples are added anyway (belt and braces), the hull is padded, an arc
//   wider than pi degrades to "all phi bins", and radii outside the circle-circle
//   intersection range [|d-rho|, d+rho] add the code's own fallback value phi(P).
//
// SEEDS WITH NON-FINITE GEOMETRY go to `wildPls` and are scanned by EVERY target; TARGETS
// with non-finite geometry scan EVERY seed. Neither can be silently dropped.
//
// The superset property is not merely argued: -CFA 1 runs the analytic full scan
// alongside and counts, per event, the analytic-accepted pairs that the candidate set did
// not contain. That counter MUST be 0.
//
// ---------------------------------------------------------------------------------------
// MAP-CANDIDATE FILE FORMAT (mode 2)
//
// Two interchangeable encodings, auto-detected from the first 8 bytes.
//
// TEXT (default; what a production fprintf dumper writes most easily). Blank lines and
// lines starting with '#' are ignored. One event header, then one line per pair:
//     E <run> <lumi> <event> <nPairs>
//     <t3Row> <plsRow>
//     ...
// <t3Row> is a row into the LST ntuple's t3_* block and <plsRow> a row into pLS_*, i.e.
// EXACTLY production's own T3 and pLS indices for the same event -- the ntuple is written
// by the same job, so no translation table is needed. Pair order is free (the loader
// sorts); duplicates are collapsed.
//
// BINARY (little-endian, for large dumps). Magic "T3PAIRS1" (8 bytes), then per event
//     int32 run, int32 lumi, int32 event, int32 nPairs, then nPairs x (int32 t3Row, int32 plsRow)
//
// Events absent from the file contribute NO candidates and are counted in
// CandStats::nMapEventsMissing -- silently attaching nothing for a missing event would
// look like a physics result.

#include <string>
#include <unordered_map>
#include <vector>

#include "EventData.h"

enum AttachCandMode : int {
  kCandAnalytic = 0,  // frozen full scan over every pLS (default)
  kCandBinned = 1,    // scalar binned prefilter (provable superset of mode 0)
  kCandMap = 2,       // external production candidate-pair list
};

// Volume / audit counters. Owned by the caller, accumulated across events.
struct CandStats {
  long long nTargets = 0;        // targets enumerated (both kinds)
  long long nFullScan = 0;       // nTargets * nPls -- what mode 0 examines
  long long nExamined = 0;       // pairs the candidate finder handed to the window test
  long long nEmitted = 0;        // pairs that passed and were emitted
  long long nAnalytic = 0;       // -CFA: pairs the analytic full scan accepts
  long long nMissing = 0;        // -CFA: analytic-accepted pairs ABSENT from the candidate
                                 //       set. MUST BE 0. This is the superset audit.
  long long nWildTargets = 0;    // targets with non-finite geometry (scanned every pLS)
  long long nMapTargetsEmpty = 0;  // mode 2: bare targets with no map entry
  long long nMapEventsMissing = 0;  // mode 2: events absent from the candidate file
};

// -------------------------------------------------------------------------------------
// The per-event candidate structure. ONE object serves both modes 1 and 2 so a single
// pointer in AttachParams covers the whole surface.
struct PlsCandIndex {
  // ---------------- mode 1: the binned index (rebuilt per event) ----------------
  int nPls = 0;
  int nRt = 0, nTan = 0, nPhi = 0;
  float rtW = 8.f;      // rt bin width [cm]; the LAST bin is open-ended
  float tanClamp = 0.f; // tanLambda axis covers [-tanClamp, +tanClamp]
  float tanW = 1.f;     // tanLambda bin width
  float phiW = 1.f;     // phi bin width [rad]; nPhi bins tile [0, 2pi) exactly
  std::vector<int> cellStart;  // CSR, size nRt*nTan*nPhi + 1
  std::vector<int> cellItems;  // pLS rows, ASCENDING within each cell
  std::vector<int> wildPls;    // non-finite seed geometry: scanned by every target

  // ---------------- mode 2: map candidates for the CURRENT event ----------------
  std::vector<int> mapStart;  // CSR over t3 rows, size nT3+1
  std::vector<int> mapItems;  // pLS rows, ASCENDING and deduplicated
  bool mapEventFound = false;

  // ---------------- query scratch (single-threaded enumeration) ----------------
  mutable std::vector<int> qBuf;
  mutable std::vector<int> qStamp;
  mutable int qEpoch = 0;
};

// Tuning of the binned index. Defaults are the ones the audit was run at.
struct CandIndexParams {
  float rtBinW = 8.f;    // -CFR  rt bin width [cm]
  float binMult = 1.f;   // -CFB  bin width as a multiple of the analytic window
  float phiPad = 0.02f;  // -CFP  extra pad on the inserted phi arc [rad]
  float tanClamp = 40.f; // tanLambda axis half-range (|eta| ~ 4.4); clamping is safe
  float rtMax = 130.f;   // last rt bin is open-ended above this [cm]
};

// ------------------------------- mode 1 ------------------------------------------------
// Builds the binned index over EVERY pLS of the event. Cost: nPls * nRt arc evaluations.
// `params` supplies the analytic window widths the bins are sized against.
struct AttachParams;
void k8BuildPlsCandIndex(const LSTEventData& ev,
                         const AttachParams& params,
                         const CandIndexParams& ip,
                         PlsCandIndex& out);

// ------------------------------- mode 2 ------------------------------------------------
// The whole file, parsed once, keyed by (run, lumi, event).
struct MapCandFile {
  struct Key {
    unsigned int run = 0, lumi = 0;
    unsigned long long evt = 0;
    bool operator==(const Key& o) const { return run == o.run && lumi == o.lumi && evt == o.evt; }
  };
  struct KeyHash {
    std::size_t operator()(const Key& k) const {
      std::size_t h = static_cast<std::size_t>(k.evt) * 1000003u;
      h ^= static_cast<std::size_t>(k.run) * 2654435761u + (h << 6) + (h >> 2);
      h ^= static_cast<std::size_t>(k.lumi) * 40503u + (h << 6) + (h >> 2);
      return h;
    }
  };
  // Flat pair storage; per event a [begin, end) span of `t3` / `pls`.
  std::vector<int> t3, pls;
  std::unordered_map<Key, std::pair<int, int>, KeyHash> span;
  long long nPairs = 0;
  bool binary = false;
};

// Loads the candidate file. Returns false (and prints the reason) on any parse failure --
// a half-read candidate list must never be mistaken for a physics result.
bool k8LoadMapCandFile(const std::string& path, MapCandFile& out);

// Projects the file's entry for THIS event into the per-t3 CSR of `idx`. Sets
// idx.mapEventFound. Pairs whose t3/pLS row is out of range are dropped and counted.
void k8SelectMapCandidates(const LSTEventData& ev, const MapCandFile& f, PlsCandIndex& idx, CandStats* st);

#endif
