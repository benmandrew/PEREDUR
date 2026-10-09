import itertools,random
def wil(x,y):
    d=[a-b for a,b in zip(x,y) if a is not None and b is not None and a!=b]; n=len(d)
    if n==0: return 1.0,0,0
    r=sorted(range(n),key=lambda i:abs(d[i])); rank=[0]*n
    for k,i in enumerate(r): rank[i]=k+1
    tot=sum(rank); obs=abs(sum(rk for rk,v in zip(rank,d) if v>0)-tot/2)
    if n>16:
        rng=random.Random(1); N=100000
        return sum(abs(sum(rk for rk in rank if rng.random()<.5)-tot/2)>=obs-1e-9 for _ in range(N))/N,sum(v>0 for v in d),n
    return sum(abs(sum(rk for rk,s in zip(rank,sg) if s)-tot/2)>=obs-1e-9 for sg in itertools.product((0,1),repeat=n))/2**n,sum(v>0 for v in d),n
