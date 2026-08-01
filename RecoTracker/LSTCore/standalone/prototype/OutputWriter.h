#ifndef PROTOTYPE_OUTPUTWRITER_H
#define PROTOTYPE_OUTPUTWRITER_H

// Writes a self-contained ROOT file impersonating the LST ntuple so the production
// efficiency harness runs UNCHANGED (plan 10.4):
//   - TTree named "tree", per-event std::vector branches
//   - sim_pt/eta/phi/pca_dxy/pca_dz/pdgId/q/vx/vy/vz copied from the input event
//   - tc_pt/eta/phi (float), tc_type/isFake/isDuplicate/nhitOT (int), sim_tcIdx (int)
//   - three TNamed: code_tag_data, gitdiff, input (helper.cc exits without them)
//   - run/lumi/evt passthrough (harmless extras; harness reads branches by name)
// Do NOT write a branch named t5_pt (its mere presence flips do_lower_level).

#include <memory>
#include <string>
#include <vector>

#include "EventData.h"
#include "Matching.h"

// A prototype track candidate with its full hit list, for exact hit-level matching.
struct OutTC {
  float pt = 0, eta = 0, phi = 0;
  int type = 4;    // LSTObjType convention: 7=pT5, 5=pT3, 4=T5, 8=pLS, 9=T4
  int nhitOT = 0;  // number of OT hits (tc_nhitOT; harness reads it unconditionally)
  std::vector<unsigned int> hitIdxs;  // ph2 rows for OT hits, pix rows for pixel hits
  std::vector<proto::HitType> hitTypes;
};

class OutputWriterImpl;

class OutputWriter {
public:
  // inputLabel goes into the "input" TNamed (e.g. "PU200RelVal").
  OutputWriter(const std::string& outPath, const std::string& inputLabel);
  ~OutputWriter();

  // M0 identity mode: copy the input event's tc_* and sim_tcIdx verbatim (no matching).
  void fillEventIdentity(const LSTEventData& ev);

  // Real mode: compute tc_isFake/tc_isDuplicate/sim_tcIdx from the TCs' hit lists with the
  // ported production matching (strict >0.75; duplicates counted against the FULL sim list
  // incl. pileup; sim_tcIdx = best-fraction TC per accepted sim, mirroring the writer).
  void fillEvent(const LSTEventData& ev, const TrkEventData& trk, const std::vector<OutTC>& tcs);

  // HYBRID mode (the M4 A/B): output = kept BASELINE pixel TCs (input rows with tc_type in
  // {7 pT5, 5 pT3, 8 pLS}, matching taken from the input's tc_simIdxAll/Frac branches —
  // requires those fields in LSTEventData) + the prototype's chain TCs (matching computed
  // from hit lists via the ported matcher). Baseline T5/T4-type rows (tc_type 4/9) are
  // REPLACED by the chains. sim_tcIdx / tc_isDuplicate are computed over the MERGED set
  // with production semantics (full-sim-list accumulation, strict >0.75, best-fraction).
  // Also fills the diagnostics branches tc_simIdxAll (per-TC full-sim-list match rows)
  // and tc_isChain (1 = chain TC, 0 = kept baseline row) — harness-invisible extras for
  // offline duplicate decomposition.
  //
  // K8 attach suppression (M7, hybrid -A 1): suppressPlsRows (optional, size nPls,
  // 1 = suppressed pLS) DROPS kept-baseline rows that deliver that pixel seed:
  // tc_type 7 rows whose pT5_plsIdx[tc_pt5Idx] is a suppressed pLS and tc_type 8 rows
  // whose tc_plsIdx is a suppressed pLS. With suppressPT3Rows (M7b, hybrid -A 2) also
  // tc_type 5 rows whose pT3_plsIdx[tc_pt3Idx] is a suppressed pLS -- the seed-family
  // extension (the MASK semantics are the caller's: -A 1 marks only the attached pLS
  // itself; -A 2 marks every pLS sharing >= 2 pixel hits with an attached pLS).
  // nSuppressedOut (optional) receives the dropped-row count; nSuppressedByType
  // (optional, int[3]) the per-type breakdown {type 7, type 5, type 8}.
  // nullptr mask = legacy behavior (bit-exact).
  //
  // M7c (hybrid -A 2): suppressRowMask (optional, size = input tc count, 1 = drop this
  // kept-baseline ROW) takes PRECEDENCE over suppressPlsRows when non-null -- the
  // caller has already resolved pLS families to rows AND applied the kinematic
  // suppression guard, so the writer just obeys the per-row verdict (counting per type
  // as before). An all-zero mask is bit-identical to no suppression.
  void fillEventHybrid(const LSTEventData& ev,
                       const TrkEventData& trk,
                       const std::vector<OutTC>& chainTCs,
                       const std::vector<char>* suppressPlsRows = nullptr,
                       int* nSuppressedOut = nullptr,
                       bool suppressPT3Rows = false,
                       int* nSuppressedByType = nullptr,
                       const std::vector<char>* suppressRowMask = nullptr);

  void writeAndClose();

private:
  std::unique_ptr<OutputWriterImpl> impl_;
};

#endif
