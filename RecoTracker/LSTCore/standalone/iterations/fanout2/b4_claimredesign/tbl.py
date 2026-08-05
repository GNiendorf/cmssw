import json,sys,os
P='/mnt/data1/gsn27/here/CMSSW_17_0_0_pre2/src/RecoTracker/LSTCore/standalone/prototype/'
keys=['eff_overall_incut','eff_vxy_0_1','eff_vxy_1_5','eff_vxy_5_10','eff_vxy_10_30',
      'eff_dxy_0_1','eff_dxy_1_5','eff_dxy_5_10','eff_dxy_10_30',
      'eff_barrel','eff_transition','eff_endcap',
      'fake_overall_incut','dup_overall_incut',
      'mean_nhitOT_barrel','mean_nhitOT_transition','mean_nhitOT_endcap']
short=['eff','vxy01','vxy15','vxy510','vxy1030','dxy01','dxy15','dxy510','dxy1030','ebar','etra','eend','fake','dup','olB','olT','olE']
rows=[]
names=sys.argv[1:]
base=None
for n in names:
    f=n if os.path.exists(n) else (P+'ab_%s.json'%n if os.path.exists(P+'ab_%s.json'%n) else 'ab_%s.json'%n)
    d=json.load(open(f))['metrics']
    if base is None:
        base={k:d[k]['base'] for k in keys if k in d}
    rows.append((n,{k:(d[k]['proto'] if k in d else float('nan')) for k in keys}))
print('%-14s'%'config'+''.join('%9s'%s for s in short))
print('%-14s'%'BASELINE'+''.join('%9.4f'%base.get(k,float('nan')) for k in keys))
for n,d in rows:
    print('%-14s'%n+''.join('%9.4f'%d[k] for k in keys))
print()
print('deltas vs baseline')
for n,d in rows:
    print('%-14s'%n+''.join('%+9.4f'%(d[k]-base.get(k,0)) for k in keys))
