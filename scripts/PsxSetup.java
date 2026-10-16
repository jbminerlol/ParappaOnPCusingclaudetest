// Ghidra pre-analysis script: builds the PS1 memory map for PaRappa the Rapper,
// sets the gp register, and seeds function starts.
//
// Args:  main                         -> imported file is SCUS text (header stripped) at 0x80010000
//        seedmain                     -> program was imported by the PSX loader; only seed functions
//        overlay <path-to-main-text> [<functions.tsv>]
//                                     -> imported file is a COMODx.BIN at 0x801C3870; names of the
//                                        main EXE functions are copied from the exported functions.tsv
//@category PaRappa
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.ProgramContext;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

import java.io.FileInputStream;
import java.math.BigInteger;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.TreeSet;

public class PsxSetup extends GhidraScript {
    static final long TEXT_BASE = 0x80010000L;
    static final long TEXT_END = 0x8006F000L;
    static final long OVL_BASE = 0x801C3870L;
    static final long RAM_END = 0x80200000L;
    static final long GP = 0x8006EA40L;

    Memory mem;

    MemoryBlock uninit(String name, long start, long end, boolean exec) throws Exception {
        if (end <= start) return null;
        MemoryBlock b = mem.createUninitializedBlock(name, toAddr(start), end - start, false);
        b.setPermissions(true, true, exec);
        return b;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        boolean overlay = args.length > 0 && args[0].equals("overlay");
        boolean seedOnly = args.length > 0 && args[0].equals("seedmain");
        mem = currentProgram.getMemory();
        long ownStart = TEXT_BASE, ownEnd = TEXT_END;
        if (!seedOnly) {
            MemoryBlock own = mem.getBlock(toAddr(overlay ? OVL_BASE : TEXT_BASE));
            own.setName(overlay ? "overlay" : "text");
            own.setPermissions(true, true, true);
            ownStart = own.getStart().getOffset();
            ownEnd = own.getEnd().getOffset() + 1;

            if (overlay) {
                Path mainText = Path.of(args[1]);
                long len = Files.size(mainText);
                try (FileInputStream in = new FileInputStream(mainText.toFile())) {
                    MemoryBlock t = mem.createInitializedBlock("text", toAddr(TEXT_BASE), in, len, monitor, false);
                    t.setPermissions(true, true, true);
                }
                uninit("bss", TEXT_END, OVL_BASE, false);
                uninit("heap", (ownEnd + 3) & ~3L, RAM_END, false);
            } else {
                uninit("bss", TEXT_END, OVL_BASE, false);
                uninit("heap", OVL_BASE, RAM_END, false);
            }
            uninit("kernel", 0x80000000L, TEXT_BASE, false);
            uninit("scratchpad", 0x1F800000L, 0x1F800400L, false);
            MemoryBlock io = uninit("io", 0x1F801000L, 0x1F803000L, false);
            io.setVolatile(true);

            // gp is constant for the whole game (set once in _start).
            ProgramContext ctx = currentProgram.getProgramContext();
            Register gp = ctx.getRegister("gp");
            for (MemoryBlock b : mem.getBlocks())
                ctx.setValue(gp, b.getStart(), b.getEnd(), BigInteger.valueOf(GP));
        }

        // Seed function starts: jal targets inside our own block + classic prologues.
        TreeSet<Long> seeds = new TreeSet<>();
        for (long a = ownStart; a + 4 <= ownEnd; a += 4) {
            int w = mem.getInt(toAddr(a));
            if ((w >>> 26) == 3) {
                long t = 0x80000000L | ((long) (w & 0x03FFFFFF) << 2);
                if (t >= ownStart && t < ownEnd) seeds.add(t);
            }
            // addiu sp,sp,-N preceded by "jr ra; <delay>" or zero padding
            if ((w >>> 16) == 0x27BD && (w & 0x8000) != 0 && a >= ownStart + 8) {
                int p2 = mem.getInt(toAddr(a - 8));
                int p1 = mem.getInt(toAddr(a - 4));
                if (p2 == 0x03E00008 || (p1 == 0 && p2 == 0)) seeds.add(a);
            }
        }
        if (!overlay) {
            seeds.add(0x80028590L);
        }
        int made = 0;
        for (long s : seeds) {
            Address a = toAddr(s);
            if (getFunctionAt(a) != null) continue;
            disassemble(a);
            if (createFunction(a, null) != null) made++;
        }
        if (!overlay) {
            label(0x80028590L, "_start");
            label(0x800259D0L, "main");
        } else if (args.length > 2) {
            int named = 0;
            for (String line : Files.readAllLines(Path.of(args[2]))) {
                String[] cols = line.split("	");
                if (cols.length < 2 || cols[0].equals("address")) continue;
                long a = Long.parseUnsignedLong(cols[0], 16);
                if (a >= OVL_BASE) continue;
                label(a, cols[1]);
                named++;
            }
            println("PsxSetup: imported " + named + " main EXE function names");
        }
        println("PsxSetup: " + (overlay ? "overlay" : "main") + " block " + Long.toHexString(ownStart) + "-" +
            Long.toHexString(ownEnd) + ", seeded " + made + " functions from " + seeds.size() + " candidates");
    }

    void label(long addr, String name) throws Exception {
        Address a = toAddr(addr);
        Function f = getFunctionAt(a);
        if (f == null) {
            disassemble(a);
            f = createFunction(a, name);
        }
        if (f != null && !name.startsWith("FUN_")) f.setName(name, SourceType.IMPORTED);
    }
}
