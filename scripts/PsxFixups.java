// Ghidra script: manual analysis corrections that apply to the main EXE program and to every
// overlay program (they all map the main EXE text). Safe to re-run.
//@category PaRappa
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Reference;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class PsxFixups extends GhidraScript {
    // Game functions that PsyQ signature matching mislabeled as library functions.
    static final long[] NOT_LIBRARY = { 0x80014324L, 0x80025E48L };
    // Game functions that analysis missed: { start, end (exclusive) }.
    static final long[][] MISSING = { { 0x80012960L, 0x80013650L } };
    // Callbacks only referenced as data (VSyncCallback, CdSyncCallback, DecDCToutCallback).
    static final long[] CALLBACKS = { 0x80017F38L, 0x8001A210L, 0x80027220L };
    // Library functions the PsyQ signatures left unnamed or ambiguous (same table as gen_decomp.py).
    static final Object[][] LIB_NAMES = {
        { 0x80040AE4L, "GsDefDispBuff" }, { 0x80040C74L, "GsSetProjection" }, { 0x80040CA4L, "GsDrawOt" },
        { 0x80040F90L, "GsSetWorkBase" }, { 0x8003B0CCL, "TransMatrix" }, { 0x800392C0L, "StUnSetRing" },
        { 0x80048A40L, "EnterCriticalSection" }, { 0x80048A50L, "ExitCriticalSection" }, { 0x8002AC20L, "SsInit" },
    };
    // Parameter counts of the PsyQ functions the game calls (from the SDK API). Applied so that the
    // decompiler passes exactly these arguments at every call site. A trailing '+' marks varargs.
    static final String LIB_ARITY =
        "ApplyMatrix 3,CdControlB 3,CdControlF 2,CdInit 0,CdIntToPos 2,CdLastCom 0,CdMix 1,CdPosToInt 1," +
        "CdRead 3,CdRead2 1,CdReadSync 2,CdSearchFile 2,CdSync 2,CdSyncCallback 1,ChangeClearPAD 1,ClearImage 4," +
        "CloseEvent 1,CompMatrix 3,DecDCTReset 1,DecDCTin 2,DecDCTout 2,DecDCToutCallback 1,DecDCTvlc 2," +
        "DecDCTvlcSize 1,DrawSync 1,EnableEvent 1,SsInit 0,StUnSetRing 0,TransMatrix 2,GsDefDispBuff 4," +
        "GsSetProjection 1,GsDrawOt 1,GsSetWorkBase 1,EnterCriticalSection 0,ExitCriticalSection 0,FntFlush 1," +
        "FntLoad 2,FntOpen 6,FntPrint 1+,GetTPage 4,GsClearDispArea 3,GsClearOt 3,GsGetActiveBuff 0,GsGetLs 2," +
        "GsGetLw 2,GsGetTimInfo 2,GsInit3D 0,GsInitCoordinate2 2,GsInitFixBg32 2,GsInitGraph 5,GsLinkObject4 3," +
        "GsMapModelingData 1,GsMulCoord2 2,GsSetLsMatrix 1,GsSetRefView2 1,GsSetRefView2L 1,GsSortBoxFill 3," +
        "GsSortClear 4,GsSortFastSprite 3,GsSortFixBg32 4,GsSortObject4J 4,GsSortPoly 3,GsSortSprite 3," +
        "GsSwapDispBuff 0,InitCARD 1,LoadClut 3,LoadClut2 3,LoadImage 2,MoveImage 3,OpenEvent 4,PadInit 1," +
        "PadRead 1,ResetCallback 0,ResetGraph 1,RotMatrixX 2,RotMatrixY 2,RotMatrixZ 2,ScaleMatrix 2,SetDispMask 1," +
        "SetDumpFnt 1,SetMem 1,SetPolyG4 1,SsGetMVol 1,SsSetMVol 2,SsSetSerialAttr 3,SsSetSerialVol 3," +
        "SsSetTickMode 1,SsStart 0,SsUtAllKeyOff 1,SsUtFlush 0,SsUtKeyOff 5,SsUtKeyOn 7,SsVabClose 1," +
        "SsVabOpenHead 2,SsVabTransBody 2,SsVabTransBodyPartly 3,SsVabTransCompleted 1,StFreeRing 1,StGetNext 2,StSetRing 2,StSetStream 5," +
        "StartCARD 0,StopCallback 0,StoreImage 2,TestEvent 1,VSync 1,VSyncCallback 1,VectorNormal 2,__adddf3 4," +
        "__divdf3 4,__fixdfsi 2,__floatsidf 1,__main 0,_bu_init 0,_card_clear 1,_card_info 1,_card_load 1,bcmp 3," +
        "close 1,delete 1,exit 1,firstfile 2,format 1,gteMIMefunc 4,nextfile 1,open 2,rand 0,ratan2 2,rcos 1,read 3," +
        "rsin 1,sprintf 2+,srand 1,strcat 2,strcmp 2,strcpy 2,strlen 1,strncmp 3,write 3";

    // Switch jump tables analysis did not resolve: { jr address, table address, entries }.
    static final long[][] JUMP_TABLES = { { 0x80022188L, 0x800111DCL, 5 } };

    @Override
    public void run() throws Exception {
        int changes = 0;
        // RAM is writable: a read-only block makes the decompiler fold loads of globals into their
        // initial values (a pointer that is still 0 in the EXE image becomes the constant 0).
        for (ghidra.program.model.mem.MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            long start = b.getStart().getOffset();
            if (start >= 0x80000000L && start < 0x80200000L && (!b.isWrite() || b.isVolatile())) {
                b.setWrite(true);
                changes++;
            }
        }
        for (long a : NOT_LIBRARY) {
            Function f = getFunctionAt(toAddr(a));
            if (f != null && !f.getName().startsWith("FUN_")) {
                f.setName(String.format("FUN_%08x", a), SourceType.ANALYSIS);
                changes++;
            }
        }
        for (Object[] ln : LIB_NAMES) {
            Function f = getFunctionAt(toAddr((Long) ln[0]));
            if (f != null && !f.getName().equals(ln[1])) {
                f.setName((String) ln[1], SourceType.USER_DEFINED);
                changes++;
            }
        }
        for (long[] r : MISSING) {
            Address start = toAddr(r[0]);
            if (getFunctionAt(start) != null) continue;
            Function containing = getFunctionContaining(start);
            if (containing != null) removeFunction(containing);
            clearListing(start, toAddr(r[1] - 1));
            new DisassembleCommand(new AddressSet(start, toAddr(r[1] - 1)), null, true).applyTo(currentProgram, monitor);
            if (createFunction(start, null) != null) changes++;
        }
        for (long cb : CALLBACKS) {
            Address a = toAddr(cb);
            if (getFunctionAt(a) != null) continue;
            Function containing = getFunctionContaining(a);
            Address parent = containing != null ? containing.getEntryPoint() : null;
            if (containing != null) removeFunction(containing);
            disassemble(a);
            if (createFunction(a, null) != null) changes++;
            if (parent != null) createFunction(parent, null);
        }
        for (long[] jt : JUMP_TABLES) {
            Address jr = toAddr(jt[0]);
            Function f = getFunctionContaining(jr);
            if (f == null) continue;
            boolean added = false;
            for (int i = 0; i < jt[2]; i++) {
                Address target = toAddr(getInt(toAddr(jt[1] + 4L * i)) & 0xFFFFFFFFL);
                boolean known = false;
                for (Reference r : getReferencesFrom(jr))
                    known |= r.getToAddress().equals(target);
                if (known) continue;
                disassemble(target);
                currentProgram.getReferenceManager().addMemoryReference(jr, target, RefType.COMPUTED_JUMP,
                    SourceType.USER_DEFINED, 0);
                added = true;
            }
            if (added) {
                CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
                changes++;
            }
        }
        changes += mergeFragments();
        changes += applyLibArity();
        if (currentProgram.getName().equals("SCUS_941.83")) changes += createOverlayCallees();
        if (currentProgram.getMemory().getBlock("overlay") != null) changes += createOverlayEntries();
        changes += completeCoverage();
        changes += commitInferredSignatures();
        changes += applyReturnTypes();
        changes += applyParamCounts();
        println("PsxFixups: " + currentProgram.getName() + " changes=" + changes);
    }

    // Commits the decompiler's inferred parameters and returns for game functions whose signature
    // was never set (as PsxExport does before exporting). On a fresh project this must come before
    // applyReturnTypes / applyParamCounts: once those mark a signature as set, its parameters (and
    // so its callers' arguments) would never be filled in. A no-op on an analyzed project.
    int commitInferredSignatures() throws Exception {
        java.util.List<Function> commit = new java.util.ArrayList<>();
        ghidra.program.model.mem.MemoryBlock ovl = currentProgram.getMemory().getBlock("overlay");
        if (ovl != null)
            for (Function f : currentProgram.getFunctionManager().getFunctions(ovl.getStart(), true)) {
                if (f.getEntryPoint().compareTo(ovl.getEnd()) > 0) break;
                commit.add(f);
            }
        for (Function f : currentProgram.getFunctionManager().getFunctions(toAddr(0x800127C4L), true)) {
            if (f.getEntryPoint().getOffset() >= 0x80028588L) break;
            commit.add(f);
        }
        commit.removeIf(f -> f.getSignatureSource() != SourceType.DEFAULT);
        if (commit.isEmpty()) return 0;

        ghidra.app.decompiler.DecompInterface dec = new ghidra.app.decompiler.DecompInterface();
        ghidra.app.decompiler.DecompileOptions opts = new ghidra.app.decompiler.DecompileOptions();
        opts.grabFromProgram(currentProgram);
        dec.setOptions(opts);
        dec.toggleCCode(true);
        dec.toggleSyntaxTree(true);
        dec.setSimplificationStyle("decompile");
        dec.openProgram(currentProgram);
        int n = 0;
        try {
            // Two rounds let signatures settle through call chains.
            for (int round = 0; round < 2; round++) {
                for (Function f : commit) {
                    monitor.checkCancelled();
                    if (f.getSignatureSource() != SourceType.DEFAULT) continue;
                    ghidra.app.decompiler.DecompileResults r = dec.decompileFunction(f, 60, monitor);
                    if (r == null || !r.decompileCompleted() || r.getHighFunction() == null) continue;
                    ghidra.program.model.pcode.HighFunctionDBUtil.commitParamsToDatabase(r.getHighFunction(), true,
                        ghidra.program.model.pcode.HighFunctionDBUtil.ReturnCommitOption.COMMIT, SourceType.ANALYSIS);
                    n++;
                }
            }
        } finally {
            dec.dispose();
        }
        return n;
    }

    // Makes every piece of game code part of a function, so the whole game is decompiled before it is
    // compiled (instead of discovering pointer-only functions at run time):
    //  1. words anywhere in the program's data that point at an entry-like address in the game code
    //     (range start, right after "jr ra; delay", or a stack-frame prologue) become functions;
    //  2. code stretches not covered by any function, starting at such a boundary, become functions.
    // The game code range is the main EXE's game part, or the code span of the overlay.
    int completeCoverage() throws Exception {
        long lo, hi;
        ghidra.program.model.mem.MemoryBlock ovl = currentProgram.getMemory().getBlock("overlay");
        if (ovl != null) {
            lo = ovl.getStart().getOffset();
            hi = lo;
            for (Function f : currentProgram.getFunctionManager().getFunctions(ovl.getStart(), true)) {
                if (!ovl.contains(f.getEntryPoint())) break;
                hi = Math.max(hi, f.getBody().getMaxAddress().getOffset() + 1);
            }
        } else if (currentProgram.getName().equals("SCUS_941.83")) {
            lo = 0x800127C4L;
            hi = 0x80028588L;
        } else {
            return 0;
        }
        java.util.TreeSet<Long> entries = new java.util.TreeSet<>();
        // 1. Pointers in initialized memory.
        for (ghidra.program.model.mem.MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.isInitialized()) continue;
            long s = (b.getStart().getOffset() + 3) & ~3L, e = b.getEnd().getOffset() + 1;
            byte[] buf = new byte[(int) (e - s)];
            currentProgram.getMemory().getBytes(toAddr(s), buf);
            for (int i = 0; i + 4 <= buf.length; i += 4) {
                long v = (buf[i] & 0xFFL) | (buf[i + 1] & 0xFFL) << 8 | (buf[i + 2] & 0xFFL) << 16 | (buf[i + 3] & 0xFFL) << 24;
                if (v >= lo && v < hi && (v & 3) == 0 && entryLike(v, lo)) entries.add(v);
            }
        }
        // 2. Gaps between functions.
        for (long a = lo; a < hi; a += 4) {
            if (getFunctionContaining(toAddr(a)) != null) continue;
            if (getInt(toAddr(a)) == 0) continue;  // padding
            if (entryLike(a, lo)) entries.add(a);
        }
        int made = 0;
        for (long e : entries) {
            Address a = toAddr(e);
            if (getFunctionAt(a) != null || getFunctionContaining(a) != null) continue;
            disassemble(a);
            if (getInstructionAt(a) != null && createFunction(a, null) != null) made++;
        }
        // Report what is still outside functions.
        long uncovered = 0;
        for (long a = lo; a < hi; a += 4)
            if (getFunctionContaining(toAddr(a)) == null && getInt(toAddr(a)) != 0) uncovered += 4;
        println("PsxFixups: coverage " + currentProgram.getName() + ": " + made + " functions added, " + uncovered +
            " non-zero code bytes outside functions in " + Long.toHexString(lo) + "-" + Long.toHexString(hi));
        return made;
    }

    boolean entryLike(long a, long lo) throws Exception {
        if (a == lo) return true;
        int w = getInt(toAddr(a));
        if ((w >>> 16) == 0x27BD && (w & 0x8000) != 0) return true;  // addiu sp,sp,-N
        return a - 8 >= lo && getInt(toAddr(a - 8)) == 0x03E00008;     // after "jr ra; delay"
    }

    // Overlay functions only reached through pointers: the main EXE's stage table (init/run/exit
    // per stage, at 0x80048D28, in COMOD order 0,1,2,3,5,6,7,8,9) and code addresses the overlay
    // builds with lui/addiu (or lui/ori) that start right after a "jr ra; delay" pair.
    int createOverlayEntries() throws Exception {
        java.util.TreeSet<Long> entries = new java.util.TreeSet<>();
        String[] order = {"COMOD0", "COMOD1", "COMOD2", "COMOD3", "COMOD5", "COMOD6", "COMOD7", "COMOD8", "COMOD9"};
        String name = currentProgram.getName();
        for (int stage = 0; stage < order.length; stage++) {
            if (!name.startsWith(order[stage])) continue;
            for (int k = 0; k < 3; k++)
                entries.add(getInt(toAddr(0x80048D28L + (stage * 3 + k) * 4L)) & 0xFFFFFFFFL);
        }
        ghidra.program.model.mem.MemoryBlock ovl = currentProgram.getMemory().getBlock("overlay");
        long lo = ovl.getStart().getOffset(), hi = ovl.getEnd().getOffset() + 1;
        for (long a = lo; a + 4 <= hi; a += 4) {
            int w = getInt(toAddr(a));
            if ((w >>> 26) != 0x0F) continue;
            int rt = (w >>> 16) & 31;
            for (long b = a + 4; b < Math.min(a + 32, hi); b += 4) {
                int v = getInt(toAddr(b));
                int op = v >>> 26;
                if ((op == 0x09 || op == 0x0D) && ((v >>> 21) & 31) == rt) {
                    long imm = v & 0xFFFF;
                    if (op == 0x09 && (imm & 0x8000) != 0) imm -= 0x10000;
                    long t = ((((long) w & 0xFFFF) << 16) + imm) & 0xFFFFFFFFL;
                    if (t >= lo + 8 && t < hi && (t & 3) == 0 && getInt(toAddr(t - 8)) == 0x03E00008) entries.add(t);
                    break;
                }
            }
        }
        int made = 0;
        for (long e : entries) {
            Address a = toAddr(e);
            if (e < lo || e >= hi || getFunctionAt(a) != null) continue;
            Function containing = getFunctionContaining(a);
            Address parent = containing != null ? containing.getEntryPoint() : null;
            if (containing != null) removeFunction(containing);
            disassemble(a);
            if (createFunction(a, null) != null) made++;
            if (parent != null && getFunctionAt(parent) == null) createFunction(parent, null);
        }
        return made;
    }

    // Main EXE functions that only the stage overlays call: seed them from the overlays' jal targets.
    int createOverlayCallees() throws Exception {
        java.io.File dir = new java.io.File(getSourceFile().getParentFile().getAbsolutePath(), "../game/ghidra_in");
        java.io.File[] files = dir.listFiles((d, n) -> n.matches("COMOD\\d\\.BIN"));
        if (files == null) return 0;
        java.util.TreeSet<Long> targets = new java.util.TreeSet<>();
        for (java.io.File f : files) {
            byte[] b = java.nio.file.Files.readAllBytes(f.toPath());
            for (int i = 0; i + 4 <= b.length; i += 4) {
                int w = (b[i] & 0xFF) | (b[i + 1] & 0xFF) << 8 | (b[i + 2] & 0xFF) << 16 | (b[i + 3] & 0xFF) << 24;
                if ((w >>> 26) != 3) continue;
                long t = 0x80000000L | ((long) (w & 0x03FFFFFF) << 2);
                if (t >= 0x800127C4L && t < 0x80028588L) targets.add(t);
            }
        }
        int made = 0;
        for (long t : targets) {
            Address a = toAddr(t);
            if (getFunctionAt(a) != null) continue;
            Function containing = getFunctionContaining(a);
            if (containing != null) continue;  // a mid-function target: leave for review
            disassemble(a);
            if (createFunction(a, null) != null) made++;
        }
        return made;
    }

    // Functions whose result callers use although Ghidra infers "void": they end with a call whose
    // result they pass straight back (v0 untouched until jr ra). Found by port/tools/audit_void_returns.py.
    // Overlay addresses are per program (the same address is a different function in each stage).
    static final long[] RETURNS_VALUE_MAIN = {0x800178c8L, 0x800193f4L, 0x8001c6e0L};
    static final Object[][] RETURNS_VALUE_OVERLAY = {
        {"COMOD0.BIN", 0x801c448cL}, {"COMOD1.BIN", 0x801c750cL}, {"COMOD2.BIN", 0x801c6804L},
        {"COMOD3.BIN", 0x801c6ba0L}, {"COMOD5.BIN", 0x801c7fa8L}, {"COMOD6.BIN", 0x801c6340L},
        {"COMOD7.BIN", 0x801c6d4cL}, {"COMOD8.BIN", 0x801c3a9cL}, {"COMOD9.BIN", 0x801c3aa0L},
    };

    // Game functions whose incoming registers the decompiler saw only passed through (in_a0, in_a1):
    // it commits them with too few parameters and their callers drop the arguments.
    static final long[][] PARAM_COUNT = {
        {0x80017ee8L, 2},  // save-screen redraw: (screen, state) -> FUN_8001e750
    };

    int applyParamCounts() throws Exception {
        int n = 0;
        for (long[] pc : PARAM_COUNT) {
            Function f = getFunctionAt(toAddr(pc[0]));
            if (f == null || (f.getParameterCount() == pc[1] && f.getSignatureSource() == SourceType.USER_DEFINED))
                continue;
            java.util.List<ghidra.program.model.listing.Variable> params = new java.util.ArrayList<>();
            for (int i = 0; i < pc[1]; i++)
                params.add(new ghidra.program.model.listing.ParameterImpl("param_" + (i + 1),
                    ghidra.program.model.data.IntegerDataType.dataType, currentProgram));
            f.updateFunction("default", f.getReturn(), params, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                true, SourceType.USER_DEFINED);
            n++;
        }
        return n;
    }

    int applyReturnTypes() throws Exception {
        java.util.List<Long> addrs = new java.util.ArrayList<>();
        for (long a : RETURNS_VALUE_MAIN) addrs.add(a);
        for (Object[] o : RETURNS_VALUE_OVERLAY)
            if (currentProgram.getName().equals(o[0])) addrs.add((Long) o[1]);
        int n = 0;
        for (long a : addrs) {
            Function f = getFunctionAt(toAddr(a));
            if (f == null) continue;
            if (f.getReturnType() instanceof ghidra.program.model.data.IntegerDataType
                && f.getSignatureSource() == SourceType.USER_DEFINED) continue;
            f.setReturnType(ghidra.program.model.data.IntegerDataType.dataType, SourceType.USER_DEFINED);
            n++;
        }
        return n;
    }

    int applyLibArity() throws Exception {
        java.util.Map<String, Function> byName = new java.util.HashMap<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) byName.putIfAbsent(f.getName(), f);
        int n = 0;
        for (String spec : LIB_ARITY.split(",")) {
            String[] kv = spec.trim().split(" ");
            Function f = byName.get(kv[0]);
            if (f == null || f.getSignatureSource() == SourceType.USER_DEFINED) continue;
            boolean varargs = kv[1].endsWith("+");
            int count = Integer.parseInt(kv[1].replace("+", ""));
            java.util.List<ghidra.program.model.listing.Variable> params = new java.util.ArrayList<>();
            for (int i = 0; i < count; i++)
                params.add(new ghidra.program.model.listing.ParameterImpl("p" + i,
                    ghidra.program.model.data.IntegerDataType.dataType, currentProgram));
            ghidra.program.model.data.DataType ret = kv[0].matches("__(adddf3|divdf3|floatsidf)")
                ? ghidra.program.model.data.Undefined8DataType.dataType
                : ghidra.program.model.data.IntegerDataType.dataType;
            f.updateFunction("default", new ghidra.program.model.listing.ReturnParameterImpl(ret, currentProgram),
                params, Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS, true, SourceType.USER_DEFINED);
            f.setVarArgs(varargs);
            n++;
        }
        return n;
    }

    // The PsyQ signature matcher creates functions at every local label of a library object
    // ("GS_137_OBJ_1B8"). Those that are only branched to are pieces of the preceding function:
    // remove them and let the parent's body grow back over them.
    int mergeFragments() throws Exception {
        java.util.List<Function> fragments = new java.util.ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (!f.getName().matches(".*_OBJ_[0-9A-F]+")) continue;
            boolean called = false;
            for (Reference r : getReferencesTo(f.getEntryPoint()))
                called |= r.getReferenceType().isCall() || r.getReferenceType().isData();
            if (!called) fragments.add(f);
        }
        java.util.Set<Function> parents = new java.util.LinkedHashSet<>();
        for (Function f : fragments) {
            Address entry = f.getEntryPoint();
            removeFunction(f);
            Function parent = getFunctionBefore(entry);
            if (parent != null) parents.add(parent);
        }
        for (Function p : parents) CreateFunctionCmd.fixupFunctionBody(currentProgram, p, monitor);
        return fragments.size();
    }
}
