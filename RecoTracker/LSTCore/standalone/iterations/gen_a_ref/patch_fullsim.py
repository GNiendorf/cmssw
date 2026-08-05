import re
p = '/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/protoA/main.cc'
s = open(p).read()

old = """              std::fprintf(auditFp, " %d %d", static_cast<int>(sidx.size()), nAcc);
              for (std::size_t k = 0; k < sidx.size(); ++k) {
                const int s = sidx[k];
                const int a2 = (s >= 0 && s < static_cast<int>(trk.simFullToAccepted.size()))
                                   ? trk.simFullToAccepted[s]
                                   : -1;
                if (a2 >= 0)
                  std::fprintf(auditFp, " %d:%.4f", a2, sfrac[k]);
              }"""
new = """              std::fprintf(auditFp, " %d %d", static_cast<int>(sidx.size()), nAcc);
              // full:accepted:frac for EVERY matched sim. The FULL row is what the harness
              // duplicate rule keys on (duplicates are counted against the whole sim list,
              // pileup included), so the offline lab cannot model dup without it.
              for (std::size_t k = 0; k < sidx.size(); ++k) {
                const int s = sidx[k];
                const int a2 = (s >= 0 && s < static_cast<int>(trk.simFullToAccepted.size()))
                                   ? trk.simFullToAccepted[s]
                                   : -1;
                std::fprintf(auditFp, " %d:%d:%.4f", s, a2, sfrac[k]);
              }"""
assert s.count(old) == 1, s.count(old)
s = s.replace(old, new)

old2 = """              std::vector<char> already(ev.sim_pt.size(), 0);
              long long nPre = 0, nPreFake = 0;"""
new2 = """              std::vector<char> already(ev.sim_pt.size(), 0);
              std::set<int> alreadyFull;  // FULL sim rows (pileup included) already covered
              long long nPre = 0, nPreFake = 0;"""
assert s.count(old2) == 1
s = s.replace(old2, new2)

old3 = """                for (int sm : sidx) {
                  const int a2 = (sm >= 0 && sm < static_cast<int>(trk.simFullToAccepted.size()))
                                     ? trk.simFullToAccepted[sm]
                                     : -1;
                  if (a2 >= 0 && a2 < static_cast<int>(already.size()))
                    already[a2] = 1;
                }
              }"""
new3 = """                for (int sm : sidx) {
                  if (sm >= 0)
                    alreadyFull.insert(sm);
                  const int a2 = (sm >= 0 && sm < static_cast<int>(trk.simFullToAccepted.size()))
                                     ? trk.simFullToAccepted[sm]
                                     : -1;
                  if (a2 >= 0 && a2 < static_cast<int>(already.size()))
                    already[a2] = 1;
                }
              }"""
assert s.count(old3) == 1
s = s.replace(old3, new3)

old4 = """                for (int sm : sl) {
                  const int a2 = (sm >= 0 && sm < static_cast<int>(trk.simFullToAccepted.size()))
                                     ? trk.simFullToAccepted[sm]
                                     : -1;
                  if (a2 >= 0 && a2 < static_cast<int>(already.size()))
                    already[a2] = 1;
                }"""
new4 = """                for (int sm : sl) {
                  if (sm >= 0)
                    alreadyFull.insert(sm);
                  const int a2 = (sm >= 0 && sm < static_cast<int>(trk.simFullToAccepted.size()))
                                     ? trk.simFullToAccepted[sm]
                                     : -1;
                  if (a2 >= 0 && a2 < static_cast<int>(already.size()))
                    already[a2] = 1;
                }"""
assert s.count(old4) == 1
s = s.replace(old4, new4)

old5 = """              std::fprintf(auditFp, "A %lld %lld", nPre, nPreFake);
              for (std::size_t k = 0; k < already.size(); ++k)
                if (already[k])
                  std::fprintf(auditFp, " %d", static_cast<int>(k));
              std::fprintf(auditFp, "\\n");"""
new5 = """              std::fprintf(auditFp, "A %lld %lld", nPre, nPreFake);
              for (std::size_t k = 0; k < already.size(); ++k)
                if (already[k])
                  std::fprintf(auditFp, " %d", static_cast<int>(k));
              std::fprintf(auditFp, "\\n");
              std::fprintf(auditFp, "F %d", static_cast<int>(alreadyFull.size()));
              for (int sm : alreadyFull)
                std::fprintf(auditFp, " %d", sm);
              std::fprintf(auditFp, "\\n");"""
assert s.count(old5) == 1
s = s.replace(old5, new5)

if '#include <set>' not in s:
    s = s.replace('#include <cstdio>', '#include <cstdio>\n#include <set>', 1)
open(p, 'w').write(s)
print('patched')
