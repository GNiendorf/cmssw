#ifndef RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaLstInputKernels_h
#define RecoTracker_MkFitAlpaka_plugins_alpaka_MkFitAlpakaLstInputKernels_h

// Stage-B prototype (round 6, lane lstin; doc/lstin.txt): LST's input device collection built on the device.
//   OT hits  : raw OT rechits (local x/y, GeomDet index, cluster size; staged on the host, no toGlobal there) +
//              a per-module Surface table -> global x/y/z with the same float operations as
//              BaseTrackerRecHit::globalPosition() (mkfitdev::localToGlobal, contraction variant chosen by a param).
//   pLS      : Patatrack's device pixel tracks (state at the beam-spot PCA + covariance), option (i): the LST pLS
//              fields that hltInputLST computes from the host KF-refit seed (hltInitialStepSeeds) are computed from
//              the Patatrack fit instead (uniform-field helix from the PCA to the outermost hit).
#include <cstdint>

#include <Eigen/Core>  // before any SoA header (Eigen columns of TracksSoA)

#include "DataFormats/TrackSoA/interface/TracksSoA.h"
#include "DataFormats/TrackingRecHitSoA/interface/TrackingRecHitsSoA.h"
#include "HeterogeneousCore/AlpakaInterface/interface/config.h"
#include "RecoTracker/LSTCore/interface/LSTInputSoA.h"
#include "RecoTracker/MkFitAlpaka/interface/hits/DeviceHitInput.h"
#include "RecoTracker/MkFitAlpaka/interface/othits/OTRecHitSoA.h"

namespace mkfitdev::lstin {

  constexpr uint32_t kNoKey = 0xffffffffu;
  constexpr int kMaxTrackHits = 255;  // nHits is uint8 in the LST pLS; longer tracks are counted and dropped

  // per tracker GeomDet index: the device hit input's module table (Surface rotation rows and position), shared with
  // MkFitAlpakaEventOfHitsProducer through the MkFitAlpakaEventOfHitsModuleTableESProducer product (round 7)
  using OTModule = ::mkfitdev::HitModuleDev;

  struct Params {
    float ptCut;
    float bsx, bsy, bsz;
    float k;             // Patatrack's field, GeV^-1 cm^-1 (PixelRecoUtilities::fieldInInvGev): R[cm] = pt / k
    uint32_t nPixelSoA;  // first OT row of the extended hits SoA (= number of pixel rows)
    uint32_t nOTSoA;     // OT rows of the extended hits SoA
    uint32_t nOT;        // legacy OT hits = rows [0, nOT) of the LST hits block
    uint32_t nPixKeys;   // size of the pixel SoA-row -> legacy cluster key map
    int contract;        // mkfitdev::Contract of the OT localToGlobal (1 = kFuseFirst, the stock host pattern)
    int minQuality;      // pixelTrack::Quality (tight = 5, as hltPhase2PixelTracks)
    uint32_t nSeedMap;   // > 0: seedOfTrack[t] = seed index of SoA track t (host seed, or the edm pixel track index
                         // with seedIdxFromTracks; -1: none -> no pLS)
    // round 8 (R7-H2 / R7-M1): pLS momentum scaled by <Bz>_hits / Bz(0) with the menu's closed-form tracker field
    // (interface/math/TkBfield.h): Patatrack's helix uses the field at the origin, the endcap tracks see less
    bool ptFieldCorrection;
    float bz0;  // tkBz(0, 0, 0) (the field Patatrack's k comes from)
    // round 10 (lane stageb): the pLS "last hit" r3LH (pseudo-hit 2, x of pseudo-hit 3) and the momentum p3LH.
    // 0 = round 7-9: the outermost hit itself, p3LH along the helix tangent at the hit's azimuth;
    // 1 = the Patatrack helix point at the outermost hit's transverse radius (the host takes the KF seed state ON
    //     the last hit, not the hit), p3LH the helix tangent there
    int pseudoLH;
    // round 10 (lane stageb): the pLS PCA quantities (PCA point, PCA momentum, dxy, dz, superbin).
    // 0 = Patatrack's PCA (rounds 7-9); 1 = the PCA of the Patatrack helix RE-ANCHORED on the outermost hit (same
    //     curvature, the helix tangent at the hit), as the host: TSCBL of the KF seed state on the last hit
    int pcaAnchor;
  };

  // per pixel track: the LST pLS of option (i) (scratch, one row per SoA track)
  struct PLS {
    int32_t pass;  // 1: becomes a pLS (quality, finite, pt cut); 0: dropped
    int32_t charge;
    int32_t superbin;
    int32_t seedIdx;  // SoA track index, or the host seed index with seedOfTrack
    uint8_t nHits;    // all hits of the track (= see_hitIdx size of the host seed)
    uint8_t nToSoA;
    uint8_t hitDetBits;
    int8_t isQuad;
    int8_t pixelType;
    float ptIn, ptErr, px, py, pz, etaErr, eta, phi, deltaPhi;
    float x[4], y[4], z[4];
    uint32_t detid[4];
    uint16_t clust[4];
    uint32_t idx[4];
  };

  // status counters (counts[]): 0 nPLS (before the LST cap), 1 nHitsIT, 2 nTracks, 3 tight tracks with > kMaxTrackHits or < 3 hits,
  // 4 hits without a legacy key, 5 tracks failing the finite check
  constexpr int kNCounts = 8;

}  // namespace mkfitdev::lstin

namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstin {
  using ::mkfitdev::lstin::OTModule;
  using ::mkfitdev::lstin::Params;
  using ::mkfitdev::lstin::PLS;

  // pass 1: per-track pLS + a deterministic serial scan (pLS index and pseudo-hit offset per track); counts on device
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
                 uint32_t* counts);

  // pass 2: OT hits + scalar, then the pLS rows and their pseudo-hits
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
                  uint32_t const* counts);

  // pass 2 with the OT hits from the device OT rechit SoA (round 9, full stage D): rows [0, nOT) = SoA rows (cluster
  // keys) with its global position (Surface::toGlobal, the producer's contractGlobal), DetId and cluster size
  void launchFillSoA(Queue& queue,
                     ::lst::LSTInputView out,
                     uint32_t maxTracks,
                     uint32_t nPLSCap,
                     ::mkfitdev::OTRecHitSoA::ConstView ot,
                     Params p,
                     PLS const* scratch,
                     uint32_t const* pIdx,
                     uint32_t const* hOff);

  // the OT rows alone (fromHostInput: the rest of the collection is copied from hltInputLST)
  void launchFillOTSoAOnly(Queue& queue, ::lst::LSTInputView out, ::mkfitdev::OTRecHitSoA::ConstView ot, uint32_t nOT);

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::mkfitdev::lstin

#endif
