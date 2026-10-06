import csv, sys
D = sys.argv[1]
ex = {(r['subject'], r['seed']): r for r in csv.DictReader(open(f'{D}/exact.csv'))}
K = ['d_frontier','u_frontier','joint','d_only','u_only','shared','d_dominated','u_dominated']
for path in sys.argv[2:]:
    n = m = to = q = c = 0; tw = te = 0.0
    for r in csv.DictReader(open(path)):
        e = ex.get((r['subject'], r['seed']))
        if not e or e['status'] != 'ok' or r['status'] != 'ok':
            continue
        n += 1; to += int(r['timeouts']); q += int(r['queried_pairs']); c += int(r['all_pairs'])
        tw += float(r['wall_s']); te += float(e['wall_s'])
        if [r[k] for k in K] != [e[k] for k in K]:
            m += 1; print(' MISMATCH', r['subject'], r['seed'], [r[k] for k in K], [e[k] for k in K], 'timeouts', r['timeouts'])
    print(f'{path.split("/")[-1]}: {n} seeds, {m} mismatches, timeouts {to}, queried {q}/{c}, wall {tw:.0f}s vs exact {te:.0f}s')
