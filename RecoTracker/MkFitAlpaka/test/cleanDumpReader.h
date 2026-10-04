#ifndef RecoTracker_MkFitAlpaka_test_cleanDumpReader_h
#define RecoTracker_MkFitAlpaka_test_cleanDumpReader_h
// Reader of the MkFitCleanDump binary file (plugins/MkFitCleanDump.cc), shared by the clean tests.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace cleandump {

  struct DTrack {
    float par[6], err[21], chi2, score;
    int32_t charge, label, status, nTot, nFound;
    std::vector<int32_t> hots;
    int8_t a = 0, b = 0;  // stock decisions: dup (noDC list) or passPre/passPost (noFilt list)
  };
  struct DEvent {
    uint64_t run, lumi, event;
    int32_t identical, nStock;
    std::vector<DTrack> noDC, noFilt;
  };

  inline bool readTrack(FILE* f, DTrack& t, int nFlags) {
    int32_t ints[4];
    float fl[2];
    if (std::fread(t.par, sizeof(t.par), 1, f) != 1)
      return false;
    if (std::fread(t.err, sizeof(t.err), 1, f) != 1 || std::fread(fl, sizeof(fl), 1, f) != 1 ||
        std::fread(ints, sizeof(ints), 1, f) != 1 || std::fread(&t.nFound, 4, 1, f) != 1)
      return false;
    t.chi2 = fl[0];
    t.score = fl[1];
    t.charge = ints[0];
    t.label = ints[1];
    t.status = ints[2];
    t.nTot = ints[3];
    t.hots.resize(t.nTot);
    if (t.nTot > 0 && std::fread(t.hots.data(), 4, t.nTot, f) != size_t(t.nTot))
      return false;
    if (std::fread(&t.a, 1, 1, f) != 1)
      return false;
    if (nFlags > 1 && std::fread(&t.b, 1, 1, f) != 1)
      return false;
    return true;
  }

  struct Dump {
    float fp[4];     // dc_fracSharedHits, dc_drth_central, dc_drth_obarrel, dc_drth_forward
    int32_t ip[2];   // minHitsQF forward, backward
    std::vector<DEvent> events;
  };

  inline bool readDump(const char* fname, Dump& d) {
    FILE* f = std::fopen(fname, "rb");
    if (!f)
      return false;
    char magic[8];
    if (std::fread(magic, 8, 1, f) != 1 || std::memcmp(magic, "MKFCLN1", 7) != 0 || std::fread(d.fp, sizeof(d.fp), 1, f) != 1 ||
        std::fread(d.ip, sizeof(d.ip), 1, f) != 1) {
      std::fclose(f);
      return false;
    }
    while (true) {
      DEvent e;
      uint64_t eid[3];
      int32_t hdr[4];
      if (std::fread(eid, sizeof(eid), 1, f) != 1 || std::fread(hdr, sizeof(hdr), 1, f) != 1)
        break;
      e.run = eid[0];
      e.lumi = eid[1];
      e.event = eid[2];
      e.identical = hdr[0];
      e.nStock = hdr[1];
      e.noDC.resize(hdr[2]);
      e.noFilt.resize(hdr[3]);
      bool ok = true;
      for (auto& t : e.noDC)
        ok = ok && readTrack(f, t, 1);
      for (auto& t : e.noFilt)
        ok = ok && readTrack(f, t, 2);
      if (!ok)
        break;
      d.events.push_back(std::move(e));
    }
    std::fclose(f);
    return true;
  }

}  // namespace cleandump

#endif
