"""TRUE-OOS table on the 349 non-overlap events (critic item 3).

Three columns on the SAME 349 events:
  LST        base_oos349_hists.root          (the input ntuple's own tc_* block)
  FLAGSHIP   oos_balanced_349_hists.root     (M18b ran ANCHOR+CTL+STACK -TT 1.2 -a 8 -RPS 1 -RD 1,
                                              i.e. exactly the M19 FLAGSHIP, golden binary)
  FREEZE     oos_freeze_349_hists.root       (this session)

Reported as DELTAS, not absolutes: LST's own efficiency moves between event sets (.8136 on the
300 vs .8098 here), so absolute values are not comparable across samples -- only deltas are.
The 300-event floors are NOT applicable here and are deliberately not evaluated.
"""
import sys
import ROOT

S = "/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone"
A = S + "/fanout4/compose_attach"
P = S + "/fanout5/final"

SRC = [("LST", A + "/base_oos349_hists.root"),
       ("FLAGSHIP", A + "/oos_balanced_349_hists.root"),
       ("FREEZE", P + "/oos_freeze_349_hists.root")]

EF = "Root__TC_base_0_0_ef_"
FR = "Root__TC_fr_"
DR = "Root__TC_dr_"
OL = "Root__TC_ol_"
REG = [("barrel", 0.0, 1.1), ("transition", 1.1, 1.7), ("endcap", 1.7, 4.6)]
VB = [(0, 1), (1, 5), (5, 10), (10, 30)]


def band(h, lo, hi, fold=False):
    s = 0.0
    for b in range(1, h.GetNbinsX() + 1):
        c = h.GetXaxis().GetBinCenter(b)
        if fold:
            c = abs(c)
        if lo <= c < hi:
            s += h.GetBinContent(b)
    return s


def metrics(path):
    f = ROOT.TFile.Open(path)
    if not f or f.IsZombie():
        return None
    m = {}

    def R(pre, var, lo=None, hi=None, fold=False):
        n = f.Get(pre + "numer_" + var); d = f.Get(pre + "denom_" + var)
        if not n or not d:
            return (None, 0, 0)
        if lo is None:
            nn, dd = n.Integral(0, n.GetNbinsX() + 1), d.Integral(0, d.GetNbinsX() + 1)
        else:
            nn, dd = band(n, lo, hi, fold), band(d, lo, hi, fold)
        return (nn / dd if dd else None, nn, dd)

    m["eff"] = R(EF, "eta")
    m["fake"] = R(FR, "eta")
    m["dup"] = R(DR, "eta")
    for lo, hi in VB:
        m["vxy%g_%g" % (lo, hi)] = R(EF, "vxy", lo, hi)
        m["dxy%g_%g" % (lo, hi)] = R(EF, "dxy", lo, hi)
    for r, lo, hi in REG:
        m["dup_" + r] = R(DR, "eta", lo, hi, True)
        m["nh_" + r] = R(OL, "eta", lo, hi, True)
    m["dup_win"] = R(DR, "eta", 1.5, 3.0, True)
    m["dup_win25"] = R(DR, "eta", 1.5, 2.5, True)
    f.Close()
    return m


def main():
    M = {}
    for n, p in SRC:
        d = metrics(p)
        if d is None:
            print("MISSING: %s (%s)" % (n, p)); return
        M[n] = d
    keys = [("eff", "eff overall (in-cut)"),
            ("vxy0_1", "eff vxy [0,1)"), ("vxy1_5", "eff vxy [1,5)"),
            ("vxy5_10", "eff vxy [5,10)"), ("vxy10_30", "eff vxy [10,30)"),
            ("dxy1_5", "eff dxy [1,5)"), ("dxy5_10", "eff dxy [5,10)"),
            ("dxy10_30", "eff dxy [10,30)"),
            ("fake", "fake rate"), ("dup", "duplicate rate"),
            ("dup_barrel", "dup barrel"), ("dup_transition", "dup transition"),
            ("dup_endcap", "dup endcap"),
            ("dup_win25", "dup |eta| 1.5-2.5  <-- WINDOW"),
            ("dup_win", "dup |eta| 1.5-3.0  <-- WINDOW"),
            ("nh_barrel", "mean nhitOT barrel"), ("nh_transition", "mean nhitOT transition"),
            ("nh_endcap", "mean nhitOT endcap")]
    print("TRUE-OOS: 349 non-overlap events (M18b frozen list, same filter + baseline)")
    print("=" * 118)
    print("%-32s %10s %10s %10s | %11s %11s %11s"
          % ("metric", "LST", "FLAGSHIP", "FREEZE", "FRZ-FLAG", "FRZ-LST", "FLAG-LST"))
    print("-" * 118)
    for k, lbl in keys:
        v = [M[n][k][0] for n, _ in SRC]
        if any(x is None for x in v):
            continue
        print("%-32s %10.5f %10.5f %10.5f | %+11.5f %+11.5f %+11.5f"
              % (lbl, v[0], v[1], v[2], v[2] - v[1], v[2] - v[0], v[1] - v[0]))
    print("-" * 118)
    print("track counts (numer/denom), FLAGSHIP -> FREEZE:")
    for k, lbl in keys[:8]:
        a = M["FLAGSHIP"][k]; b = M["FREEZE"][k]
        print("  %-30s %6d/%-6d -> %6d/%-6d   (%+d tracks)"
              % (lbl, a[1], a[2], b[1], b[2], b[1] - a[1]))


if __name__ == "__main__":
    main()
