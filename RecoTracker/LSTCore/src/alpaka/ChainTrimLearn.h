#ifndef RecoTracker_LSTCore_src_alpaka_ChainTrimLearn_h
#define RecoTracker_LSTCore_src_alpaka_ChainTrimLearn_h

#include <cstdint>

#include "HeterogeneousCore/AlpakaInterface/interface/workdivision.h"

#include "RecoTracker/LSTCore/interface/alpaka/Common.h"
#include "RecoTracker/LSTCore/interface/ChainConfig.h"
#include "RecoTracker/LSTCore/interface/ChainsSoA.h"

#include "ChainGate.h"
#include "ChainWeld.h"

// Terminal trim: drop an end node of a welded chain when the chain head says the shorter chain is
// the more credible track. It runs between the weld (ChainWeld.h) and the gate (ChainGate.h), and
// it edits the chain in place -- node offset, node count, MD union, layer count and score.
//
// THE DESIGN PRINCIPLE IS THAT ONLY THE CHOICE IS LEARNED. A chain has exactly three terminal
// variants and all three are constructed MECHANICALLY: the full chain, the same chain with its
// innermost node dropped, and with its outermost node dropped. Each edit is an O(1) endpoint move
// inside the chain's own allocation -- the outer-dropped MD union is the PREFIX of the full
// first-appearance union over the first nNodes - 1 members, and the inner-dropped union is the same
// walk started one node in. No network proposes a variant and none can invent one. What the head
// decides is only which of the three to keep, and it decides it on the objective the pipeline
// actually cares about (is this a real track) using the SAME feature builder and the SAME frozen
// chain head the chain row is later gated with, rather than on a hand-set threshold over a proxy.
//
// ChainWeld.h's ChainTrimTerminals is the alternative rule, kept as trimMode 0: it takes the same
// decision from the ratio of combined helix chi2 before and after a drop. ChainConfig::trimMode
// selects between them and over the guard variants; mode 5, the default, requires the winning
// variant to beat the full chain by trimMarginGap logit units rather than merely to be ahead.
//
// ChainVariantProbe is a diagnostic that runs only when its dump file is named in the environment;
// ChainTrimLearned is the deployed kernel.

namespace ALPAKA_ACCELERATOR_NAMESPACE::lst {

  namespace chaintrim {
    // Layout of the diagnostic dump. Per-variant block: nMD, nLayers, dcaXY, 3 logits, then the
    // 25-float feature row.
    constexpr int kVarWords = 6 + Params_ChainFeat::kFeatures;
    // Per-chain row: the chi2 of the three variants, then the three blocks.
    constexpr int kProbeWords = 3 + 3 * kVarWords;
    constexpr int kVariants = 3;  // 0 = full, 1 = inner terminal dropped, 2 = outer terminal dropped
  }  // namespace chaintrim

  // Build the MD union of a node run exactly as the weld does: a deduped first-appearance walk over
  // the member triplets' three mini-doublets, innermost member first. Returns the layer count and
  // fills `unionOut` with the union, whose size lands in `nMDOut`.
  //
  // `unionOut` may alias the chain's own MD list, which is what makes the OUTER variant free: the
  // walk writes a position only after reading every position before it, so re-deriving a prefix of
  // a list in place is safe.
  ALPAKA_FN_ACC ALPAKA_FN_INLINE int chainVariantUnion(ModulesConst modules,
                                                       MiniDoubletsConst miniDoublets,
                                                       SegmentsConst segments,
                                                       TripletsConst triplets,
                                                       ChainNodesConst nodes,
                                                       ChainItemsConst items,
                                                       uint32_t nodeOffset,
                                                       int nNodes,
                                                       uint32_t* unionOut,
                                                       int& nMDOut) {
    int nMDs = 0;
    uint32_t layerMask = 0u;
    for (int k = 0; k < nNodes; ++k) {
      uint32_t const tripletIdx = nodes.tripletIndex()[items.nodeItems()[nodeOffset + k]];
      unsigned int firstMD, midMD, lastMD;
      chainNodeMDs(triplets, segments, tripletIdx, firstMD, midMD, lastMD);
      unsigned int const mdTriple[3] = {firstMD, midMD, lastMD};
      for (int tripleIdx = 0; tripleIdx < 3; ++tripleIdx) {
        uint32_t const mdIndex = static_cast<uint32_t>(mdTriple[tripleIdx]);
        bool seen = false;
        for (int existingIdx = 0; existingIdx < nMDs && !seen; ++existingIdx)
          seen = (unionOut[existingIdx] == mdIndex);
        if (!seen) {
          unionOut[nMDs] = mdIndex;
          ++nMDs;
          layerMask |= (1u << chainMdLayer(modules, miniDoublets, mdIndex));
        }
      }
    }
    nMDOut = nMDs;
    int nLayers = 0;  // popcount of the layer mask
    for (uint32_t bits = layerMask; bits != 0u; bits &= bits - 1u)
      ++nLayers;
    return nLayers;
  }

  // Diagnostic dump of the trim decision (pure observation; the host only allocates its buffers and
  // launches it when the dump is requested). For every chain with nNodes >= 3 it builds the three
  // variants, records the combined-fit chi2 the chi2 rule would have compared, and scores each
  // variant with the deployed feature builder and head. It changes nothing.
  struct ChainVariantProbe {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst miniDoublets,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainItemsConst items,
                                  ChainsConst chains,
                                  uint32_t* innerMD,
                                  float* probeOut) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        float* probeRow = probeOut + static_cast<size_t>(chainIdx) * chaintrim::kProbeWords;
        for (int i = 0; i < chaintrim::kProbeWords; ++i)
          probeRow[i] = 0.f;

        int const nNodes = chains.nNodes()[chainIdx];
        if (nNodes < 3)
          continue;
        uint32_t const nodeOffset = chains.nodeOffset()[chainIdx];
        int const nMDs = chains.nMDs()[chainIdx];
        uint32_t const mdBase = 3u * nodeOffset;
        uint32_t const* fullUnion = &items.mdItems()[mdBase];

        // Outer-dropped union: the prefix of the full union over the first nNodes - 1 members. It
        // is read out of the chain's own list, so only its LENGTH and layer mask need computing.
        int nMDOuter = 0;
        uint32_t layerMaskOuter = 0u;
        for (int k = 0; k + 1 < nNodes; ++k) {
          uint32_t const tripletIdx = nodes.tripletIndex()[items.nodeItems()[nodeOffset + k]];
          unsigned int firstMD, midMD, lastMD;
          chainNodeMDs(triplets, segments, tripletIdx, firstMD, midMD, lastMD);
          unsigned int const mdTriple[3] = {firstMD, midMD, lastMD};
          for (int tripleIdx = 0; tripleIdx < 3; ++tripleIdx) {
            uint32_t const mdIndex = static_cast<uint32_t>(mdTriple[tripleIdx]);
            bool seen = false;
            for (int existingIdx = 0; existingIdx < nMDOuter && !seen; ++existingIdx)
              seen = (fullUnion[existingIdx] == mdIndex);
            if (!seen) {
              ++nMDOuter;
              layerMaskOuter |= (1u << chainMdLayer(modules, miniDoublets, mdIndex));
            }
          }
        }
        int nLayersOuter = 0;
        for (uint32_t bits = layerMaskOuter; bits != 0u; bits &= bits - 1u)
          ++nLayersOuter;

        int nMDInner = 0;
        int const nLayersInner = chainVariantUnion(modules,
                                                   miniDoublets,
                                                   segments,
                                                   triplets,
                                                   nodes,
                                                   items,
                                                   nodeOffset + 1u,
                                                   nNodes - 1,
                                                   &innerMD[mdBase],
                                                   nMDInner);

        // Variant 0 = full, 1 = inner terminal dropped, 2 = outer terminal dropped.
        uint32_t const variantOffset[chaintrim::kVariants] = {nodeOffset, nodeOffset + 1u, nodeOffset};
        int const variantNodes[chaintrim::kVariants] = {nNodes, nNodes - 1, nNodes - 1};
        uint32_t const* variantMDs[chaintrim::kVariants] = {fullUnion, &innerMD[mdBase], fullUnion};
        int const variantNMD[chaintrim::kVariants] = {nMDs, nMDInner, nMDOuter};
        int const variantLayers[chaintrim::kVariants] = {chains.nLayers()[chainIdx], nLayersInner, nLayersOuter};

        for (int variantIdx = 0; variantIdx < chaintrim::kVariants; ++variantIdx) {
          probeRow[variantIdx] = static_cast<float>(
              chainFitChi2Combined(acc, miniDoublets, variantMDs[variantIdx], variantNMD[variantIdx]));
          float features[Params_ChainFeat::kFeatures];
          float dca = 1e9f;
          chainBuildFeatures(acc,
                             modules,
                             miniDoublets,
                             segments,
                             triplets,
                             nodes,
                             edges,
                             mdIncidence,
                             lsIncidence,
                             items,
                             variantOffset[variantIdx],
                             variantNodes[variantIdx],
                             variantMDs[variantIdx],
                             variantNMD[variantIdx],
                             variantLayers[variantIdx],
                             features,
                             dca);
          float logits[dnn::chain3mlp::kOutput];
          chainGateLogits(acc, features, dca, logits);
          float* variantBlock = probeRow + 3 + variantIdx * chaintrim::kVarWords;
          variantBlock[0] = static_cast<float>(variantNMD[variantIdx]);
          variantBlock[1] = static_cast<float>(variantLayers[variantIdx]);
          variantBlock[2] = dca;
          variantBlock[3] = logits[0];
          variantBlock[4] = logits[1];
          variantBlock[5] = logits[2];
          for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
            variantBlock[6 + i] = features[i];
        }
      }
    }
  };

  // The terminal trim, with the decision taken by the chain head. Each variant's 25-float row is
  // built by the deployed feature builder, scored by the deployed frozen head, and the variant with
  // the largest realness margin mX = max(zPrompt, zDisp) - zFake wins. Ties keep the FULL chain and
  // inner beats outer on an exact tie, so the outcome does not depend on evaluation order.
  //
  // The edit is an O(1) endpoint move (inner drop = nodeOffset + 1, outer drop = nNodes - 1), both
  // inside the chain's original 3 * nNodes MD allocation, so no reallocation and no compaction is
  // involved. Only chains with nNodes >= 3 are eligible: dropping a terminal of a 2-node chain
  // would leave a lone triplet, which is not a chain.
  //
  // No layer floor is needed as a constant: a chain with nNodes >= 3 whose terminal is dropped
  // still spans >= 4 layers by construction of the weld, which is the emission floor anyway. So
  // config.trimMinLayersAfter exists only to FORBID the 5 -> 4 layer move, a policy choice, and it is
  // exposed as trimMode 2.
  struct ChainTrimLearned {
    ALPAKA_FN_ACC void operator()(Acc1D const& acc,
                                  ModulesConst modules,
                                  MiniDoubletsConst miniDoublets,
                                  SegmentsConst segments,
                                  TripletsConst triplets,
                                  ChainNodesConst nodes,
                                  ChainEdgesConst edges,
                                  ChainIncidenceConst mdIncidence,
                                  ChainIncidenceConst lsIncidence,
                                  ChainItemsConst constItems,
                                  Chains chains,
                                  ChainItems items,
                                  ChainConfig config) const {
      uint32_t const nChains = static_cast<uint32_t>(chains.metadata().size());

      for (uint32_t chainIdx : cms::alpakatools::uniform_elements(acc, nChains)) {
        int const nNodes = chains.nNodes()[chainIdx];
        if (nNodes < 3)
          continue;
        uint32_t const nodeOffset = chains.nodeOffset()[chainIdx];
        int const nMDs = chains.nMDs()[chainIdx];
        uint32_t const mdBase = 3u * nodeOffset;
        uint32_t const* fullUnion = &items.mdItems()[mdBase];

        if (config.trimMode == 3) {
          // The concentrating guard: a chain that already fits well is left alone.
          double const chi2Full = chainFitChi2Combined(acc, miniDoublets, fullUnion, nMDs);
          if (chi2Full <= static_cast<double>(config.trimAbsChi2))
            continue;
        }

        // Outer-dropped union: the PREFIX of the full first-appearance union over the first
        // nNodes - 1 members, so only its length and layer mask have to be recomputed. The assert
        // states that prefix property -- the walk must rediscover the chain's own MDs in order.
        int nMDOuter = 0;
        uint32_t layerMaskOuter = 0u;
        for (int k = 0; k + 1 < nNodes; ++k) {
          uint32_t const tripletIdx = nodes.tripletIndex()[constItems.nodeItems()[nodeOffset + k]];
          unsigned int firstMD, midMD, lastMD;
          chainNodeMDs(triplets, segments, tripletIdx, firstMD, midMD, lastMD);
          unsigned int const mdTriple[3] = {firstMD, midMD, lastMD};
          for (int tripleIdx = 0; tripleIdx < 3; ++tripleIdx) {
            uint32_t const mdIndex = static_cast<uint32_t>(mdTriple[tripleIdx]);
            bool seen = false;
            for (int existingIdx = 0; existingIdx < nMDOuter && !seen; ++existingIdx)
              seen = (fullUnion[existingIdx] == mdIndex);
            if (!seen) {
              ALPAKA_ASSERT_ACC(fullUnion[nMDOuter] == mdIndex);
              ++nMDOuter;
              layerMaskOuter |= (1u << chainMdLayer(modules, miniDoublets, mdIndex));
            }
          }
        }
        int nLayersOuter = 0;
        for (uint32_t bits = layerMaskOuter; bits != 0u; bits &= bits - 1u)
          ++nLayersOuter;

        // Inner-dropped union, built into the per-chain scratch region so the read of the full
        // union stays hazard-free; copied into place only if it wins.
        int nMDInner = 0;
        int const nLayersInner = chainVariantUnion(modules,
                                                   miniDoublets,
                                                   segments,
                                                   triplets,
                                                   nodes,
                                                   constItems,
                                                   nodeOffset + 1u,
                                                   nNodes - 1,
                                                   &items.mdScratch()[mdBase],
                                                   nMDInner);

        // Variant 0 = full, 1 = inner terminal dropped, 2 = outer terminal dropped. The index is
        // also what lands in chains.trimAction().
        uint32_t const variantOffset[chaintrim::kVariants] = {nodeOffset, nodeOffset + 1u, nodeOffset};
        int const variantNodes[chaintrim::kVariants] = {nNodes, nNodes - 1, nNodes - 1};
        uint32_t const* variantMDs[chaintrim::kVariants] = {fullUnion, &items.mdScratch()[mdBase], fullUnion};
        int const variantNMD[chaintrim::kVariants] = {nMDs, nMDInner, nMDOuter};
        int const variantLayers[chaintrim::kVariants] = {chains.nLayers()[chainIdx], nLayersInner, nLayersOuter};

        int chosenVariant = 0;
        // trimMode 4 is a CONTROL, not a candidate rule: the same unguarded 3-way argmax the head
        // takes, decided by the chi2 proxy instead. It separates "the head chooses better" from
        // "trimming more is better".
        if (config.trimMode == 4) {
          double bestChi2 = 0.0;
          for (int variantIdx = 0; variantIdx < chaintrim::kVariants; ++variantIdx) {
            double const chi2 = chainFitChi2Combined(acc, miniDoublets, variantMDs[variantIdx], variantNMD[variantIdx]);
            if (variantIdx == 0 || chi2 < bestChi2) {
              bestChi2 = chi2;
              chosenVariant = variantIdx;
            }
          }
          if (chosenVariant == 0)
            continue;
          uint32_t const newOffset = (chosenVariant == 1) ? nodeOffset + 1u : nodeOffset;
          float edgeSum = 0.f;
          for (int k = 0; k < nNodes - 2; ++k)
            edgeSum += edges.logOdds()[constItems.edgeItems()[newOffset + k]];
          if (chosenVariant == 1) {
            for (int mdSlot = 0; mdSlot < nMDInner; ++mdSlot)
              items.mdItems()[3u * newOffset + mdSlot] = items.mdScratch()[mdBase + mdSlot];
            chains.nMDs()[chainIdx] = static_cast<uint16_t>(nMDInner);
          } else {
            chains.nMDs()[chainIdx] = static_cast<uint16_t>(nMDOuter);
          }
          chains.nodeOffset()[chainIdx] = newOffset;
          chains.nNodes()[chainIdx] = static_cast<uint16_t>(nNodes - 1);
          chains.nLayers()[chainIdx] = static_cast<uint8_t>(variantLayers[chosenVariant]);
          chains.score()[chainIdx] = edgeSum + config.lambdaLen * static_cast<float>(variantLayers[chosenVariant]);
          chains.trimAction()[chainIdx] = static_cast<int8_t>(chosenVariant);
          continue;
        }
        float bestMargin = 0.f;
        float fullMargin = 0.f;
        // The winning variant's feature row and dcaXY are kept rather than left to the feature
        // kernel. That kernel would build the SAME row from the SAME geometry -- the trim writes
        // nodeOffset / nNodes / nMDs / nLayers before it runs -- so keeping the row is an identity,
        // not an approximation, and it removes one of the four feature builds an eligible chain
        // would otherwise pay for.
        float bestFeatures[Params_ChainFeat::kFeatures];
        float fullFeatures[Params_ChainFeat::kFeatures];
        float bestDca = 1e9f;
        float fullDca = 1e9f;
        for (int variantIdx = 0; variantIdx < chaintrim::kVariants; ++variantIdx) {
          if (variantIdx > 0 && config.trimMode == 2 && variantLayers[variantIdx] < config.trimMinLayersAfter)
            continue;
          float features[Params_ChainFeat::kFeatures];
          float dca = 1e9f;
          chainBuildFeatures(acc,
                             modules,
                             miniDoublets,
                             segments,
                             triplets,
                             nodes,
                             edges,
                             mdIncidence,
                             lsIncidence,
                             constItems,
                             variantOffset[variantIdx],
                             variantNodes[variantIdx],
                             variantMDs[variantIdx],
                             variantNMD[variantIdx],
                             variantLayers[variantIdx],
                             features,
                             dca);
          float logits[dnn::chain3mlp::kOutput];
          chainGateLogits(acc, features, dca, logits);
          float const margin = alpaka::math::max(acc, logits[1], logits[2]) - logits[0];
          if (variantIdx == 0)
            fullMargin = margin;
          if (variantIdx == 0 || margin > bestMargin) {
            bestMargin = margin;
            chosenVariant = variantIdx;
            for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
              bestFeatures[i] = features[i];
            bestDca = dca;
          }
          if (variantIdx == 0) {
            for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
              fullFeatures[i] = features[i];
            fullDca = dca;
          }
        }
        // trimMode 5: the head must be DECISIVE, not merely ahead, before a node is dropped. One
        // constant, where the chi2 rule needs four.
        if (config.trimMode == 5 && bestMargin - fullMargin < config.trimMarginGap) {
          chosenVariant = 0;
          for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
            bestFeatures[i] = fullFeatures[i];
          bestDca = fullDca;
        }
        // Publish the winner's row either way: whether or not the chain is edited, the row it will
        // be gated on has already been built here.
        for (int i = 0; i < Params_ChainFeat::kFeatures; ++i)
          chains.features()[chainIdx][i] = bestFeatures[i];
        chains.dcaXY()[chainIdx] = bestDca;
        chains.featValid()[chainIdx] = 1u;
        if (chosenVariant == 0)
          continue;

        uint32_t const newOffset = (chosenVariant == 1) ? nodeOffset + 1u : nodeOffset;
        int const newNodes = nNodes - 1;
        float edgeSum = 0.f;
        for (int k = 0; k < newNodes - 1; ++k)
          edgeSum += edges.logOdds()[constItems.edgeItems()[newOffset + k]];

        if (chosenVariant == 1) {
          uint32_t const newMdBase = 3u * newOffset;
          for (int mdSlot = 0; mdSlot < nMDInner; ++mdSlot)
            items.mdItems()[newMdBase + mdSlot] = items.mdScratch()[mdBase + mdSlot];
          chains.nMDs()[chainIdx] = static_cast<uint16_t>(nMDInner);
        } else {
          chains.nMDs()[chainIdx] = static_cast<uint16_t>(nMDOuter);
        }
        chains.nodeOffset()[chainIdx] = newOffset;
        chains.nNodes()[chainIdx] = static_cast<uint16_t>(newNodes);
        chains.nLayers()[chainIdx] = static_cast<uint8_t>(variantLayers[chosenVariant]);
        chains.score()[chainIdx] = edgeSum + config.lambdaLen * static_cast<float>(variantLayers[chosenVariant]);
        chains.trimAction()[chainIdx] = static_cast<int8_t>(chosenVariant);
      }
    }
  };

}  // namespace ALPAKA_ACCELERATOR_NAMESPACE::lst

#endif
