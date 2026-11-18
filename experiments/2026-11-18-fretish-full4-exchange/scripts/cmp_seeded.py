import csv,collections,glob,statistics as st
from scipy.stats import wilcoxon
from math import comb
import os
SP=os.environ['F4X_WORK']+'/'
def load(files,lvl):
    d={}
    for f in files:
        for r in csv.DictReader(open(f)):
            if r['level_name']==lvl: d[(r['spec'],int(r['seed']))]=r
    return d
b=lambda r,k: r[k].lower() in ('true','1')
def mcn(a,c):
    n=a+c;k=min(a,c);return min(1,2*sum(comb(n,i) for i in range(k+1))/2**n) if n else 1
def compare(name,x,c):
    keys=sorted(set(x)&set(c)); subs=sorted({k[0] for k in keys})
    print(f'\n== {name}: {len(keys)} paired runs, {len(subs)} subjects, seeds {sorted({k[1] for k in keys})}')
    print('   commits', {r['commit'][:8] for r in x.values()}, 'vs', {r['commit'][:8] for r in c.values()})
    for m in ['implies_ideal','found_repair']:
        up=sum(b(x[k],m) and not b(c[k],m) for k in keys); dn=sum(b(c[k],m) and not b(x[k],m) for k in keys)
        xm=[st.mean(b(x[k],m) for k in keys if k[0]==s) for s in subs]; cm=[st.mean(b(c[k],m) for k in keys if k[0]==s) for s in subs]
        diff=[a-q for a,q in zip(xm,cm)]
        try: p=wilcoxon(xm,cm).pvalue
        except ValueError: p=float('nan')
        print(f'   {m}: x {100*st.mean(xm):.1f}% c {100*st.mean(cm):.1f}% (diff {100*st.mean(diff):+.1f}pp) runs +{up}/-{dn} McNemar p={mcn(up,dn):.3f} subject signed-rank p={p:.3f}')
    sysd=collections.defaultdict(list)
    for s in subs:
        sysd[s.split('__')[0]].append(st.mean(b(x[k],'implies_ideal')-b(c[k],'implies_ideal') for k in keys if k[0]==s))
    print('   per system recovery diff (pp):',', '.join(f'{k} {100*st.mean(v):+.1f} (n={len(v)})' for k,v in sorted(sysd.items())))
    print('   wall median x/c:', st.median(float(x[k]['wall_time_s']) for k in keys), st.median(float(c[k]['wall_time_s']) for k in keys))
rx=load(glob.glob(SP+'f4xres/remove-exchange-seeded-*.csv'),'remove-exchange')
lr=load([os.path.expanduser('~/projects/peredur-wt-demote/experiments/results-fretish-seeded-removal.csv')],'lean-remove')
compare('remove-exchange vs lean-remove',rx,lr)
nx=load(glob.glob(SP+'f4xres/remove-no-blame-exchange-seeded-*.csv'),'remove-no-blame-exchange')
nb=load(glob.glob(SP+'ctl/seeded-remove-*.csv'),'remove-no-blame')
compare('remove-no-blame-exchange vs remove-no-blame',nx,nb)
compare('remove-no-blame (739edd4) vs lean-remove (612876c), for scale',nb,lr)
