#ifndef PROTOTYPE_TRIM_H
#define PROTOTYPE_TRIM_H

// ANGLE B2 -- TERMINAL TRIM (the fake -> efficiency converter).
//
// M13 anatomy: 66% of the residual fake chain TCs are "contaminated" -- a real track with
// ONE WRONG ARM welded on (harness matchFrac 0.5-0.75, i.e. the majority of the hit list
// is one sim's but the coverage misses the > 0.75 harness threshold). Those chains are not
// junk to be killed; they are TRUE tracks carrying a parasitic terminal T3. Killing them
// costs the track; TRIMMING the parasite converts the fake into a true.
//
// Mechanism (post-K6 weld, PRE chain-gate, PRE K9 claim -- so the gate, the DCA split, the
// 3-class margins and the claim all see the TRIMMED object; nothing downstream needs to
// know the trim happened):
//   for each chain with >= 3 member T3s:
//     chi2Full = full-chain xy Kasa circle chi2/hit + rz line chi2/hit over the chain's MD
//                anchor hits (arithmetic IDENTICAL to ChainFeatures features 5 + 6);
//     for each TERMINAL member (innermost, outermost): recompute the same combined chi2
//                over the MD union of the REMAINING members (K6 first-appearance order);
//     eligible iff the remainder still has >= 2 T3s (guaranteed at nNodes >= 3) and
//                >= minLayersAfter (4) distinct layers;
//     trim the eligible end with the LARGER improvement factor chi2Full/chi2Remaining iff
//                that factor > ttFactor (-TT).
// The trimmed chain is rebuilt exactly as K6 would have emitted it: node list minus the
// terminal, weld-edge list minus the terminal edge, MD union recomputed by first
// appearance, nLayers recomputed by layer bitmask popcount, and
//   score = sum(remaining weld-edge logOdds) + lambdaLen * nLayersAfter
// (the K6 score contract). Chains that are not trimmed are copied VERBATIM, so with
// ttFactor <= 0 / trim disabled the Chains object is bit-identical to K6's output.

#include <cstdint>
#include <vector>

#include "EventData.h"
#include "Stages.h"

struct TrimStats {
  long long nExamined = 0;   // chains with >= 3 nodes (trim candidates)
  long long nTrimInner = 0;  // innermost terminal dropped
  long long nTrimOuter = 0;  // outermost terminal dropped
};

// Combined chain fit chi2/hit over an MD list: xy Kasa circle chi2/hit + rz line chi2/hit,
// both in cm^2, both with the exact ChainFeatures.cc guards (degenerate -> 0 contribution).
// Optional split outputs for the offline TT study.
double chainFitChi2Combined(
    const LSTEventData& ev, const int* mdItems, int nMD, double* xyOut = nullptr, double* rzOut = nullptr);

// In-place terminal trim (contract above). `action` (optional, resized to nChains) records
// 0 = untouched, 1 = innermost dropped, 2 = outermost dropped.
//
// absChi2Min (-TA) is the CONCENTRATING guard, measured offline on the trimdump ledger: the
// ratio test alone is volume-only (its fake->true conversion PURITY is flat at ~5.7% for
// every -TT from 1 to 30, so -TT buys fake and pays track length at a fixed exchange rate).
// Requiring the FULL chain's combined chi2/hit to exceed an absolute floor restricts the
// trim to chains that genuinely mis-fit: at absChi2Min = 3 cm^2 the purity is 11.9% and the
// conversions-per-wasted-trim ratio 2.5x better, i.e. ~3x the fake conversions at equal trim
// volume (and equal length cost). 0 = off.
void k6TrimTerminals(const LSTEventData& ev,
                     const EdgeScores& s,
                     float lambdaLen,
                     float ttFactor,
                     int minLayersAfter,
                     float absChi2Min,
                     Chains& chains,
                     std::vector<int8_t>* action,
                     TrimStats& st);

// Offline TT-study helper (trimdump mode only): expand every chain with >= 3 nodes into
// THREE Chains entries -- variant 0 = full, 1 = innermost dropped, 2 = outermost dropped --
// so labelChainsHarness() can be run once over all three and the harness matchFrac of each
// variant compared directly. srcChain/variant are parallel to the output chain index.
void buildTrimStudyChains(const LSTEventData& ev,
                          const Chains& chains,
                          const EdgeScores& s,
                          float lambdaLen,
                          Chains& out,
                          std::vector<int>& srcChain,
                          std::vector<int8_t>& variant);

#endif
