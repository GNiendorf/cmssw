#include "LSTEvent.h"
#include "Circle.h"

#include "write_lst_ntuple.h"
#include <tuple>

using namespace ALPAKA_ACCELERATOR_NAMESPACE::lst;

//________________________________________________________________________________________________________________________________
void createOutputBranches() {
  // Event identity from the input tracking ntuple: aligns output entries (which are written in
  // stream-completion order under multi-streaming) back to tracking-ntuple events, and provides
  // the stable event key for event-level train/test splits
  ana.tx->createBranch<unsigned int>("run");
  ana.tx->createBranch<unsigned int>("lumi");
  ana.tx->createBranch<unsigned long long>("evt");

  createSimTrackContainerBranches();
  createTrackCandidateBranches();

  if (ana.jet_branches)
    createJetBranches();

  if (ana.md_branches)
    createMiniDoubletBranches();
  if (ana.ls_branches)
    createLineSegmentBranches();
  if (ana.t3_branches)
    createTripletBranches();
  if (ana.pls_branches)
    createPixelLineSegmentBranches();
  if (ana.occ_branches)
    createOccupancyBranches();

  // DNN branches
  if (ana.t3dnn_branches)
    createT3DNNBranches();
}

//________________________________________________________________________________________________________________________________
void fillOutputBranches(LSTEvent* event) {
  float matchfrac = 0.75;

  ana.tx->setBranch<unsigned int>("run", trk.getU("run"));
  ana.tx->setBranch<unsigned int>("lumi", trk.getU("lumi"));
  ana.tx->setBranch<unsigned long long>("evt", trk.getUL("event"));

  unsigned int n_accepted_simtrk = setSimTrackContainerBranches(event);

  if (ana.jet_branches)
    setGenJetBranches(event);

  if (ana.occ_branches)
    setOccupancyBranches(event);

  if (ana.t3dnn_branches)
    setT3DNNBranches(event, matchfrac);

  auto const md_idx_map = (ana.md_branches ? setMiniDoubletBranches(event, n_accepted_simtrk, matchfrac)
                                           : std::map<unsigned int, unsigned int>());
  auto const ls_idx_map = (ana.ls_branches ? setLineSegmentBranches(event, n_accepted_simtrk, matchfrac, md_idx_map)
                                           : std::map<unsigned int, unsigned int>());
  auto const t3_idx_map = (ana.t3_branches ? setTripletBranches(event, n_accepted_simtrk, matchfrac, ls_idx_map)
                                           : std::map<unsigned int, unsigned int>());
  auto const pls_idx_map =
      (ana.pls_branches ? setPixelLineSegmentBranches(event, n_accepted_simtrk, matchfrac, ls_idx_map)
                        : std::map<unsigned int, unsigned int>());

  setTrackCandidateBranches(event, n_accepted_simtrk, pls_idx_map, matchfrac);

  // Now actually fill the ttree
  ana.tx->fill();

  // Then clear the branches to default values (e.g. -999, or clear the vectors to empty vectors)
  ana.tx->clear();
}

//________________________________________________________________________________________________________________________________
void createT3DNNBranches() {
  // Common branches for T3 properties based on TripletsSoA fields
  ana.tx->createBranch<std::vector<float>>("t3_betaIn");
  ana.tx->createBranch<std::vector<float>>("t3_centerX");
  ana.tx->createBranch<std::vector<float>>("t3_centerY");
  ana.tx->createBranch<std::vector<float>>("t3_radius");
  ana.tx->createBranch<std::vector<float>>("t3_fakeScore");
  ana.tx->createBranch<std::vector<float>>("t3_promptScore");
  ana.tx->createBranch<std::vector<float>>("t3_displacedScore");
  ana.tx->createBranch<std::vector<float>>("t3_pMatched");
  ana.tx->createBranch<std::vector<float>>("t3_sim_vxy");
  ana.tx->createBranch<std::vector<float>>("t3_sim_vz");

  // Hit-specific branches (T3 has 4 hits from two segments)
  std::vector<std::string> hitIndices = {"0", "1", "2", "3", "4", "5"};
  std::vector<std::string> hitProperties = {"r", "x", "y", "z", "eta", "phi", "detId", "layer", "moduleType"};

  for (const auto& idx : hitIndices) {
    for (const auto& prop : hitProperties) {
      std::string branchName = "t3_hit_" + idx + "_" + prop;
      if (prop == "detId" || prop == "layer" || prop == "moduleType") {
        ana.tx->createBranch<std::vector<int>>(branchName);
      } else {
        ana.tx->createBranch<std::vector<float>>(branchName);
      }
    }
  }

  // Additional metadata branches
  ana.tx->createBranch<std::vector<int>>("t3_layer_binary");
  ana.tx->createBranch<std::vector<std::vector<int>>>("t3_matched_simIdx");
}

//________________________________________________________________________________________________________________________________
void createJetBranches() {
  ana.tx->createBranch<std::vector<float>>("sim_genjet_deltaEta");
  ana.tx->createBranch<std::vector<float>>("sim_genjet_deltaPhi");
  ana.tx->createBranch<std::vector<float>>("sim_genjet_deltaR");
  ana.tx->createBranch<std::vector<int>>("sim_genjet_idx");
  ana.tx->createBranch<std::vector<float>>("genjet_eta");
  ana.tx->createBranch<std::vector<float>>("genjet_phi");
  ana.tx->createBranch<std::vector<float>>("genjet_pt");
}

//________________________________________________________________________________________________________________________________
void createSimTrackContainerBranches() {
  // Simulated Track Container
  //
  //  The container will hold per entry a simulated track in the event. Only the current bunch crossing, and
  //  primary vertex (hard-scattered) tracks will be saved to reduce the size of the output.
  //
  ana.tx->createBranch<std::vector<float>>("sim_pt");       // pt
  ana.tx->createBranch<std::vector<float>>("sim_eta");      // eta
  ana.tx->createBranch<std::vector<float>>("sim_phi");      // phi
  ana.tx->createBranch<std::vector<float>>("sim_pca_dxy");  // dxy of point of closest approach
  ana.tx->createBranch<std::vector<float>>("sim_pca_dz");   // dz of point of clossest approach
  ana.tx->createBranch<std::vector<int>>("sim_q");          // charge +1, -1, 0
  ana.tx->createBranch<std::vector<int>>("sim_pdgId");      // pdgId
  // production vertex x position (values are derived from simvtx_* and sim_parentVtxIdx branches in the tracking ntuple)
  ana.tx->createBranch<std::vector<float>>("sim_vx");
  // production vertex y position (values are derived from simvtx_* and sim_parentVtxIdx branches in the tracking ntuple)
  ana.tx->createBranch<std::vector<float>>("sim_vy");
  // production vertex z position (values are derived from simvtx_* and sim_parentVtxIdx branches in the tracking ntuple)
  ana.tx->createBranch<std::vector<float>>("sim_vz");
  // production vertex r (sqrt(x**2 + y**2)) position (values are derived from simvtx_* and sim_parentVtxIdx branches in the tracking ntuple)
  ana.tx->createBranch<std::vector<float>>("sim_vtxperp");
  // idx of sim_* in the tracking ntuple (N.B. this may be redundant)
  ana.tx->createBranch<std::vector<float>>("sim_trkNtupIdx");
  // idx to the best match (highest nhit match) tc_* container
  ana.tx->createBranch<std::vector<int>>("sim_tcIdxBest");
  // match fraction to the best match (highest nhit match) tc_* container
  ana.tx->createBranch<std::vector<float>>("sim_tcIdxBestFrac");
  // idx to the best match (highest nhit match and > 75%) tc_* container
  ana.tx->createBranch<std::vector<int>>("sim_tcIdx");
  // list of idx to any matches (> 0%) to tc_* container
  ana.tx->createBranch<std::vector<std::vector<int>>>("sim_tcIdxAll");
  // list of match fraction for each match (> 0%) to tc_* container
  ana.tx->createBranch<std::vector<std::vector<float>>>("sim_tcIdxAllFrac");

  if (ana.extra_sim_branches) {
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_simHitX");    // list of simhit's X positions
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_simHitY");    // list of simhit's Y positions
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_simHitZ");    // list of simhit's Z positions
    ana.tx->createBranch<std::vector<std::vector<int>>>("sim_simHitDetId");  // list of simhit's detId
    // list of simhit's layers (N.B. layer is numbered 1 2 3 4 5 6 for barrel, 7 8 9 10 11 for endcaps)
    ana.tx->createBranch<std::vector<std::vector<int>>>("sim_simHitLayer");
    // list of simhit's distance in xy-plane to the expected point based on simhit's z position and helix formed from pt,eta,phi,vx,vy,vz,q of the simulated track
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_simHitDistxyHelix");
    // length of 11 float numbers with min(simHitDistxyHelix) value for each layer. Useful for finding e.g. "sim tracks that traversed barrel detector entirelyand left a reasonable hit in layer 1 2 3 4 5 6 layers."
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_simHitLayerMinDistxyHelix");
    // length of 11 float numbers with min(simHitDistxyHelix) value for each layer. Useful for finding e.g. "sim tracks that traversed barrel detector entirelyand left a reasonable hit in layer 1 2 3 4 5 6 layers."
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_simHitLayerMinDistxyPrevHit");
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_recoHitX");    // list of recohit's X positions
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_recoHitY");    // list of recohit's Y positions
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_recoHitZ");    // list of recohit's Z positions
    ana.tx->createBranch<std::vector<std::vector<int>>>("sim_recoHitDetId");  // list of recohit's detId
  }

  if (ana.md_branches) {
    // list of idx to matches (> 0%) to md_* container
    ana.tx->createBranch<std::vector<std::vector<int>>>("sim_mdIdxAll");
    // list of match fraction for each match (> 0%) to md_* container
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_mdIdxAllFrac");
  }
  if (ana.ls_branches) {
    // list of idx to matches (> 0%) to ls_* container
    ana.tx->createBranch<std::vector<std::vector<int>>>("sim_lsIdxAll");
    // list of match fraction for each match (> 0%) to ls_* container
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_lsIdxAllFrac");
  }
  if (ana.t3_branches) {
    // list of idx to matches (> 0%) to t3_* container
    ana.tx->createBranch<std::vector<std::vector<int>>>("sim_t3IdxAll");
    // list of match fraction for each match (> 0%) to t3_* container
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_t3IdxAllFrac");
  }
  if (ana.pls_branches) {
    // list of idx to matches (> 0%) to pls_* container
    ana.tx->createBranch<std::vector<std::vector<int>>>("sim_plsIdxAll");
    // list of match fraction for each match (> 0%) to pls_* container
    ana.tx->createBranch<std::vector<std::vector<float>>>("sim_plsIdxAllFrac");
  }
}

//________________________________________________________________________________________________________________________________
void createTrackCandidateBranches() {
  // Track Candidates
  //
  //  The container will hold per entry a track candidate built by LST in the event.
  //
  ana.tx->createBranch<std::vector<float>>("tc_pt");   // pt
  ana.tx->createBranch<std::vector<float>>("tc_eta");  // eta
  ana.tx->createBranch<std::vector<float>>("tc_phi");  // phi
  ana.tx->createBranch<std::vector<float>>("tc_pMatched");
  ana.tx->createBranch<std::vector<int>>("tc_type");         // type = 7 (pT5), 5 (pT3), 4 (T5), 8 (pLS), 9 (T4)
  ana.tx->createBranch<std::vector<int>>("tc_isFake");       // 1 if tc is fake 0 other if not
  ana.tx->createBranch<std::vector<int>>("tc_isDuplicate");  // 1 if tc is duplicate 0 other if not
  ana.tx->createBranch<std::vector<int>>("tc_simIdx");  // idx of best matched (highest nhit and > 75%) simulated track
  ana.tx->createBranch<std::vector<int>>("tc_nhitOT");
  ana.tx->createBranch<std::vector<int>>("tc_nhits");
  ana.tx->createBranch<std::vector<int>>("tc_nlayers");
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<int>>>("tc_simIdxAll");
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<float>>>("tc_simIdxAllFrac");
  if (ana.pls_branches)
    ana.tx->createBranch<std::vector<int>>(
        "tc_plsIdx");  // index to the pls_* if it is the said type, if not set to -999
  // Per-TC hit content: the tracking-ntuple hit rows of each track candidate and their HitType,
  // straight out of candsBase.hitIndices(). Type-agnostic on purpose, because the chain pipeline
  // has no per-object-type collection (Quintuplets, PixelTriplets) to route through.
  ana.tx->createBranch<std::vector<std::vector<int>>>("tc_hitIdx");
  ana.tx->createBranch<std::vector<std::vector<int>>>("tc_hitType");
}

//________________________________________________________________________________________________________________________________
void createMiniDoubletBranches() {
  // Mini-Doublets (i.e. Two reco hits paired in a single pT-module of Outer Tracker of CMS, a.k.a. MD)
  //
  //  The container will hold per entry a mini-doublet built by LST in the event.
  //
#ifdef CUT_VALUE_DEBUG
  ana.tx->createBranch<std::vector<int>>("md_rawIdx");  // raw index in the SoA
#endif
  ana.tx->createBranch<std::vector<bool>>("md_isPLS");
  ana.tx->createBranch<std::vector<float>>("md_pt");   // pt (computed based on delta phi change)
  ana.tx->createBranch<std::vector<float>>("md_eta");  // eta (computed based on anchor hit's eta)
  ana.tx->createBranch<std::vector<float>>("md_phi");  // phi (computed based on anchor hit's phi)
#ifdef CUT_VALUE_DEBUG
  ana.tx->createBranch<std::vector<float>>("md_dphi");
  ana.tx->createBranch<std::vector<float>>("md_dphichange");
  ana.tx->createBranch<std::vector<float>>("md_dz");
#endif
  ana.tx->createBranch<std::vector<float>>("md_anchor_x");  // anchor hit x
  ana.tx->createBranch<std::vector<float>>("md_anchor_y");  // anchor hit y
  ana.tx->createBranch<std::vector<float>>("md_anchor_z");  // anchor hit z
  ana.tx->createBranch<std::vector<float>>("md_other_x");   // other hit x
  ana.tx->createBranch<std::vector<float>>("md_other_y");   // other hit y
  ana.tx->createBranch<std::vector<float>>("md_other_z");   // other hit z
  // hit index of the anchor/other hit: row into the tracking ntuple ph2_* branches for
  // outer-tracker MDs (md_isPLS == 0), row into pix_* branches for pLS pseudo-MDs (md_isPLS == 1)
  ana.tx->createBranch<std::vector<int>>("md_anchorHitIdx");
  ana.tx->createBranch<std::vector<int>>("md_otherHitIdx");
  // type of the module where the mini-doublet sit (type = 1 (PS), 0 (2S))
  ana.tx->createBranch<std::vector<int>>("md_type");
  // layer index of the module where the mini-doublet sit (layer = 1 2 3 4 5 6 (barrel) 7 8 9 10 11 (endcap))
  ana.tx->createBranch<std::vector<int>>("md_layer");
  // detId = detector unique ID that contains a lot of information that can be parsed later if needed
  ana.tx->createBranch<std::vector<int>>("md_detId");
  ana.tx->createBranch<std::vector<int>>("md_isFake");  // 1 if md is fake 0 other if not
  ana.tx->createBranch<std::vector<int>>("md_simIdx");  // idx of best matched (highest nhit and > 75%) simulated track
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<int>>>("md_simIdxAll");
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<float>>>("md_simIdxAllFrac");
}

//________________________________________________________________________________________________________________________________
void createLineSegmentBranches() {
  // Line Segments (i.e. Two mini-doublets, a.k.a. LS)
  //
  //  The container will hold per entry a line-segment built by LST in the event.
  //
#ifdef CUT_VALUE_DEBUG
  ana.tx->createBranch<std::vector<int>>("ls_rawIdx");  // raw index in the SoA
#endif
  ana.tx->createBranch<std::vector<bool>>("ls_isPLS");
  // pt (computed based on radius of the circle formed by three points: (origin), (anchor hit 1), (anchor hit 2))
  ana.tx->createBranch<std::vector<float>>("ls_pt");
  ana.tx->createBranch<std::vector<float>>("ls_eta");   // eta (computed based on last anchor hit's eta)
  ana.tx->createBranch<std::vector<float>>("ls_phi");   // phi (computed based on first anchor hit's phi)
  ana.tx->createBranch<std::vector<int>>("ls_mdIdx0");  // index to the first MD
  ana.tx->createBranch<std::vector<int>>("ls_mdIdx1");  // index to the second MD
  ana.tx->createBranch<std::vector<int>>("ls_isFake");  // 1 if md is fake 0 other if not
  ana.tx->createBranch<std::vector<int>>("ls_simIdx");  // idx of best matched (highest nhit and > 75%) simulated track
#ifdef CUT_VALUE_DEBUG
  ana.tx->createBranch<std::vector<float>>("ls_zLos");
  ana.tx->createBranch<std::vector<float>>("ls_zHis");
  ana.tx->createBranch<std::vector<float>>("ls_rtLos");
  ana.tx->createBranch<std::vector<float>>("ls_rtHis");
  ana.tx->createBranch<std::vector<float>>("ls_dPhis");
  ana.tx->createBranch<std::vector<float>>("ls_dPhiMins");
  ana.tx->createBranch<std::vector<float>>("ls_dPhiMaxs");
  ana.tx->createBranch<std::vector<float>>("ls_dPhiChanges");
  ana.tx->createBranch<std::vector<float>>("ls_dPhiChangeMins");
  ana.tx->createBranch<std::vector<float>>("ls_dPhiChangeMaxs");
  ana.tx->createBranch<std::vector<float>>("ls_dAlphaInners");
  ana.tx->createBranch<std::vector<float>>("ls_dAlphaOuters");
  ana.tx->createBranch<std::vector<float>>("ls_dAlphaInnerOuters");
#endif
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<int>>>("ls_simIdxAll");
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<float>>>("ls_simIdxAllFrac");
}

//________________________________________________________________________________________________________________________________
void createTripletBranches() {
  // Triplets (i.e. Three mini-doublets, a.k.a. T3)
  //
  //  The container will hold per entry a triplets built by LST in the event.
  //
#ifdef CUT_VALUE_DEBUG
  ana.tx->createBranch<std::vector<int>>("t3_rawIdx");  // raw index in the SoA
#endif
  // pt (computed based on radius of the circle formed by three points: anchor hit 1, 2, 3
  ana.tx->createBranch<std::vector<float>>("t3_pt");
  ana.tx->createBranch<std::vector<float>>("t3_eta");        // eta (computed based on last anchor hit's eta)
  ana.tx->createBranch<std::vector<float>>("t3_phi");        // phi (computed based on first anchor hit's phi)
  ana.tx->createBranch<std::vector<int>>("t3_lsIdx0");       // index to the first LS
  ana.tx->createBranch<std::vector<int>>("t3_lsIdx1");       // index to the second LS
  ana.tx->createBranch<std::vector<int>>("t3_isFake");       // 1 if t3 is fake 0 other if not
  ana.tx->createBranch<std::vector<int>>("t3_isDuplicate");  // 1 if t3 is duplicate 0 other if not
  ana.tx->createBranch<std::vector<int>>("t3_simIdx");  // idx of best matched (highest nhit and > 75%) simulated track
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<int>>>("t3_simIdxAll");
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<float>>>("t3_simIdxAllFrac");
}

//________________________________________________________________________________________________________________________________
void createPixelLineSegmentBranches() {
  // Pixel Line Segments (a.k.a pLS)
  //
  //  The container will hold per entry a pixel line segment (built by an external algo, e.g. patatrack) accepted by LST in the event.
  //
#ifdef CUT_VALUE_DEBUG
  ana.tx->createBranch<std::vector<int>>("pLS_rawIdx");  // raw index in the SoA
#endif
  ana.tx->createBranch<std::vector<int>>("pLS_lsIdx");  // LS-format part
  // pt (taken from pt of the 3-vector from see_stateTrajGlbPx/Py/Pz)
  ana.tx->createBranch<std::vector<float>>("pLS_pt");
  ana.tx->createBranch<std::vector<float>>("pLS_ptErr");
  // eta (taken from eta of the 3-vector from see_stateTrajGlbPx/Py/Pz)
  ana.tx->createBranch<std::vector<float>>("pLS_eta");
  ana.tx->createBranch<std::vector<float>>("pLS_etaErr");
  // phi (taken from phi of the 3-vector from see_stateTrajGlbPx/Py/Pz)
  ana.tx->createBranch<std::vector<float>>("pLS_phi");
  ana.tx->createBranch<std::vector<int>>("pLS_nhit");         // Number of actual hit: 3 if triplet, 4 if quadruplet
  ana.tx->createBranch<std::vector<int>>("pLS_seedIdx");      // row into the tracking ntuple see_* branches
  ana.tx->createBranch<std::vector<float>>("pLS_hit0_x");     // pLS's reco hit0 x
  ana.tx->createBranch<std::vector<float>>("pLS_hit0_y");     // pLS's reco hit0 y
  ana.tx->createBranch<std::vector<float>>("pLS_hit0_z");     // pLS's reco hit0 z
  ana.tx->createBranch<std::vector<float>>("pLS_hit1_x");     // pLS's reco hit1 x
  ana.tx->createBranch<std::vector<float>>("pLS_hit1_y");     // pLS's reco hit1 y
  ana.tx->createBranch<std::vector<float>>("pLS_hit1_z");     // pLS's reco hit1 z
  ana.tx->createBranch<std::vector<float>>("pLS_hit2_x");     // pLS's reco hit2 x
  ana.tx->createBranch<std::vector<float>>("pLS_hit2_y");     // pLS's reco hit2 y
  ana.tx->createBranch<std::vector<float>>("pLS_hit2_z");     // pLS's reco hit2 z
  ana.tx->createBranch<std::vector<float>>("pLS_hit3_x");     // pLS's reco hit3 x (if triplet, this is set to -999)
  ana.tx->createBranch<std::vector<float>>("pLS_hit3_y");     // pLS's reco hit3 y (if triplet, this is set to -999)
  ana.tx->createBranch<std::vector<float>>("pLS_hit3_z");     // pLS's reco hit3 z (if triplet, this is set to -999)
  ana.tx->createBranch<std::vector<int>>("pLS_isFake");       // 1 if pLS is fake 0 other if not
  ana.tx->createBranch<std::vector<int>>("pLS_isDuplicate");  // 1 if pLS is duplicate 0 other if not
  ana.tx->createBranch<std::vector<int>>("pLS_simIdx");  // idx of best matched (highest nhit and > 75%) simulated track
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<int>>>("pLS_simIdxAll");
  // list of idx of all matched (> 0%) simulated track
  ana.tx->createBranch<std::vector<std::vector<float>>>("pLS_simIdxAllFrac");
  ana.tx->createBranch<std::vector<float>>("pLS_circleCenterX");
  ana.tx->createBranch<std::vector<float>>("pLS_circleCenterY");
  ana.tx->createBranch<std::vector<float>>("pLS_circleRadius");
  ana.tx->createBranch<std::vector<float>>("pLS_px");
  ana.tx->createBranch<std::vector<float>>("pLS_py");
  ana.tx->createBranch<std::vector<float>>("pLS_pz");
  ana.tx->createBranch<std::vector<bool>>("pLS_isQuad");
  ana.tx->createBranch<std::vector<int>>("pLS_charge");
  ana.tx->createBranch<std::vector<float>>("pLS_deltaPhi");
  // LST's own ALGORITHMIC duplicate flag for pixel seeds, in three snapshots: the raw
  // pixelSegments.isDup() value (a 1 / 2 bitmask, not a boolean) recorded at the three points where
  // the column changes meaning. Distinct from pLS_isDuplicate, which is the TRUTH-level
  // sim-matching verdict.
  //   Self  = after CheckHitspLS pass 1 (pixelLineSegmentCleaning); bit 0 only.
  //   Pass2 = after CheckHitspLS pass 2; bit 1 added. Both passes are pure seed self-cleaning.
  //   Final = after CrossCleanpLS, which writes 1 and CLOBBERS the bitmask. This is the admission
  //           state: AddpLSasTrackCandidate takes isQuad rows with Final == 0.
  ana.tx->createBranch<std::vector<int>>("pLS_isDupAlgSelf");
  ana.tx->createBranch<std::vector<int>>("pLS_isDupAlgPass2");
  ana.tx->createBranch<std::vector<int>>("pLS_isDupAlgFinal");
  // pixelSegments.score(): the tie-break CheckHitspLS uses to pick which member of a
  // duplicate seed family survives (lower score wins at equal isQuad).
  ana.tx->createBranch<std::vector<float>>("pLS_score");
}

//________________________________________________________________________________________________________________________________
void createOccupancyBranches() {
  ana.tx->createBranch<std::vector<int>>("module_layers");
  ana.tx->createBranch<std::vector<int>>("module_detIds");
  ana.tx->createBranch<std::vector<int>>("module_subdets");
  ana.tx->createBranch<std::vector<int>>("module_rings");
  ana.tx->createBranch<std::vector<int>>("module_rods");
  ana.tx->createBranch<std::vector<int>>("module_modules");
  ana.tx->createBranch<std::vector<bool>>("module_isTilted");
  ana.tx->createBranch<std::vector<float>>("module_eta");
  ana.tx->createBranch<std::vector<float>>("module_r");
  ana.tx->createBranch<std::vector<int>>("md_occupancies");
  ana.tx->createBranch<std::vector<int>>("sg_occupancies");
  ana.tx->createBranch<std::vector<int>>("t3_occupancies");
  ana.tx->createBranch<int>("tc_occupancies");
}

//________________________________________________________________________________________________________________________________
void setGenJetBranches(LSTEvent* event) {
  //--------------------------------------------
  //
  //
  // Gen Jets
  //
  //
  //--------------------------------------------

  auto const& trk_genjet_eta = trk.getVF("genjet_eta");
  auto const& trk_genjet_phi = trk.getVF("genjet_phi");
  auto const& trk_genjet_pt = trk.getVF("genjet_pt");

  for (unsigned int ijet = 0; ijet < trk_genjet_pt.size(); ++ijet) {
    ana.tx->pushbackToBranch<float>("genjet_eta", trk_genjet_eta[ijet]);
    ana.tx->pushbackToBranch<float>("genjet_phi", trk_genjet_phi[ijet]);
    ana.tx->pushbackToBranch<float>("genjet_pt", trk_genjet_pt[ijet]);
  }
}

//________________________________________________________________________________________________________________________________
unsigned int setSimTrackContainerBranches(LSTEvent* event) {
  //--------------------------------------------
  //
  //
  // Sim Tracks
  //
  //
  //--------------------------------------------

  auto const& trk_sim_pt = trk.getVF("sim_pt");
  auto const& trk_sim_eta = trk.getVF("sim_eta");
  auto const& trk_sim_phi = trk.getVF("sim_phi");
  auto const& trk_sim_bunchCrossing = trk.getVI("sim_bunchCrossing");
  auto const& trk_sim_event = trk.getVI("sim_event");
  auto const& trk_sim_pca_dxy = trk.getVF("sim_pca_dxy");
  auto const& trk_sim_pca_dz = trk.getVF("sim_pca_dz");
  auto const& trk_sim_q = trk.getVI("sim_q");
  auto const& trk_sim_pdgId = trk.getVI("sim_pdgId");
  auto const& trk_sim_parentVtxIdx = trk.getVI("sim_parentVtxIdx");
  auto const& trk_simvtx_x = trk.getVF("simvtx_x");
  auto const& trk_simvtx_y = trk.getVF("simvtx_y");
  auto const& trk_simvtx_z = trk.getVF("simvtx_z");
  auto const& trk_sim_simHitIdx = trk.getVVI("sim_simHitIdx");
  auto const& trk_simhit_subdet = trk.getVUS("simhit_subdet");
  auto const& trk_simhit_layer = trk.getVUS("simhit_layer");
  auto const& trk_simhit_x = trk.getVF("simhit_x");
  auto const& trk_simhit_y = trk.getVF("simhit_y");
  auto const& trk_simhit_z = trk.getVF("simhit_z");
  auto const& trk_simhit_detId = trk.getVU("simhit_detId");
  auto const& trk_simhit_hitIdx = trk.getVVI("simhit_hitIdx");
  auto const& trk_ph2_x = trk.getVF("ph2_x");
  auto const& trk_ph2_y = trk.getVF("ph2_y");
  auto const& trk_ph2_z = trk.getVF("ph2_z");
  auto const& trk_ph2_detId = trk.getVU("ph2_detId");

  // Total number of simulated tracks with the condition that the simulated track came from a particle produced in the hard scattering and from the current bunch-crossing)
  // "accepted" here would mean that in the tracking ntuple (sim_bunchCrossing == 0 and sim_event == 0)
  unsigned int n_accepted_simtrk = 0;

  // Looping over the simulated tracks in the tracking ntuple
  for (unsigned int isimtrk = 0; isimtrk < trk_sim_pt.size(); ++isimtrk) {
    // Skip out-of-time pileup
    if (trk_sim_bunchCrossing[isimtrk] != 0)
      continue;

    // Skip non-hard-scatter
    if (trk_sim_event[isimtrk] != 0)
      continue;

    // Now we have a list of "accepted" tracks (no condition on vtx_z/perp, nor pt, eta etc are applied yet)

    if (ana.jet_branches) {
      auto const& trk_sim_genjet_deltaEta = trk.getVF("sim_genjet_deltaEta");
      auto const& trk_sim_genjet_deltaPhi = trk.getVF("sim_genjet_deltaPhi");
      auto const& trk_sim_genjet_deltaR = trk.getVF("sim_genjet_deltaR");
      auto const& trk_sim_genjet_idx = trk.getVI("sim_genjet_idx");

      ana.tx->pushbackToBranch<float>("sim_genjet_deltaEta", trk_sim_genjet_deltaEta[isimtrk]);
      ana.tx->pushbackToBranch<float>("sim_genjet_deltaPhi", trk_sim_genjet_deltaPhi[isimtrk]);
      ana.tx->pushbackToBranch<float>("sim_genjet_deltaR", trk_sim_genjet_deltaR[isimtrk]);
      ana.tx->pushbackToBranch<int>("sim_genjet_idx", trk_sim_genjet_idx[isimtrk]);
    }

    // Fill the branch with simulated tracks.
    // N.B. these simulated tracks are looser than MTV denominator
    ana.tx->pushbackToBranch<float>("sim_pt", trk_sim_pt[isimtrk]);
    ana.tx->pushbackToBranch<float>("sim_eta", trk_sim_eta[isimtrk]);
    ana.tx->pushbackToBranch<float>("sim_phi", trk_sim_phi[isimtrk]);
    ana.tx->pushbackToBranch<float>("sim_pca_dxy", trk_sim_pca_dxy[isimtrk]);
    ana.tx->pushbackToBranch<float>("sim_pca_dz", trk_sim_pca_dz[isimtrk]);
    ana.tx->pushbackToBranch<int>("sim_q", trk_sim_q[isimtrk]);
    ana.tx->pushbackToBranch<int>("sim_pdgId", trk_sim_pdgId[isimtrk]);

    // For vertex we need to look it up from simvtx info for the given simtrack
    // for each simulated track, there is an index that points to the production vertex
    int vtxidx = trk_sim_parentVtxIdx[isimtrk];
    ana.tx->pushbackToBranch<float>("sim_vx", trk_simvtx_x[vtxidx]);  // using the index we retrieve xyz position
    ana.tx->pushbackToBranch<float>("sim_vy", trk_simvtx_y[vtxidx]);
    ana.tx->pushbackToBranch<float>("sim_vz", trk_simvtx_z[vtxidx]);
    ana.tx->pushbackToBranch<float>(
        "sim_vtxperp", sqrt(trk_simvtx_x[vtxidx] * trk_simvtx_x[vtxidx] + trk_simvtx_y[vtxidx] * trk_simvtx_y[vtxidx]));

    // The trkNtupIdx is the idx in the trackingNtuple
    ana.tx->pushbackToBranch<float>("sim_trkNtupIdx", isimtrk);

    if (ana.extra_sim_branches) {
      // Retrieve some track parameter information so we can build a helix
      float pt = trk_sim_pt[isimtrk];
      float eta = trk_sim_eta[isimtrk];
      float phi = trk_sim_phi[isimtrk];
      float vx = trk_simvtx_x[vtxidx];
      float vy = trk_simvtx_y[vtxidx];
      float vz = trk_simvtx_z[vtxidx];
      float charge = trk_sim_q[isimtrk];

      // Build the helix model. This model is useful to compute some specific expected hits.
      lst_math::Helix helix(pt, eta, phi, vx, vy, vz, charge);

      // Information to keep track of so we can save to output
      std::vector<int> simHitLayer;
      std::vector<float> simHitDistxyHelix;
      std::vector<float> simHitX;
      std::vector<float> simHitY;
      std::vector<float> simHitZ;
      std::vector<int> simHitDetId;
      std::vector<float> recoHitX;
      std::vector<float> recoHitY;
      std::vector<float> recoHitZ;
      std::vector<int> recoHitDetId;
      std::vector<float> simHitLayerMinDistxyHelix(11, 999);

      std::vector<std::vector<int>> simHitIdxs(11);
      float k2Rinv1GeVf = (2.99792458e-3 * 3.8) / 2;

      // Loop over the simhits (truth hits)
      for (size_t isimhit = 0; isimhit < trk_sim_simHitIdx[isimtrk].size(); ++isimhit) {
        // Retrieve the actual index to the simhit_* container of the tracking ntuple
        int isimhitidx = trk_sim_simHitIdx[isimtrk][isimhit];

        // Following computes the distance of the simhit's actual positionin xy to the "expected" xy position based on simhit's z position.
        // i.e. Take simhit's z position -> plug them into helix parametric function to obtain the xy position for that given z.
        // Then compare the computed xy position from the helix to the simhit's actualy xy position.
        // This is a measure of "how off from the original trajectory the simhits are?"
        // For example, if the particle got deflected early on due to material, then the xy position distance would be large.
        float distxyconsistent =
            distxySimHitConsistentWithHelix(helix, isimhitidx, trk_simhit_x, trk_simhit_y, trk_simhit_z);

        // Also retrieve some basic information about the simhit's location (layers, isbarrel?, etc.)
        // subdet == 4 means endcap of the outer tracker, subdet == 5 means barrel of the outer tracker)
        int subdet = trk_simhit_subdet[isimhitidx];
        int is_endcap = subdet == 4;

        // Now compute "logical layer" index
        // N.B. if a hit is in the inner tracker, layer would be staying at layer = 0
        int layer = 0;
        if (subdet == 4 or subdet == 5)  // this is not an outer tracker hit
          // this accounting makes it so that you have layer 1 2 3 4 5 6 in the barrel, and 7 8 9 10 11 in the endcap. (becuase endcap is ph2_subdet == 4)
          layer = trk_simhit_layer[isimhitidx] + 6 * (is_endcap);

        // keep track of isimhits in each layers so we can compute mindistxy from previous hit in previous layer
        if (subdet == 4 or subdet == 5)
          simHitIdxs[layer - 1].push_back(isimhitidx);

        // For this hit, now we push back to the vector that we are keeping track of
        simHitLayer.push_back(layer);
        simHitDistxyHelix.push_back(distxyconsistent);
        simHitX.push_back(trk_simhit_x[isimhitidx]);
        simHitY.push_back(trk_simhit_y[isimhitidx]);
        simHitZ.push_back(trk_simhit_z[isimhitidx]);
        simHitDetId.push_back(trk_simhit_detId[isimhitidx]);

        // Also retrieve all the reco-hits matched to this simhit and also aggregate them
        for (size_t irecohit = 0; irecohit < trk_simhit_hitIdx[isimhitidx].size(); ++irecohit) {
          recoHitX.push_back(trk_ph2_x[trk_simhit_hitIdx[isimhitidx][irecohit]]);
          recoHitY.push_back(trk_ph2_y[trk_simhit_hitIdx[isimhitidx][irecohit]]);
          recoHitZ.push_back(trk_ph2_z[trk_simhit_hitIdx[isimhitidx][irecohit]]);
          recoHitDetId.push_back(trk_ph2_detId[trk_simhit_hitIdx[isimhitidx][irecohit]]);
        }

        // If the given simhit that we are dealing with is not in the outer tracker (i.e. layer == 0. see few lines above.)
        // then, skip this simhit and go to the next hit.
        if (layer == 0)
          continue;

        // If it is a outer tracker hit, then we keep track of out of the 11 layers, what is the minimum "DistxyHelix" (distance to the expected point in the helix in xy)
        // This variable will have a fixed 11 float numbers, and using this to restrict "at least one hit that is not too far from the expected helix" can be useful to select some interesting denominator tracks.
        if (distxyconsistent < simHitLayerMinDistxyHelix[layer - 1]) {
          simHitLayerMinDistxyHelix[layer - 1] = distxyconsistent;
        }
      }

      std::vector<float> simHitLayerMinDistxyHelixPrevHit(11, 999);
      std::vector<float> simHitLayeriSimHitMinDixtxyHelixPrevHit(11, -999);
      // // The algorithm will be to start with the main helix from the sim information and get the isimhit with least distxy.
      // // Then, from that you find the min distxy and repeat
      // for (int ilogicallayer = 0; ilogicallayer < 11; ++ilogicallayer)
      // {
      //     int ilayer = ilogicallayer - 1;
      //     float prev_pt, prev_eta, prev_phi, prev_vx, prev_vy, prev_vz;
      //     if (ilayer == 0)
      //     {
      //         prev_pt = pt;
      //         prev_eta = eta;
      //         prev_phi = phi;
      //         prev_vx = vx;
      //         prev_vy = vy;
      //         prev_vz = vz;
      //     }
      //     else
      //     {
      //         int isimhitidx = simHitLayeriSimHitMinDixtxyHelixPrevHit[ilayer - 1];
      //         TVector3 pp(trk.simhit_px()[isimhitidx], trk.simhit_py()[isimhitidx], trk.simhit_pz()[isimhitidx]);
      //         prev_pt = pp.Pt();
      //         prev_eta = pp.Eta();
      //         prev_phi = pp.Phi();
      //         prev_vx = trk.simhit_x()[isimhitidx];
      //         prev_vy = trk.simhit_y()[isimhitidx];
      //         prev_vz = trk.simhit_z()[isimhitidx];
      //     }
      //     SDLMath::Helix prev_helix(prev_pt, prev_eta, prev_phi, prev_vx, prev_vy, prev_vz, charge);
      //     for (int isimhit = 0; isimhit < simHitIdxs[ilayer].size(); ++isimhit)
      //     {
      //         int isimhitidx = simHitIdxs[ilayer][isimhit];
      //         float distxyconsistent = distxySimHitConsistentWithHelix(prev_helix, isimhitidx);
      //         if (simHitLayerMinDistxyHelixPrevHit[ilayer] > distxyconsistent)
      //         {
      //             simHitLayerMinDistxyHelixPrevHit[ilayer] = distxyconsistent;
      //             simHitLayeriSimHitMinDixtxyHelixPrevHit[ilayer] = isimhitidx;
      //         }
      //     }
      // }

      // Now we fill the branch
      ana.tx->pushbackToBranch<std::vector<int>>("sim_simHitLayer", simHitLayer);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_simHitDistxyHelix", simHitDistxyHelix);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_simHitLayerMinDistxyHelix", simHitLayerMinDistxyHelix);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_simHitLayerMinDistxyPrevHit", simHitLayerMinDistxyHelixPrevHit);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_simHitX", simHitX);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_simHitY", simHitY);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_simHitZ", simHitZ);
      ana.tx->pushbackToBranch<std::vector<int>>("sim_simHitDetId", simHitDetId);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_recoHitX", recoHitX);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_recoHitY", recoHitY);
      ana.tx->pushbackToBranch<std::vector<float>>("sim_recoHitZ", recoHitZ);
      ana.tx->pushbackToBranch<std::vector<int>>("sim_recoHitDetId", recoHitDetId);
    }

    // Increase the counter for accepted simtrk
    n_accepted_simtrk++;
  }

  return n_accepted_simtrk;
}

//________________________________________________________________________________________________________________________________
std::map<unsigned int, unsigned int> setMiniDoubletBranches(LSTEvent* event,
                                                            unsigned int n_accepted_simtrk,
                                                            float matchfrac) {
  //--------------------------------------------
  //
  //
  // Mini-Doublets
  //
  //
  //--------------------------------------------

  auto const& trk_sim_pt = trk.getVF("sim_pt");
  auto const& trk_ph2_subdet = trk.getVUS("ph2_subdet");
  auto const& trk_ph2_layer = trk.getVUS("ph2_layer");
  auto const& trk_ph2_detId = trk.getVU("ph2_detId");
  auto const& trk_simhit_simTrkIdx = trk.getVI("simhit_simTrkIdx");
  auto const& trk_ph2_simHitIdx = trk.getVVI("ph2_simHitIdx");
  auto const& trk_pix_simHitIdx = trk.getVVI("pix_simHitIdx");

  auto const& hitsBase = event->getInput<HitsBaseSoA>();
  auto const& ranges = event->getRanges();
  auto const& miniDoublets = event->getMiniDoublets<MiniDoubletsSoA>();
  auto const& miniDoubletsOccupancy = event->getMiniDoublets<MiniDoubletsOccupancySoA>();

  // Following are some vectors to keep track of the information to write to the ntuple
  // N.B. following two branches have a length for the entire sim track, but what actually will be written in sim_mdIdxAll branch is NOT that long
  // Later in the code, it will restrict to only the ones to write out.
  // The reason at this stage, the entire mdIdxAll is being tracked is to compute duplicate properly later on
  // When computing a duplicate object it is important to consider all simulated tracks including pileup tracks
  int n_total_simtrk = trk_sim_pt.size();
  std::vector<std::vector<int>> sim_mdIdxAll(n_total_simtrk);
  std::vector<std::vector<float>> sim_mdIdxAllFrac(n_total_simtrk);
  std::vector<std::vector<int>> md_simIdxAll;
  std::vector<std::vector<float>> md_simIdxAllFrac;

  // global md index that will be used to keep track of md being outputted to the ntuple
  // each time a md is written out the following will be counted up
  unsigned int md_idx = 0;

  // map to keep track of (GPU mdIdx) -> (md_idx in ntuple output)
  // There is a specific mdIdx used to navigate the GPU array of mini-doublets
  std::map<unsigned int, unsigned int> md_idx_map;

  // First loop over the modules (roughly there are ~13k pair of pt modules); include pixel module (last)
  unsigned int nRanges = miniDoubletsOccupancy.metadata().size();
  for (unsigned int idx = 0; idx < nRanges; ++idx) {
    bool isPLS = idx + 1 == nRanges;
    // For each pt module pair, we loop over mini-doublets created
    for (unsigned int iMD = 0; iMD < miniDoubletsOccupancy.nMDs()[idx]; iMD++) {
      // Compute the specific MD index to access specific spot in the array of GPU memory
      unsigned int mdIdx = ranges.miniDoubletModuleIndices()[idx] + iMD;

      // From that gpu memory index "mdIdx" -> output ntuple's md index is mapped
      // This is useful later when connecting higher level objects to point to specific one in the ntuple
      md_idx_map[mdIdx] = md_idx;

      // Access the list of hits in the mini-doublets (there are only two in this case)
      auto [hit_idx, hit_type] = getHitIdxsAndHitTypesFromMD(event, mdIdx);

      // And then compute matching between simtrack and the mini-doublets
      auto [simidx, simidxfrac] =
          matchedSimTrkIdxsAndFracs(hit_idx, hit_type, trk_simhit_simTrkIdx, trk_ph2_simHitIdx, trk_pix_simHitIdx);

      // Obtain the lower and upper hit information to compute some basic property of the mini-doublets
      unsigned int lowerHitIndex = miniDoublets.anchorHitIndices()[mdIdx];
      unsigned int upperHitIndex = miniDoublets.outerHitIndices()[mdIdx];
      unsigned int hit0 = hitsBase.idxs()[lowerHitIndex];
      unsigned int hit1 = hitsBase.idxs()[upperHitIndex];
      float anchor_x = hitsBase.xs()[lowerHitIndex];
      float anchor_y = hitsBase.ys()[lowerHitIndex];
      float anchor_z = hitsBase.zs()[lowerHitIndex];
      float other_x = hitsBase.xs()[upperHitIndex];
      float other_y = hitsBase.ys()[upperHitIndex];
      float other_z = hitsBase.zs()[upperHitIndex];

      // Construct the anchor hit 3 vector
      lst_math::Hit anchor_hit(anchor_x, anchor_y, anchor_z, lowerHitIndex);

      // Pt is computed via dphichange and the eta and phi are computed based on anchor hit
      float dphichange = miniDoublets.dphichanges()[mdIdx];
      float dphi = miniDoublets.dphis()[mdIdx];
      float dz = miniDoublets.dzs()[mdIdx];
      float k2Rinv1GeVf = (2.99792458e-3 * 3.8) / 2;
      float pt = anchor_hit.rt() * k2Rinv1GeVf / sin(dphichange);
      float eta = anchor_hit.eta();
      float phi = anchor_hit.phi();

      // Obtain where the actual hit is located in terms of their layer, module, rod, and ring number
      int subdet = isPLS ? 0 : trk_ph2_subdet[hit0];
      int is_endcap = subdet == 4;
      // this accounting makes it so that you have layer 1 2 3 4 5 6 in the barrel, and 7 8 9 10 11 in the endcap. (becuase endcap is ph2_subdet == 4)
      int layer = isPLS ? 0 : trk_ph2_layer[hit0] + 6 * (is_endcap);
      int detId = isPLS ? kPixelModuleId : trk_ph2_detId[hit0];
      // See https://github.com/SegmentLinking/TrackLooper/blob/158804cab7fd0976264a7bc4cee236f4986328c2/SDL/Module.cc and Module.h
      int ring = isPLS ? 0 : (detId & (15 << 12)) >> 12;
      int isPS = isPLS ? 0 : (is_endcap ? (layer <= 2 ? ring <= 10 : ring <= 7) : layer <= 3);

      // Write out the ntuple
#ifdef CUT_VALUE_DEBUG
      ana.tx->pushbackToBranch<int>("md_rawIdx", mdIdx);
#endif
      ana.tx->pushbackToBranch<bool>("md_isPLS", isPLS);
      ana.tx->pushbackToBranch<float>("md_pt", pt);
      ana.tx->pushbackToBranch<float>("md_eta", eta);
      ana.tx->pushbackToBranch<float>("md_phi", phi);
#ifdef CUT_VALUE_DEBUG
      ana.tx->pushbackToBranch<float>("md_dphichange", dphichange);
      ana.tx->pushbackToBranch<float>("md_dphi", dphi);
      ana.tx->pushbackToBranch<float>("md_dz", dz);
#endif
      ana.tx->pushbackToBranch<float>("md_anchor_x", anchor_x);
      ana.tx->pushbackToBranch<float>("md_anchor_y", anchor_y);
      ana.tx->pushbackToBranch<float>("md_anchor_z", anchor_z);
      ana.tx->pushbackToBranch<float>("md_other_x", other_x);
      ana.tx->pushbackToBranch<float>("md_other_y", other_y);
      ana.tx->pushbackToBranch<float>("md_other_z", other_z);
      ana.tx->pushbackToBranch<int>("md_anchorHitIdx", hit0);
      ana.tx->pushbackToBranch<int>("md_otherHitIdx", hit1);
      ana.tx->pushbackToBranch<int>("md_type", isPS);
      ana.tx->pushbackToBranch<int>("md_layer", layer);
      ana.tx->pushbackToBranch<int>("md_detId", detId);

      // Compute whether this is a fake
      bool isfake = true;
      for (size_t isim = 0; isim < simidx.size(); ++isim) {
        if (simidxfrac[isim] > matchfrac) {
          isfake = false;
          break;
        }
      }
      ana.tx->pushbackToBranch<int>("md_isFake", isfake);

      // For this md, keep track of all the simidx that are matched
      md_simIdxAll.push_back(simidx);
      md_simIdxAllFrac.push_back(simidxfrac);

      // The book keeping of opposite mapping is done here
      // For each matched sim idx, we go back and keep track of which obj it is matched to.
      // Loop over all the matched sim idx
      for (size_t is = 0; is < simidx.size(); ++is) {
        // For this matched sim index keep track (sim -> md) mapping
        int sim_idx = simidx.at(is);
        float sim_idx_frac = simidxfrac.at(is);
        sim_mdIdxAll.at(sim_idx).push_back(md_idx);
        sim_mdIdxAllFrac.at(sim_idx).push_back(sim_idx_frac);
      }

      // Also, among the simidx matches, find the best match (highest fractional match)
      // N.B. the simidx is already returned sorted by highest number of "nhits" match
      // So as it loops over, the condition will ensure that the highest fraction with highest nhits will be matched with the priority given to highest fraction
      int md_simIdx = -999;
      float md_simIdxBestFrac = 0;
      for (size_t isim = 0; isim < simidx.size(); ++isim) {
        int thisidx = simidx[isim];
        float thisfrac = simidxfrac[isim];
        if (thisfrac > md_simIdxBestFrac and thisfrac > matchfrac) {
          md_simIdxBestFrac = thisfrac;
          md_simIdx = thisidx;
        }
      }

      // the best match index will then be saved here
      ana.tx->pushbackToBranch<int>("md_simIdx", md_simIdx);

      // Count up the md_idx
      md_idx++;
    }
  }

  // Now save the (obj -> simidx) mapping
  ana.tx->setBranch<std::vector<std::vector<int>>>("md_simIdxAll", md_simIdxAll);
  ana.tx->setBranch<std::vector<std::vector<float>>>("md_simIdxAllFrac", md_simIdxAllFrac);

  // Not all (sim->objIdx) will be saved but only for the sim that is from hard scatter and current bunch crossing
  // So a restriction up to only "n_accepted_simtrk" done by chopping off the rest
  // N.B. the reason we can simply take the first "n_accepted_simtrk" is because the tracking ntuple is organized such that those sim tracks show up on the first "n_accepted_simtrk" of tracks.
  std::vector<std::vector<int>> sim_mdIdxAll_to_write;
  std::vector<std::vector<float>> sim_mdIdxAllFrac_to_write;
  std::copy(sim_mdIdxAll.begin(), sim_mdIdxAll.begin() + n_accepted_simtrk, std::back_inserter(sim_mdIdxAll_to_write));
  std::copy(sim_mdIdxAllFrac.begin(),
            sim_mdIdxAllFrac.begin() + n_accepted_simtrk,
            std::back_inserter(sim_mdIdxAllFrac_to_write));
  ana.tx->setBranch<std::vector<std::vector<int>>>("sim_mdIdxAll", sim_mdIdxAll_to_write);
  ana.tx->setBranch<std::vector<std::vector<float>>>("sim_mdIdxAllFrac", sim_mdIdxAllFrac_to_write);

  return md_idx_map;
}

//________________________________________________________________________________________________________________________________
std::map<unsigned int, unsigned int> setLineSegmentBranches(LSTEvent* event,
                                                            unsigned int n_accepted_simtrk,
                                                            float matchfrac,
                                                            std::map<unsigned int, unsigned int> const& md_idx_map) {
  //--------------------------------------------
  //
  //
  // Line Segments
  //
  //
  //--------------------------------------------

  auto const& trk_sim_pt = trk.getVF("sim_pt");
  auto const& trk_simhit_simTrkIdx = trk.getVI("simhit_simTrkIdx");
  auto const& trk_ph2_simHitIdx = trk.getVVI("ph2_simHitIdx");
  auto const& trk_pix_simHitIdx = trk.getVVI("pix_simHitIdx");

  auto const& hitsBase = event->getInput<HitsBaseSoA>();
  auto const& ranges = event->getRanges();
  auto const& segments = event->getSegments<SegmentsSoA>();
  auto const& segmentsOccupancy = event->getSegments<SegmentsOccupancySoA>();

  // Following are some vectors to keep track of the information to write to the ntuple
  // N.B. following two branches have a length for the entire sim track, but what actually will be written in sim_objIdxAll branch is NOT that long
  // Later in the code, it will restrict to only the ones to write out.
  // The reason at this stage, the entire objIdxAll is being tracked is to compute duplicate properly later on
  // When computing a duplicate object it is important to consider all simulated tracks including pileup tracks
  int n_total_simtrk = trk_sim_pt.size();
  std::vector<std::vector<int>> sim_lsIdxAll(n_total_simtrk);
  std::vector<std::vector<float>> sim_lsIdxAllFrac(n_total_simtrk);
  std::vector<std::vector<int>> ls_simIdxAll;
  std::vector<std::vector<float>> ls_simIdxAllFrac;

  // global index that will be used to keep track of obj being outputted to the ntuple
  // each time a obj is written out the following will be counted up
  unsigned int ls_idx = 0;

  // map to keep track of (GPU objIdx) -> (obj_idx in ntuple output)
  // There is a specific objIdx used to navigate the GPU array of mini-doublets
  std::map<unsigned int, unsigned int> ls_idx_map;

  // First loop over the modules (roughly there are ~13k pair of pt modules); include pixel module (last)
  unsigned int nRanges = segmentsOccupancy.metadata().size();
  for (unsigned int idx = 0; idx < nRanges; ++idx) {
    bool isPLS = idx + 1 == nRanges;
    // For each pt module pair, we loop over objects created
    for (unsigned int iLS = 0; iLS < segmentsOccupancy.nSegments()[idx]; iLS++) {
      // Compute the specific obj index to access specific spot in the array of GPU memory
      unsigned int lsIdx = ranges.segmentModuleIndices()[idx] + iLS;

      // From that gpu memory index "objIdx" -> output ntuple's obj index is mapped
      // This is useful later when connecting higher level objects to point to specific one in the ntuple
      ls_idx_map[lsIdx] = ls_idx;

      // Access the list of hits in the objects (there are only two in this case)
      auto [hit_idx, hit_type] = getHitIdxsAndHitTypesFromLS(event, lsIdx);

      // And then compute matching between simtrack and the objects
      auto [simidx, simidxfrac] =
          matchedSimTrkIdxsAndFracs(hit_idx, hit_type, trk_simhit_simTrkIdx, trk_ph2_simHitIdx, trk_pix_simHitIdx);
      std::vector<unsigned int> mdIdxs = getMDsFromLS(event, lsIdx);

      // Computing line segment pt estimate (assuming beam spot is at zero)
      lst_math::Hit hitA(0, 0, 0);
      lst_math::Hit hitB(hitsBase.xs()[hit_idx[0]], hitsBase.ys()[hit_idx[0]], hitsBase.zs()[hit_idx[0]]);
      lst_math::Hit hitC(hitsBase.xs()[hit_idx[2]], hitsBase.ys()[hit_idx[2]], hitsBase.zs()[hit_idx[2]]);
      lst_math::Hit center = lst_math::getCenterFromThreePoints(hitA, hitB, hitC);
      float pt = lst_math::ptEstimateFromRadius(center.rt());
      float eta = hitC.eta();
      float phi = hitB.phi();

#ifdef CUT_VALUE_DEBUG
      float zHi = segments.zHis()[lsIdx];
      float zLo = segments.zLos()[lsIdx];
      float rtHi = segments.rtHis()[lsIdx];
      float rtLo = segments.rtLos()[lsIdx];
      float dAlphaInner = segments.dAlphaInners()[lsIdx];
      float dAlphaOuter = segments.dAlphaOuters()[lsIdx];
      float dAlphaInnerOuter = segments.dAlphaInnerOuters()[lsIdx];
      float dPhi = segments.dPhis()[lsIdx];
      float dPhiMin = segments.dPhiMins()[lsIdx];
      float dPhiMax = segments.dPhiMaxs()[lsIdx];
      float dPhiChange = segments.dPhiChanges()[lsIdx];
      float dPhiChangeMin = segments.dPhiChangeMins()[lsIdx];
      float dPhiChangeMax = segments.dPhiChangeMaxs()[lsIdx];
#endif

      // Write out the ntuple
#ifdef CUT_VALUE_DEBUG
      ana.tx->pushbackToBranch<int>("ls_rawIdx", lsIdx);
#endif
      ana.tx->pushbackToBranch<bool>("ls_isPLS", isPLS);
      ana.tx->pushbackToBranch<float>("ls_pt", pt);
      ana.tx->pushbackToBranch<float>("ls_eta", eta);
      ana.tx->pushbackToBranch<float>("ls_phi", phi);
#ifdef CUT_VALUE_DEBUG
      ana.tx->pushbackToBranch<float>("ls_zHis", zHi);
      ana.tx->pushbackToBranch<float>("ls_zLos", zLo);
      ana.tx->pushbackToBranch<float>("ls_rtHis", rtHi);
      ana.tx->pushbackToBranch<float>("ls_rtLos", rtLo);
      ana.tx->pushbackToBranch<float>("ls_dPhis", dPhi);
      ana.tx->pushbackToBranch<float>("ls_dPhiMins", dPhiMin);
      ana.tx->pushbackToBranch<float>("ls_dPhiMaxs", dPhiMax);
      ana.tx->pushbackToBranch<float>("ls_dPhiChanges", dPhiChange);
      ana.tx->pushbackToBranch<float>("ls_dPhiChangeMins", dPhiChangeMin);
      ana.tx->pushbackToBranch<float>("ls_dPhiChangeMaxs", dPhiChangeMax);
      ana.tx->pushbackToBranch<float>("ls_dAlphaInners", dAlphaInner);
      ana.tx->pushbackToBranch<float>("ls_dAlphaOuters", dAlphaOuter);
      ana.tx->pushbackToBranch<float>("ls_dAlphaInnerOuters", dAlphaInnerOuter);

#endif
      if (ana.md_branches) {
        ana.tx->pushbackToBranch<int>("ls_mdIdx0", md_idx_map.at(mdIdxs[0]));
        ana.tx->pushbackToBranch<int>("ls_mdIdx1", md_idx_map.at(mdIdxs[1]));
      }

      // Compute whether this is a fake
      bool isfake = true;
      for (size_t isim = 0; isim < simidx.size(); ++isim) {
        if (simidxfrac[isim] > matchfrac) {
          isfake = false;
          break;
        }
      }
      ana.tx->pushbackToBranch<int>("ls_isFake", isfake);

      // For this obj, keep track of all the simidx that are matched
      ls_simIdxAll.push_back(simidx);
      ls_simIdxAllFrac.push_back(simidxfrac);

      // The book keeping of opposite mapping is done here
      // For each matched sim idx, we go back and keep track of which obj it is matched to.
      // Loop over all the matched sim idx
      for (size_t is = 0; is < simidx.size(); ++is) {
        int sim_idx = simidx.at(is);
        float sim_idx_frac = simidxfrac.at(is);
        if (sim_idx < n_total_simtrk) {
          sim_lsIdxAll.at(sim_idx).push_back(ls_idx);
          sim_lsIdxAllFrac.at(sim_idx).push_back(sim_idx_frac);
        }
      }

      // Also, among the simidx matches, find the best match (highest fractional match)
      // N.B. the simidx is already returned sorted by highest number of "nhits" match
      // So as it loops over, the condition will ensure that the highest fraction with highest nhits will be matched with the priority given to highest fraction
      int ls_simIdx = -999;
      float ls_simIdxBestFrac = 0;
      for (size_t isim = 0; isim < simidx.size(); ++isim) {
        int thisidx = simidx[isim];
        float thisfrac = simidxfrac[isim];
        if (thisfrac > ls_simIdxBestFrac and thisfrac > matchfrac) {
          ls_simIdxBestFrac = thisfrac;
          ls_simIdx = thisidx;
        }
      }

      // the best match index will then be saved here
      ana.tx->pushbackToBranch<int>("ls_simIdx", ls_simIdx);

      // Count up the index
      ls_idx++;
    }
  }

  // Now save the (obj -> simidx) mapping
  ana.tx->setBranch<std::vector<std::vector<int>>>("ls_simIdxAll", ls_simIdxAll);
  ana.tx->setBranch<std::vector<std::vector<float>>>("ls_simIdxAllFrac", ls_simIdxAllFrac);

  // Not all (sim->objIdx) will be saved but only for the sim that is from hard scatter and current bunch crossing
  // So a restriction up to only "n_accepted_simtrk" done by chopping off the rest
  // N.B. the reason we can simply take the first "n_accepted_simtrk" is because the tracking ntuple is organized such that those sim tracks show up on the first "n_accepted_simtrk" of tracks.
  std::vector<std::vector<int>> sim_lsIdxAll_to_write;
  std::vector<std::vector<float>> sim_lsIdxAllFrac_to_write;
  std::copy(sim_lsIdxAll.begin(), sim_lsIdxAll.begin() + n_accepted_simtrk, std::back_inserter(sim_lsIdxAll_to_write));
  std::copy(sim_lsIdxAllFrac.begin(),
            sim_lsIdxAllFrac.begin() + n_accepted_simtrk,
            std::back_inserter(sim_lsIdxAllFrac_to_write));
  ana.tx->setBranch<std::vector<std::vector<int>>>("sim_lsIdxAll", sim_lsIdxAll_to_write);
  ana.tx->setBranch<std::vector<std::vector<float>>>("sim_lsIdxAllFrac", sim_lsIdxAllFrac_to_write);

  return ls_idx_map;
}

//________________________________________________________________________________________________________________________________
std::map<unsigned int, unsigned int> setTripletBranches(LSTEvent* event,
                                                        unsigned int n_accepted_simtrk,
                                                        float matchfrac,
                                                        std::map<unsigned int, unsigned int> const& ls_idx_map) {
  //--------------------------------------------
  //
  //
  // Triplet
  //
  //
  //--------------------------------------------

  auto const& trk_sim_pt = trk.getVF("sim_pt");
  auto const& trk_simhit_simTrkIdx = trk.getVI("simhit_simTrkIdx");
  auto const& trk_ph2_simHitIdx = trk.getVVI("ph2_simHitIdx");
  auto const& trk_pix_simHitIdx = trk.getVVI("pix_simHitIdx");

  auto const& hitsBase = event->getInput<HitsBaseSoA>();
  auto const& ranges = event->getRanges();
  auto const& triplets = event->getTriplets<TripletsSoA>();
  auto const& tripletOccupancies = event->getTriplets<TripletsOccupancySoA>();

  int n_total_simtrk = trk_sim_pt.size();
  std::vector<int> sim_t3_matched(n_accepted_simtrk);
  std::vector<std::vector<int>> sim_t3IdxAll(n_total_simtrk);
  std::vector<std::vector<float>> sim_t3IdxAllFrac(n_total_simtrk);
  std::vector<std::vector<int>> t3_simIdxAll;
  std::vector<std::vector<float>> t3_simIdxAllFrac;
  // Then obtain the lower module index
  unsigned int t3_idx = 0;  // global t3 index that will be used to keep track of t3 being outputted to the ntuple
  // map to keep track of (GPU t3Idx) -> (t3_idx in ntuple output)
  std::map<unsigned int, unsigned int> t3_idx_map;
  // printT3s(event);
  unsigned int nRanges = tripletOccupancies.metadata().size();
  for (unsigned int idx = 0; idx < nRanges; ++idx) {
    for (unsigned int iT3 = 0; iT3 < tripletOccupancies.nTriplets()[idx]; iT3++) {
      unsigned int t3Idx = ranges.tripletModuleIndices()[idx] + iT3;
#ifdef CUT_VALUE_DEBUG
      ana.tx->pushbackToBranch<int>("t3_rawIdx", t3Idx);
#endif
      t3_idx_map[t3Idx] = t3_idx;
      auto [hit_idx, hit_type] = getHitIdxsAndHitTypesFromT3(event, t3Idx);
      auto [simidx, simidxfrac] =
          matchedSimTrkIdxsAndFracs(hit_idx, hit_type, trk_simhit_simTrkIdx, trk_ph2_simHitIdx, trk_pix_simHitIdx);
      std::vector<unsigned int> lsIdxs = getLSsFromT3(event, t3Idx);
      if (ana.ls_branches) {
        ana.tx->pushbackToBranch<int>("t3_lsIdx0", ls_idx_map.at(lsIdxs[0]));
        ana.tx->pushbackToBranch<int>("t3_lsIdx1", ls_idx_map.at(lsIdxs[1]));
      }
      // Computing line segment pt estimate (assuming beam spot is at zero)
      lst_math::Hit hitA(hitsBase.xs()[hit_idx[0]], hitsBase.ys()[hit_idx[0]], hitsBase.zs()[hit_idx[0]]);
      lst_math::Hit hitC(hitsBase.xs()[hit_idx[4]], hitsBase.ys()[hit_idx[4]], hitsBase.zs()[hit_idx[4]]);
      float pt = __H2F(triplets.radius()[t3Idx]) * k2Rinv1GeVf * 2;
      float eta = hitC.eta();
      float phi = hitA.phi();
      ana.tx->pushbackToBranch<float>("t3_pt", pt);
      ana.tx->pushbackToBranch<float>("t3_eta", eta);
      ana.tx->pushbackToBranch<float>("t3_phi", phi);
      bool isfake = true;
      for (size_t isim = 0; isim < simidx.size(); ++isim) {
        if (simidxfrac[isim] > matchfrac) {
          isfake = false;
          break;
        }
      }
      ana.tx->pushbackToBranch<int>("t3_isFake", isfake);
      t3_simIdxAll.push_back(simidx);
      t3_simIdxAllFrac.push_back(simidxfrac);
      for (size_t is = 0; is < simidx.size(); ++is) {
        int sim_idx = simidx.at(is);
        if (sim_idx < n_accepted_simtrk) {
          sim_t3_matched.at(sim_idx) += 1;
        }
        float sim_idx_frac = simidxfrac.at(is);
        if (sim_idx < n_total_simtrk) {
          sim_t3IdxAll.at(sim_idx).push_back(t3_idx);
          sim_t3IdxAllFrac.at(sim_idx).push_back(sim_idx_frac);
        }
      }
      int t3_simIdx = -999;
      float t3_simIdxBestFrac = 0;
      for (size_t isim = 0; isim < simidx.size(); ++isim) {
        int thisidx = simidx[isim];
        float thisfrac = simidxfrac[isim];
        if (thisfrac > t3_simIdxBestFrac and thisfrac > matchfrac) {
          t3_simIdxBestFrac = thisfrac;
          t3_simIdx = thisidx;
        }
      }
      ana.tx->pushbackToBranch<int>("t3_simIdx", t3_simIdx);
      // count global
      t3_idx++;
    }
  }
  ana.tx->setBranch<std::vector<std::vector<int>>>("t3_simIdxAll", t3_simIdxAll);
  ana.tx->setBranch<std::vector<std::vector<float>>>("t3_simIdxAllFrac", t3_simIdxAllFrac);
  std::vector<std::vector<int>> sim_t3IdxAll_to_write;
  std::vector<std::vector<float>> sim_t3IdxAllFrac_to_write;
  std::copy(sim_t3IdxAll.begin(), sim_t3IdxAll.begin() + n_accepted_simtrk, std::back_inserter(sim_t3IdxAll_to_write));
  std::copy(sim_t3IdxAllFrac.begin(),
            sim_t3IdxAllFrac.begin() + n_accepted_simtrk,
            std::back_inserter(sim_t3IdxAllFrac_to_write));
  ana.tx->setBranch<std::vector<std::vector<int>>>("sim_t3IdxAll", sim_t3IdxAll_to_write);
  ana.tx->setBranch<std::vector<std::vector<float>>>("sim_t3IdxAllFrac", sim_t3IdxAllFrac_to_write);

  // Using the intermedaite variables to compute whether a given object is a duplicate
  std::vector<int> t3_isDuplicate(t3_simIdxAll.size());
  for (unsigned int i = 0; i < t3_simIdxAll.size(); i++) {
    bool isDuplicate = true;
    for (unsigned int isim = 0; isim < t3_simIdxAll[i].size(); isim++) {
      int simidx = t3_simIdxAll[i][isim];
      if (simidx < n_accepted_simtrk) {
        if (sim_t3_matched[simidx] > 1) {
          isDuplicate = true;
        }
      }
    }
    t3_isDuplicate[i] = isDuplicate;
  }
  ana.tx->setBranch<std::vector<int>>("t3_isDuplicate", t3_isDuplicate);

  return t3_idx_map;
}

//________________________________________________________________________________________________________________________________
std::map<unsigned int, unsigned int> setPixelLineSegmentBranches(
    LSTEvent* event,
    unsigned int n_accepted_simtrk,
    float matchfrac,
    std::map<unsigned int, unsigned int> const& ls_idx_map) {
  //--------------------------------------------
  //
  //
  // pLS
  //
  //
  //--------------------------------------------

  auto const& trk_sim_pt = trk.getVF("sim_pt");
  auto const& trk_see_pt = trk.getVF("see_pt");
  auto const& trk_see_eta = trk.getVF("see_eta");
  auto const& trk_see_phi = trk.getVF("see_phi");
  auto const& trk_see_hitIdx = trk.getVVI("see_hitIdx");
  auto const& trk_see_hitType = trk.getVVI("see_hitType");
  auto const& trk_pix_x = trk.getVF("pix_x");
  auto const& trk_pix_y = trk.getVF("pix_y");
  auto const& trk_pix_z = trk.getVF("pix_z");
  auto const& trk_ph2_x = trk.getVF("ph2_x");
  auto const& trk_ph2_y = trk.getVF("ph2_y");
  auto const& trk_ph2_z = trk.getVF("ph2_z");
  auto const& trk_simhit_simTrkIdx = trk.getVI("simhit_simTrkIdx");
  auto const& trk_ph2_simHitIdx = trk.getVVI("ph2_simHitIdx");
  auto const& trk_pix_simHitIdx = trk.getVVI("pix_simHitIdx");

  auto const& ranges = event->getRanges();
  auto const& pixelSeeds = event->getInput<PixelSeedsSoA>();
  auto const& pixelSegments = event->getPixelSegments();
  auto const& segmentsOccupancy = event->getSegments<SegmentsOccupancySoA>();

  int n_total_simtrk = trk_sim_pt.size();
  std::vector<int> sim_pLS_matched(n_accepted_simtrk, 0);
  std::vector<std::vector<int>> sim_plsIdxAll(n_total_simtrk);
  std::vector<std::vector<float>> sim_plsIdxAllFrac(n_total_simtrk);
  std::vector<std::vector<int>> pls_simIdxAll;
  std::vector<std::vector<float>> pls_simIdxAllFrac;
  // Then obtain the lower module index
  unsigned int pls_idx = 0;  // global pls index that will be used to keep track of pls being outputted to the ntuple
  // map to keep track of (GPU plsIdx) -> (pls_idx in ntuple output)
  std::map<unsigned int, unsigned int> pls_idx_map;
  unsigned int pixelModule = segmentsOccupancy.metadata().size() - 1;
  unsigned int n_pls = segmentsOccupancy.nSegments()[pixelModule];
  unsigned int pls_range_start = ranges.segmentModuleIndices()[pixelModule];
  for (unsigned int ipLS = 0; ipLS < n_pls; ipLS++) {
#ifdef CUT_VALUE_DEBUG
    ana.tx->pushbackToBranch<int>("pLS_rawIdx", ipLS);
#endif
    unsigned int plsIdx = pls_range_start + ipLS;
    pls_idx_map[plsIdx] = pls_idx;
    auto [hit_idx, hit_type] = getHitIdxsAndHitTypesFrompLS(event, ipLS);
    auto [simidx, simidxfrac] =
        matchedSimTrkIdxsAndFracs(hit_idx, hit_type, trk_simhit_simTrkIdx, trk_ph2_simHitIdx, trk_pix_simHitIdx);
    if (ana.ls_branches)
      ana.tx->pushbackToBranch<int>("pLS_lsIdx", ls_idx_map.at(plsIdx));
    ana.tx->pushbackToBranch<float>("pLS_pt", pixelSeeds.ptIn()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_ptErr", pixelSeeds.ptErr()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_eta", pixelSeeds.eta()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_etaErr", pixelSeeds.etaErr()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_phi", pixelSeeds.phi()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_circleCenterX", pixelSegments.circleCenterX()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_circleCenterY", pixelSegments.circleCenterY()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_circleRadius", pixelSegments.circleRadius()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_px", pixelSeeds.px()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_py", pixelSeeds.py()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_pz", pixelSeeds.pz()[ipLS]);
    ana.tx->pushbackToBranch<bool>("pLS_isQuad", static_cast<bool>(pixelSeeds.isQuad()[ipLS]));
    ana.tx->pushbackToBranch<int>("pLS_charge", pixelSeeds.charge()[ipLS]);
    ana.tx->pushbackToBranch<float>("pLS_deltaPhi", pixelSeeds.deltaPhi()[ipLS]);
    // The three algorithmic isDup snapshots and the self-cleaning tie-break score. The snapshot
    // vectors are empty unless LST_DUP_SNAPSHOTS was set; -999 then marks "not recorded", so an
    // ntuple written without the snapshots can never be mistaken for a measured zero.
    ana.tx->pushbackToBranch<int>(
        "pLS_isDupAlgSelf", ipLS < event->plsIsDupSelf_.size() ? static_cast<int>(event->plsIsDupSelf_[ipLS]) : -999);
    ana.tx->pushbackToBranch<int>(
        "pLS_isDupAlgPass2",
        ipLS < event->plsIsDupPass2_.size() ? static_cast<int>(event->plsIsDupPass2_[ipLS]) : -999);
    ana.tx->pushbackToBranch<int>(
        "pLS_isDupAlgFinal",
        ipLS < event->plsIsDupFinal_.size() ? static_cast<int>(event->plsIsDupFinal_[ipLS]) : -999);
    ana.tx->pushbackToBranch<float>("pLS_score", pixelSegments.score()[ipLS]);
    ana.tx->pushbackToBranch<int>("pLS_nhit", hit_idx.size());
    unsigned int seedIdx = pixelSeeds.seedIdx()[ipLS];
    ana.tx->pushbackToBranch<int>("pLS_seedIdx", seedIdx);
    for (size_t ihit = 0; ihit < trk_see_hitIdx[seedIdx].size() && ihit < lst::Params_pLS::kHits; ++ihit) {
      int hitidx = trk_see_hitIdx[seedIdx][ihit];
      bool isPixel = static_cast<HitType>(trk_see_hitType[seedIdx][ihit]) == HitType::Pixel;
      auto const& x = isPixel ? trk_pix_x[hitidx] : trk_ph2_x[hitidx];
      auto const& y = isPixel ? trk_pix_y[hitidx] : trk_ph2_y[hitidx];
      auto const& z = isPixel ? trk_pix_z[hitidx] : trk_ph2_z[hitidx];
      ana.tx->pushbackToBranch<float>(TString::Format("pLS_hit%zu_x", ihit), x);
      ana.tx->pushbackToBranch<float>(TString::Format("pLS_hit%zu_y", ihit), y);
      ana.tx->pushbackToBranch<float>(TString::Format("pLS_hit%zu_z", ihit), z);
    }
    if (trk_see_hitIdx[seedIdx].size() == 3) {
      ana.tx->pushbackToBranch<float>("pLS_hit3_x", -999);
      ana.tx->pushbackToBranch<float>("pLS_hit3_y", -999);
      ana.tx->pushbackToBranch<float>("pLS_hit3_z", -999);
    }
    bool isfake = true;
    for (size_t isim = 0; isim < simidx.size(); ++isim) {
      if (simidxfrac[isim] > matchfrac) {
        isfake = false;
        break;
      }
    }
    ana.tx->pushbackToBranch<int>("pLS_isFake", isfake);
    pls_simIdxAll.push_back(simidx);
    pls_simIdxAllFrac.push_back(simidxfrac);
    for (size_t is = 0; is < simidx.size(); ++is) {
      int sim_idx = simidx.at(is);
      if (sim_idx < n_accepted_simtrk) {
        sim_pLS_matched[sim_idx]++;
      }
      float sim_idx_frac = simidxfrac.at(is);
      if (sim_idx < n_total_simtrk) {
        sim_plsIdxAll.at(sim_idx).push_back(pls_idx);
        sim_plsIdxAllFrac.at(sim_idx).push_back(sim_idx_frac);
      }
    }
    int pls_simIdx = -999;
    float pls_simIdxBestFrac = 0;
    for (size_t isim = 0; isim < simidx.size(); ++isim) {
      int thisidx = simidx[isim];
      float thisfrac = simidxfrac[isim];
      if (thisfrac > pls_simIdxBestFrac and thisfrac > matchfrac) {
        pls_simIdxBestFrac = thisfrac;
        pls_simIdx = thisidx;
      }
    }
    ana.tx->pushbackToBranch<int>("pLS_simIdx", pls_simIdx);
    // count global
    pls_idx++;
  }
  ana.tx->setBranch<std::vector<std::vector<int>>>("pLS_simIdxAll", pls_simIdxAll);
  ana.tx->setBranch<std::vector<std::vector<float>>>("pLS_simIdxAllFrac", pls_simIdxAllFrac);
  std::vector<std::vector<int>> sim_plsIdxAll_to_write;
  std::vector<std::vector<float>> sim_plsIdxAllFrac_to_write;
  std::copy(
      sim_plsIdxAll.begin(), sim_plsIdxAll.begin() + n_accepted_simtrk, std::back_inserter(sim_plsIdxAll_to_write));
  std::copy(sim_plsIdxAllFrac.begin(),
            sim_plsIdxAllFrac.begin() + n_accepted_simtrk,
            std::back_inserter(sim_plsIdxAllFrac_to_write));
  ana.tx->setBranch<std::vector<std::vector<int>>>("sim_plsIdxAll", sim_plsIdxAll_to_write);
  ana.tx->setBranch<std::vector<std::vector<float>>>("sim_plsIdxAllFrac", sim_plsIdxAllFrac_to_write);

  std::vector<int> pLS_isDuplicate(pls_simIdxAll.size(), 0);
  for (size_t i = 0; i < pls_simIdxAll.size(); ++i) {
    for (int simidx : pls_simIdxAll[i]) {
      if (simidx < n_accepted_simtrk && sim_pLS_matched[simidx] > 1) {
        pLS_isDuplicate[i] = 1;
        break;
      }
    }
  }
  ana.tx->setBranch<std::vector<int>>("pLS_isDuplicate", pLS_isDuplicate);

  return pls_idx_map;
}

//________________________________________________________________________________________________________________________________
void setTrackCandidateBranches(LSTEvent* event,
                               unsigned int n_accepted_simtrk,
                               std::map<unsigned int, unsigned int> pls_idx_map,
                               float matchfrac) {
  //--------------------------------------------
  //
  //
  // Track Candidates
  //
  //
  //--------------------------------------------

  auto const& trk_sim_pt = trk.getVF("sim_pt");
  auto const& trk_ph2_x = trk.getVF("ph2_x");
  auto const& trk_ph2_y = trk.getVF("ph2_y");
  auto const& trk_ph2_z = trk.getVF("ph2_z");
  auto const& trk_simhit_simTrkIdx = trk.getVI("simhit_simTrkIdx");
  auto const& trk_ph2_simHitIdx = trk.getVVI("ph2_simHitIdx");
  auto const& trk_pix_simHitIdx = trk.getVVI("pix_simHitIdx");

  auto const& ranges = event->getRanges();
  auto const& modules = event->getModules<ModulesSoA>();
  auto const& trackCandidatesBase = event->getTrackCandidatesBase();
  auto const& trackCandidatesExtended = event->getTrackCandidatesExtended();

  // Following are some vectors to keep track of the information to write to the ntuple
  // N.B. following two branches have a length for the entire sim track, but what actually will be written in sim_tcIdxAll branch is NOT that long
  // Later in the code, it will restrict to only the ones to write out.
  // The reason at this stage, the entire tcIdxAll is being tracked is to compute duplicate properly later on
  // When computing a duplicate object it is important to consider all simulated tracks including pileup tracks
  int n_total_simtrk = trk_sim_pt.size();
  std::vector<std::vector<int>> sim_tcIdxAll(n_total_simtrk);
  std::vector<std::vector<float>> sim_tcIdxAllFrac(n_total_simtrk);
  std::vector<std::vector<int>> tc_simIdxAll;
  std::vector<std::vector<float>> tc_simIdxAllFrac;

  // Number of total track candidates created in this event
  unsigned int nTrackCandidates = trackCandidatesBase.nTrackCandidates();

  // Looping over each track candidate
  for (unsigned int tc_idx = 0; tc_idx < nTrackCandidates; tc_idx++) {
    // Compute reco quantities of track candidate based on final object
    // The following function reads off and computes the matched sim track indices
    float percent_matched;
    auto [type, pt, eta, phi, isFake, simidx, simidxfrac] = parseTrackCandidateAllMatch(event,
                                                                                        tc_idx,
                                                                                        trk_ph2_x,
                                                                                        trk_ph2_y,
                                                                                        trk_ph2_z,
                                                                                        trk_simhit_simTrkIdx,
                                                                                        trk_ph2_simHitIdx,
                                                                                        trk_pix_simHitIdx,
                                                                                        percent_matched,
                                                                                        matchfrac);

    int nPixHits = 0, nOtHits = 0, nLayers = 0;
    for (int layerSlot = 0; layerSlot < Params_TC::kLayers; ++layerSlot) {
      if (trackCandidatesExtended.lowerModuleIndices()[tc_idx][layerSlot] == lst::kTCEmptyLowerModule)
        continue;

      ++nLayers;
      const bool isPixel = (trackCandidatesExtended.logicalLayers()[tc_idx][layerSlot] == 0);

      for (unsigned int hitSlot = 0; hitSlot < Params_TC::kHitsPerLayer; ++hitSlot) {
        if (trackCandidatesBase.hitIndices()[tc_idx][layerSlot][hitSlot] == lst::kTCEmptyHitIdx)
          continue;

        if (isPixel)
          nPixHits++;
        else
          nOtHits++;
      }
    }

    ana.tx->pushbackToBranch<int>("tc_nhitOT", nOtHits);
    ana.tx->pushbackToBranch<int>("tc_nhits", nPixHits + nOtHits);
    ana.tx->pushbackToBranch<int>("tc_nlayers", nLayers);

    // Generic per-TC hit content (tracking-ntuple rows + HitType), read from the track candidate
    // row itself rather than routed through a per-object-type collection.
    {
      auto [tc_hit_idx, tc_hit_type] = getHitIdxsAndHitTypesFromTC(event, tc_idx);
      std::vector<int> hitIdxOut(tc_hit_idx.begin(), tc_hit_idx.end());
      std::vector<int> hitTypeOut;
      hitTypeOut.reserve(tc_hit_type.size());
      for (auto const& ht : tc_hit_type)
        hitTypeOut.push_back(static_cast<int>(ht));
      ana.tx->pushbackToBranch<std::vector<int>>("tc_hitIdx", hitIdxOut);
      ana.tx->pushbackToBranch<std::vector<int>>("tc_hitType", hitTypeOut);
    }

    // Fill some branches for this track candidate
    ana.tx->pushbackToBranch<float>("tc_pt", pt);
    ana.tx->pushbackToBranch<float>("tc_eta", eta);
    ana.tx->pushbackToBranch<float>("tc_phi", phi);
    ana.tx->pushbackToBranch<int>("tc_type", type);
    // Every pT5 / pT3 / T5 / T4 row is chain-emitted and has no backing object row, so the pLS
    // index is the only per-type index left. A chain row reports -999.
    bool const chainRowTC = isChainTCRow(event, tc_idx);
    if (ana.pls_branches) {
      int plsOut = -999;
      if (!chainRowTC && type == LSTObjType::pLS)
        plsOut = pls_idx_map[ranges.segmentModuleIndices()[modules.nLowerModules()] +
                             trackCandidatesExtended.directObjectIndices()[tc_idx]];
      ana.tx->pushbackToBranch<int>("tc_plsIdx", plsOut);
    }

    ana.tx->pushbackToBranch<int>("tc_isFake", isFake);
    ana.tx->pushbackToBranch<float>("tc_pMatched", percent_matched);

    // For this tc, keep track of all the simidx that are matched
    tc_simIdxAll.push_back(simidx);
    tc_simIdxAllFrac.push_back(simidxfrac);

    // The book keeping of opposite mapping is done here
    // For each matched sim idx, we go back and keep track of which tc it is matched to.
    // Loop over all the matched sim idx
    for (size_t is = 0; is < simidx.size(); ++is) {
      // For this matched sim index keep track (sim -> tc) mapping
      int sim_idx = simidx.at(is);
      float sim_idx_frac = simidxfrac.at(is);
      sim_tcIdxAll.at(sim_idx).push_back(tc_idx);
      sim_tcIdxAllFrac.at(sim_idx).push_back(sim_idx_frac);
    }

    // Also, among the simidx matches, find the best match (highest fractional match)
    // N.B. the simidx is already returned sorted by highest number of "nhits" match
    // So as it loops over, the condition will ensure that the highest fraction with highest nhits will be matched with the priority given to highest fraction
    int tc_simIdx = -999;
    float tc_simIdxBestFrac = 0;
    for (size_t isim = 0; isim < simidx.size(); ++isim) {
      int thisidx = simidx[isim];
      float thisfrac = simidxfrac[isim];
      if (thisfrac > tc_simIdxBestFrac and thisfrac > matchfrac) {
        tc_simIdxBestFrac = thisfrac;
        tc_simIdx = thisidx;
      }
    }

    // the best match index will then be saved here
    ana.tx->pushbackToBranch<int>("tc_simIdx", tc_simIdx);
  }

  // Now save the (tc -> simidx) mapping
  ana.tx->setBranch<std::vector<std::vector<int>>>("tc_simIdxAll", tc_simIdxAll);
  ana.tx->setBranch<std::vector<std::vector<float>>>("tc_simIdxAllFrac", tc_simIdxAllFrac);

  // Not all (sim->tcIdx) will be saved but only for the sim that is from hard scatter and current bunch crossing
  // So a restriction up to only "n_accepted_simtrk" done by chopping off the rest
  // N.B. the reason we can simply take the first "n_accepted_simtrk" is because the tracking ntuple is organized such that those sim tracks show up on the first "n_accepted_simtrk" of tracks.
  std::vector<std::vector<int>> sim_tcIdxAll_to_write;
  std::vector<std::vector<float>> sim_tcIdxAllFrac_to_write;
  std::copy(sim_tcIdxAll.begin(),
            sim_tcIdxAll.begin() + n_accepted_simtrk,
            std::back_inserter(
                sim_tcIdxAll_to_write));  // this is where the vector is only copying the first "n_accepted_simtrk"
  std::copy(sim_tcIdxAllFrac.begin(),
            sim_tcIdxAllFrac.begin() + n_accepted_simtrk,
            std::back_inserter(sim_tcIdxAllFrac_to_write));  // ditto
  ana.tx->setBranch<std::vector<std::vector<int>>>("sim_tcIdxAll", sim_tcIdxAll_to_write);
  ana.tx->setBranch<std::vector<std::vector<float>>>("sim_tcIdxAllFrac", sim_tcIdxAllFrac_to_write);

  // Using the intermedaite variables to compute whether a given track candidate is a duplicate
  std::vector<int> tc_isDuplicate(tc_simIdxAll.size());

  // Loop over the track candidates
  for (unsigned int tc_idx = 0; tc_idx < tc_simIdxAll.size(); ++tc_idx) {
    bool isDuplicate = false;
    // Loop over the sim idx matched to this track candidate
    for (unsigned int isim = 0; isim < tc_simIdxAll[tc_idx].size(); ++isim) {
      int sim_idx = tc_simIdxAll[tc_idx][isim];
      int n_sim_matched = 0;
      for (size_t ism = 0; ism < sim_tcIdxAll.at(sim_idx).size(); ++ism) {
        if (sim_tcIdxAllFrac.at(sim_idx).at(ism) > matchfrac) {
          n_sim_matched += 1;
          if (n_sim_matched > 1) {
            isDuplicate = true;
            break;
          }
        }
      }
    }
    tc_isDuplicate[tc_idx] = isDuplicate;
  }
  ana.tx->setBranch<std::vector<int>>("tc_isDuplicate", tc_isDuplicate);

  // Similarly, the best match for the (sim -> tc is computed)
  // TODO: Is this redundant? I am not sure if it is guaranteed that sim_tcIdx will have same result with tc_simIdx.
  // I think it will be, but I have not rigorously checked. I only checked about first few thousands and it was all true. as long as tc->sim was pointing to a sim that is among the n_accepted.
  // For the most part I think this won't be a problem.
  for (size_t i = 0; i < sim_tcIdxAll_to_write.size(); ++i) {
    // bestmatch is not always the first one
    int bestmatch_idx = -999;
    float bestmatch_frac = -999;
    for (size_t jj = 0; jj < sim_tcIdxAll_to_write.at(i).size(); ++jj) {
      int idx = sim_tcIdxAll_to_write.at(i).at(jj);
      float frac = sim_tcIdxAllFrac_to_write.at(i).at(jj);
      if (bestmatch_frac < frac) {
        bestmatch_idx = idx;
        bestmatch_frac = frac;
      }
    }
    ana.tx->pushbackToBranch<int>("sim_tcIdxBest", bestmatch_idx);
    ana.tx->pushbackToBranch<float>("sim_tcIdxBestFrac", bestmatch_frac);
    if (bestmatch_frac > matchfrac)  // then this is a good match according to MTV
      ana.tx->pushbackToBranch<int>("sim_tcIdx", bestmatch_idx);
    else
      ana.tx->pushbackToBranch<int>("sim_tcIdx", -999);
  }
}

//________________________________________________________________________________________________________________________________
void setOccupancyBranches(LSTEvent* event) {
  auto modules = event->getModules<ModulesSoA>();
  auto miniDoublets = event->getMiniDoublets<MiniDoubletsOccupancySoA>();
  auto segments = event->getSegments<SegmentsOccupancySoA>();
  auto triplets = event->getTriplets<TripletsOccupancySoA>();
  auto trackCandidatesBase = event->getTrackCandidatesBase();

  std::vector<int> moduleLayer;
  std::vector<int> moduleDetId;
  std::vector<int> moduleSubdet;
  std::vector<int> moduleRing;
  std::vector<int> moduleRod;
  std::vector<int> moduleModule;
  std::vector<float> moduleEta;
  std::vector<float> moduleR;
  std::vector<bool> moduleIsTilted;
  std::vector<int> trackCandidateOccupancy;
  std::vector<int> tripletOccupancy;
  std::vector<int> segmentOccupancy;
  std::vector<int> mdOccupancy;

  for (unsigned int lowerIdx = 0; lowerIdx <= modules.nLowerModules(); lowerIdx++) {
    //layer = 0, subdet = 0 => pixel module
    moduleLayer.push_back(modules.layers()[lowerIdx]);
    moduleDetId.push_back(modules.detIds()[lowerIdx]);
    moduleSubdet.push_back(modules.subdets()[lowerIdx]);
    moduleRing.push_back(modules.rings()[lowerIdx]);
    moduleRod.push_back(modules.rods()[lowerIdx]);
    moduleEta.push_back(modules.eta()[lowerIdx]);
    moduleR.push_back(modules.r()[lowerIdx]);
    bool isTilted = (modules.subdets()[lowerIdx] == 5 and modules.sides()[lowerIdx] != 3);
    moduleIsTilted.push_back(isTilted);
    moduleModule.push_back(modules.modules()[lowerIdx]);
    segmentOccupancy.push_back(segments.totOccupancySegments()[lowerIdx]);
    mdOccupancy.push_back(miniDoublets.totOccupancyMDs()[lowerIdx]);

    if (lowerIdx < modules.nLowerModules()) {
      tripletOccupancy.push_back(triplets.totOccupancyTriplets()[lowerIdx]);
    }
  }

  ana.tx->setBranch<std::vector<int>>("module_layers", moduleLayer);
  ana.tx->setBranch<std::vector<int>>("module_detIds", moduleDetId);
  ana.tx->setBranch<std::vector<int>>("module_subdets", moduleSubdet);
  ana.tx->setBranch<std::vector<int>>("module_rings", moduleRing);
  ana.tx->setBranch<std::vector<int>>("module_rods", moduleRod);
  ana.tx->setBranch<std::vector<int>>("module_modules", moduleModule);
  ana.tx->setBranch<std::vector<bool>>("module_isTilted", moduleIsTilted);
  ana.tx->setBranch<std::vector<float>>("module_eta", moduleEta);
  ana.tx->setBranch<std::vector<float>>("module_r", moduleR);
  ana.tx->setBranch<std::vector<int>>("md_occupancies", mdOccupancy);
  ana.tx->setBranch<std::vector<int>>("sg_occupancies", segmentOccupancy);
  ana.tx->setBranch<std::vector<int>>("t3_occupancies", tripletOccupancy);
  ana.tx->setBranch<int>("tc_occupancies", trackCandidatesBase.nTrackCandidates());
}

//________________________________________________________________________________________________________________________________
void fillT3DNNBranches(LSTEvent* event, unsigned int iT3) {
  auto const& trk_ph2_subdet = trk.getVUS("ph2_subdet");
  auto const& trk_ph2_layer = trk.getVUS("ph2_layer");
  auto const& trk_ph2_detId = trk.getVU("ph2_detId");

  auto const& hitsBase = event->getInput<HitsBaseSoA>();
  auto const& hitsExtended = event->getHits<HitsExtendedSoA>();
  auto const& modules = event->getModules<ModulesSoA>();

  auto hitIdx = getHitsFromT3(event, iT3);
  std::vector<lst_math::Hit> hitObjects;

  for (unsigned int i = 0; i < hitIdx.size(); ++i) {
    unsigned int hit = hitIdx[i];
    float x = hitsBase.xs()[hit];
    float y = hitsBase.ys()[hit];
    float z = hitsBase.zs()[hit];
    lst_math::Hit hitObj(x, y, z);
    hitObjects.push_back(hitObj);

    std::string idx = std::to_string(i);
    ana.tx->pushbackToBranch<float>("t3_hit_" + idx + "_r", sqrt(x * x + y * y));
    ana.tx->pushbackToBranch<float>("t3_hit_" + idx + "_x", x);
    ana.tx->pushbackToBranch<float>("t3_hit_" + idx + "_y", y);
    ana.tx->pushbackToBranch<float>("t3_hit_" + idx + "_z", z);
    ana.tx->pushbackToBranch<float>("t3_hit_" + idx + "_eta", hitObj.eta());
    ana.tx->pushbackToBranch<float>("t3_hit_" + idx + "_phi", hitObj.phi());

    int subdet = trk_ph2_subdet[hitsBase.idxs()[hit]];
    int is_endcap = subdet == 4;
    int layer = trk_ph2_layer[hitsBase.idxs()[hit]] + 6 * is_endcap;
    int detId = trk_ph2_detId[hitsBase.idxs()[hit]];
    unsigned int module = hitsExtended.moduleIndices()[hit];

    ana.tx->pushbackToBranch<int>("t3_hit_" + idx + "_detId", detId);
    ana.tx->pushbackToBranch<int>("t3_hit_" + idx + "_layer", layer);
    ana.tx->pushbackToBranch<int>("t3_hit_" + idx + "_moduleType", modules.moduleType()[module]);
  }
}

//________________________________________________________________________________________________________________________________
void setT3DNNBranches(LSTEvent* event, float matchfrac) {
  auto const& trk_sim_parentVtxIdx = trk.getVI("sim_parentVtxIdx");
  auto const& trk_simvtx_x = trk.getVF("simvtx_x");
  auto const& trk_simvtx_y = trk.getVF("simvtx_y");
  auto const& trk_simvtx_z = trk.getVF("simvtx_z");
  auto const& trk_simhit_simTrkIdx = trk.getVI("simhit_simTrkIdx");
  auto const& trk_ph2_simHitIdx = trk.getVVI("ph2_simHitIdx");
  auto const& trk_pix_simHitIdx = trk.getVVI("pix_simHitIdx");

  auto const& triplets = event->getTriplets<TripletsSoA>();
  auto const& tripletsOccupancy = event->getTriplets<TripletsOccupancySoA>();
  auto const& modules = event->getModules<ModulesSoA>();
  auto const& ranges = event->getRanges();

  unsigned int nRanges = tripletsOccupancy.metadata().size();
  for (unsigned int lowerModuleIdx = 0; lowerModuleIdx < nRanges; ++lowerModuleIdx) {
    int nTriplets = tripletsOccupancy.nTriplets()[lowerModuleIdx];
    for (unsigned int idx = 0; idx < nTriplets; idx++) {
      unsigned int tripletIndex = ranges.tripletModuleIndices()[lowerModuleIdx] + idx;

      // Get hit indices and types
      auto hit_idx = getHitsFromT3(event, tripletIndex);
      auto hit_type = getHitTypesFromT3(event, tripletIndex);
      auto module_idx = getModuleIdxsFromT3(event, tripletIndex);

      // Calculate layer binary representation
      int layer_binary = 0;
      for (size_t i = 0; i < module_idx.size(); i += 2) {
        layer_binary |= (1 << (modules.layers()[module_idx[i]] + 6 * (modules.subdets()[module_idx[i]] == 4)));
      }

      // Get matching information with percent matched
      float percent_matched;
      std::vector<int> simidx = matchedSimTrkIdxs(hit_idx,
                                                  hit_type,
                                                  trk_simhit_simTrkIdx,
                                                  trk_ph2_simHitIdx,
                                                  trk_pix_simHitIdx,
                                                  false,
                                                  matchfrac,
                                                  &percent_matched);

      // Fill the branches with T3-specific data
      ana.tx->pushbackToBranch<float>("t3_betaIn", triplets.betaIn()[tripletIndex]);
      ana.tx->pushbackToBranch<float>("t3_centerX", triplets.centerX()[tripletIndex]);
      ana.tx->pushbackToBranch<float>("t3_centerY", triplets.centerY()[tripletIndex]);
      ana.tx->pushbackToBranch<float>("t3_radius", triplets.radius()[tripletIndex]);
      ana.tx->pushbackToBranch<float>("t3_fakeScore", triplets.fakeScore()[tripletIndex]);
      ana.tx->pushbackToBranch<float>("t3_promptScore", triplets.promptScore()[tripletIndex]);
      ana.tx->pushbackToBranch<float>("t3_displacedScore", triplets.displacedScore()[tripletIndex]);
      ana.tx->pushbackToBranch<int>("t3_layer_binary", layer_binary);
      ana.tx->pushbackToBranch<std::vector<int>>("t3_matched_simIdx", simidx);
      ana.tx->pushbackToBranch<float>("t3_pMatched", percent_matched);

      // Add vertex information for matched sim tracks
      if (simidx.size() == 0) {
        // No matched sim track - set default values
        ana.tx->pushbackToBranch<float>("t3_sim_vxy", 0.0);
        ana.tx->pushbackToBranch<float>("t3_sim_vz", 0.0);
      } else {
        // Get vertex information from the first matched sim track
        int vtxidx = trk_sim_parentVtxIdx[simidx[0]];
        float vtx_x = trk_simvtx_x[vtxidx];
        float vtx_y = trk_simvtx_y[vtxidx];
        float vtx_z = trk_simvtx_z[vtxidx];

        // Calculate transverse distance from origin
        float vxy = sqrt(vtx_x * vtx_x + vtx_y * vtx_y);

        ana.tx->pushbackToBranch<float>("t3_sim_vxy", vxy);
        ana.tx->pushbackToBranch<float>("t3_sim_vz", vtx_z);
      }

      // Fill hit-specific information
      fillT3DNNBranches(event, tripletIndex);
    }
  }
}

//________________________________________________________________________________________________________________________________
// True iff track candidate row `idx` was emitted by the chain pipeline rather than carried over
// from LST's own builders.
//
// The row TYPE cannot answer this: a chain that picked up a pixel seed is written as type 7, the
// same label a carried pT5 row carries. Provenance comes from the row RANGE instead -- chain rows
// are appended after the surviving carried rows, so the chain block is the last nChainTCs entries
// of the collection.
bool isChainTCRow(LSTEvent* event, unsigned int idx) {
  auto const& base = event->getTrackCandidatesBase();
  auto const& chains = event->getChains();
  unsigned int const nTC = base.nTrackCandidates();
  unsigned int const nChainTC = chains.nChainTCs();
  return nChainTC <= nTC && idx >= nTC - nChainTC;
}

// Kinematics and hit content of a chain-emitted track candidate row. A chain-backed row carries the
// T5 / T4 class label but is backed by a welded CHAIN rather than by a Quintuplet or a Quadruplet,
// so its pt / eta / phi come from ChainsSoA (pt is the lower median of the member triplet pt, eta
// and phi come from the innermost member triplet) and its hits come from the row itself.
// Precondition: isChainTCRow(event, idx).
std::tuple<float, float, float, std::vector<unsigned int>, std::vector<HitType>> parseChainTC(LSTEvent* event,
                                                                                              unsigned int idx) {
  auto const& trackCandidatesExtended = event->getTrackCandidatesExtended();
  auto const& chains = event->getChains();
  unsigned int const chainIdx = trackCandidatesExtended.directObjectIndices()[idx];
  auto [hit_idx, hit_type] = getHitIdxsAndHitTypesFromTC(event, idx);
  // A bare-triplet attach row is backed by no chain and by no PixelTriplets entry, and carries the
  // sentinel 0xFFFFFFFF where a chain row index would be; objectIndices[0] is the pLS row it
  // attached and objectIndices[1] the sparse triplet row. pt comes from the pixel seed, which
  // measures it better -- this is what LST's own pT3 rows do too -- but eta and phi are the
  // TRIPLET's, taken exactly as the t3_* branches define them (eta of the last anchor hit, phi of
  // the first), because the eta-banded thresholds this class is judged by are fitted on the
  // triplet eta and not on the seed's.
  if (chainIdx == 0xFFFFFFFFu) {
    auto const& pixelSeeds = event->getInput<PixelSeedsSoA>();
    auto const& hitsBaseChain = event->getInput<HitsBaseSoA>();
    unsigned int const ipLS = trackCandidatesExtended.objectIndices()[idx][0];
    unsigned int const tripletIdx = trackCandidatesExtended.objectIndices()[idx][1];
    auto [t3HitIdx, t3HitType] = getHitIdxsAndHitTypesFromT3(event, tripletIdx);
    lst_math::Hit firstAnchorHit(
        hitsBaseChain.xs()[t3HitIdx[0]], hitsBaseChain.ys()[t3HitIdx[0]], hitsBaseChain.zs()[t3HitIdx[0]]);
    lst_math::Hit lastAnchorHit(
        hitsBaseChain.xs()[t3HitIdx[4]], hitsBaseChain.ys()[t3HitIdx[4]], hitsBaseChain.zs()[t3HitIdx[4]]);
    return {pixelSeeds.ptIn()[ipLS], lastAnchorHit.eta(), firstAnchorHit.phi(), hit_idx, hit_type};
  }
  return {chains.tcPt()[chainIdx], chains.tcEta()[chainIdx], chains.tcPhi()[chainIdx], hit_idx, hit_type};
}

//________________________________________________________________________________________________________________________________
std::tuple<int, float, float, float, int, std::vector<int>> parseTrackCandidate(
    LSTEvent* event,
    unsigned int idx,
    std::vector<float> const& trk_ph2_x,
    std::vector<float> const& trk_ph2_y,
    std::vector<float> const& trk_ph2_z,
    std::vector<int> const& trk_simhit_simTrkIdx,
    std::vector<std::vector<int>> const& trk_ph2_simHitIdx,
    std::vector<std::vector<int>> const& trk_pix_simHitIdx,
    float matchfrac) {
  // Get the type of the track candidate
  auto const& trackCandidatesBase = event->getTrackCandidatesBase();
  short type = trackCandidatesBase.trackCandidateType()[idx];

  // Compute pt eta phi and hit indices that will be used to figure out whether the TC matched
  float pt, eta, phi;
  std::vector<unsigned int> hit_idx;
  std::vector<HitType> hit_type;
  bool const chainRow = isChainTCRow(event, idx);
  if (chainRow) {
    // Every chain-backed row -- bare T4 / T5 class and pixel-attached type 7 alike -- takes its
    // kinematics from ChainsSoA and its hits from the row, which for type 7 already carries the
    // attached seed's pixel hits in the two pixel layer slots.
    std::tie(pt, eta, phi, hit_idx, hit_type) = parseChainTC(event, idx);
  } else {
    // Every pT5 / pT3 / T5 / T4 row is chain-emitted and was taken by the branch above, so the
    // bare pLS is the only non-chain row that can reach here.
    if (type == LSTObjType::pLS)
      std::tie(pt, eta, phi, hit_idx, hit_type) = parsepLS(event, idx);
    else
      throw std::logic_error("unsupported non-chain type " + std::to_string(type));
  }

  // Perform matching
  std::vector<int> simidx = matchedSimTrkIdxs(
      hit_idx, hit_type, trk_simhit_simTrkIdx, trk_ph2_simHitIdx, trk_pix_simHitIdx, false, matchfrac);
  int isFake = simidx.size() == 0;

  return {type, pt, eta, phi, isFake, simidx};
}

//________________________________________________________________________________________________________________________________
std::tuple<int, float, float, float, int, std::vector<int>, std::vector<float>> parseTrackCandidateAllMatch(
    LSTEvent* event,
    unsigned int idx,
    std::vector<float> const& trk_ph2_x,
    std::vector<float> const& trk_ph2_y,
    std::vector<float> const& trk_ph2_z,
    std::vector<int> const& trk_simhit_simTrkIdx,
    std::vector<std::vector<int>> const& trk_ph2_simHitIdx,
    std::vector<std::vector<int>> const& trk_pix_simHitIdx,
    float& percent_matched,
    float matchfrac) {
  // Get the type of the track candidate
  auto const& trackCandidatesBase = event->getTrackCandidatesBase();
  short type = trackCandidatesBase.trackCandidateType()[idx];

  // Compute pt eta phi and hit indices that will be used to figure out whether the TC matched
  float pt, eta, phi;
  std::vector<unsigned int> hit_idx;
  std::vector<HitType> hit_type;
  bool const chainRow = isChainTCRow(event, idx);
  if (chainRow) {
    // Every chain-backed row -- bare T4 / T5 class and pixel-attached type 7 alike -- takes its
    // kinematics from ChainsSoA and its hits from the row, which for type 7 already carries the
    // attached seed's pixel hits in the two pixel layer slots.
    std::tie(pt, eta, phi, hit_idx, hit_type) = parseChainTC(event, idx);
  } else {
    // Every pT5 / pT3 / T5 / T4 row is chain-emitted and was taken by the branch above, so the
    // bare pLS is the only non-chain row that can reach here.
    if (type == LSTObjType::pLS)
      std::tie(pt, eta, phi, hit_idx, hit_type) = parsepLS(event, idx);
    else
      throw std::logic_error("unsupported non-chain type " + std::to_string(type));
  }

  // Perform matching
  auto [simidx, simidxfrac] = matchedSimTrkIdxsAndFracs(
      hit_idx, hit_type, trk_simhit_simTrkIdx, trk_ph2_simHitIdx, trk_pix_simHitIdx, false, matchfrac, &percent_matched);
  int isFake = simidx.size() == 0;

  return {type, pt, eta, phi, isFake, simidx, simidxfrac};
}

//________________________________________________________________________________________________________________________________
std::tuple<float, float, float, std::vector<unsigned int>, std::vector<HitType>> parsepLS(LSTEvent* event,
                                                                                          unsigned int idx) {
  auto const& trackCandidatesExtended = event->getTrackCandidatesExtended();
  auto pixelSeeds = event->getInput<PixelSeedsSoA>();

  // Getting pLS index
  unsigned int pLS = trackCandidatesExtended.directObjectIndices()[idx];

  // Getting pt eta and phi
  float pt = pixelSeeds.ptIn()[pLS];
  float eta = pixelSeeds.eta()[pLS];
  float phi = pixelSeeds.phi()[pLS];

  // Getting hit indices and types
  auto hit_idx = getHitIdxsFrompLS(event, pLS);
  auto hit_type = getHitTypesFrompLS(event, pLS);

  return {pt, eta, phi, hit_idx, hit_type};
}

//________________________________________________________________________________________________________________________________
void printHitMultiplicities(LSTEvent* event) {
  auto modules = event->getModules<ModulesSoA>();
  auto hitRanges = event->getHits<HitsRangesSoA>();

  int nHits = 0;
  for (unsigned int idx = 0; idx <= modules.nLowerModules();
       idx++)  // "<=" because cheating to include pixel track candidate lower module
  {
    nHits += hitRanges.hitRanges()[2 * idx][1] - hitRanges.hitRanges()[2 * idx][0] + 1;
    nHits += hitRanges.hitRanges()[2 * idx + 1][1] - hitRanges.hitRanges()[2 * idx + 1][0] + 1;
  }
  std::cout << " nHits: " << nHits << std::endl;
}

//________________________________________________________________________________________________________________________________
void printMiniDoubletMultiplicities(LSTEvent* event) {
  MiniDoubletsOccupancyConst miniDoublets = event->getMiniDoublets<MiniDoubletsOccupancySoA>();

  int nMiniDoublets = 0;
  int totOccupancyMiniDoublets = 0;
  unsigned int nRanges = miniDoublets.metadata().size();
  for (unsigned int idx = 0; idx < nRanges; idx++) {
    nMiniDoublets += miniDoublets.nMDs()[idx];
    totOccupancyMiniDoublets += miniDoublets.totOccupancyMDs()[idx];
  }
  std::cout << " nMiniDoublets: " << nMiniDoublets << std::endl;
  std::cout << " totOccupancyMiniDoublets (including trucated ones): " << totOccupancyMiniDoublets << std::endl;
}

//________________________________________________________________________________________________________________________________
void printAllObjects(LSTEvent* event) {
  printMDs(event);
  printLSs(event);
  printpLSs(event);
  printT3s(event);
}

//________________________________________________________________________________________________________________________________
void printMDs(LSTEvent* event) {
  MiniDoubletsConst miniDoublets = event->getMiniDoublets<MiniDoubletsSoA>();
  MiniDoubletsOccupancyConst miniDoubletsOccupancy = event->getMiniDoublets<MiniDoubletsOccupancySoA>();
  auto hitsBase = event->getInput<HitsBaseSoA>();
  auto ranges = event->getRanges();

  unsigned int nRanges = miniDoubletsOccupancy.metadata().size();
  for (unsigned int idx = 0; idx < nRanges; ++idx) {
    for (unsigned int iMD = 0; iMD < miniDoubletsOccupancy.nMDs()[idx]; iMD++) {
      unsigned int mdIdx = ranges.miniDoubletModuleIndices()[idx] + iMD;
      unsigned int LowerHitIndex = miniDoublets.anchorHitIndices()[mdIdx];
      unsigned int UpperHitIndex = miniDoublets.outerHitIndices()[mdIdx];
      unsigned int hit0 = hitsBase.idxs()[LowerHitIndex];
      unsigned int hit1 = hitsBase.idxs()[UpperHitIndex];
      std::cout << "VALIDATION 'MD': "
                << "MD"
                << " hit0: " << hit0 << " hit1: " << hit1 << std::endl;
    }
  }
}

//________________________________________________________________________________________________________________________________
void printLSs(LSTEvent* event) {
  SegmentsConst segments = event->getSegments<SegmentsSoA>();
  SegmentsOccupancyConst segmentsOccupancy = event->getSegments<SegmentsOccupancySoA>();
  MiniDoubletsConst miniDoublets = event->getMiniDoublets<MiniDoubletsSoA>();
  auto hitsBase = event->getInput<HitsBaseSoA>();
  auto ranges = event->getRanges();

  int nSegments = 0;
  unsigned int nRanges = segmentsOccupancy.metadata().size();
  for (unsigned int idx = 0; idx < nRanges; ++idx) {
    nSegments += segmentsOccupancy.nSegments()[idx];
    for (unsigned int jdx = 0; jdx < segmentsOccupancy.nSegments()[idx]; jdx++) {
      unsigned int sgIdx = ranges.segmentModuleIndices()[idx] + jdx;
      unsigned int InnerMiniDoubletIndex = segments.mdIndices()[sgIdx][0];
      unsigned int OuterMiniDoubletIndex = segments.mdIndices()[sgIdx][1];
      unsigned int InnerMiniDoubletLowerHitIndex = miniDoublets.anchorHitIndices()[InnerMiniDoubletIndex];
      unsigned int InnerMiniDoubletUpperHitIndex = miniDoublets.outerHitIndices()[InnerMiniDoubletIndex];
      unsigned int OuterMiniDoubletLowerHitIndex = miniDoublets.anchorHitIndices()[OuterMiniDoubletIndex];
      unsigned int OuterMiniDoubletUpperHitIndex = miniDoublets.outerHitIndices()[OuterMiniDoubletIndex];
      unsigned int hit0 = hitsBase.idxs()[InnerMiniDoubletLowerHitIndex];
      unsigned int hit1 = hitsBase.idxs()[InnerMiniDoubletUpperHitIndex];
      unsigned int hit2 = hitsBase.idxs()[OuterMiniDoubletLowerHitIndex];
      unsigned int hit3 = hitsBase.idxs()[OuterMiniDoubletUpperHitIndex];
      std::cout << "VALIDATION 'LS': "
                << "LS"
                << " hit0: " << hit0 << " hit1: " << hit1 << " hit2: " << hit2 << " hit3: " << hit3 << std::endl;
    }
  }
  std::cout << "VALIDATION nSegments: " << nSegments << std::endl;
}

//________________________________________________________________________________________________________________________________
void printpLSs(LSTEvent* event) {
  SegmentsConst segments = event->getSegments<SegmentsSoA>();
  SegmentsOccupancyConst segmentsOccupancy = event->getSegments<SegmentsOccupancySoA>();
  MiniDoubletsConst miniDoublets = event->getMiniDoublets<MiniDoubletsSoA>();
  auto hitsBase = event->getInput<HitsBaseSoA>();
  auto ranges = event->getRanges();

  unsigned int idx = segmentsOccupancy.metadata().size() - 1;
  int npLS = segmentsOccupancy.nSegments()[idx];
  for (unsigned int jdx = 0; jdx < npLS; jdx++) {
    unsigned int sgIdx = ranges.segmentModuleIndices()[idx] + jdx;
    unsigned int InnerMiniDoubletIndex = segments.mdIndices()[sgIdx][0];
    unsigned int OuterMiniDoubletIndex = segments.mdIndices()[sgIdx][1];
    unsigned int InnerMiniDoubletLowerHitIndex = miniDoublets.anchorHitIndices()[InnerMiniDoubletIndex];
    unsigned int InnerMiniDoubletUpperHitIndex = miniDoublets.outerHitIndices()[InnerMiniDoubletIndex];
    unsigned int OuterMiniDoubletLowerHitIndex = miniDoublets.anchorHitIndices()[OuterMiniDoubletIndex];
    unsigned int OuterMiniDoubletUpperHitIndex = miniDoublets.outerHitIndices()[OuterMiniDoubletIndex];
    unsigned int hit0 = hitsBase.idxs()[InnerMiniDoubletLowerHitIndex];
    unsigned int hit1 = hitsBase.idxs()[InnerMiniDoubletUpperHitIndex];
    unsigned int hit2 = hitsBase.idxs()[OuterMiniDoubletLowerHitIndex];
    unsigned int hit3 = hitsBase.idxs()[OuterMiniDoubletUpperHitIndex];
    std::cout << "VALIDATION 'pLS': "
              << "pLS"
              << " hit0: " << hit0 << " hit1: " << hit1 << " hit2: " << hit2 << " hit3: " << hit3 << std::endl;
  }
  std::cout << "VALIDATION npLS: " << npLS << std::endl;
}

//________________________________________________________________________________________________________________________________
void printT3s(LSTEvent* event) {
  auto const triplets = event->getTriplets<TripletsSoA>();
  auto const tripletsOccupancy = event->getTriplets<TripletsOccupancySoA>();
  SegmentsConst segments = event->getSegments<SegmentsSoA>();
  MiniDoubletsConst miniDoublets = event->getMiniDoublets<MiniDoubletsSoA>();
  auto hitsBase = event->getInput<HitsBaseSoA>();
  int nTriplets = 0;
  unsigned int nRanges = tripletsOccupancy.metadata().size();
  for (unsigned int idx = 0; idx < nRanges; ++idx) {
    nTriplets += tripletsOccupancy.nTriplets()[idx];
    for (unsigned int jdx = 0; jdx < tripletsOccupancy.nTriplets()[idx]; jdx++) {
      unsigned int tpIdx = idx * 5000 + jdx;
      unsigned int InnerSegmentIndex = triplets.segmentIndices()[tpIdx][0];
      unsigned int OuterSegmentIndex = triplets.segmentIndices()[tpIdx][1];
      unsigned int InnerSegmentInnerMiniDoubletIndex = segments.mdIndices()[InnerSegmentIndex][0];
      unsigned int InnerSegmentOuterMiniDoubletIndex = segments.mdIndices()[InnerSegmentIndex][1];
      unsigned int OuterSegmentOuterMiniDoubletIndex = segments.mdIndices()[OuterSegmentIndex][1];

      unsigned int hit_idx0 = miniDoublets.anchorHitIndices()[InnerSegmentInnerMiniDoubletIndex];
      unsigned int hit_idx1 = miniDoublets.outerHitIndices()[InnerSegmentInnerMiniDoubletIndex];
      unsigned int hit_idx2 = miniDoublets.anchorHitIndices()[InnerSegmentOuterMiniDoubletIndex];
      unsigned int hit_idx3 = miniDoublets.outerHitIndices()[InnerSegmentOuterMiniDoubletIndex];
      unsigned int hit_idx4 = miniDoublets.anchorHitIndices()[OuterSegmentOuterMiniDoubletIndex];
      unsigned int hit_idx5 = miniDoublets.outerHitIndices()[OuterSegmentOuterMiniDoubletIndex];

      unsigned int hit0 = hitsBase.idxs()[hit_idx0];
      unsigned int hit1 = hitsBase.idxs()[hit_idx1];
      unsigned int hit2 = hitsBase.idxs()[hit_idx2];
      unsigned int hit3 = hitsBase.idxs()[hit_idx3];
      unsigned int hit4 = hitsBase.idxs()[hit_idx4];
      unsigned int hit5 = hitsBase.idxs()[hit_idx5];
      std::cout << "VALIDATION 'T3': "
                << "T3"
                << " hit0: " << hit0 << " hit1: " << hit1 << " hit2: " << hit2 << " hit3: " << hit3 << " hit4: " << hit4
                << " hit5: " << hit5 << std::endl;
    }
  }
  std::cout << "VALIDATION nTriplets: " << nTriplets << std::endl;
}
