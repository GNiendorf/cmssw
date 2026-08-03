// M20 (T3ATTACH-BUILD): map-candidate ingestion. No physics here -- this file only turns
// an external (t3Row, plsRow) candidate list into the per-event CSR the enumeration reads.
// The binned index (mode 1) is built in PixelAttach.cc, where the helix propagation the
// arcs need already lives.

#include "PixelAttachCand.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr char kBinMagic[8] = {'T', '3', 'P', 'A', 'I', 'R', 'S', '1'};

bool readBinary(std::FILE* fp, MapCandFile& out) {
  out.binary = true;
  while (true) {
    int hdr[4];
    const std::size_t got = std::fread(hdr, sizeof(int), 4, fp);
    if (got == 0)
      return true;  // clean end of file
    if (got != 4) {
      std::fprintf(stderr, "k8LoadMapCandFile: truncated event header (%zu of 4 ints)\n", got);
      return false;
    }
    const int nPairs = hdr[3];
    if (nPairs < 0) {
      std::fprintf(stderr, "k8LoadMapCandFile: negative nPairs %d\n", nPairs);
      return false;
    }
    const int begin = static_cast<int>(out.t3.size());
    out.t3.resize(begin + nPairs);
    out.pls.resize(begin + nPairs);
    for (int i = 0; i < nPairs; ++i) {
      int rec[2];
      if (std::fread(rec, sizeof(int), 2, fp) != 2) {
        std::fprintf(stderr, "k8LoadMapCandFile: truncated pair record %d of %d\n", i, nPairs);
        return false;
      }
      out.t3[begin + i] = rec[0];
      out.pls[begin + i] = rec[1];
    }
    MapCandFile::Key k;
    k.run = static_cast<unsigned int>(hdr[0]);
    k.lumi = static_cast<unsigned int>(hdr[1]);
    k.evt = static_cast<unsigned long long>(static_cast<unsigned int>(hdr[2]));
    out.span[k] = {begin, begin + nPairs};
    out.nPairs += nPairs;
  }
}

bool readText(std::FILE* fp, MapCandFile& out) {
  out.binary = false;
  char line[512];
  bool haveEvent = false;
  MapCandFile::Key key;
  int begin = 0, declared = 0;
  auto closeEvent = [&]() {
    if (!haveEvent)
      return;
    const int end = static_cast<int>(out.t3.size());
    out.span[key] = {begin, end};
    out.nPairs += end - begin;
    if (declared >= 0 && declared != end - begin)
      std::fprintf(stderr,
                   "k8LoadMapCandFile: WARNING event %u:%u:%llu declared %d pairs, read %d\n",
                   key.run,
                   key.lumi,
                   key.evt,
                   declared,
                   end - begin);
  };
  while (std::fgets(line, sizeof(line), fp)) {
    const char* p = line;
    while (*p == ' ' || *p == '\t')
      ++p;
    if (*p == '\0' || *p == '\n' || *p == '#')
      continue;
    if (*p == 'E' || *p == 'e') {
      unsigned int run = 0, lumi = 0;
      unsigned long long evt = 0;
      int n = -1;
      const int fields = std::sscanf(p + 1, "%u %u %llu %d", &run, &lumi, &evt, &n);
      if (fields < 3) {
        std::fprintf(stderr, "k8LoadMapCandFile: malformed event line: %s", line);
        return false;
      }
      closeEvent();
      key.run = run;
      key.lumi = lumi;
      key.evt = evt;
      begin = static_cast<int>(out.t3.size());
      declared = (fields >= 4) ? n : -1;
      haveEvent = true;
      continue;
    }
    int a = -1, b = -1;
    if (std::sscanf(p, "%d %d", &a, &b) != 2) {
      std::fprintf(stderr, "k8LoadMapCandFile: malformed pair line: %s", line);
      return false;
    }
    if (!haveEvent) {
      std::fprintf(stderr, "k8LoadMapCandFile: pair line before any 'E' event line\n");
      return false;
    }
    out.t3.push_back(a);
    out.pls.push_back(b);
  }
  closeEvent();
  return true;
}

}  // namespace

bool k8LoadMapCandFile(const std::string& path, MapCandFile& out) {
  out.t3.clear();
  out.pls.clear();
  out.span.clear();
  out.nPairs = 0;
  std::FILE* fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    std::fprintf(stderr, "k8LoadMapCandFile: cannot open %s\n", path.c_str());
    return false;
  }
  char magic[8] = {};
  const std::size_t got = std::fread(magic, 1, 8, fp);
  bool ok = false;
  if (got == 8 && std::memcmp(magic, kBinMagic, 8) == 0) {
    ok = readBinary(fp, out);
  } else {
    std::rewind(fp);
    ok = readText(fp, out);
  }
  std::fclose(fp);
  if (!ok) {
    out.t3.clear();
    out.pls.clear();
    out.span.clear();
    out.nPairs = 0;
    return false;
  }
  std::printf("map candidates: %s (%s) -- %zu events, %lld pairs\n",
              path.c_str(),
              out.binary ? "binary" : "text",
              out.span.size(),
              out.nPairs);
  return true;
}

void k8SelectMapCandidates(const LSTEventData& ev, const MapCandFile& f, PlsCandIndex& idx, CandStats* st) {
  const int nT3 = static_cast<int>(ev.t3_lsIdx0.size());
  const int nPls = static_cast<int>(ev.pLS_pt.size());
  idx.mapStart.assign(nT3 + 1, 0);
  idx.mapItems.clear();
  idx.mapEventFound = false;

  MapCandFile::Key k;
  k.run = ev.run;
  k.lumi = ev.lumi;
  k.evt = ev.evt;
  auto it = f.span.find(k);
  if (it == f.span.end()) {
    if (st != nullptr)
      ++st->nMapEventsMissing;
    return;
  }
  idx.mapEventFound = true;
  const int b = it->second.first, e = it->second.second;

  // Counting sort into per-t3 spans, then sort + unique each span so the emission order
  // contract (plsRow ascending within a target) is preserved exactly as in mode 0.
  std::vector<int> cnt(nT3 + 1, 0);
  for (int i = b; i < e; ++i) {
    const int t = f.t3[i], p = f.pls[i];
    if (t < 0 || t >= nT3 || p < 0 || p >= nPls)
      continue;
    ++cnt[t];
  }
  int run = 0;
  for (int t = 0; t < nT3; ++t) {
    idx.mapStart[t] = run;
    run += cnt[t];
  }
  idx.mapStart[nT3] = run;
  idx.mapItems.assign(run, 0);
  std::vector<int> fill(idx.mapStart.begin(), idx.mapStart.end() - 1);
  for (int i = b; i < e; ++i) {
    const int t = f.t3[i], p = f.pls[i];
    if (t < 0 || t >= nT3 || p < 0 || p >= nPls)
      continue;
    idx.mapItems[fill[t]++] = p;
  }
  // Dedup in place; mapStart is rebuilt as the compacted CSR.
  int w = 0;
  std::vector<int> newStart(nT3 + 1, 0);
  for (int t = 0; t < nT3; ++t) {
    newStart[t] = w;
    const int s = idx.mapStart[t], en = idx.mapStart[t + 1];
    if (en > s) {
      std::sort(idx.mapItems.begin() + s, idx.mapItems.begin() + en);
      int last = -1;
      for (int i = s; i < en; ++i) {
        if (idx.mapItems[i] == last)
          continue;
        last = idx.mapItems[i];
        idx.mapItems[w++] = last;
      }
    }
  }
  newStart[nT3] = w;
  idx.mapItems.resize(w);
  idx.mapStart.swap(newStart);
}
