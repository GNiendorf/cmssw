import ROOT, sys
EF="Root__TC_base_0_0_ef_"; FR="Root__TC_fr_"; DR="Root__TC_dr_"
VXY=[(0,1),(1,5),(5,10),(10,30)]; ETA=[("B",0.0,1.1),("T",1.1,1.7),("E",1.7,None)]
def sb(h,lo,hi):
    t=0.0; ax=h.GetXaxis()
    for b in range(1,h.GetNbinsX()+1):
        c=abs(ax.GetBinCenter(b))
        if c>=lo and (hi is None or c<hi): t+=h.GetBinContent(b)
    return t
f=ROOT.TFile.Open(sys.argv[1])
for var in ("vxy","dxy"):
    hd=f.Get(EF+"denom_"+var)
    print(var, [(lo,hi,int(sb(hd,lo,hi))) for lo,hi in VXY])
hd=f.Get(EF+"denom_eta"); hdr=f.Get(DR+"denom_eta"); hfr=f.Get(FR+"denom_eta")
for r,lo,hi in ETA:
    print("eta",r,"effden=%d dupden=%d fakeden=%d"%(sb(hd,lo,hi),sb(hdr,lo,hi),sb(hfr,lo,hi)))
print("eff_all_den=%d"%hd.Integral(0,hd.GetNbinsX()+1))
