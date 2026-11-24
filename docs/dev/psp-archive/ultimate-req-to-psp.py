"""Convert Ultimate/Hanfor .req files (boolean ones) into peredur .psp JSON.

I/O split: an atom is an output if it occurs in a consequent field (S, or P of
absence/universality-with-no-trigger), else an input. A requirement whose atoms
are all inputs becomes an assumption. Unsupported patterns are reported and
dropped.
"""
import re, sys, json, glob, os, collections
SC = r'(?P<scope>Globally|Before "(?P<r1>[^"]*)"|After "(?P<q1>[^"]*)" until "(?P<r2>[^"]*)"|After "(?P<q2>[^"]*)"|Between "(?P<q3>[^"]*)" and "(?P<r3>[^"]*)")\s*,\s*'
Q = r'"([^"]*)"'
PATS = [
 ("bounded-response", rf'it is always the case that if {Q} holds,? then {Q} ?holds after at most "?(\w+)"? time units?'),
 ("bounded-invariance", rf'it is always the case that if {Q} holds,? then {Q} holds for at least "?(\w+)"? time units?'),
 ("universality-if", rf'it is always the case that if {Q} holds,? then {Q} ?holds as well'),
 ("precedence", rf'it is always the case that if {Q} holds,? then {Q} previously held'),
 ("bounded-recurrence", rf'it is always the case that {Q} holds at least every "?(\w+)"? time units?'),
 ("universality", rf'it is always the case that {Q} holds'),
 ("absence", rf'it is never the case that {Q} holds'),
]
def expr(e):
    e = e.strip()
    if re.search(r'[<>=+*/]|==', e.replace('==>','')): raise ValueError("non-boolean: "+e)
    e = e.replace('&&','&').replace('||','|').replace('==>','->')
    return e
def atoms(e): return set(re.findall(r'[A-Za-z_]\w*', e)) - {"true","false"}
def parse(path):
    decl={}; reqs=[]; bad=[]
    for l in open(path, errors="replace"):
        l=l.strip()
        m=re.match(r'(Input|Output|Internal|CONST)\s+(\S+)\s+(?:is|IS)\s+(\S+)',l)
        if m: decl[m.group(2)]=(m.group(1),m.group(3)); continue
        m=re.match(r'(\S+)\s*:\s*(.*)$',l)
        if not m: continue
        rid, s = m.group(1), re.sub(r"\s+,", ",", re.sub(r"\s+", " ", re.sub(r'"\s*([^"]*?)\s*"', r'"\1"', m.group(2)))).rstrip(". ")
        ms=re.match(SC, s)
        if not ms: bad.append((rid,"scope",s)); continue
        rest=s[ms.end():]
        g=ms.groupdict()
        scope={"Globally":"globally"}.get(g["scope"]) or ("before" if g["r1"] is not None else "afteruntil" if g["q1"] is not None else "after" if g["q2"] is not None else "between")
        q = g["q1"] or g["q2"] or g["q3"]; r = g["r1"] or g["r2"] or g["r3"]
        for kind,pat in PATS:
            mm=re.fullmatch(pat, rest.strip())
            if mm: break
        else: bad.append((rid,"pattern",s)); continue
        try:
            a=[expr(x) for x in mm.groups() if not re.fullmatch(r'\w+',x or '') or kind in("universality","absence","universality-if","precedence") or x is mm.groups()[0] or x is mm.groups()[1]]
            gs=mm.groups()
            e={"scope":scope}
            if q is not None: e["q"]=expr(q)
            if r is not None: e["r"]=expr(r)
            trig=set()
            if kind in("bounded-response","bounded-invariance"):
                cnt=gs[2]
                if not cnt.isdigit(): raise ValueError("symbolic count "+cnt)
                cnt=int(cnt)
                if not 1<=cnt<=64: raise ValueError(f"count {cnt} out of 1..64")
                e.update(pattern=kind,count=cnt,p=expr(gs[0]),s=expr(gs[1])); trig=atoms(e["p"]); cons=atoms(e["s"])
            elif kind=="bounded-recurrence":
                cnt=gs[1]
                if not cnt.isdigit() or not 1<=int(cnt)<=64: raise ValueError("count "+cnt)
                e.update(pattern=kind,count=int(cnt),s=expr(gs[0])); cons=atoms(e["s"])
            elif kind=="universality-if":
                e.update(pattern="universality",s=f"({expr(gs[0])}) -> ({expr(gs[1])})"); trig=atoms(expr(gs[0])); cons=atoms(expr(gs[1]))
            elif kind=="precedence":
                # "if P holds then S previously held": S precedes P
                e.update(pattern="precedence",p=expr(gs[0]),s=expr(gs[1])); trig=atoms(e["s"]); cons=atoms(e["p"])
            elif kind=="universality":
                e.update(pattern="universality",s=expr(gs[0])); cons=atoms(e["s"])
            else:
                e.update(pattern="absence",p=expr(gs[0])); cons=atoms(e["p"])
            trig |= atoms(e.get("q","")) | atoms(e.get("r",""))
        except ValueError as ex: bad.append((rid,str(ex),s)); continue
        pass
        reqs.append((e,trig,cons))
    return decl,reqs,bad
def build(path):
    decl,reqs,bad=parse(path)
    if any(t!="bool" for (_,t) in decl.values()): return None,decl,reqs,bad
    outs=set().union(*[c for _,_,c in reqs]) if reqs else set()
    outs |= {v for v,(k,_) in decl.items() if k in("Output","Internal")}
    allat=set().union(*[t|c for _,t,c in reqs]) if reqs else set()
    ins=allat-outs
    A=[];G=[]
    for e,t,c in reqs:
        (A if (t|c)<=ins else G).append(e)
    return {"assumptions":A,"guarantees":G,"in_atoms":sorted(ins),"out_atoms":sorted(outs)},decl,reqs,bad
if __name__=="__main__":
    src,dst=sys.argv[1],sys.argv[2]; os.makedirs(dst,exist_ok=True)
    rows=[]
    for f in sorted(glob.glob(os.path.join(src,"*.req"))):
        spec,decl,reqs,bad=build(f)
        n=os.path.basename(f)[:-4]
        if spec is None: rows.append((n,"non-bool",len(reqs),len(bad),0,0,0,0)); continue
        if not spec["guarantees"]: rows.append((n,"no-guarantees",len(reqs),len(bad),0,0,0,0)); continue
        json.dump(spec,open(os.path.join(dst,n+".psp"),"w"),indent=1)
        rows.append((n,"ok",len(reqs),len(bad),len(spec["assumptions"]),len(spec["guarantees"]),len(spec["in_atoms"]),len(spec["out_atoms"])))
        for b in bad: print("DROP",n,*b,file=sys.stderr)
    import csv; w=csv.writer(open(os.path.join(dst,"manifest.csv"),"w")); w.writerow(["spec","status","converted","dropped","assumptions","guarantees","inputs","outputs"]); w.writerows(rows)
