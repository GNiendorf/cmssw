#include "Stages.h"

#include <cstdio>
#include <cstdlib>

namespace {

  // One CSR: count per key -> exclusive prefix (in place, into offsets) -> scatter with a
  // cursor copy. Mirrors the planned count/prefix/scatter kernel structure (plan section 3
  // minimal-pass pipeline); no sorting, every item lands exactly once.
  void buildCsr(int nKeys, int nItems, const std::vector<int>& key, std::vector<int>& offsets, std::vector<int>& items) {
    offsets.assign(nKeys + 1, 0);
    for (int t = 0; t < nItems; ++t)
      ++offsets[key[t]];
    int running = 0;
    for (int k = 0; k <= nKeys; ++k) {
      int c = offsets[k];
      offsets[k] = running;
      running += c;
    }
    items.resize(nItems);
    std::vector<int> cursor(offsets.begin(), offsets.begin() + nKeys);
    for (int t = 0; t < nItems; ++t)
      items[cursor[key[t]]++] = t;
  }

  void checkCount(const char* label, long long emitted, long long expected) {
    if (emitted != expected) {
      std::fprintf(stderr, "k2BuildEdges: %s emitted %lld != exact %lld\n", label, emitted, expected);
      std::abort();
    }
  }

}  // namespace

void k1BuildIncidence(const LSTEventData& ev, ChainGraph& g) {
  const int nMD = static_cast<int>(ev.md_anchorHitIdx.size());
  const int nLS = static_cast<int>(ev.ls_mdIdx0.size());
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());

  buildCsr(nMD, nT3, ev.t3_md0, g.mdT3OutOffsets, g.mdT3OutItems);
  buildCsr(nMD, nT3, ev.t3_md2, g.mdT3InOffsets, g.mdT3InItems);
  buildCsr(nLS, nT3, ev.t3_lsIdx0, g.lsT3OutOffsets, g.lsT3OutItems);
  buildCsr(nLS, nT3, ev.t3_lsIdx1, g.lsT3InOffsets, g.lsT3InItems);
}

void k2BuildEdges(const LSTEventData& ev, ChainGraph& g) {
  const int nMD = static_cast<int>(g.mdT3OutOffsets.size()) - 1;
  const int nLS = static_cast<int>(g.lsT3OutOffsets.size()) - 1;

  // Degree-arithmetic exact counts (plan 3b): allocation bound with nothing re-executed.
  g.e1CountExact = 0;
  for (int m = 0; m < nMD; ++m) {
    const long long degIn = g.mdT3InOffsets[m + 1] - g.mdT3InOffsets[m];
    const long long degOut = g.mdT3OutOffsets[m + 1] - g.mdT3OutOffsets[m];
    g.e1CountExact += degIn * degOut;
  }
  g.e2CountExact = 0;
  for (int l = 0; l < nLS; ++l) {
    const long long degIn = g.lsT3InOffsets[l + 1] - g.lsT3InOffsets[l];
    const long long degOut = g.lsT3OutOffsets[l + 1] - g.lsT3OutOffsets[l];
    g.e2CountExact += degIn * degOut;
  }

  g.edges.clear();
  g.edges.reserve(static_cast<size_t>(g.e1CountExact + g.e2CountExact));

  // E1 (T5 relation): inner ends at MD m, outer starts at MD m, i.e.
  // t3_md2[inner] == t3_md0[outer] (plan section 3, Quintuplet.h:1764 condition).
  long long e1Emitted = 0, e1SelfSkipped = 0;
  for (int m = 0; m < nMD; ++m) {
    for (int i = g.mdT3InOffsets[m]; i < g.mdT3InOffsets[m + 1]; ++i) {
      const int inner = g.mdT3InItems[i];
      for (int j = g.mdT3OutOffsets[m]; j < g.mdT3OutOffsets[m + 1]; ++j) {
        const int outer = g.mdT3OutItems[j];
        // inner == outer would need md2 == md0 within one T3; guard anyway.
        if (inner == outer) {
          ++e1SelfSkipped;
          continue;
        }
        g.edges.push_back({inner, outer, static_cast<uint8_t>(1)});
        ++e1Emitted;
      }
    }
  }

  // E2 (T4 relation): inner.ls1 == l == outer.ls0. With the AccessHelper MD convention
  // {LS0.md0, LS0.md1, LS1.md1}, sharing LS l means t3_md1[inner] == t3_md0[outer] and
  // t3_md2[inner] == t3_md1[outer]. The pair also being an E1 edge would require
  // t3_md2[inner] == t3_md0[outer] == t3_md1[inner], impossible for a valid T3 (three
  // distinct MDs) -- so for well-formed input the E1/E2 pair sets are disjoint and this
  // guard never fires. It is still exactly the "pair satisfies the T5 condition" test,
  // O(1) with no lookup structure, so it is kept and its fires counted.
  long long e2Emitted = 0, e2SelfSkipped = 0, e2DupOfE1 = 0;
  for (int l = 0; l < nLS; ++l) {
    for (int i = g.lsT3InOffsets[l]; i < g.lsT3InOffsets[l + 1]; ++i) {
      const int inner = g.lsT3InItems[i];
      for (int j = g.lsT3OutOffsets[l]; j < g.lsT3OutOffsets[l + 1]; ++j) {
        const int outer = g.lsT3OutItems[j];
        if (inner == outer) {
          ++e2SelfSkipped;
          continue;
        }
        if (ev.t3_md2[inner] == ev.t3_md0[outer]) {
          ++e2DupOfE1;
          continue;
        }
        g.edges.push_back({inner, outer, static_cast<uint8_t>(2)});
        ++e2Emitted;
      }
    }
  }

  checkCount("E1", e1Emitted + e1SelfSkipped, g.e1CountExact);
  checkCount("E2", e2Emitted + e2SelfSkipped + e2DupOfE1, g.e2CountExact);
}
