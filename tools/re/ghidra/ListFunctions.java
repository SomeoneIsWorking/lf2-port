// Dump every function as: address<TAB>size<TAB>name
//@category LF2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.PrintWriter;

public class ListFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "functions.tsv";
        PrintWriter pw = new PrintWriter(out);
        int n = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            pw.printf("%s\t%d\t%s%n", f.getEntryPoint(), f.getBody().getNumAddresses(), f.getName());
            n++;
        }
        pw.close();
        println("wrote " + n + " functions to " + out);
    }
}
