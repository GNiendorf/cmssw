#ifndef RecoTracker_LSTCore_src_alpaka_RungProbe_h
#define RecoTracker_LSTCore_src_alpaka_RungProbe_h

// Rung probe (ladder campaign instrumentation, env-gated by LST_PROBE_DIR, inert otherwise).
// For every candidate pair whose hits are ALL in the caller's hit mask, the AUTHORITATIVE selection function is
// run in probe mode (no early return) and the record keeps every cut variable, every threshold, the failed-cut
// bitmask, the verdict of the unmodified (Probe=false) call and whether the object exists in the SoA.

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "MiniDoublet.h"
#include "Segment.h"

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  struct MDProbeRecord {
    int lowerHit, upperHit;  // indices into the input ntuple's ph2_* arrays (hitsBase.idxs)
    int moduleIdx;           // LST lower-module index
    unsigned int detId;      // detId of the lower module
    unsigned int fail;       // MDProbeBit mask from the probe-mode call
    int verdictProbe;        // return value of the probe-mode call (0 = rejected, 1, 2 = loose)
    int verdictAuth;         // return value of the unmodified call, as the creation kernel makes it
    int mdIdx;               // SoA index of the mini-doublet built from this pair, -1 if none
    float dz, drt, dPhi, dPhiChange, miniCut;
  };

  struct LSProbeRecord {
    int hits[4];  // ph2 indices: inner anchor, inner other, outer anchor, outer other
    int innerMD, outerMD;
    int innerModule, outerModule;
    unsigned int fail;  // LSProbeBit mask
    int verdictProbe, verdictAuth;
    int loosePass;  // passLooseSegmentCuts, the counting kernel's twin
    int lsIdx;      // SoA index of the segment, -1 if none
    float pos, lo, hi, dPhi, dPhiCut, dPhiChange;
    float dAlpha[3];
    float dAlphaCut[3];
    float lineResid, lineResidCut;
  };

  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool rungProbeMasked(HitsBaseConst hitsBase,
                                                      unsigned int hit,
                                                      uint8_t const* mask,
                                                      unsigned int maskSize) {
    const unsigned int idx = hitsBase.idxs()[hit];
    return hitsBase.detid()[hit] != kPixelModuleId and idx < maskSize and mask[idx] != 0;
  }

  struct ProbeMiniDoublets {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  HitsBaseConst hitsBase,
                                  HitsExtendedConst hitsExtended,
                                  HitsRangesConst hitsRanges,
                                  MiniDoubletsConst mds,
                                  MiniDoubletsOccupancyConst mdsOccupancy,
                                  ObjectRangesConst ranges,
                                  const float ptCut,
                                  const uint16_t clustSizeCut,
                                  uint8_t const* mask,
                                  unsigned int maskSize,
                                  MDProbeRecord* out,
                                  unsigned int cap,
                                  unsigned int* nOut) const {
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0] == 1));
      for (uint16_t lowerModuleIndex : cms::alpakatools::uniform_elements(acc, modules.nLowerModules())) {
        if (hitsRanges.hitRangesLower()[lowerModuleIndex] == -1)
          continue;
        const int nLowerHits = hitsRanges.hitRangesnLower()[lowerModuleIndex];
        const int nUpperHits = hitsRanges.hitRangesnUpper()[lowerModuleIndex];
        const unsigned int upHitArrayIndex = hitsRanges.hitRangesUpper()[lowerModuleIndex];
        const unsigned int loHitArrayIndex = hitsRanges.hitRangesLower()[lowerModuleIndex];
        ModuleMDData mod = loadModuleMDData(acc, modules, lowerModuleIndex, ptCut);
        const unsigned int firstMD = ranges.miniDoubletModuleIndices()[lowerModuleIndex];
        const unsigned int nMDs = mdsOccupancy.nMDs()[lowerModuleIndex];

        for (int iLo = 0; iLo < nLowerHits; ++iLo) {
          const unsigned int lo = loHitArrayIndex + iLo;
          if (not rungProbeMasked(hitsBase, lo, mask, maskSize))
            continue;
          for (int iUp = 0; iUp < nUpperHits; ++iUp) {
            const unsigned int up = upHitArrayIndex + iUp;
            if (not rungProbeMasked(hitsBase, up, mask, maskSize))
              continue;

            float dz, dphi, dphichange, shiftedX, shiftedY, shiftedZ, noShiftedDphi, noShiftedDphiChange;
            MDProbe probe;
            const int verdictProbe = runMiniDoubletDefaultAlgo<true>(acc,
                                                                     mod,
                                                                     dz,
                                                                     dphi,
                                                                     dphichange,
                                                                     shiftedX,
                                                                     shiftedY,
                                                                     shiftedZ,
                                                                     noShiftedDphi,
                                                                     noShiftedDphiChange,
                                                                     hitsBase.xs()[lo],
                                                                     hitsBase.ys()[lo],
                                                                     hitsBase.zs()[lo],
                                                                     hitsExtended.rts()[lo],
                                                                     hitsBase.xs()[up],
                                                                     hitsBase.ys()[up],
                                                                     hitsBase.zs()[up],
                                                                     hitsExtended.rts()[up],
                                                                     ptCut,
                                                                     hitsBase.clustsize()[lo],
                                                                     hitsBase.clustsize()[up],
                                                                     clustSizeCut,
                                                                     &probe);
            const int verdictAuth = runMiniDoubletDefaultAlgo(acc,
                                                              mod,
                                                              dz,
                                                              dphi,
                                                              dphichange,
                                                              shiftedX,
                                                              shiftedY,
                                                              shiftedZ,
                                                              noShiftedDphi,
                                                              noShiftedDphiChange,
                                                              hitsBase.xs()[lo],
                                                              hitsBase.ys()[lo],
                                                              hitsBase.zs()[lo],
                                                              hitsExtended.rts()[lo],
                                                              hitsBase.xs()[up],
                                                              hitsBase.ys()[up],
                                                              hitsBase.zs()[up],
                                                              hitsExtended.rts()[up],
                                                              ptCut,
                                                              hitsBase.clustsize()[lo],
                                                              hitsBase.clustsize()[up],
                                                              clustSizeCut);
            int mdIdx = -1;
            for (unsigned int j = firstMD; j < firstMD + nMDs; ++j) {
              const unsigned int a = mds.anchorHitIndices()[j];
              const unsigned int b = mds.outerHitIndices()[j];
              if ((a == lo and b == up) or (a == up and b == lo)) {
                mdIdx = static_cast<int>(j);
                break;
              }
            }

            const unsigned int iRec = alpaka::atomicAdd(acc, nOut, 1u, alpaka::hierarchy::Threads{});
            if (iRec >= cap)
              continue;
            MDProbeRecord& r = out[iRec];
            r.lowerHit = static_cast<int>(hitsBase.idxs()[lo]);
            r.upperHit = static_cast<int>(hitsBase.idxs()[up]);
            r.moduleIdx = lowerModuleIndex;
            r.detId = modules.detIds()[lowerModuleIndex];
            r.fail = probe.fail;
            r.verdictProbe = verdictProbe;
            r.verdictAuth = verdictAuth;
            r.mdIdx = mdIdx;
            r.dz = probe.dz;
            r.drt = probe.drt;
            r.dPhi = probe.dPhi;
            r.dPhiChange = probe.dPhiChange;
            r.miniCut = probe.miniCut;
          }
        }
      }
    }
  };

  struct ProbeSegments {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  HitsBaseConst hitsBase,
                                  MiniDoubletsConst mds,
                                  MiniDoubletsOccupancyConst mdsOccupancy,
                                  SegmentsConst segments,
                                  SegmentsOccupancyConst segmentsOccupancy,
                                  ObjectRangesConst ranges,
                                  const float ptCut,
                                  uint8_t const* mask,
                                  unsigned int maskSize,
                                  LSProbeRecord* out,
                                  unsigned int cap,
                                  unsigned int* nOut) const {
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0] == 1));
      for (uint16_t innerLowerModuleIndex : cms::alpakatools::uniform_elements(acc, modules.nLowerModules())) {
        const unsigned int nInnerMDs = mdsOccupancy.nMDs()[innerLowerModuleIndex];
        const uint16_t nConnectedModules = modules.nConnectedModules()[innerLowerModuleIndex];
        if (nInnerMDs == 0 or nConnectedModules == 0)
          continue;
        ModuleSegData innerMod = loadModuleSegData(modules, innerLowerModuleIndex, ptCut);
        const unsigned int firstInner = ranges.miniDoubletModuleIndices()[innerLowerModuleIndex];
        const unsigned int firstLS = ranges.segmentModuleIndices()[innerLowerModuleIndex];
        const unsigned int nLS = segmentsOccupancy.nSegments()[innerLowerModuleIndex];

        for (unsigned int i = 0; i < nInnerMDs; ++i) {
          const unsigned int innerMD = firstInner + i;
          const unsigned int h0 = mds.anchorHitIndices()[innerMD];
          const unsigned int h1 = mds.outerHitIndices()[innerMD];
          if (not rungProbeMasked(hitsBase, h0, mask, maskSize) or not rungProbeMasked(hitsBase, h1, mask, maskSize))
            continue;
          for (uint16_t c = 0; c < nConnectedModules; ++c) {
            const uint16_t outerLowerModuleIndex = modules.moduleMap()[innerLowerModuleIndex][c];
            const unsigned int nOuterMDs = mdsOccupancy.nMDs()[outerLowerModuleIndex];
            if (nOuterMDs == 0)
              continue;
            ModuleSegData outerMod = loadModuleSegData(modules, outerLowerModuleIndex, ptCut);
            const unsigned int firstOuter = ranges.miniDoubletModuleIndices()[outerLowerModuleIndex];
            for (unsigned int o = 0; o < nOuterMDs; ++o) {
              const unsigned int outerMD = firstOuter + o;
              const unsigned int h2 = mds.anchorHitIndices()[outerMD];
              const unsigned int h3 = mds.outerHitIndices()[outerMD];
              if (not rungProbeMasked(hitsBase, h2, mask, maskSize) or
                  not rungProbeMasked(hitsBase, h3, mask, maskSize))
                continue;

              float dPhi = 0, dPhiMin = 0, dPhiMax = 0, dPhiChange = 0, dPhiChangeMin = 0, dPhiChangeMax = 0;
#ifdef CUT_VALUE_DEBUG
              float zLo, zHi, rtLo, rtHi, dAlphaIn, dAlphaOut, dAlphaIO;
#endif
              LSProbe probe;
              const bool verdictProbe = runSegmentDefaultAlgo<true>(acc,
                                                                    innerMod,
                                                                    outerMod,
                                                                    mds,
                                                                    innerMD,
                                                                    outerMD,
                                                                    dPhi,
                                                                    dPhiMin,
                                                                    dPhiMax,
                                                                    dPhiChange,
                                                                    dPhiChangeMin,
                                                                    dPhiChangeMax,
#ifdef CUT_VALUE_DEBUG
                                                                    dAlphaIn,
                                                                    dAlphaOut,
                                                                    dAlphaIO,
                                                                    zLo,
                                                                    zHi,
                                                                    rtLo,
                                                                    rtHi,
#endif
                                                                    ptCut,
                                                                    &probe);
              const bool verdictAuth = runSegmentDefaultAlgo(acc,
                                                             innerMod,
                                                             outerMod,
                                                             mds,
                                                             innerMD,
                                                             outerMD,
                                                             dPhi,
                                                             dPhiMin,
                                                             dPhiMax,
                                                             dPhiChange,
                                                             dPhiChangeMin,
                                                             dPhiChangeMax,
#ifdef CUT_VALUE_DEBUG
                                                             dAlphaIn,
                                                             dAlphaOut,
                                                             dAlphaIO,
                                                             zLo,
                                                             zHi,
                                                             rtLo,
                                                             rtHi,
#endif
                                                             ptCut);
              const bool loosePass = passLooseSegmentCuts(acc, innerMod, outerMod, mds, innerMD, outerMD, ptCut);

              int lsIdx = -1;
              for (unsigned int s = firstLS; s < firstLS + nLS; ++s) {
                if (segments.mdIndices()[s][0] == innerMD and segments.mdIndices()[s][1] == outerMD) {
                  lsIdx = static_cast<int>(s);
                  break;
                }
              }

              const unsigned int iRec = alpaka::atomicAdd(acc, nOut, 1u, alpaka::hierarchy::Threads{});
              if (iRec >= cap)
                continue;
              LSProbeRecord& r = out[iRec];
              r.hits[0] = static_cast<int>(hitsBase.idxs()[h0]);
              r.hits[1] = static_cast<int>(hitsBase.idxs()[h1]);
              r.hits[2] = static_cast<int>(hitsBase.idxs()[h2]);
              r.hits[3] = static_cast<int>(hitsBase.idxs()[h3]);
              r.innerMD = static_cast<int>(innerMD);
              r.outerMD = static_cast<int>(outerMD);
              r.innerModule = innerLowerModuleIndex;
              r.outerModule = outerLowerModuleIndex;
              r.fail = probe.fail;
              r.verdictProbe = verdictProbe;
              r.verdictAuth = verdictAuth;
              r.loosePass = loosePass;
              r.lsIdx = lsIdx;
              r.pos = probe.pos;
              r.lo = probe.lo;
              r.hi = probe.hi;
              r.dPhi = probe.dPhi;
              r.dPhiCut = probe.dPhiCut;
              r.dPhiChange = probe.dPhiChange;
              for (int k = 0; k < 3; ++k) {
                r.dAlpha[k] = probe.dAlpha[k];
                r.dAlphaCut[k] = probe.dAlphaCut[k];
              }
              r.lineResid = probe.lineResid;
              r.lineResidCut = probe.lineResidCut;
            }
          }
        }
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
