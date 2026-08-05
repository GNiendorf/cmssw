import sys, numpy as np, uproot
for path in sys.argv[1:]:
    t=uproot.open(path)["tree"]
    a=t.arrays(["tc_pt","tc_eta","tc_isFake","tc_isDuplicate","tc_isChain","tc_dbgBr","tc_dbgNL"],library="np")
    d={k:np.concatenate([np.asarray(x) for x in v]) for k,v in a.items()}
    pt9=d["tc_pt"]>0.9; ae=np.abs(d["tc_eta"]); fk=d["tc_isFake"].astype(bool)
    dup=d["tc_isDuplicate"].astype(bool); ch=d["tc_isChain"]>0
    band=pt9&(ae>=1.1)&(ae<1.7)
    print("== %s ==" % path.split("/")[-1])
    for b,bn in ((0,"T4-IP"),(1,"T4-exempt"),(2,"5+IP"),(3,"5+exempt")):
        s=band&ch&(d["tc_dbgBr"]==b)
        if s.sum()<20: continue
        print("   band %-10s n=%6d fake=%.4f dup=%.4f  fakes=%5d dups=%5d"
              %(bn,s.sum(),fk[s].mean(),dup[s].mean(),fk[s].sum(),dup[s].sum()))
    print("   band TOTAL n=%d fake=%.4f dup=%.4f | global dup=%.4f"
          %(band.sum(),fk[band].mean(),dup[band].mean(),dup[pt9].mean()))
