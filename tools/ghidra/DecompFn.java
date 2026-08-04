// Decompile the function containing the VA in env LF2_DECOMP_VA to stdout.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class DecompFn extends GhidraScript {
    @Override
    public void run() throws Exception {
        String va = System.getenv("LF2_DECOMP_VA");
        if (va == null) { println("LF2_DECOMP_VA not set"); return; }
        Address a = currentProgram.getAddressFactory().getDefaultAddressSpace()
                    .getAddress(Long.parseLong(va, 16));
        Function f = getFunctionContaining(a);
        if (f == null) { println("no function contains " + va); return; }
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        DecompileResults r = d.decompileFunction(f, 120, monitor);
        println("=== " + f.getName() + " @ " + f.getEntryPoint());
        println(r.getDecompiledFunction().getC());
        d.dispose();
    }
}
