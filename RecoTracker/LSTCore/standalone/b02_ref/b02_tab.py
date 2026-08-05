import json,sys,os
S='/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone'
K=['eff_overall_incut','eff_barrel','eff_transition','eff_endcap',
   'dup_overall_incut','dup_barrel','dup_transition','dup_endcap',
   'fake_overall_incut','fake_barrel','fake_transition','fake_endcap',
   'eff_vxy_1_5','eff_vxy_5_10','eff_vxy_10_30','eff_dxy_1_5',
   'mean_nhitOT','mean_nhitOT_barrel','mean_nhitOT_transition','mean_nhitOT_endcap','n_tc']
def load(t):
    p=f'{S}/synth_ref/r_{t}.json'
    if not os.path.exists(p): return None
    return json.load(open(p))['metrics']
ref=load(sys.argv[1])
hdr=['tag']+[k.replace('eff_','e').replace('dup_','d').replace('fake_','f').replace('mean_nhitOT','nh').replace('overall_incut','').replace('barrel','B').replace('transition','T').replace('endcap','E') for k in K]
print(' '.join('%-9s'%h for h in hdr))
for t in sys.argv[1:]:
    m=load(t)
    if m is None: print('%-9s MISSING'%t); continue
    row=['%-9s'%t]
    for k in K:
        v=m[k]['proto']
        row.append('%-9.5f'%v if k!='n_tc' else '%-9d'%int(v))
    print(' '.join(row))
    if ref is not None and t!=sys.argv[1]:
        d=['%-9s'%'  dlt']
        for k in K:
            dv=m[k]['proto']-ref[k]['proto']
            d.append('%-+9.5f'%dv if k!='n_tc' else '%-+9d'%int(dv))
        print(' '.join(d))
