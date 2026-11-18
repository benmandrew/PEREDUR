import os
import csv,sys,statistics as st
from math import comb
def load(fs,lvl=None):
    d={}
    for f in fs:
        for r in csv.DictReader(open(f)):
            if lvl and r['level_name']!=lvl: continue
            d[(r['spec'],int(r['seed']))]=r
    return d
x=load([os.environ['F4X_WORK']+'/f4xres/remove-exchange-av2.csv',os.environ['F4X_WORK']+'/f4xres/remove-exchange-av3.csv'])
c=load([sys.argv[1]],'remove-full')
print('exchange rows',len(x),'levels',{r['level_name'] for r in x.values()},'commit',{r['commit'][:8] for r in x.values()},'dirty',{r['dirty'] for r in x.values()})
keys=sorted(set(x)&set(c)); print('paired',len(keys),'specs',len({k[0] for k in keys}))
b=lambda r,k: r[k].lower() in ('true','1')
def mcn(a,bb):
    n=a+bb; k=min(a,bb); return min(1,2*sum(comb(n,i) for i in range(k+1))/2**n) if n else 1
for m in ['implies_ideal','found_repair','timed_out']:
    xa=sum(b(x[k],m) for k in keys); ca=sum(b(c[k],m) for k in keys)
    up=sum(b(x[k],m) and not b(c[k],m) for k in keys); dn=sum(b(c[k],m) and not b(x[k],m) for k in keys)
    print(f'{m}: exchange {xa}/{len(keys)} control {ca}/{len(keys)}  discordant +{up}/-{dn} McNemar p={mcn(up,dn):.3f}')
print('spec  n  known x/c  nrep med x/c  wall med x/c')
for s in sorted({k[0] for k in keys}):
    ks=[k for k in keys if k[0]==s]
    f=lambda d,m: st.median(float(d[k][m]) for k in ks)
    print(f'{s:22} {len(ks)} {sum(b(x[k],"implies_ideal") for k in ks):2}/{sum(b(c[k],"implies_ideal") for k in ks):2}  {f(x,"n_repairs"):5}/{f(c,"n_repairs"):5}  {f(x,"wall_time_s"):6.0f}/{f(c,"wall_time_s"):6.0f}')
