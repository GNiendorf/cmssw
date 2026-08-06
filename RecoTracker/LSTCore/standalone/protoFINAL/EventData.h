#ifndef PROTOTYPE_EVENTDATA_H
#define PROTOTYPE_EVENTDATA_H

// Flat per-event data for the chain-tracking offline prototype (plan section 10).
// Loaded from the LST --allobj ntuple (CUT_VALUE_DEBUG build) plus the aligned tracking
// ntuple. Everything is a plain std::vector so stage functions stay pure
// arrays-in/arrays-out; the eventual Alpaka port wraps each stage in a kernel without
// restructuring (plan 10.3).

#include <cstdint>
#include <string>
#include <vector>

// Event-independent module info from the module_* branches (occ block).
// Row = lower-module index, 0..nLowerModules; the last row is the pixel pseudo-module.
struct ModuleTable {
  std::vector<int> detIds, layers, subdets, rings, rods, modules;
  std::vector<bool> isTilted;
  std::vector<float> eta, r;
};

// One LST-ntuple entry (one event), LST-reconstruction side.
struct LSTEventData {
  unsigned int run = 0, lumi = 0;
  unsigned long long evt = 0;

  // sim tracks (writer pre-filters to bunchCrossing==0 && event==0, hard scatter in-time;
  // row = "accepted sim" index used by sim_tcIdx and the *_simIdx branches)
  std::vector<float> sim_pt, sim_eta, sim_phi, sim_pca_dxy, sim_pca_dz, sim_vx, sim_vy, sim_vz;
  std::vector<int> sim_q, sim_pdgId;

  // baseline track candidates (for the M0 identity milestone and baseline comparison)
  std::vector<float> tc_pt, tc_eta, tc_phi;
  std::vector<int> tc_type, tc_isFake, tc_isDuplicate, tc_nhitOT;
  // Per baseline TC: row into the pT5_* / pT3_* / pLS_* object blocks when the TC is of
  // that type, -999 otherwise (writer tc_pt5Idx/tc_pt3Idx/tc_plsIdx, --allobj builds).
  // Used by K8 suppression to map a suppressed pLS back to its baseline pixel TCs.
  std::vector<int> tc_pt5Idx, tc_pt3Idx, tc_plsIdx;
  std::vector<int> sim_tcIdx;  // per accepted sim: best-matched TC index or -999
  // Per baseline TC: matched sim rows and fractions (>75%), from the tc_simIdxAll/Frac
  // branches. NOTE: FULL tracking-ntuple sim rows (write_lst_ntuple.cc keeps the tc->sim
  // direction in full-sim space; only sim_tcIdxAll is truncated to accepted rows).
  std::vector<std::vector<int>> tc_simIdxAll;
  std::vector<std::vector<float>> tc_simIdxAllFrac;

  // MiniDoublets. anchor/otherHitIdx are rows into the tracking ntuple ph2_* branches for
  // outer-tracker MDs (md_isPLS false) and pix_* rows for pLS pseudo-MDs (md_isPLS true).
  std::vector<float> md_anchor_x, md_anchor_y, md_anchor_z;
  std::vector<float> md_other_x, md_other_y, md_other_z;
  std::vector<int> md_anchorHitIdx, md_otherHitIdx;
  std::vector<int> md_type;   // 1 = PS, 0 = 2S
  std::vector<int> md_layer;  // 1-6 barrel, 7-11 endcap
  std::vector<int> md_detId;
  std::vector<bool> md_isPLS;
  std::vector<float> md_dphichange;  // present only in CUT_VALUE_DEBUG builds
  // >75%-matched sim indices per MD (both hits matched) and their match fractions.
  // NOTE: FULL tracking-ntuple sim rows (same space as t3_matched_simIdx), not
  // accepted-sim rows; accepted sims occupy rows [0, sim_pt.size()) so values below
  // sim_pt.size() index the ev.sim_* block directly (see Labels.cc).
  std::vector<std::vector<int>> md_simIdxAll;
  std::vector<std::vector<float>> md_simIdxAllFrac;

  // LineSegments (indices into the md_* containers above)
  std::vector<int> ls_mdIdx0, ls_mdIdx1;
  std::vector<bool> ls_isPLS;

  // Triplets = graph nodes (indices into ls_* containers)
  std::vector<int> t3_lsIdx0, t3_lsIdx1;
  std::vector<float> t3_pt, t3_eta, t3_phi;
  std::vector<float> t3_radius, t3_centerX, t3_centerY, t3_betaIn;
  std::vector<float> t3_fakeScore, t3_promptScore, t3_displacedScore;
  std::vector<float> t3_pMatched;  // continuous max match fraction (no threshold)
  // >75% matched sim indices; NOTE: these are FULL tracking-ntuple sim rows (t3dnn block,
  // matchedSimTrkIdxs on the full list), not accepted-sim rows.
  std::vector<std::vector<int>> t3_matched_simIdx;
  // Baseline cross-cleaning flags (t3dnn block, row-aligned with t3_*): T3 consumed by a
  // pT5 / pT3 in the baseline reconstruction.
  std::vector<bool> t3_partOfPT5, t3_partOfPT3;

  // Derived by the reader (not branches): per-T3 MD indices via the LS chain,
  // {LS0.md0, LS0.md1 (shared middle), LS1.md1} — AccessHelper convention.
  std::vector<int> t3_md0, t3_md1, t3_md2;

  // pixel line segments (seeds)
  std::vector<float> pLS_pt, pLS_ptErr, pLS_eta, pLS_etaErr, pLS_phi;
  std::vector<float> pLS_px, pLS_py, pLS_pz;
  std::vector<float> pLS_circleCenterX, pLS_circleCenterY, pLS_circleRadius, pLS_deltaPhi;
  std::vector<int> pLS_charge, pLS_nhit, pLS_seedIdx;  // seedIdx = row into trk see_* branches
  std::vector<bool> pLS_isQuad;
  std::vector<int> pLS_isFake, pLS_isDuplicate;
  // P1 RE-BASELINE: LST's OWN algorithmic pixelSegments.isDup() bitmask, three snapshots
  // (writer pLS_isDupAlgSelf / Pass2 / Final). NOT the truth-level pLS_isDuplicate above.
  //   Self  = after CheckHitspLS pass 1 (pixelLineSegmentCleaning), bit 0 only
  //   Pass2 = after CheckHitspLS pass 2, bit 1 added; BOTH passes are pure seed
  //           self-cleaning and survive the P2.7 deletion
  //   Final = after CrossCleanpLS (which writes 1 and clobbers the bitmask); the rows with
  //           Final == 0 && isQuad are exactly today's carried type-8 TCs
  // Empty when the ntuple predates the instrument; every consumer must check size().
  std::vector<int> pLS_isDupAlgSelf, pLS_isDupAlgPass2, pLS_isDupAlgFinal;
  // pixelSegments.score(): CheckHitspLS's tie-break within a duplicate seed family.
  std::vector<float> pLS_score;
  // Innermost seed hit position (writer pLS_hit0_*; K8 zResidAtInnermost input).
  std::vector<float> pLS_hit0_x, pLS_hit0_y, pLS_hit0_z;
  // >75%-matched sim indices per pLS. INDEX SPACE (verified 2026-08-01, M7 Task A):
  // FULL tracking-ntuple sim rows, NOT accepted-sim rows as previously stated here.
  // Source: write_lst_ntuple.cc pushes matchedSimTrkIdxsAndFracs output straight into
  // pLS_simIdxAll; that helper indexes trk_simhit_simTrkIdx = full sim space, the SAME
  // space as md_simIdxAll / t3_matched_simIdx / tc_simIdxAll. Empirical (5 evts): only
  // 2.5-7.4% of values are < sim_pt.size() (max value ~179k vs nAccepted ~600-1600, the
  // rest are pileup sims) — identical profile to tc_simIdxAll, which is documented FULL.
  // Accepted sims are the contiguous PREFIX of the full list (verified: rows with
  // bunchCrossing==0 && event==0 are exactly rows 0..nAccepted-1 on all checked events),
  // so accepted row r == full row r. CONVERSION RULE for the K8 label builder: a value v
  // here refers to the SAME sim as a chain-sim full row v — compare directly, no offset;
  // v additionally indexes the ev.sim_* block iff v < sim_pt.size() (otherwise it is a
  // pileup/out-of-time sim with no accepted row; trk.simFullToAccepted[v] == -1).
  std::vector<std::vector<int>> pLS_simIdxAll;

  // Baseline pixel objects (K8 suppression bookkeeping): per baseline pT5/pT3 row, the
  // pLS row it consumed (into pLS_* above). Row spaces match tc_pt5Idx / tc_pt3Idx.
  std::vector<int> pT5_plsIdx;
  std::vector<int> pT3_plsIdx;

  // B1 (claim-universe unification, -PU): the OUTER-TRACKER hit content of the baseline
  // pixel TCs, needed to pre-claim their hits before K9 arbitrates the chains.
  // INDEX SPACE (verified 2026-08-01 on LSTNtuple_PU200RelVal_300evt.root, evts 0/1):
  // t5_hitIndices and pT3_otHitIndices are TRACKING-NTUPLE ph2 ROWS -- the SAME space as
  // md_anchorHitIdx / md_otherHitIdx (write_lst_ntuple.cc pushes hitsBase.idxs() into
  // md_anchorHitIdx and the quintuplet/pixelTriplet hitIndices() SoA fields carry the
  // identical translated rows). Cross-check: routing pT5 -> pT5_t5Idx -> t5_t3Idx0/1 ->
  // t3 -> ls -> md -> md_anchorHitIdx/md_otherHitIdx reproduces t5_hitIndices EXACTLY for
  // every nLayers == 5 T5, and t5_hitIndices additionally carries the ExtendT5FromDupT5
  // layer-6/7 hits that the T3 route cannot see (t5_nLayers takes values 5, 6 and 7).
  // Therefore the hit branches (not the T3 route) are the exact mapping and are used.
  // pT3_otHitIndices matched the T3 route exactly (pT3 is never extended).
  // Type-8 (bare pLS) TCs have NO outer-tracker hits, so they contribute nothing.
  std::vector<int> pT5_t5Idx;                      // per pT5 row -> t5 row
  std::vector<std::vector<int>> t5_hitIndices;     // per t5 row -> 2*nLayers ph2 rows
  std::vector<std::vector<int>> pT3_otHitIndices;  // per pT3 row -> 6 ph2 rows
};

// Aligned tracking-ntuple truth for exact hit-level sim matching (plan 10.4).
struct TrkEventData {
  std::vector<int> simhit_simTrkIdx;
  std::vector<std::vector<int>> ph2_simHitIdx;  // per ph2 hit row -> simhit rows
  std::vector<std::vector<int>> pix_simHitIdx;  // per pix hit row -> simhit rows
  std::vector<std::vector<int>> see_hitIdx;     // per seed row -> hit rows
  std::vector<std::vector<int>> see_hitType;    // per seed row -> lst::HitType values
  // Full sim list (size = n_total_simtrk; needed because tc_isDuplicate is counted against
  // ALL sim tracks incl. pileup, while the LST ntuple sim_* block stores only accepted ones)
  std::vector<int> sim_bunchCrossing, sim_event, sim_q;
  std::vector<float> sim_pt;
  // Map full-sim-row -> accepted-sim row (or -1): reconstructed by the reader from
  // bunchCrossing==0 && event==0 in order, mirroring write_lst_ntuple.cc.
  std::vector<int> simFullToAccepted;
};

#endif
