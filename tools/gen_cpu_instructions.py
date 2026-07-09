#!/usr/bin/env python3
# Regenerate src/cpu_instructions.cpp from the authoritative DEF88186.csv.
# Run from the ZeroPoint repo root:  python3 tools/gen_cpu_instructions.py
import csv, re, sys

CSV = "../ZPdevtools/docs/cpu/DEF88186.csv"  # authoritative opcode spec
OUT = "src/cpu_instructions.cpp"

MODE_TO_ADDR = {
 'Immediate': 'addrImmediate',
 'Absolute': 'addrAbsolute',
 'Absolute Long': 'addrAbsoluteLong',
 'Absolute Indexed,X': 'addrAbsoluteIndexedX',
 'Absolute Indexed,Y': 'addrAbsoluteIndexedY',
 'Absolute Long Indexed,X': 'addrAbsoluteLongIndexedX',
 'Absolute Long Indexed,Y': 'addrAbsoluteLongIndexedY',
 'Direct Page': 'addrDirectPage',
 'DP Indexed,X': 'addrDirectPageIndexedX',
 'DP Indexed,Y': 'addrDirectPageIndexedY',
 'DP Indirect': 'addrDirectPageIndirect',
 'DP Indirect Long': 'addrDirectPageIndirectLong',
 'DP Indexed Indirect,X': 'addrDirectPageIndexedIndirectX',
 'DP Indirect Indexed, Y': 'addrDirectPageIndirectIndexedY',
 'DP Indirect Long Indexed, Y': 'addrDirectPageIndirectLongIndexedY',
 'Stack Relative': 'addrStackRelative',
 'SR Indirect Indexed,Y': 'addrStackRelativeIndirectIndexedY',
 'Absolute Indirect': 'addrAbsoluteIndirect',
 'Absolute Indirect Long': 'addrAbsoluteIndirectLong',
 'Absolute Indexed Indirect': 'addrAbsoluteIndexedIndirect',
}

# Mnemonics whose op method takes an effective address: op(addr).
ADDR_OPS = {'LDA','LDX','LDY','STA','STX','STY','STZ','ADC','SBC','MUL','INC',
            'DEC','AND','ORA','XOR','EOR','BIT','CMP','CPX','CPY','JMP','JSR'}
SHIFT_OPS = {'ASL','LSR','ROL','ROR'}   # op(addr, bool accumulator)

# Op methods that increment cycleCount internally (from cpu.cpp). For these the
# handler must NOT add cycles again.
SELF_COUNTING = {
 'opBCS','opBEQ','opBMI','opBRA','opBRK','opBRL','opBVS','opCALL','opCLC','opCLD',
 'opCLI','opCLV','opCOP','opDIV','opHLT','opJSR','opLOOP','opLPEND','opMUL','opMVN',
 'opMVP','opNOP','opPEA','opPEI','opPER','opPHA','opPHB','opPHD','opPHK','opPHP',
 'opPHX','opPHY','opPLA','opPOPF','opPUSH','opRCL','opREP','opRET','opRTI','opRTL',
 'opRTS','opSDB','opSEC','opSED','opSEI','opSEP','opSHL','opSHR','opTAX','opTAY',
 'opTCD','opTCS','opTDC','opTSC','opTXA','opTXY','opTYA','opTYX','opWAI',
}

def opname(base):
    return 'opXOR' if base == 'EOR' else 'op' + base

def cyc(s):
    m = re.match(r'\s*(\d+)', s or '')
    return int(m.group(1)) if m else 2

rows = list(csv.reader(open(CSV)))
handlers = {}   # op -> (comment, body_lines)
for r in rows[1:]:
    if len(r) < 9 or not r[4].strip():
        continue
    op = int(r[4], 16)
    asm = r[0].strip(); mode = r[5].strip(); cycles = cyc(r[8])
    base = re.split(r'[ (]', asm)[0].upper() if asm else 'NOP'
    on = opname(base)
    lines = []

    if base == 'DIV':
        # DIV X,Y divides the index registers; DIV long,X/long,Y divide the
        # accumulator by a memory operand (opDIVmem). Both self-count cycles.
        if mode == 'X,Y':
            lines.append('    cpu->opDIV();')
        else:
            lines.append('    cpu->opDIVmem(cpu->%s());' % MODE_TO_ADDR[mode])
    elif base in ('INC', 'DEC') and mode == 'Accumulator':
        call = 'cpu->op%sA();' % base                       # opINCA / opDECA
        lines.append('    ' + call)
        lines.append('    cpu->cycleCount += %d;' % cycles)
    elif base in SHIFT_OPS:
        if mode == 'Accumulator':
            lines.append('    cpu->%s(0, true);' % on)
        else:
            lines.append('    cpu->%s(cpu->%s(), false);' % (on, MODE_TO_ADDR[mode]))
        lines.append('    cpu->cycleCount += %d;' % cycles)   # shifts don't self-count
    elif base in ('SHL', 'SHR'):
        isY = 'true' if asm.rstrip().upper().endswith('Y') else 'false'
        lines.append('    cpu->%s(%s);' % (on, isY))          # self-counts
    elif base in ADDR_OPS:
        addr = MODE_TO_ADDR.get(mode)
        if addr is None:
            print("!! unmapped addressed mode:", hex(op), asm, mode); sys.exit(1)
        lines.append('    cpu->%s(cpu->%s());' % (on, addr))
        if on not in SELF_COUNTING:
            lines.append('    cpu->cycleCount += %d;' % cycles)
    else:
        # No-argument op (implied / stack / branch / block-move / transfer...).
        lines.append('    cpu->%s();' % on)
        if on not in SELF_COUNTING:
            lines.append('    cpu->cycleCount += %d;' % cycles)

    handlers[op] = (asm if asm else base, lines)

# Emit the file.
out = []
out.append('#include "cpu_instructions.h"')
out.append('#include "cpu.h"')
out.append('')
out.append('// ============================================================================')
out.append('// DEF88186 CPU instruction handlers.')
out.append('//')
out.append('// GENERATED from ZPdevtools/docs/cpu/DEF88186.csv (the authoritative opcode')
out.append('// spec that cpuasm and the C compilers target).  Do not hand-edit opcode')
out.append('// assignments here - change the CSV and regenerate so the emulator, the')
out.append('// assembler, and the docs stay in lock-step.  Each handler resolves the')
out.append('// addressing mode and calls the matching CPU::opXXX() method.')
out.append('// ============================================================================')
out.append('')
out.append('namespace ZeroPoint {')
out.append('')
for op in range(256):
    comment, lines = handlers[op]
    out.append('void cpu_inst_0x%02X(CPU* cpu) {  // %s' % (op, comment))
    out.extend(lines)
    out.append('}')
    out.append('')

out.append('// Dispatch table: opcode -> handler.')
out.append('const CPUInstructionHandler CPU_INSTRUCTION_TABLE[256] = {')
for base in range(0, 256, 4):
    row = ', '.join('cpu_inst_0x%02X' % (base + i) for i in range(4))
    out.append('    %s,  // 0x%02X-0x%02X' % (row, base, base + 3))
out.append('};')
out.append('')
out.append('} // namespace ZeroPoint')
out.append('')

open(OUT, 'w').write('\n'.join(out))
print("wrote", OUT, "with 256 handlers")
