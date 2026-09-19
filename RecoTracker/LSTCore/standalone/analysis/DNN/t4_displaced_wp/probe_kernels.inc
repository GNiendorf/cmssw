  // ---- rung 4: T5 and T4 builder probes.  One record per pair of created triplets the builder would pair
  // (T5: shared mini-doublet; T4: shared segment) with every hit in the mask.
  struct T5ProbeRecord {
    int hits[10];
    int mds[5];
    int t3[2];
    int modules[5];
    short lstLayers[5];
    short moduleTypes[5];
    unsigned int fail;
    int verdictProbe, verdictAuth;
    int t5Idx;
    int t3Flags[2];
    int startValid;
    float dnnScore, wp98;
    float dBeta[2];
    float dBetaCut2[2];
    float rzChi2;
    int rzLinear;
    int tight;
    float radii[3];
    float innerCenter[2];
    float t3Scores[6];
    float xyz[15];
    int connectedMax;
    int modTot, modN, modCap;
  };

  struct T4ProbeRecord {
    int hits[8];
    int mds[4];
    int t3[2];
    int modules[4];
    short lstLayers[4];
    short moduleTypes[4];
    unsigned int fail;
    int verdictProbe, verdictAuth;
    int t4Idx;
    int t4IsDup;
    int t3Flags[2];
    int t3PartOf[2];
    int charge[2];
    float scores[3];
    float wpDisp, wpFake;
    float dBeta, dBetaCut2;
    float rzChi2;
    int rzLinear;
    float radii[4];
    float innerCenter[2];
    float t3Scores[6];
    float xyz[12];
    int connectedLSMax;
    int modTot, modN, modCap;
  };

  ALPAKA_FN_ACC ALPAKA_FN_INLINE bool rungProbeT3Masked(HitsBaseConst hitsBase,
                                                        TripletsConst triplets,
                                                        unsigned int t3,
                                                        uint8_t const* mask,
                                                        unsigned int maskSize) {
    for (int k = 0; k < 6; ++k)
      if (not rungProbeMasked(hitsBase, triplets.hitIndices()[t3][k], mask, maskSize))
        return false;
    return true;
  }

  struct ProbeQuintuplets {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  HitsBaseConst hitsBase,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  TripletsOccupancyConst tripletsOcc,
                                  QuintupletsConst quintuplets,
                                  QuintupletsOccupancyConst quintupletsOcc,
                                  ObjectRangesConst ranges,
                                  const float ptCut,
                                  uint8_t const* mask,
                                  unsigned int maskSize,
                                  T5ProbeRecord* out,
                                  unsigned int cap,
                                  unsigned int* nOut) const {
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0] == 1));
      for (uint16_t lm1 : cms::alpakatools::uniform_elements(acc, modules.nLowerModules())) {
        const unsigned int nInner = tripletsOcc.nTriplets()[lm1];
        if (nInner == 0)
          continue;
        const unsigned int innerOff = ranges.tripletModuleIndices()[lm1];
        for (unsigned int i = 0; i < nInner; ++i) {
          const unsigned int it = innerOff + i;
          if (not rungProbeT3Masked(hitsBase, triplets, it, mask, maskSize))
            continue;
          const uint16_t lm2 = triplets.lowerModuleIndices()[it][1];
          const uint16_t lm3 = triplets.lowerModuleIndices()[it][2];
          const unsigned int nOuter = tripletsOcc.nTriplets()[lm3];
          const unsigned int outerOff = ranges.tripletModuleIndices()[lm3];
          const unsigned int md3 = segments.mdIndices()[triplets.segmentIndices()[it][1]][1];
          for (unsigned int o = 0; o < nOuter; ++o) {
            const unsigned int ot = outerOff + o;
            if (segments.mdIndices()[triplets.segmentIndices()[ot][0]][0] != md3)
              continue;
            if (not rungProbeT3Masked(hitsBase, triplets, ot, mask, maskSize))
              continue;
            const uint16_t lm4 = triplets.lowerModuleIndices()[ot][1];
            const uint16_t lm5 = triplets.lowerModuleIndices()[ot][2];

            float innerRadius, outerRadius, bridgeRadius, regCx, regCy, regR, rzChi2, chi2, naChi2, dBeta1, dBeta2,
                dnnScore;
            float t5Embed[Params_T5::kEmbed] = {0.f};
            bool tight = false;
            T5Probe probe;
            const bool verdictProbe = runQuintupletDefaultAlgo<true>(acc, modules, mds, segments, triplets, lm1, lm2, lm3,
                                                                     lm4, lm5, it, ot, innerRadius, outerRadius,
                                                                     bridgeRadius, regCx, regCy, regR, rzChi2, chi2,
                                                                     naChi2, dBeta1, dBeta2, dnnScore, tight, t5Embed,
                                                                     ptCut, &probe);
            float aR, aO, aB, aCx, aCy, aRr, aRz, aChi2, aNa, aD1, aD2, aDnn;
            float aEmbed[Params_T5::kEmbed] = {0.f};
            bool aTight = false;
            const bool verdictAuth = runQuintupletDefaultAlgo(acc, modules, mds, segments, triplets, lm1, lm2, lm3, lm4,
                                                              lm5, it, ot, aR, aO, aB, aCx, aCy, aRr, aRz, aChi2, aNa,
                                                              aD1, aD2, aDnn, aTight, aEmbed, ptCut);
            int t5Idx = -1;
            const int firstT5 = ranges.quintupletModuleIndices()[lm1];
            if (firstT5 != -1) {
              const unsigned int nT5 = quintupletsOcc.nQuintuplets()[lm1];
              for (unsigned int q = firstT5; q < firstT5 + nT5; ++q) {
                if (quintuplets.tripletIndices()[q][0] == it and quintuplets.tripletIndices()[q][1] == ot) {
                  t5Idx = static_cast<int>(q);
                  break;
                }
              }
            }

            const unsigned int iRec = alpaka::atomicAdd(acc, nOut, 1u, alpaka::hierarchy::Threads{});
            if (iRec >= cap)
              continue;
            T5ProbeRecord& r = out[iRec];
            const unsigned int s1 = triplets.segmentIndices()[it][0], s2 = triplets.segmentIndices()[it][1];
            const unsigned int s3 = triplets.segmentIndices()[ot][0], s4 = triplets.segmentIndices()[ot][1];
            const unsigned int md[5] = {segments.mdIndices()[s1][0], segments.mdIndices()[s2][0],
                                        segments.mdIndices()[s2][1], segments.mdIndices()[s3][1],
                                        segments.mdIndices()[s4][1]};
            const uint16_t lm[5] = {lm1, lm2, lm3, lm4, lm5};
            for (int k = 0; k < 5; ++k) {
              r.hits[2 * k] = static_cast<int>(hitsBase.idxs()[mds.anchorHitIndices()[md[k]]]);
              r.hits[2 * k + 1] = static_cast<int>(hitsBase.idxs()[mds.outerHitIndices()[md[k]]]);
              r.mds[k] = static_cast<int>(md[k]);
              r.modules[k] = lm[k];
              r.lstLayers[k] = modules.lstLayers()[lm[k]];
              r.moduleTypes[k] = modules.moduleType()[lm[k]];
              r.xyz[3 * k] = mds.anchorX()[md[k]];
              r.xyz[3 * k + 1] = mds.anchorY()[md[k]];
              r.xyz[3 * k + 2] = mds.anchorZ()[md[k]];
            }
            r.t3[0] = static_cast<int>(it);
            r.t3[1] = static_cast<int>(ot);
            r.fail = probe.fail;
            r.verdictProbe = verdictProbe;
            r.verdictAuth = verdictAuth;
            r.t5Idx = t5Idx;
            r.t3Flags[0] = triplets.flags()[it];
            r.t3Flags[1] = triplets.flags()[ot];
            r.startValid = isValidQuintRegion(modules, lm1);
            r.dnnScore = dnnScore;
            const float eta1 = alpaka::math::abs(acc, mds.anchorEta()[md[0]]);
            const uint8_t ptIdx = (innerRadius * k2Rinv1GeVf * 2 > 5.0f);
            const uint8_t etaIdx = (eta1 > 2.5f) ? (dnn::kEtaBins - 1) : static_cast<unsigned int>(eta1 / dnn::kEtaSize);
            r.wp98 = dnn::t5dnn::kWp98[ptIdx][etaIdx];
            r.dBeta[0] = dBeta1;
            r.dBeta[1] = dBeta2;
            r.dBetaCut2[0] = probe.dBetaCut2[0];
            r.dBetaCut2[1] = probe.dBetaCut2[1];
            r.rzChi2 = rzChi2;
            r.rzLinear = probe.rzLinear;
            r.tight = tight;
            r.radii[0] = innerRadius;
            r.radii[1] = bridgeRadius;
            r.radii[2] = outerRadius;
            r.innerCenter[0] = triplets.centerX()[it];
            r.innerCenter[1] = triplets.centerY()[it];
            r.t3Scores[0] = triplets.fakeScore()[it];
            r.t3Scores[1] = triplets.promptScore()[it];
            r.t3Scores[2] = triplets.displacedScore()[it];
            r.t3Scores[3] = triplets.fakeScore()[ot];
            r.t3Scores[4] = triplets.promptScore()[ot];
            r.t3Scores[5] = triplets.displacedScore()[ot];
            r.connectedMax = static_cast<int>(triplets.connectedMax()[it]);
            r.modTot = static_cast<int>(quintupletsOcc.totOccupancyQuintuplets()[lm1]);
            r.modN = static_cast<int>(quintupletsOcc.nQuintuplets()[lm1]);
            r.modCap = ranges.quintupletModuleOccupancy()[lm1];
          }
        }
      }
    }
  };

  struct ProbeQuadruplets {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  HitsBaseConst hitsBase,
                                  MiniDoubletsConst mds,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  TripletsOccupancyConst tripletsOcc,
                                  QuadrupletsConst quadruplets,
                                  QuadrupletsOccupancyConst quadrupletsOcc,
                                  ObjectRangesConst ranges,
                                  const float ptCut,
                                  uint8_t const* mask,
                                  unsigned int maskSize,
                                  T4ProbeRecord* out,
                                  unsigned int cap,
                                  unsigned int* nOut) const {
      ALPAKA_ASSERT_ACC((alpaka::getWorkDiv<alpaka::Grid, alpaka::Blocks>(acc)[0] == 1));
      for (uint16_t lm1 : cms::alpakatools::uniform_elements(acc, modules.nLowerModules())) {
        const unsigned int nInner = tripletsOcc.nTriplets()[lm1];
        if (nInner == 0)
          continue;
        const unsigned int innerOff = ranges.tripletModuleIndices()[lm1];
        for (unsigned int i = 0; i < nInner; ++i) {
          const unsigned int it = innerOff + i;
          if (not rungProbeT3Masked(hitsBase, triplets, it, mask, maskSize))
            continue;
          const uint16_t lm2 = triplets.lowerModuleIndices()[it][1];
          const uint16_t lm3 = triplets.lowerModuleIndices()[it][2];
          const unsigned int nOuter = tripletsOcc.nTriplets()[lm2];
          const unsigned int outerOff = ranges.tripletModuleIndices()[lm2];
          const unsigned int sharedSeg = triplets.segmentIndices()[it][1];
          for (unsigned int o = 0; o < nOuter; ++o) {
            const unsigned int ot = outerOff + o;
            if (triplets.segmentIndices()[ot][0] != sharedSeg)
              continue;
            if (not rungProbeT3Masked(hitsBase, triplets, ot, mask, maskSize))
              continue;
            const uint16_t lm4 = triplets.lowerModuleIndices()[ot][2];

            float rzChi2, dBeta, naChi2, regCx, regCy, regR, naR, chi2, promptScore, displacedScore, fakeScore;
            T4Probe probe;
            const bool verdictProbe = runQuadrupletDefaultAlgo<true>(acc, modules, mds, segments, triplets, lm1, lm2, lm3,
                                                                     lm4, it, ot, regCx, regCy, regR, naR, chi2, ptCut,
                                                                     rzChi2, naChi2, dBeta, promptScore, displacedScore,
                                                                     fakeScore, &probe);
            float aRz, aD, aNa, aCx, aCy, aR, aNaR, aChi2, aP, aDs, aF;
            const bool verdictAuth = runQuadrupletDefaultAlgo(acc, modules, mds, segments, triplets, lm1, lm2, lm3, lm4,
                                                              it, ot, aCx, aCy, aR, aNaR, aChi2, ptCut, aRz, aNa, aD, aP,
                                                              aDs, aF);
            int t4Idx = -1;
            int t4IsDup = -1;
            const int firstT4 = ranges.quadrupletModuleIndices()[lm1];
            if (firstT4 != -1) {
              const unsigned int nT4 = quadrupletsOcc.nQuadruplets()[lm1];
              for (unsigned int q = firstT4; q < firstT4 + nT4; ++q) {
                if (quadruplets.tripletIndices()[q][0] == it and quadruplets.tripletIndices()[q][1] == ot) {
                  t4Idx = static_cast<int>(q);
                  t4IsDup = quadruplets.isDup()[q];
                  break;
                }
              }
            }

            const unsigned int iRec = alpaka::atomicAdd(acc, nOut, 1u, alpaka::hierarchy::Threads{});
            if (iRec >= cap)
              continue;
            T4ProbeRecord& r = out[iRec];
            const unsigned int s1 = triplets.segmentIndices()[it][0], s2 = triplets.segmentIndices()[it][1];
            const unsigned int s3 = triplets.segmentIndices()[ot][1];
            const unsigned int md[4] = {segments.mdIndices()[s1][0], segments.mdIndices()[s2][0],
                                        segments.mdIndices()[s2][1], segments.mdIndices()[s3][1]};
            const uint16_t lm[4] = {lm1, lm2, lm3, lm4};
            for (int k = 0; k < 4; ++k) {
              r.hits[2 * k] = static_cast<int>(hitsBase.idxs()[mds.anchorHitIndices()[md[k]]]);
              r.hits[2 * k + 1] = static_cast<int>(hitsBase.idxs()[mds.outerHitIndices()[md[k]]]);
              r.mds[k] = static_cast<int>(md[k]);
              r.modules[k] = lm[k];
              r.lstLayers[k] = modules.lstLayers()[lm[k]];
              r.moduleTypes[k] = modules.moduleType()[lm[k]];
              r.xyz[3 * k] = mds.anchorX()[md[k]];
              r.xyz[3 * k + 1] = mds.anchorY()[md[k]];
              r.xyz[3 * k + 2] = mds.anchorZ()[md[k]];
            }
            r.t3[0] = static_cast<int>(it);
            r.t3[1] = static_cast<int>(ot);
            r.fail = probe.fail;
            r.verdictProbe = verdictProbe;
            r.verdictAuth = verdictAuth;
            r.t4Idx = t4Idx;
            r.t4IsDup = t4IsDup;
            r.t3Flags[0] = triplets.flags()[it];
            r.t3Flags[1] = triplets.flags()[ot];
            r.t3PartOf[0] = (triplets.partOfT5()[it] ? 1 : 0) | (triplets.partOfPT5()[it] ? 2 : 0) |
                            (triplets.partOfPT3()[it] ? 4 : 0);
            r.t3PartOf[1] = (triplets.partOfT5()[ot] ? 1 : 0) | (triplets.partOfPT5()[ot] ? 2 : 0) |
                            (triplets.partOfPT3()[ot] ? 4 : 0);
            r.charge[0] = triplets.charge()[it];
            r.charge[1] = triplets.charge()[ot];
            r.scores[0] = fakeScore;
            r.scores[1] = promptScore;
            r.scores[2] = displacedScore;
            const float eta1 = alpaka::math::abs(acc, mds.anchorEta()[md[0]]);
            const float t4pt = (triplets.radius()[it] + triplets.radius()[ot]) * k2Rinv1GeVf;
            const uint8_t ptIdx = (t4pt > 5.f);
            const uint8_t etaIdx = (eta1 > 2.5f) ? (dnn::t4dnn::kEtaBins - 1) : static_cast<unsigned int>(eta1 / 0.1f);
            r.wpDisp = dnn::t4dnn::kWp_displaced[ptIdx][etaIdx];
            r.wpFake = dnn::t4dnn::kWp_fake[ptIdx][etaIdx];
            r.dBeta = dBeta;
            r.dBetaCut2 = probe.dBetaCut2;
            r.rzChi2 = rzChi2;
            r.rzLinear = probe.rzLinear;
            r.radii[0] = triplets.radius()[it];
            r.radii[1] = triplets.radius()[ot];
            r.radii[2] = regR;
            r.radii[3] = naR;
            r.innerCenter[0] = triplets.centerX()[it];
            r.innerCenter[1] = triplets.centerY()[it];
            r.t3Scores[0] = triplets.fakeScore()[it];
            r.t3Scores[1] = triplets.promptScore()[it];
            r.t3Scores[2] = triplets.displacedScore()[it];
            r.t3Scores[3] = triplets.fakeScore()[ot];
            r.t3Scores[4] = triplets.promptScore()[ot];
            r.t3Scores[5] = triplets.displacedScore()[ot];
            r.connectedLSMax = static_cast<int>(triplets.connectedLSMax()[it]);
            r.modTot = static_cast<int>(quadrupletsOcc.totOccupancyQuadruplets()[lm1]);
            r.modN = static_cast<int>(quadrupletsOcc.nQuadruplets()[lm1]);
            r.modCap = ranges.quadrupletModuleOccupancy()[lm1];
          }
        }
      }
    }
  };
