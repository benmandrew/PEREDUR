import csv, sys, statistics as st
for path in sys.argv[1:]:
    rows = [r for r in csv.DictReader(open(path)) if r['status'] == 'ok']
    if not rows: continue
    f = lambda n, d: 100 * st.median(int(r[n]) / int(r[d]) for r in rows if int(r[d]) > 0)
    q = sum(int(r['queried_pairs']) for r in rows); c = sum(int(r['all_pairs']) for r in rows); to = sum(int(r['timeouts']) for r in rows)
    print(f"{path.split('/')[-1]:28s} n={len(rows):2d} d_only {f('d_only','joint'):5.1f} u_only {f('u_only','joint'):5.1f} shared {f('shared','joint'):4.1f} (sem {f('sem_shared','joint'):4.1f}) d_dom {f('d_dominated','d_frontier'):5.1f} u_dom {f('u_dominated','u_frontier'):5.1f} | queried {100*q/c:5.2f}% undecided {to} ({100*to/c:.3f}% of pairs) wall {sum(float(r['wall_s']) for r in rows)/3600:.1f}h")
