#!/usr/bin/env python3
"""Row-by-row diff of two compare_ab.py scoreboards.

usage: p25_scoreboard.py <reference.json> <new.json> [<hists_ref.root> <hists_new.root>]

The first two arguments are the JSONs compare_ab.py writes. When the two histogram files are also
given, every row that MOVED is additionally reported with the numerator/denominator track counts
behind it, so a delta can be read as "n tracks", not just as a ratio.
"""
import json
import sys


def load(p):
    return json.load(open(p))["metrics"]


def main():
    ref, new = load(sys.argv[1]), load(sys.argv[2])
    keys = list(ref.keys()) + [k for k in new if k not in ref]
    moved = []
    print(f"{'metric':<26} {'frozen':>12} {'P2.5':>12} {'delta':>13} {'rel':>10}")
    print("-" * 78)
    for k in keys:
        a = ref.get(k, {}).get("proto")
        b = new.get(k, {}).get("proto")
        if a is None or b is None:
            print(f"{k:<26} {'-' if a is None else f'{a:.6f}':>12} "
                  f"{'-' if b is None else f'{b:.6f}':>12} {'MISSING':>13}")
            continue
        d = b - a
        rel = (d / a * 100.0) if a else 0.0
        flag = "" if d == 0.0 else "   <-- MOVED"
        print(f"{k:<26} {a:>12.6f} {b:>12.6f} {d:>+13.3e} {rel:>+9.4f}%{flag}")
        if d != 0.0:
            moved.append((k, a, b, d))
    print()
    print(f"rows total {len(keys)}   rows moved {len(moved)}   rows identical {len(keys)-len(moved)}")

    if len(sys.argv) > 4:
        import uproot
        counts(sys.argv[3], sys.argv[4], [m[0] for m in moved])
    return 0


def counts(href, hnew, moved):
    """Numerator/denominator integrals behind the moved rows, so a delta reads as track counts."""
    import uproot
    fa, fb = uproot.open(href), uproot.open(hnew)
    names = set(fa.keys()) | set(fb.keys())
    print()
    print("track counts behind the moved rows (numerator / denominator integrals):")
    for m in moved:
        stem = m.replace("eff_", "").replace("dup_", "").replace("fake_", "")
        hits = sorted(n for n in names if stem in n)
        for n in hits[:6]:
            try:
                ia = fa[n].values().sum() if n in fa else float("nan")
                ib = fb[n].values().sum() if n in fb else float("nan")
            except Exception:
                continue
            if ia != ib:
                print(f"  {m:<24} {n.split(';')[0]:<48} {ia:>10.1f} -> {ib:>10.1f}  ({ib-ia:+.1f})")


if __name__ == "__main__":
    sys.exit(main())
