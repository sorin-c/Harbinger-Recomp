# Ghidra Jython post-script: export function list, strings xrefs and full decompilation
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor
import os
out = os.environ.get('GOUT', '/tmp')
fm = currentProgram.getFunctionManager()
with open(os.path.join(out, 'functions.tsv'), 'w') as f:
    for fn in fm.getFunctions(True):
        f.write('%s\t%s\t%d\n' % (fn.getEntryPoint(), fn.getName(True), fn.getBody().getNumAddresses()))
di = DecompInterface(); di.openProgram(currentProgram); mon = ConsoleTaskMonitor()
with open(os.path.join(out, 'decomp.c'), 'w') as f:
    for fn in fm.getFunctions(True):
        r = di.decompileFunction(fn, 60, mon)
        if r and r.decompileCompleted():
            f.write('// %s %s\n%s\n' % (fn.getEntryPoint(), fn.getName(True), r.getDecompiledFunction().getC()))
        else:
            f.write('// %s %s DECOMPILE FAILED\n' % (fn.getEntryPoint(), fn.getName(True)))
