import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

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
 * Output, one block per repair:
 *   @@REPAIR <index> <elapsed ms since the search began> <depth>
 *   asm ...;
 *   @@END
 * and a final "@@DONE <count> <runtime ms>" when the search ends by itself.
 */
public final class StreamRepairs {
    private static long start;
    private static int count;

    private static void print(List<BasicAssumption> repair, long depth) {
        long elapsed = System.currentTimeMillis() - start;
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
        if (args.length != 3) {
            System.err.println("usage: StreamRepairs <file.spectra> <UF|BFS|ALUR> <depth, -1 for none>");
            System.exit(2);
        }
        File file = new File(args[0]);
        String alg = args[1];
        int depth = Integer.parseInt(args[2]);

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
        System.out.println("@@DONE " + count + " " + (System.currentTimeMillis() - start));
        System.out.flush();
    }
}
