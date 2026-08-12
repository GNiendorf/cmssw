#!/usr/bin/env python3
"""Pool dup/fake/n_tc over the 6 holdout runs (n_tc-weighted; the rates share the n_tc
denominator, so the pooled rate is exactly sum(rate*n_tc)/sum(n_tc))."""
import json, sys
def load(p):
    t=open(p).read()
    try: return json.loads(t)
    except Exception: pass
    for ln in reversed(t.strip().split("\n")):
        if ln.strip().startswith("{"): return json.loads(ln)
K=["dup_overall_incut","fake_overall_incut","dup_barrel","fake_barrel",
   "dup_transition","fake_transition","dup_endcap","fake_endcap"]
for spec in sys.argv[1:]:
    nm,pat=spec.split("=",1)
    ds=[load(pat%e) for e in (2000,3000,4000,5000,6000,7000)]
    N=sum(d["n_tc"] for d in ds)
    out={k: sum(d[k]*d["n_tc"] for d in ds)/N for k in K}
    out["n_tc"]=N; out["n_tc_t4cl"]=sum(d["n_tc_t4cl"] for d in ds)
    print("%-14s " % nm + "  ".join("%s %.5f" % (k.replace("_incut","").replace("overall","ovl"), out[k]) for k in K)
          + "  n_tc %d  t4cl %d" % (out["n_tc"], out["n_tc_t4cl"]))
