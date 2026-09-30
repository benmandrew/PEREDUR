"""Reduce each family's pooled PEREDUR repairs to their frontier, writing
<H>/out/<fam>/maximal-P.txt; the cross-tool matrix already holds every
PEREDUR repair, so the joint frontier needs nothing more."""
import os, subprocess, sys, time
H = os.path.expanduser("~/overlap"); B = os.path.expanduser("~/projects/counter/build-release")
CAP = int(os.environ.get("CAP", "7200")); log = open(f"{H}/out/log.tsv", "a")
for fam in sys.argv[1:]:
    t = time.time()
    try:
        r = subprocess.run([f"{B}/maximal", f"{H}/pool/{fam}/P", "--jobs", "30"], capture_output=True, text=True, timeout=CAP)
        rc, text = r.returncode, r.stdout
    except subprocess.TimeoutExpired:
        rc, text = "cap", ""
    open(f"{H}/out/{fam}/maximal-P.txt", "w").write(text)
    log.write(f"{fam}\tmaximal-P\t{rc}\t{time.time()-t:.1f}\n"); log.flush()
