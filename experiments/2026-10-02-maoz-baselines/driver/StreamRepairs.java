import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.TreeSet;

import tau.smlab.syntech.counterstrategy.repair.BFSModelRepair;
import tau.smlab.syntech.gameinput.model.GameInput;
import tau.smlab.syntech.gameinputtrans.TranslationProvider;
import tau.smlab.syntech.jtlv.Env;
import tau.smlab.syntech.repair.AbstractRepair;
import tau.smlab.syntech.repair.BasicAssumption;
import tau.smlab.syntech.repair.alur.SpecificationRefinement;
import tau.smlab.syntech.repair.chatterjee.TheUltimateFixer;
import tau.smlab.syntech.spectragameinput.SpectraInputProviderNoIDE;

/**
 * Runs GLASS (UF), JVTS-Repair (BFS) or AMT13 (ALUR) from the ICSE 2019
 * artifact with the arguments RepairExporterExec passes, but prints each
 * repair the moment the algorithm records it. RepairExporterExec prints only
 * once the search ends, so a run stopped at a time cap printed nothing.
 *
 * Printing happens inside recordRepair, on the algorithm's own thread: the
 * BDD package is not thread-safe, and a repair's text is read off its BDDs.
 *
 * A repair is printed only the first time its assumption set is seen: the
 * set of its `asm` statements with whitespace collapsed, in any order.
 * JVTS-Repair records one set many times (805 calls, 221 sets on Lift).
 * Once `max` distinct repairs are printed the driver stops, so a job ends at
 * that many repairs or at the harness's time cap, whichever comes first.
 *
 * Output, one block per distinct repair:
 *   @@REPAIR <index> <elapsed ms since the search began> <depth>
 *   asm ...;
 *   @@END
 * then "@@DONE <distinct> <raw> <runtime ms>" when the search ends by itself,
 * or "@@CAP <distinct> <raw> <runtime ms>" when it is stopped at `max`.
 * <raw> counts every recordRepair call, repeats included.
 */
public final class StreamRepairs {
    private static long start;
    private static int count;
    private static int raw;
    private static int max;
    private static final Set<String> seen = new HashSet<>();

    private static String key(List<BasicAssumption> repair) {
        TreeSet<String> parts = new TreeSet<>();
        for (BasicAssumption ba : repair) {
            parts.add(ba.toString().trim().replaceAll("\\s+", " "));
        }
        return String.join("\n", parts);
    }

    private static void print(List<BasicAssumption> repair, long depth) {
        long elapsed = System.currentTimeMillis() - start;
        raw++;
        if (!seen.add(key(repair))) {
            return;
        }
        count++;
        StringBuilder out = new StringBuilder();
        out.append("@@REPAIR ").append(count).append(' ').append(elapsed)
           .append(' ').append(depth).append('\n');
        for (BasicAssumption ba : repair) {
            out.append(ba.toString()).append('\n');
        }
        out.append("@@END\n");
        System.out.print(out);
        System.out.flush();
        if (max > 0 && count >= max) {
            System.out.println("@@CAP " + count + " " + raw + " "
                               + (System.currentTimeMillis() - start));
            System.out.flush();
            System.exit(0);
        }
    }

    static final class UF extends TheUltimateFixer {
        UF(GameInput gi) { super(gi, true, true); }
        @Override protected void recordRepair(List<BasicAssumption> r, long t, long d) {
            super.recordRepair(r, t, d);
            print(r, d);
        }
    }

    static final class BFS extends BFSModelRepair {
        BFS(GameInput gi, int depth) { super(gi, depth, false, false, false, true); }
        @Override protected void recordRepair(List<BasicAssumption> r, long t, long d) {
            super.recordRepair(r, t, d);
            print(r, d);
        }
    }

    static final class ALUR extends SpecificationRefinement {
        ALUR(GameInput gi, HashMap<String, ArrayList<String>> p, int depth) { super(gi, p, depth, 3, true); }
        @Override protected void recordRepair(List<BasicAssumption> r, long t, long d) {
            super.recordRepair(r, t, d);
            print(r, d);
        }
    }

    public static void main(String[] args) throws Exception {
        if (args.length != 4) {
            System.err.println("usage: StreamRepairs <file.spectra> <UF|BFS|ALUR> <depth, -1 for none> <max distinct repairs, 0 for none>");
            System.exit(2);
        }
        File file = new File(args[0]);
        String alg = args[1];
        int depth = Integer.parseInt(args[2]);
        max = Integer.parseInt(args[3]);

        Env.resetEnv();
        Env.enableReorder();
        GameInput gi = new SpectraInputProviderNoIDE().getGameInput(file.getAbsolutePath());
        TranslationProvider.translate(gi);

        AbstractRepair repair;
        switch (alg) {
            case "UF":
                repair = new UF(gi);
                break;
            case "BFS":
                repair = new BFS(gi, depth);
                break;
            case "ALUR": {
                HashMap<String, ArrayList<String>> p = new HashMap<>();
                for (String k : new String[] {"P1", "P2", "P3", "P4"}) {
                    p.put(k, new ArrayList<String>());
                }
                repair = new ALUR(gi, p, depth);
                break;
            }
            default:
                throw new IllegalArgumentException("unknown algorithm " + alg);
        }
        start = System.currentTimeMillis();
        repair.execute();
        if (repair.isRealizable()) {
            System.out.println("@@REALIZABLE");
        }
        System.out.println("@@DONE " + count + " " + raw + " " + (System.currentTimeMillis() - start));
        System.out.flush();
    }
}
