#ifndef write_lst_ntuple_h
#define write_lst_ntuple_h

#include <iostream>
#include <tuple>

#include "lst_math.h"
#include "LSTEvent.h"

#include "AnalysisConfig.h"
#include "trkCore.h"
#include "AccessHelper.h"

using LSTEvent = ALPAKA_ACCELERATOR_NAMESPACE::lst::LSTEvent;

// Common
void createOutputBranches();
void createJetBranches();
void createT3DNNBranches();
void createSimTrackContainerBranches();
void createTrackCandidateBranches();
void createMiniDoubletBranches();
void createLineSegmentBranches();
void createTripletBranches();
void createPixelLineSegmentBranches();
void createOccupancyBranches();

void fillOutputBranches(LSTEvent* event);
void setOccupancyBranches(LSTEvent* event);
void setGenJetBranches(LSTEvent* event);
unsigned int setSimTrackContainerBranches(LSTEvent* event);
void setTrackCandidateBranches(LSTEvent* event,
                               unsigned int n_accepted_simtrk,
                               std::map<unsigned int, unsigned int> pls_idx_map,
                               float matchfrac);
std::map<unsigned int, unsigned int> setMiniDoubletBranches(LSTEvent* event,
                                                            unsigned int n_accepted_simtrk,
                                                            float matchfrac);
std::map<unsigned int, unsigned int> setLineSegmentBranches(LSTEvent* event,
                                                            unsigned int n_accepted_simtrk,
                                                            float matchfrac,
                                                            std::map<unsigned int, unsigned int> const& md_idx_map);
std::map<unsigned int, unsigned int> setTripletBranches(LSTEvent* event,
                                                        unsigned int n_accepted_simtrk,
                                                        float matchfrac,
                                                        std::map<unsigned int, unsigned int> const& ls_idx_map);
std::map<unsigned int, unsigned int> setPixelLineSegmentBranches(LSTEvent* event,
                                                                 unsigned int n_accepted_simtrk,
                                                                 float matchfrac,
                                                                 std::map<unsigned int, unsigned int> const& ls_idx_map);

void fillT3DNNBranches(LSTEvent* event, unsigned int iT3);
void setT3DNNBranches(LSTEvent* event, float matchfrac = 0.75);

std::tuple<int, float, float, float, int, std::vector<int>> parseTrackCandidate(
    LSTEvent* event,
    unsigned int,
    std::vector<float> const& trk_ph2_x,
    std::vector<float> const& trk_ph2_y,
    std::vector<float> const& trk_ph2_z,
    std::vector<int> const& trk_simhit_simTrkIdx,
    std::vector<std::vector<int>> const& trk_ph2_simHitIdx,
    std::vector<std::vector<int>> const& trk_pix_simHitIdx,
    float matchfrac = 0.75);
std::tuple<int, float, float, float, int, std::vector<int>, std::vector<float>> parseTrackCandidateAllMatch(
    LSTEvent* event,
    unsigned int,
    std::vector<float> const& trk_ph2_x,
    std::vector<float> const& trk_ph2_y,
    std::vector<float> const& trk_ph2_z,
    std::vector<int> const& trk_simhit_simTrkIdx,
    std::vector<std::vector<int>> const& trk_ph2_simHitIdx,
    std::vector<std::vector<int>> const& trk_pix_simHitIdx,
    float& percent_matched,
    float matchfrac = 0.75);
// True iff track candidate row `idx` was emitted by the chain pipeline rather than carried over
// from LST's own builders. P2.4 makes the row TYPE insufficient on its own (an attach-upgraded
// chain is type 7, the same label a carried pT5 row carries), so provenance comes from the row
// range: chain rows are the last nChainTCs entries of the collection.
bool isChainTCRow(LSTEvent* event, unsigned int idx);

std::tuple<float, float, float, std::vector<unsigned int>, std::vector<lst::HitType>> parsepLS(LSTEvent* event,
                                                                                               unsigned int);

// Print multiplicities
void printMiniDoubletMultiplicities(LSTEvent* event);
void printHitMultiplicities(LSTEvent* event);

// Print objects (GPU)
void printAllObjects(LSTEvent* event);
void printMDs(LSTEvent* event);
void printLSs(LSTEvent* event);
void printpLSs(LSTEvent* event);
void printT3s(LSTEvent* event);

#endif
