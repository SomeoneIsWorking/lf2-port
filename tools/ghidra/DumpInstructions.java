// Dump every disassembled instruction as: address<TAB>length<TAB>mnemonic<TAB>hexbytes<TAB>text
// This is the decoder's test corpus AND its oracle -- our decoder must agree with
// Ghidra on the length and mnemonic of every instruction in .text.
//@category LF2
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.MemoryBlock;
import java.io.PrintWriter;

public class DumpInstructions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String out = getScriptArgs().length > 0 ? getScriptArgs()[0] : "instructions.tsv";
        MemoryBlock text = currentProgram.getMemory().getBlock(".text");
        if (text == null) {
            println("no .text block");
            return;
        }
        AddressSetView range = currentProgram.getMemory().getLoadedAndInitializedAddressSet()
                .intersect(new ghidra.program.model.address.AddressSet(text.getStart(), text.getEnd()));

        PrintWriter pw = new PrintWriter(out);
        long n = 0;
        for (Instruction ins : currentProgram.getListing().getInstructions(range, true)) {
            StringBuilder hex = new StringBuilder();
            for (byte b : ins.getBytes()) {
                hex.append(String.format("%02x", b));
            }
            pw.printf("%s\t%d\t%s\t%s\t%s%n",
                    ins.getAddress(), ins.getLength(), ins.getMnemonicString(),
                    hex, ins.toString().replace('\t', ' '));
            n++;
        }
        pw.close();
        println("wrote " + n + " instructions to " + out);
    }
}
