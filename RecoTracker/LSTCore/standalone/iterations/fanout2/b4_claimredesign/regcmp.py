import sys, hashlib
import ROOT
def digest(path, tname="tree", nmax=None):
    f = ROOT.TFile.Open(path)
    t = f.Get(tname)
    if not t:
        # find first TTree
        for k in f.GetListOfKeys():
            o = k.ReadObj()
            if isinstance(o, ROOT.TTree):
                t = o; break
    brs = sorted([b.GetName() for b in t.GetListOfBranches()])
    n = t.GetEntries() if nmax is None else min(nmax, t.GetEntries())
    out = {}
    for b in brs:
        h = hashlib.md5()
        for i in range(n):
            t.GetEntry(i)
            v = getattr(t, b)
            try:
                h.update(str(list(v)).encode())
            except TypeError:
                h.update(str(v).encode())
        out[b] = h.hexdigest()
    f.Close()
    return brs, out, n
if __name__ == "__main__":
    a, b = sys.argv[1], sys.argv[2]
    nmax = int(sys.argv[3]) if len(sys.argv) > 3 else None
    ba, da, na = digest(a, nmax=nmax)
    bb, db, nb = digest(b, nmax=nmax)
    print("entries: %d vs %d" % (na, nb))
    if ba != bb:
        print("BRANCH SET DIFFERS"); print(set(ba) ^ set(bb)); sys.exit(1)
    bad = [x for x in ba if da[x] != db[x]]
    print("branches: %d, identical: %d, differing: %d" % (len(ba), len(ba)-len(bad), len(bad)))
    if bad: print("DIFF:", bad); sys.exit(1)
    print("BIT-EXACT")
