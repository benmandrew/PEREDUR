"""Figures for 2026-09-09-fretish-grading and 2026-09-09-fretish-maximal-curve.

Usage: python3 analyse_fretish_grading.py [experiments-dir]

Reads results-gradsel-fret-m.csv, the run.json of every run under
results-gradsel-fret-m/, curves-gradsel-fret-m.csv and the scorer's
timings.txt and failures.txt under curves-gradsel-fret-m/av2/. Python 3
standard library only; every p-value is an exact binomial computed here.
"""
import collections
import csv
import datetime
import hashlib
import json
import math
import os
import statistics as st
import sys

E = sys.argv[1] if len(sys.argv) > 1 else 'experiments'
rows = list(csv.DictReader(open(f'{E}/results-gradsel-fret-m.csv')))
ARMS = [('nsga2-apportion', 'mrs'), ('nsga2-apportion', 'aurus'), ('weighted', 'mrs'), ('weighted', 'aurus')]
FAM = sorted({r['spec'] for r in rows})
SEEDS = [str(s) for s in range(30)]


def arm(r):
    return (r['selection'], r['level_name'])


def exact_two_sided(b, c):
    """Exact McNemar on discordant counts b and c; also the sign test."""
    n = b + c
    if n == 0:
        return 1.0
    k = min(b, c)
    return min(1.0, 2 * sum(math.comb(n, i) for i in range(k + 1)) / 2 ** n)


def med(x):
    return st.median(x) if x else float('nan')


# n_accumulated_repairs in run.json is AccumulatorStats::n_contributed, the
# accumulated specifications the final population did not already hold. The
# accumulated set itself is accumulated/index.tsv, one line per arrival, which
# is what score_curves.py counts as `solutions`. Tombstoned guarantees are
# omitted from the JSON a repair is written as, so two arrivals distinct as
# search state can be byte-identical as documents; `docs` counts the latter.
man, accset, docs = {}, {}, {}
for r in rows:
    k = (arm(r), r['spec'], r['seed'])
    d = f"{E}/results-gradsel-fret-m/sweep_K_{r['level_name']}_{r['selection']}_wkoff_log_{r['spec']}_seed{int(r['seed']):02d}"
    man[k] = json.load(open(d + '/run.json'))
    tsv = d + '/accumulated/index.tsv'
    index = [line.split('\t')[0] for line in open(tsv).read().splitlines()[1:]] if os.path.exists(tsv) else []
    accset[k] = len(index)
    docs[k] = len({hashlib.sha1(open(f'{d}/accumulated/{f}', 'rb').read()).hexdigest() for f in index})
idx = {(arm(r), r['spec'], r['seed']): r for r in rows}

print('== per arm')
for a in ARMS:
    rs = [r for r in rows if arm(r) == a]
    ms = [man[(a, r['spec'], r['seed'])] for r in rs]
    w = [float(r['wall_time_s']) for r in rs]
    nr = [int(r['n_repairs']) for r in rs]
    contrib = [m['n_accumulated_repairs'] for m in ms]
    acc = [accset[(a, r['spec'], r['seed'])] for r in rs]
    dd = [docs[(a, r['spec'], r['seed'])] for r in rs]
    rel = collections.Counter(r['best_relation'] for r in rs)
    print(a, 'n', len(rs), 'found', sum(int(r['found_repair']) for r in rs), 'ideal', sum(int(r['implies_ideal']) for r in rs),
          'wall mean %.2f med %.2f max %.2f sum_h %.3f' % (st.mean(w), med(w), max(w), sum(w) / 3600),
          'n_repairs mean %.2f med %s' % (st.mean(nr), med(nr)),
          'accumulated set mean %.2f med %s max %d' % (st.mean(acc), med(acc), max(acc)),
          'distinct documents mean %.2f (sum %d of %d)' % (st.mean(dd), sum(dd), sum(acc)),
          'n_accumulated_repairs (contributed) mean %.1f med %s max %d' % (st.mean(contrib), med(contrib), max(contrib)),
          dict(rel), 'impl_timeouts', sum(m['implication']['timeouts'] for m in ms),
          'fp_refuted', sum(m['implication']['fingerprint_refuted'] for m in ms),
          'comparisons', sum(m['implication']['comparisons'] for m in ms),
          dict(collections.Counter(m['stopped_by'] for m in ms)),
          'n_implies mean %.2f' % st.mean(int(r['n_implies']) for r in rs))

print('== runs with no repair')
for r in rows:
    if r['found_repair'] == '0':
        print(arm(r), r['spec'], r['seed'], r['wall_time_s'])


def contrast(name, pairs):
    for ep in ('found_repair', 'implies_ideal'):
        b = sum(1 for x, y in pairs if idx[x][ep] == '1' and idx[y][ep] == '0')
        c = sum(1 for x, y in pairs if idx[x][ep] == '0' and idx[y][ep] == '1')
        print(name, ep, 'first-only', b, 'second-only', c, 'p = %.4f' % exact_two_sided(b, c), 'of', len(pairs))


print('== factor contrasts, paired on (spec, seed)')
SEL = {g: [((('nsga2-apportion', g), s, sd), (('weighted', g), s, sd)) for s in FAM for sd in SEEDS] for g in ('mrs', 'aurus')}
GRA = {sel: [(((sel, 'mrs'), s, sd), ((sel, 'aurus'), s, sd)) for s in FAM for sd in SEEDS] for sel in ('nsga2-apportion', 'weighted')}
for name, pairs in (('selection nsga2-apportion vs weighted', SEL['mrs'] + SEL['aurus']),
                    ('grading mrs vs aurus', GRA['nsga2-apportion'] + GRA['weighted'])):
    contrast(name, pairs)
    ratios = [float(idx[y]['wall_time_s']) / float(idx[x]['wall_time_s']) for x, y in pairs]
    faster = sum(1 for x, y in pairs if float(idx[x]['wall_time_s']) < float(idx[y]['wall_time_s']))
    slower = sum(1 for x, y in pairs if float(idx[x]['wall_time_s']) > float(idx[y]['wall_time_s']))
    print(name, 'wall ratio second/first median %.3f' % med(ratios), 'first faster on', faster, 'slower on', slower,
          'sign p = %.3g' % exact_two_sided(faster, slower),
          'n_repairs med', med([int(idx[x]['n_repairs']) for x, _ in pairs]), med([int(idx[y]['n_repairs']) for _, y in pairs]))
    for fam in FAM:
        fp = [(x, y) for x, y in pairs if x[1] == fam]
        print('   ', fam, 'implies_ideal first-only', sum(1 for x, y in fp if idx[x]['implies_ideal'] == '1' and idx[y]['implies_ideal'] == '0'),
              'second-only', sum(1 for x, y in fp if idx[x]['implies_ideal'] == '0' and idx[y]['implies_ideal'] == '1'),
              'wall ratio med %.3f' % med([float(idx[y]['wall_time_s']) / float(idx[x]['wall_time_s']) for x, y in fp]))
    contrast('    ' + name + ' [fsm + fsm-combined only]', [(x, y) for x, y in pairs if x[1] in ('fsm', 'fsm-combined')])
print('== each factor within each level of the other')
for g, pairs in SEL.items():
    contrast('selection within ' + g, pairs)
for sel, pairs in GRA.items():
    contrast('grading within ' + sel, pairs)

print('== per family implies_ideal / found / wall median per arm')
for s in FAM:
    out = []
    for a in ARMS:
        rs = [r for r in rows if arm(r) == a and r['spec'] == s]
        out.append('%d/%d w%.1f' % (sum(int(r['implies_ideal']) for r in rs), sum(int(r['found_repair']) for r in rs), med([float(r['wall_time_s']) for r in rs])))
    print(s, out, dict(collections.Counter(r['best_relation'] for r in rows if r['spec'] == s)))

print('== per family x arm: accumulated set mean, contributed mean, implication timeouts (runs with one)')
for s in FAM:
    out = []
    for a in ARMS:
        ms = [man[(a, s, sd)] for sd in SEEDS]
        out.append('set %.1f contrib %.1f to %d (runs %d)' % (st.mean(accset[(a, s, sd)] for sd in SEEDS), st.mean(m['n_accumulated_repairs'] for m in ms),
                                                             sum(m['implication']['timeouts'] for m in ms), sum(1 for m in ms if m['implication']['timeouts'])))
    print(s, out)
for g in ('mrs', 'aurus'):
    w_set = sum(accset[(('weighted', g), s, sd)] for s in FAM for sd in SEEDS)
    n_set = sum(accset[(('nsga2-apportion', g), s, sd)] for s in FAM for sd in SEEDS)
    print('accumulated set, weighted over nsga2-apportion within', g, '%.2fx' % (w_set / n_set))

print('== run window and binaries')
win = []
for m in man.values():
    f = datetime.datetime.fromisoformat(m['finished_utc'].replace('Z', '+00:00'))
    win.append((f - datetime.timedelta(seconds=m['wall_s']), f))
print('first start', min(x for x, _ in win), 'last finish', max(y for _, y in win))
print('commits', collections.Counter(m['commit_short'] for m in man.values()), 'dirty', collections.Counter(str(m['dirty']) for m in man.values()))
print('input_screen', collections.Counter(json.dumps(m['input_screen']) for m in man.values()))

print('== curves')
cr = list(csv.DictReader(open(f'{E}/curves-gradsel-fret-m.csv')))
by = collections.defaultdict(list)
for r in cr:
    by[((r['selection_scheme'], r['status_grading']), r['spec'], r['seed'], r['metric'])].append(r)


def last(k, met):
    L = by.get(k + (met,))
    return float(max(L, key=lambda r: float(r['elapsed_s']))['value']) if L else 0.0


for a in ARMS:
    keys = [(a, s, sd) for s in FAM for sd in SEEDS]
    endv = {met: [last(k, met) for k in keys] for met in ('solutions', 'maximal_solutions', 'ideal_solutions', 'maximal_ideal_solutions')}
    ttf = [float(by[k + ('time_to_first_repair',)][0]['value']) for k in keys if by[k + ('time_to_first_repair',)][0]['censored'] == '0']
    tti = [float(by[k + ('time_to_first_ideal_repair',)][0]['value']) for k in keys if by[k + ('time_to_first_ideal_repair',)][0]['censored'] == '0']
    agree = sum(1 for i, k in enumerate(keys) if (endv['maximal_ideal_solutions'][i] > 0) == (idx[k]['implies_ideal'] == '1'))
    maxmatch = sum(1 for i, k in enumerate(keys) if int(endv['maximal_solutions'][i]) == int(idx[k]['n_repairs']))
    print(a, {m: 'mean %.2f' % st.mean(v) for m, v in endv.items()},
          'ttf med %.2f n %d' % (med(ttf), len(ttf)), 'tti med %.2f n %d' % (med(tti), len(tti)),
          'maximal_ideal>0 agrees with implies_ideal', agree, 'maximal == n_repairs', maxmatch)
print('censored rows by metric', dict(collections.Counter(r['metric'] for r in cr if r['censored'] == '1')))
cuts = collections.Counter(len({r['elapsed_s'] for r in by[(a, s, sd, 'maximal_solutions')]}) for a in ARMS for s in FAM for sd in SEEDS)
print('maximal cuts per run', sorted(cuts.items()), 'runs with a maximal curve', sum(n for c, n in cuts.items() if c))

print('== maximal pass against the search filter at run end')
diff = collections.Counter()
for a in ARMS:
    for s in FAM:
        for sd in SEEDS:
            k = (a, s, sd)
            v, n = int(last(k, 'maximal_solutions')), int(idx[k]['n_repairs'])
            if v != n:
                print('  ', k, 'maximal', v, 'n_repairs', n, 'search implication timeouts', man[k]['implication']['timeouts'])
                diff['more' if v > n else 'fewer'] += 1
print(dict(diff))


def at(k, met, t):
    v = 0.0
    for r in sorted(by.get(k + (met,), []), key=lambda r: float(r['elapsed_s'])):
        if float(r['elapsed_s']) <= t:
            v = float(r['value'])
    return v


print('== curve means at fixed times, carried forward')
for t in (0.5, 1, 2, 5, 10):
    print(t, [(a, 'sol %.2f max %.2f maxideal %.2f' % tuple(st.mean(at((a, s, sd), met, t) for s in FAM for sd in SEEDS)
                                                           for met in ('solutions', 'maximal_solutions', 'maximal_ideal_solutions'))) for a in ARMS])

print('== scorer')
T = [line.split() for line in open(f'{E}/curves-gradsel-fret-m/av2/timings.txt')]
el = [float(t[1]) for t in T]
print('lines', len(T), 'rc', dict(collections.Counter(t[2] for t in T)),
      'sum_h %.3f mean %.2f med %.1f max %.1f' % (sum(el) / 3600, st.mean(el), med(el), max(el)),
      'accumulated mean %.1f max %d' % (st.mean(float(t[0]) for t in T), max(int(t[0]) for t in T)))
for s in FAM:
    print('  ', s, ['%s/%s mean %.1f max %.0f' % (a[0], a[1],
                    st.mean(float(t[1]) for t in T if f'_{a[1]}_{a[0]}_wkoff_log_{s}_seed' in t[3]),
                    max(float(t[1]) for t in T if f'_{a[1]}_{a[0]}_wkoff_log_{s}_seed' in t[3])) for a in ARMS])
print('failures.txt bytes', os.path.getsize(f'{E}/curves-gradsel-fret-m/av2/failures.txt'))
