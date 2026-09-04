#@runtime Jython
# Decompile the function containing each VA in $LF2_DECOMP_TARGETS to
# scratch/decomp/<VA>.c, one file per target.
#
# WHY THIS EXISTS. A disassembly answers "what bytes are here" but not "what does this
# function DO". Reading 2000 bytes of x86 to find four
# geometry constants is how a wrong constant gets copied into the port -- the pre-fight
# overlay's row spacing was measured off blit rectangles and was wrong, and no amount of
# staring at instructions would have said so. The decompiler's C says it in a line.
#
# The project and program are the port's own binary; scratch/ is gitignored, so the .gpr/.rep
# pair persists between runs and the (slow) auto-analysis is paid for once.
#
# ARGS come from the environment because Ghidra's headless interface exposes no script argv:
#   LF2_DECOMP_TARGETS   file with one hex VA per line (# comments and blanks ignored)
#   LF2_DECOMP_OUT       output dir, default scratch/decomp
#
# It prints one line per target, INCLUDING the misses -- a VA in no function, or one the
# decompiler refuses, has to say so rather than leaving a gap in the output directory that
# reads like a file nobody looked at.
import os

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

targets_path = os.environ.get("LF2_DECOMP_TARGETS", "")
out_dir = os.environ.get("LF2_DECOMP_OUT", "scratch/decomp")

if not targets_path or not os.path.isfile(targets_path):
    print("DecompDump: LF2_DECOMP_TARGETS names no readable file (%r) -- NOTHING was "
          "decompiled" % targets_path)
else:
    vas = []
    for raw in open(targets_path):
        line = raw.split("#")[0].strip()
        if line:
            vas.append(line)

    if not vas:
        print("DecompDump: %s held no addresses, so NOTHING was decompiled" % targets_path)
    else:
        if not os.path.isdir(out_dir):
            os.makedirs(out_dir)

        space = currentProgram.getAddressFactory().getDefaultAddressSpace()
        fm = currentProgram.getFunctionManager()
        ifc = DecompInterface()
        ifc.openProgram(currentProgram)
        monitor = ConsoleTaskMonitor()

        ok = 0
        for va in vas:
            addr = space.getAddress(va)
            fn = fm.getFunctionContaining(addr)
            if fn is None:
                print("DecompDump: %s is inside NO function -- nothing written" % va)
                continue
            res = ifc.decompileFunction(fn, 120, monitor)
            if not res.decompileCompleted():
                print("DecompDump: %s (%s) FAILED to decompile: %s"
                      % (va, fn.getName(), res.getErrorMessage()))
                continue
            path = os.path.join(out_dir, "%s.c" % fn.getEntryPoint())
            f = open(path, "w")
            f.write(res.getDecompiledFunction().getC())
            f.close()
            ok += 1
            print("DecompDump: %s -> %s (%s, %d bytes)"
                  % (va, path, fn.getName(), fn.getBody().getNumAddresses()))
        print("DecompDump: %d of %d target(s) decompiled" % (ok, len(vas)))
