import sys,re,os
P="/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/fanout5/recon_dup"
def g(t,k):
    m=re.search(re.escape(k)+r"\s+([\d.]+)\s+([\d.]+)\s+([+-][\d.]+)",t); return float(m.group(1)) if m else float('nan')
print("%-9s %6s %6s %6s %6s %6s %6s %6s %6s | %6s %6s %6s"%("tag","eff","v15","v510","v1030","d15","d510","dup","fake","nhb","nht","nhe"))
for tag in sys.argv[1:]:
    f=f"{P}/rd_agg_{tag}.txt"
    if not os.path.exists(f): print("%-9s (pending)"%tag); continue
    t=open(f).read()
    print("%-9s %6.4f %6.4f %6.4f %6.4f %6.4f %6.4f %6.4f %6.4f | %6.3f %6.3f %6.3f"%(tag,
      g(t,"eff overall (pt>0.9)"),g(t,"eff vxy [1,5)"),g(t,"eff vxy [5,10)"),g(t,"eff vxy [10,30)"),
      g(t,"eff dxy [1,5)"),g(t,"eff dxy [5,10)"),g(t,"dup rate (pt>0.9)"),g(t,"fake rate (pt>0.9)"),
      g(t,"mean nhitOT barrel"),g(t,"mean nhitOT transition"),g(t,"mean nhitOT endcap")))
