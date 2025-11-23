#include "cpu_instructions.h"
#include "cpu.h"

namespace ZeroPoint {

// ============================================================================
// CPU INSTRUCTION IMPLEMENTATION TEMPLATE
// ============================================================================
//
// Each instruction handler follows this pattern:
//
// void cpu_inst_0xXX(CPU* cpu) {
//     // 1. Fetch operands (if any) using cpu->fetch(), cpu->fetch16(), etc.
//     // 2. Calculate effective addresses using cpu->addrXXX() methods
//     // 3. Implement instruction logic
//     // 4. Update cpu->cycleCount
// }
//
// EXAMPLE:
// void cpu_inst_0x40(CPU* cpu) {  // LDA #const
//     uint32_t addr = cpu->addrImmediate();
//     cpu->opLDA(addr);
//     cpu->cycleCount += 2;
// }
//
// You can access CPU internals via the public interface or use the existing
// opXXX() methods for common operations (opLDA, opADC, etc.)
//
// ============================================================================

// ============================================================================
// Instruction Handlers (0x00-0xFF)
// ============================================================================
// TODO: Implement each instruction below.
// Many can delegate to existing CPU::opXXX() methods.

void cpu_inst_0x00(CPU* cpu) {  // NOP
    cpu->opNOP();
}

void cpu_inst_0x01(CPU* cpu) {  // BIT #const
    // TODO: IMPLEMENT ME
    // Hint: Use addrImmediate() and check A register against immediate value
    cpu->cycleCount += 2;
}

void cpu_inst_0x02(CPU* cpu) {  // BIT addr
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 3;
}

void cpu_inst_0x03(CPU* cpu) {  // BIT addr,X
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 3;
}

void cpu_inst_0x04(CPU* cpu) {  // BIT dp
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 3;
}

void cpu_inst_0x05(CPU* cpu) {  // BIT dp,X
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 4;
}

void cpu_inst_0x06(CPU* cpu) {  // BMI
    cpu->opBMI();
}

void cpu_inst_0x07(CPU* cpu) {  // BRA
    cpu->opBRA();
}

void cpu_inst_0x08(CPU* cpu) {  // BRL
    cpu->opBRL();
}

void cpu_inst_0x09(CPU* cpu) {  // BVS
    cpu->opBVS();
}

void cpu_inst_0x0A(CPU* cpu) {  // BCS/BGE
    cpu->opBCS();
}

void cpu_inst_0x0B(CPU* cpu) {  // BEQ
    cpu->opBEQ();
}

void cpu_inst_0x0C(CPU* cpu) {  // JMP (addr,X)
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 4;
}

void cpu_inst_0x0D(CPU* cpu) {  // JMP (addr)
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 4;
}

void cpu_inst_0x0E(CPU* cpu) {  // JMP [addr]
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 4;
}

void cpu_inst_0x0F(CPU* cpu) {  // JMP addr
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 4;
}

void cpu_inst_0x10(CPU* cpu) {  // JMP long
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 4;
}

void cpu_inst_0x11(CPU* cpu) {  // JSR (addr,X)
    // TODO: IMPLEMENT ME
    cpu->cycleCount += 4;
}

void cpu_inst_0x12(CPU* cpu) {  // JSR addr
    // TODO: IMPLEMENT ME
    // Hint: Use cpu->opJSR()
}

void cpu_inst_0x13(CPU* cpu) {  // LOOP
    cpu->opLOOP();
}

void cpu_inst_0x14(CPU* cpu) {  // LPEND
    cpu->opLPEND();
}

void cpu_inst_0x15(CPU* cpu) {  // CALL
    cpu->opCALL();
}

void cpu_inst_0x16(CPU* cpu) {  // RET
    cpu->opRET();
}

void cpu_inst_0x17(CPU* cpu) {  // RTI
    cpu->opRTI();
}

void cpu_inst_0x18(CPU* cpu) {  // RTL
    cpu->opRTL();
}

void cpu_inst_0x19(CPU* cpu) {  // RTS
    cpu->opRTS();
}

void cpu_inst_0x1A(CPU* cpu) {  // SEP
    cpu->opSEP();
}

void cpu_inst_0x1B(CPU* cpu) {  // SDB
    cpu->opSDB();
}

void cpu_inst_0x1C(CPU* cpu) {  // WAI
    cpu->opWAI();
}

void cpu_inst_0x1D(CPU* cpu) {  // TDC
    cpu->opTDC();
}

void cpu_inst_0x1E(CPU* cpu) {  // TSC
    cpu->opTSC();
}

void cpu_inst_0x1F(CPU* cpu) {  // TCS
    cpu->opTCS();
}

void cpu_inst_0x20(CPU* cpu) {  // TAX
    cpu->opTAX();
}

void cpu_inst_0x21(CPU* cpu) {  // TXA
    cpu->opTXA();
}

void cpu_inst_0x22(CPU* cpu) {  // TAY
    cpu->opTAY();
}

void cpu_inst_0x23(CPU* cpu) {  // TCD
    cpu->opTCD();
}

void cpu_inst_0x24(CPU* cpu) {  // TXY
    cpu->opTXY();
}

void cpu_inst_0x25(CPU* cpu) {  // TYA
    cpu->opTYA();
}

void cpu_inst_0x26(CPU* cpu) {  // TYX
    cpu->opTYX();
}

void cpu_inst_0x27(CPU* cpu) {  // REP
    cpu->opREP();
}

void cpu_inst_0x28(CPU* cpu) {  // CLC
    cpu->opCLC();
}

void cpu_inst_0x29(CPU* cpu) {  // SEC
    cpu->opSEC();
}

void cpu_inst_0x2A(CPU* cpu) {  // CLI
    cpu->opCLI();
}

void cpu_inst_0x2B(CPU* cpu) {  // SEI
    cpu->opSEI();
}

void cpu_inst_0x2C(CPU* cpu) {  // CLD
    cpu->opCLD();
}

void cpu_inst_0x2D(CPU* cpu) {  // SED
    cpu->opSED();
}

void cpu_inst_0x2E(CPU* cpu) {  // CLV
    cpu->opCLV();
}

void cpu_inst_0x2F(CPU* cpu) {  // PHA
    cpu->opPHA();
}

void cpu_inst_0x30(CPU* cpu) {  // PLA
    cpu->opPLA();
}

void cpu_inst_0x31(CPU* cpu) {  // POPF
    cpu->opPOPF();
}

void cpu_inst_0x32(CPU* cpu) {  // PHX
    cpu->opPHX();
}

void cpu_inst_0x33(CPU* cpu) {  // PHY
    cpu->opPHY();
}

void cpu_inst_0x34(CPU* cpu) {  // PHP
    cpu->opPHP();
}

void cpu_inst_0x35(CPU* cpu) {  // PHB
    cpu->opPHB();
}

void cpu_inst_0x36(CPU* cpu) {  // PHD
    cpu->opPHD();
}

void cpu_inst_0x37(CPU* cpu) {  // PHK
    cpu->opPHK();
}

void cpu_inst_0x38(CPU* cpu) {  // PUSH
    cpu->opPUSH();
}

void cpu_inst_0x39(CPU* cpu) {  // PEA
    cpu->opPEA();
}

void cpu_inst_0x3A(CPU* cpu) {  // PEI
    cpu->opPEI();
}

void cpu_inst_0x3B(CPU* cpu) {  // PER
    cpu->opPER();
}

void cpu_inst_0x3C(CPU* cpu) {  // BRK
    cpu->opBRK();
}

void cpu_inst_0x3D(CPU* cpu) {  // COP
    cpu->opCOP();
}

void cpu_inst_0x3E(CPU* cpu) {  // HLT
    cpu->opHLT();
}

void cpu_inst_0x3F(CPU* cpu) {  // Reserved
    cpu->cycleCount += 1;
}

// LDA variants (0x40-0x4F)
void cpu_inst_0x40(CPU* cpu) {  // LDA #const
    cpu->opLDA(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0x41(CPU* cpu) {  // LDA addr
    cpu->opLDA(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x42(CPU* cpu) {  // LDA long
    cpu->opLDA(cpu->addrAbsoluteLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0x43(CPU* cpu) {  // LDA addr,X
    cpu->opLDA(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x44(CPU* cpu) {  // LDA addr,Y
    cpu->opLDA(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0x45(CPU* cpu) {  // LDA long,X
    cpu->opLDA(cpu->addrAbsoluteLongIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0x46(CPU* cpu) {  // LDA dp
    cpu->opLDA(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x47(CPU* cpu) {  // LDA dp,X
    cpu->opLDA(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x48(CPU* cpu) {  // LDA (dp)
    cpu->opLDA(cpu->addrDirectPageIndirect());
    cpu->cycleCount += 4;
}

void cpu_inst_0x49(CPU* cpu) {  // LDA [dp]
    cpu->opLDA(cpu->addrDirectPageIndirectLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0x4A(CPU* cpu) {  // LDA (dp,X)
    cpu->opLDA(cpu->addrDirectPageIndexedIndirectX());
    cpu->cycleCount += 5;
}

void cpu_inst_0x4B(CPU* cpu) {  // LDA (dp),Y
    cpu->opLDA(cpu->addrDirectPageIndirectIndexedY());
    cpu->cycleCount += 4;
}

void cpu_inst_0x4C(CPU* cpu) {  // LDA [dp],Y
    cpu->opLDA(cpu->addrDirectPageIndirectLongIndexedY());
    cpu->cycleCount += 5;
}

void cpu_inst_0x4D(CPU* cpu) {  // LDA sr,S
    cpu->opLDA(cpu->addrStackRelative());
    cpu->cycleCount += 3;
}

void cpu_inst_0x4E(CPU* cpu) {  // LDA (sr,S),Y
    cpu->opLDA(cpu->addrStackRelativeIndirectIndexedY());
    cpu->cycleCount += 6;
}

void cpu_inst_0x4F(CPU* cpu) {  // Reserved
    cpu->cycleCount += 1;
}

// STA variants (0x50-0x5F)
void cpu_inst_0x50(CPU* cpu) {  // STA addr
    cpu->opSTA(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x51(CPU* cpu) {  // STA long
    cpu->opSTA(cpu->addrAbsoluteLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0x52(CPU* cpu) {  // STA addr,X
    cpu->opSTA(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x53(CPU* cpu) {  // STA addr,Y
    cpu->opSTA(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0x54(CPU* cpu) {  // STA long,X
    cpu->opSTA(cpu->addrAbsoluteLongIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0x55(CPU* cpu) {  // STA dp
    cpu->opSTA(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x56(CPU* cpu) {  // STA dp,X
    cpu->opSTA(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x57(CPU* cpu) {  // STA (dp)
    cpu->opSTA(cpu->addrDirectPageIndirect());
    cpu->cycleCount += 4;
}

void cpu_inst_0x58(CPU* cpu) {  // STA [dp]
    cpu->opSTA(cpu->addrDirectPageIndirectLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0x59(CPU* cpu) {  // STA (dp,X)
    cpu->opSTA(cpu->addrDirectPageIndexedIndirectX());
    cpu->cycleCount += 5;
}

void cpu_inst_0x5A(CPU* cpu) {  // STA (dp),Y
    cpu->opSTA(cpu->addrDirectPageIndirectIndexedY());
    cpu->cycleCount += 4;
}

void cpu_inst_0x5B(CPU* cpu) {  // STA [dp],Y
    cpu->opSTA(cpu->addrDirectPageIndirectLongIndexedY());
    cpu->cycleCount += 5;
}

void cpu_inst_0x5C(CPU* cpu) {  // STA sr,S
    cpu->opSTA(cpu->addrStackRelative());
    cpu->cycleCount += 3;
}

void cpu_inst_0x5D(CPU* cpu) {  // STA (sr,S),Y
    cpu->opSTA(cpu->addrStackRelativeIndirectIndexedY());
    cpu->cycleCount += 6;
}

void cpu_inst_0x5E(CPU* cpu) {  // Reserved
    cpu->cycleCount += 1;
}

void cpu_inst_0x5F(CPU* cpu) {  // Reserved
    cpu->cycleCount += 1;
}

// LDX/STX variants (0x60-0x67)
void cpu_inst_0x60(CPU* cpu) {  // LDX #const
    cpu->opLDX(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0x61(CPU* cpu) {  // LDX addr
    cpu->opLDX(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x62(CPU* cpu) {  // LDX addr,Y
    cpu->opLDX(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0x63(CPU* cpu) {  // LDX dp
    cpu->opLDX(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x64(CPU* cpu) {  // LDX dp,Y
    cpu->opLDX(cpu->addrDirectPageIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0x65(CPU* cpu) {  // STX addr
    cpu->opSTX(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x66(CPU* cpu) {  // STX dp
    cpu->opSTX(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x67(CPU* cpu) {  // STX dp,Y
    cpu->opSTX(cpu->addrDirectPageIndexedY());
    cpu->cycleCount += 3;
}

// LDY/STY variants (0x68-0x6F)
void cpu_inst_0x68(CPU* cpu) {  // LDY #const
    cpu->opLDY(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0x69(CPU* cpu) {  // LDY addr
    cpu->opLDY(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x6A(CPU* cpu) {  // LDY addr,X
    cpu->opLDY(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x6B(CPU* cpu) {  // LDY dp
    cpu->opLDY(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x6C(CPU* cpu) {  // LDY dp,X
    cpu->opLDY(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x6D(CPU* cpu) {  // STY addr
    cpu->opSTY(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x6E(CPU* cpu) {  // STY dp
    cpu->opSTY(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x6F(CPU* cpu) {  // STY dp,X
    cpu->opSTY(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

// STZ variants (0x70-0x73)
void cpu_inst_0x70(CPU* cpu) {  // STZ addr
    cpu->opSTZ(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x71(CPU* cpu) {  // STZ addr,X
    cpu->opSTZ(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x72(CPU* cpu) {  // STZ dp
    cpu->opSTZ(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x73(CPU* cpu) {  // STZ dp,X
    cpu->opSTZ(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

// CMP variants (0x74-0x82)
void cpu_inst_0x74(CPU* cpu) {  // CMP #const
    cpu->opCMP(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0x75(CPU* cpu) {  // CMP addr
    cpu->opCMP(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x76(CPU* cpu) {  // CMP long
    cpu->opCMP(cpu->addrAbsoluteLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0x77(CPU* cpu) {  // CMP addr,X
    cpu->opCMP(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x78(CPU* cpu) {  // CMP addr,Y
    cpu->opCMP(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0x79(CPU* cpu) {  // CMP long,X
    cpu->opCMP(cpu->addrAbsoluteLongIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0x7A(CPU* cpu) {  // CMP dp
    cpu->opCMP(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x7B(CPU* cpu) {  // CMP dp,X
    cpu->opCMP(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x7C(CPU* cpu) {  // CMP (dp)
    cpu->opCMP(cpu->addrDirectPageIndirect());
    cpu->cycleCount += 4;
}

void cpu_inst_0x7D(CPU* cpu) {  // CMP [dp]
    cpu->opCMP(cpu->addrDirectPageIndirectLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0x7E(CPU* cpu) {  // CMP (dp,X)
    cpu->opCMP(cpu->addrDirectPageIndexedIndirectX());
    cpu->cycleCount += 5;
}

void cpu_inst_0x7F(CPU* cpu) {  // CMP (dp),Y
    cpu->opCMP(cpu->addrDirectPageIndirectIndexedY());
    cpu->cycleCount += 4;
}

void cpu_inst_0x80(CPU* cpu) {  // CMP [dp],Y
    cpu->opCMP(cpu->addrDirectPageIndirectLongIndexedY());
    cpu->cycleCount += 5;
}

void cpu_inst_0x81(CPU* cpu) {  // CMP sr,S
    cpu->opCMP(cpu->addrStackRelative());
    cpu->cycleCount += 3;
}

void cpu_inst_0x82(CPU* cpu) {  // CMP (sr,S),Y
    cpu->opCMP(cpu->addrStackRelativeIndirectIndexedY());
    cpu->cycleCount += 6;
}

// CPX/CPY variants (0x83-0x88)
void cpu_inst_0x83(CPU* cpu) {  // CPX #const
    cpu->opCPX(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0x84(CPU* cpu) {  // CPX addr
    cpu->opCPX(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x85(CPU* cpu) {  // CPX dp
    cpu->opCPX(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x86(CPU* cpu) {  // CPY #const
    cpu->opCPY(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0x87(CPU* cpu) {  // CPY addr
    cpu->opCPY(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x88(CPU* cpu) {  // CPY dp
    cpu->opCPY(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

// INC/DEC variants (0x89-0x96)
void cpu_inst_0x89(CPU* cpu) {  // INC
    cpu->opINC(0);  // Accumulator mode
    cpu->cycleCount += 2;
}

void cpu_inst_0x8A(CPU* cpu) {  // DEC
    cpu->opDEC(0);  // Accumulator mode
    cpu->cycleCount += 2;
}

void cpu_inst_0x8B(CPU* cpu) {  // INC addr
    cpu->opINC(cpu->addrAbsolute());
    cpu->cycleCount += 4;
}

void cpu_inst_0x8C(CPU* cpu) {  // INC addr,X
    cpu->opINC(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0x8D(CPU* cpu) {  // INC dp
    cpu->opINC(cpu->addrDirectPage());
    cpu->cycleCount += 3;
}

void cpu_inst_0x8E(CPU* cpu) {  // INC dp,X
    cpu->opINC(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0x8F(CPU* cpu) {  // DEC addr
    cpu->opDEC(cpu->addrAbsolute());
    cpu->cycleCount += 4;
}

void cpu_inst_0x90(CPU* cpu) {  // DEC addr,X
    cpu->opDEC(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0x91(CPU* cpu) {  // DEC dp
    cpu->opDEC(cpu->addrDirectPage());
    cpu->cycleCount += 3;
}

void cpu_inst_0x92(CPU* cpu) {  // DEC dp,X
    cpu->opDEC(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0x93(CPU* cpu) {  // INX
    cpu->opINX();
}

void cpu_inst_0x94(CPU* cpu) {  // DEX
    cpu->opDEX();
}

void cpu_inst_0x95(CPU* cpu) {  // INY
    cpu->opINY();
}

void cpu_inst_0x96(CPU* cpu) {  // DEY
    cpu->opDEY();
}

// ADC variants (0x97-0xA5)
void cpu_inst_0x97(CPU* cpu) {  // ADC #const
    cpu->opADC(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0x98(CPU* cpu) {  // ADC addr
    cpu->opADC(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0x99(CPU* cpu) {  // ADC long
    cpu->opADC(cpu->addrAbsoluteLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0x9A(CPU* cpu) {  // ADC addr,X
    cpu->opADC(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x9B(CPU* cpu) {  // ADC addr,Y
    cpu->opADC(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0x9C(CPU* cpu) {  // ADC long,X
    cpu->opADC(cpu->addrAbsoluteLongIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0x9D(CPU* cpu) {  // ADC dp
    cpu->opADC(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0x9E(CPU* cpu) {  // ADC dp,X
    cpu->opADC(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0x9F(CPU* cpu) {  // ADC (dp)
    cpu->opADC(cpu->addrDirectPageIndirect());
    cpu->cycleCount += 4;
}

void cpu_inst_0xA0(CPU* cpu) {  // ADC [dp]
    cpu->opADC(cpu->addrDirectPageIndirectLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xA1(CPU* cpu) {  // ADC (dp,X)
    cpu->opADC(cpu->addrDirectPageIndexedIndirectX());
    cpu->cycleCount += 5;
}

void cpu_inst_0xA2(CPU* cpu) {  // ADC (dp),Y
    cpu->opADC(cpu->addrDirectPageIndirectIndexedY());
    cpu->cycleCount += 4;
}

void cpu_inst_0xA3(CPU* cpu) {  // ADC [dp],Y
    cpu->opADC(cpu->addrDirectPageIndirectLongIndexedY());
    cpu->cycleCount += 5;
}

void cpu_inst_0xA4(CPU* cpu) {  // ADC sr,S
    cpu->opADC(cpu->addrStackRelative());
    cpu->cycleCount += 3;
}

void cpu_inst_0xA5(CPU* cpu) {  // ADC (sr,S),Y
    cpu->opADC(cpu->addrStackRelativeIndirectIndexedY());
    cpu->cycleCount += 6;
}

// SBC variants (0xA6-0xB4)
void cpu_inst_0xA6(CPU* cpu) {  // SBC #const
    cpu->opSBC(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0xA7(CPU* cpu) {  // SBC addr
    cpu->opSBC(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0xA8(CPU* cpu) {  // SBC long
    cpu->opSBC(cpu->addrAbsoluteLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xA9(CPU* cpu) {  // SBC addr,X
    cpu->opSBC(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0xAA(CPU* cpu) {  // SBC addr,Y
    cpu->opSBC(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0xAB(CPU* cpu) {  // SBC long,X
    cpu->opSBC(cpu->addrAbsoluteLongIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0xAC(CPU* cpu) {  // SBC dp
    cpu->opSBC(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0xAD(CPU* cpu) {  // SBC dp,X
    cpu->opSBC(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0xAE(CPU* cpu) {  // SBC (dp)
    cpu->opSBC(cpu->addrDirectPageIndirect());
    cpu->cycleCount += 4;
}

void cpu_inst_0xAF(CPU* cpu) {  // SBC [dp]
    cpu->opSBC(cpu->addrDirectPageIndirectLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xB0(CPU* cpu) {  // SBC (dp,X)
    cpu->opSBC(cpu->addrDirectPageIndexedIndirectX());
    cpu->cycleCount += 5;
}

void cpu_inst_0xB1(CPU* cpu) {  // SBC (dp),Y
    cpu->opSBC(cpu->addrDirectPageIndirectIndexedY());
    cpu->cycleCount += 4;
}

void cpu_inst_0xB2(CPU* cpu) {  // SBC [dp],Y
    cpu->opSBC(cpu->addrDirectPageIndirectLongIndexedY());
    cpu->cycleCount += 5;
}

void cpu_inst_0xB3(CPU* cpu) {  // SBC sr,S
    cpu->opSBC(cpu->addrStackRelative());
    cpu->cycleCount += 3;
}

void cpu_inst_0xB4(CPU* cpu) {  // SBC (sr,S),Y
    cpu->opSBC(cpu->addrStackRelativeIndirectIndexedY());
    cpu->cycleCount += 6;
}

// AND variants (0xB5-0xC3)
void cpu_inst_0xB5(CPU* cpu) {  // AND #const
    cpu->opAND(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0xB6(CPU* cpu) {  // AND addr
    cpu->opAND(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0xB7(CPU* cpu) {  // AND long
    cpu->opAND(cpu->addrAbsoluteLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xB8(CPU* cpu) {  // AND addr,X
    cpu->opAND(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0xB9(CPU* cpu) {  // AND addr,Y
    cpu->opAND(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0xBA(CPU* cpu) {  // AND long,X
    cpu->opAND(cpu->addrAbsoluteLongIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0xBB(CPU* cpu) {  // AND dp
    cpu->opAND(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0xBC(CPU* cpu) {  // AND dp,X
    cpu->opAND(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0xBD(CPU* cpu) {  // AND (dp)
    cpu->opAND(cpu->addrDirectPageIndirect());
    cpu->cycleCount += 4;
}

void cpu_inst_0xBE(CPU* cpu) {  // AND [dp]
    cpu->opAND(cpu->addrDirectPageIndirectLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xBF(CPU* cpu) {  // AND (dp,X)
    cpu->opAND(cpu->addrDirectPageIndexedIndirectX());
    cpu->cycleCount += 5;
}

void cpu_inst_0xC0(CPU* cpu) {  // AND (dp),Y
    cpu->opAND(cpu->addrDirectPageIndirectIndexedY());
    cpu->cycleCount += 4;
}

void cpu_inst_0xC1(CPU* cpu) {  // AND [dp],Y
    cpu->opAND(cpu->addrDirectPageIndirectLongIndexedY());
    cpu->cycleCount += 5;
}

void cpu_inst_0xC2(CPU* cpu) {  // AND sr,S
    cpu->opAND(cpu->addrStackRelative());
    cpu->cycleCount += 3;
}

void cpu_inst_0xC3(CPU* cpu) {  // AND (sr,S),Y
    cpu->opAND(cpu->addrStackRelativeIndirectIndexedY());
    cpu->cycleCount += 6;
}

// ORA variants (0xC4-0xD2)
void cpu_inst_0xC4(CPU* cpu) {  // ORA #const
    cpu->opORA(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0xC5(CPU* cpu) {  // ORA addr
    cpu->opORA(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0xC6(CPU* cpu) {  // ORA long
    cpu->opORA(cpu->addrAbsoluteLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xC7(CPU* cpu) {  // ORA addr,X
    cpu->opORA(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0xC8(CPU* cpu) {  // ORA addr,Y
    cpu->opORA(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0xC9(CPU* cpu) {  // ORA long,X
    cpu->opORA(cpu->addrAbsoluteLongIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0xCA(CPU* cpu) {  // ORA dp
    cpu->opORA(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0xCB(CPU* cpu) {  // ORA dp,X
    cpu->opORA(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0xCC(CPU* cpu) {  // ORA (dp)
    cpu->opORA(cpu->addrDirectPageIndirect());
    cpu->cycleCount += 4;
}

void cpu_inst_0xCD(CPU* cpu) {  // ORA [dp]
    cpu->opORA(cpu->addrDirectPageIndirectLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xCE(CPU* cpu) {  // ORA (dp,X)
    cpu->opORA(cpu->addrDirectPageIndexedIndirectX());
    cpu->cycleCount += 5;
}

void cpu_inst_0xCF(CPU* cpu) {  // ORA (dp),Y
    cpu->opORA(cpu->addrDirectPageIndirectIndexedY());
    cpu->cycleCount += 4;
}

void cpu_inst_0xD0(CPU* cpu) {  // ORA [dp],Y
    cpu->opORA(cpu->addrDirectPageIndirectLongIndexedY());
    cpu->cycleCount += 5;
}

void cpu_inst_0xD1(CPU* cpu) {  // ORA sr,S
    cpu->opORA(cpu->addrStackRelative());
    cpu->cycleCount += 3;
}

void cpu_inst_0xD2(CPU* cpu) {  // ORA (sr,S),Y
    cpu->opORA(cpu->addrStackRelativeIndirectIndexedY());
    cpu->cycleCount += 6;
}

// XOR variants (0xD3-0xE1)
void cpu_inst_0xD3(CPU* cpu) {  // XOR #const
    cpu->opXOR(cpu->addrImmediate());
    cpu->cycleCount += 2;
}

void cpu_inst_0xD4(CPU* cpu) {  // XOR addr
    cpu->opXOR(cpu->addrAbsolute());
    cpu->cycleCount += 3;
}

void cpu_inst_0xD5(CPU* cpu) {  // XOR long
    cpu->opXOR(cpu->addrAbsoluteLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xD6(CPU* cpu) {  // XOR addr,X
    cpu->opXOR(cpu->addrAbsoluteIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0xD7(CPU* cpu) {  // XOR addr,Y
    cpu->opXOR(cpu->addrAbsoluteIndexedY());
    cpu->cycleCount += 3;
}

void cpu_inst_0xD8(CPU* cpu) {  // XOR long,X
    cpu->opXOR(cpu->addrAbsoluteLongIndexedX());
    cpu->cycleCount += 4;
}

void cpu_inst_0xD9(CPU* cpu) {  // XOR dp
    cpu->opXOR(cpu->addrDirectPage());
    cpu->cycleCount += 2;
}

void cpu_inst_0xDA(CPU* cpu) {  // XOR dp,X
    cpu->opXOR(cpu->addrDirectPageIndexedX());
    cpu->cycleCount += 3;
}

void cpu_inst_0xDB(CPU* cpu) {  // XOR (dp)
    cpu->opXOR(cpu->addrDirectPageIndirect());
    cpu->cycleCount += 4;
}

void cpu_inst_0xDC(CPU* cpu) {  // XOR [dp]
    cpu->opXOR(cpu->addrDirectPageIndirectLong());
    cpu->cycleCount += 4;
}

void cpu_inst_0xDD(CPU* cpu) {  // XOR (dp,X)
    cpu->opXOR(cpu->addrDirectPageIndexedIndirectX());
    cpu->cycleCount += 5;
}

void cpu_inst_0xDE(CPU* cpu) {  // XOR (dp),Y
    cpu->opXOR(cpu->addrDirectPageIndirectIndexedY());
    cpu->cycleCount += 4;
}

void cpu_inst_0xDF(CPU* cpu) {  // XOR [dp],Y
    cpu->opXOR(cpu->addrDirectPageIndirectLongIndexedY());
    cpu->cycleCount += 5;
}

void cpu_inst_0xE0(CPU* cpu) {  // XOR sr,S
    cpu->opXOR(cpu->addrStackRelative());
    cpu->cycleCount += 3;
}

void cpu_inst_0xE1(CPU* cpu) {  // XOR (sr,S),Y
    cpu->opXOR(cpu->addrStackRelativeIndirectIndexedY());
    cpu->cycleCount += 6;
}

// Shift/Rotate operations (0xE2-0xFA)
void cpu_inst_0xE2(CPU* cpu) {  // ASL
    cpu->opASL(0, true);  // Accumulator mode
    cpu->cycleCount += 2;
}

void cpu_inst_0xE3(CPU* cpu) {  // ASL addr
    cpu->opASL(cpu->addrAbsolute(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xE4(CPU* cpu) {  // ASL addr,X
    cpu->opASL(cpu->addrAbsoluteIndexedX(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xE5(CPU* cpu) {  // ASL dp
    cpu->opASL(cpu->addrDirectPage(), false);
    cpu->cycleCount += 3;
}

void cpu_inst_0xE6(CPU* cpu) {  // ASL dp,X
    cpu->opASL(cpu->addrDirectPageIndexedX(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xE7(CPU* cpu) {  // LSR
    cpu->opLSR(0, true);  // Accumulator mode
    cpu->cycleCount += 2;
}

void cpu_inst_0xE8(CPU* cpu) {  // LSR addr
    cpu->opLSR(cpu->addrAbsolute(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xE9(CPU* cpu) {  // LSR addr,X
    cpu->opLSR(cpu->addrAbsoluteIndexedX(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xEA(CPU* cpu) {  // LSR dp
    cpu->opLSR(cpu->addrDirectPage(), false);
    cpu->cycleCount += 3;
}

void cpu_inst_0xEB(CPU* cpu) {  // LSR dp,X
    cpu->opLSR(cpu->addrDirectPageIndexedX(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xEC(CPU* cpu) {  // ROL
    cpu->opROL(0, true);  // Accumulator mode
    cpu->cycleCount += 2;
}

void cpu_inst_0xED(CPU* cpu) {  // ROL addr
    cpu->opROL(cpu->addrAbsolute(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xEE(CPU* cpu) {  // ROL addr,X
    cpu->opROL(cpu->addrAbsoluteIndexedX(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xEF(CPU* cpu) {  // ROL dp
    cpu->opROL(cpu->addrDirectPage(), false);
    cpu->cycleCount += 3;
}

void cpu_inst_0xF0(CPU* cpu) {  // ROL dp,X
    cpu->opROL(cpu->addrDirectPageIndexedX(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xF1(CPU* cpu) {  // ROR
    cpu->opROR(0, true);  // Accumulator mode
    cpu->cycleCount += 2;
}

void cpu_inst_0xF2(CPU* cpu) {  // ROR addr
    cpu->opROR(cpu->addrAbsolute(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xF3(CPU* cpu) {  // ROR addr,X
    cpu->opROR(cpu->addrAbsoluteIndexedX(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xF4(CPU* cpu) {  // ROR dp
    cpu->opROR(cpu->addrDirectPage(), false);
    cpu->cycleCount += 3;
}

void cpu_inst_0xF5(CPU* cpu) {  // ROR dp,X
    cpu->opROR(cpu->addrDirectPageIndexedX(), false);
    cpu->cycleCount += 4;
}

void cpu_inst_0xF6(CPU* cpu) {  // SHL
    cpu->opSHL(false);  // X register
    cpu->cycleCount += 1;
}

void cpu_inst_0xF7(CPU* cpu) {  // SHLY
    cpu->opSHL(true);  // Y register
    cpu->cycleCount += 1;
}

void cpu_inst_0xF8(CPU* cpu) {  // SHR
    cpu->opSHR(false);  // X register
    cpu->cycleCount += 1;
}

void cpu_inst_0xF9(CPU* cpu) {  // SHRY
    cpu->opSHR(true);  // Y register
    cpu->cycleCount += 1;
}

void cpu_inst_0xFA(CPU* cpu) {  // RCL
    cpu->opRCL();
}

// Multiply/Divide/Block Move (0xFB-0xFF)
void cpu_inst_0xFB(CPU* cpu) {  // MUL
    cpu->opMUL(cpu->addrImmediate());
    cpu->cycleCount += 8;
}

void cpu_inst_0xFC(CPU* cpu) {  // DIV
    cpu->opDIV();
    cpu->cycleCount += 12;
}

void cpu_inst_0xFD(CPU* cpu) {  // MVN
    cpu->opMVN();
}

void cpu_inst_0xFE(CPU* cpu) {  // MVP
    cpu->opMVP();
}

void cpu_inst_0xFF(CPU* cpu) {  // Reserved
    cpu->cycleCount += 1;
}

// ============================================================================
// Dispatch Table - Maps opcode to handler function
// ============================================================================

const CPUInstructionHandler CPU_INSTRUCTION_TABLE[256] = {
    cpu_inst_0x00, cpu_inst_0x01, cpu_inst_0x02, cpu_inst_0x03,  // 0x00-0x03
    cpu_inst_0x04, cpu_inst_0x05, cpu_inst_0x06, cpu_inst_0x07,  // 0x04-0x07
    cpu_inst_0x08, cpu_inst_0x09, cpu_inst_0x0A, cpu_inst_0x0B,  // 0x08-0x0B
    cpu_inst_0x0C, cpu_inst_0x0D, cpu_inst_0x0E, cpu_inst_0x0F,  // 0x0C-0x0F
    cpu_inst_0x10, cpu_inst_0x11, cpu_inst_0x12, cpu_inst_0x13,  // 0x10-0x13
    cpu_inst_0x14, cpu_inst_0x15, cpu_inst_0x16, cpu_inst_0x17,  // 0x14-0x17
    cpu_inst_0x18, cpu_inst_0x19, cpu_inst_0x1A, cpu_inst_0x1B,  // 0x18-0x1B
    cpu_inst_0x1C, cpu_inst_0x1D, cpu_inst_0x1E, cpu_inst_0x1F,  // 0x1C-0x1F
    cpu_inst_0x20, cpu_inst_0x21, cpu_inst_0x22, cpu_inst_0x23,  // 0x20-0x23
    cpu_inst_0x24, cpu_inst_0x25, cpu_inst_0x26, cpu_inst_0x27,  // 0x24-0x27
    cpu_inst_0x28, cpu_inst_0x29, cpu_inst_0x2A, cpu_inst_0x2B,  // 0x28-0x2B
    cpu_inst_0x2C, cpu_inst_0x2D, cpu_inst_0x2E, cpu_inst_0x2F,  // 0x2C-0x2F
    cpu_inst_0x30, cpu_inst_0x31, cpu_inst_0x32, cpu_inst_0x33,  // 0x30-0x33
    cpu_inst_0x34, cpu_inst_0x35, cpu_inst_0x36, cpu_inst_0x37,  // 0x34-0x37
    cpu_inst_0x38, cpu_inst_0x39, cpu_inst_0x3A, cpu_inst_0x3B,  // 0x38-0x3B
    cpu_inst_0x3C, cpu_inst_0x3D, cpu_inst_0x3E, cpu_inst_0x3F,  // 0x3C-0x3F
    cpu_inst_0x40, cpu_inst_0x41, cpu_inst_0x42, cpu_inst_0x43,  // 0x40-0x43
    cpu_inst_0x44, cpu_inst_0x45, cpu_inst_0x46, cpu_inst_0x47,  // 0x44-0x47
    cpu_inst_0x48, cpu_inst_0x49, cpu_inst_0x4A, cpu_inst_0x4B,  // 0x48-0x4B
    cpu_inst_0x4C, cpu_inst_0x4D, cpu_inst_0x4E, cpu_inst_0x4F,  // 0x4C-0x4F
    cpu_inst_0x50, cpu_inst_0x51, cpu_inst_0x52, cpu_inst_0x53,  // 0x50-0x53
    cpu_inst_0x54, cpu_inst_0x55, cpu_inst_0x56, cpu_inst_0x57,  // 0x54-0x57
    cpu_inst_0x58, cpu_inst_0x59, cpu_inst_0x5A, cpu_inst_0x5B,  // 0x58-0x5B
    cpu_inst_0x5C, cpu_inst_0x5D, cpu_inst_0x5E, cpu_inst_0x5F,  // 0x5C-0x5F
    cpu_inst_0x60, cpu_inst_0x61, cpu_inst_0x62, cpu_inst_0x63,  // 0x60-0x63
    cpu_inst_0x64, cpu_inst_0x65, cpu_inst_0x66, cpu_inst_0x67,  // 0x64-0x67
    cpu_inst_0x68, cpu_inst_0x69, cpu_inst_0x6A, cpu_inst_0x6B,  // 0x68-0x6B
    cpu_inst_0x6C, cpu_inst_0x6D, cpu_inst_0x6E, cpu_inst_0x6F,  // 0x6C-0x6F
    cpu_inst_0x70, cpu_inst_0x71, cpu_inst_0x72, cpu_inst_0x73,  // 0x70-0x73
    cpu_inst_0x74, cpu_inst_0x75, cpu_inst_0x76, cpu_inst_0x77,  // 0x74-0x77
    cpu_inst_0x78, cpu_inst_0x79, cpu_inst_0x7A, cpu_inst_0x7B,  // 0x78-0x7B
    cpu_inst_0x7C, cpu_inst_0x7D, cpu_inst_0x7E, cpu_inst_0x7F,  // 0x7C-0x7F
    cpu_inst_0x80, cpu_inst_0x81, cpu_inst_0x82, cpu_inst_0x83,  // 0x80-0x83
    cpu_inst_0x84, cpu_inst_0x85, cpu_inst_0x86, cpu_inst_0x87,  // 0x84-0x87
    cpu_inst_0x88, cpu_inst_0x89, cpu_inst_0x8A, cpu_inst_0x8B,  // 0x88-0x8B
    cpu_inst_0x8C, cpu_inst_0x8D, cpu_inst_0x8E, cpu_inst_0x8F,  // 0x8C-0x8F
    cpu_inst_0x90, cpu_inst_0x91, cpu_inst_0x92, cpu_inst_0x93,  // 0x90-0x93
    cpu_inst_0x94, cpu_inst_0x95, cpu_inst_0x96, cpu_inst_0x97,  // 0x94-0x97
    cpu_inst_0x98, cpu_inst_0x99, cpu_inst_0x9A, cpu_inst_0x9B,  // 0x98-0x9B
    cpu_inst_0x9C, cpu_inst_0x9D, cpu_inst_0x9E, cpu_inst_0x9F,  // 0x9C-0x9F
    cpu_inst_0xA0, cpu_inst_0xA1, cpu_inst_0xA2, cpu_inst_0xA3,  // 0xA0-0xA3
    cpu_inst_0xA4, cpu_inst_0xA5, cpu_inst_0xA6, cpu_inst_0xA7,  // 0xA4-0xA7
    cpu_inst_0xA8, cpu_inst_0xA9, cpu_inst_0xAA, cpu_inst_0xAB,  // 0xA8-0xAB
    cpu_inst_0xAC, cpu_inst_0xAD, cpu_inst_0xAE, cpu_inst_0xAF,  // 0xAC-0xAF
    cpu_inst_0xB0, cpu_inst_0xB1, cpu_inst_0xB2, cpu_inst_0xB3,  // 0xB0-0xB3
    cpu_inst_0xB4, cpu_inst_0xB5, cpu_inst_0xB6, cpu_inst_0xB7,  // 0xB4-0xB7
    cpu_inst_0xB8, cpu_inst_0xB9, cpu_inst_0xBA, cpu_inst_0xBB,  // 0xB8-0xBB
    cpu_inst_0xBC, cpu_inst_0xBD, cpu_inst_0xBE, cpu_inst_0xBF,  // 0xBC-0xBF
    cpu_inst_0xC0, cpu_inst_0xC1, cpu_inst_0xC2, cpu_inst_0xC3,  // 0xC0-0xC3
    cpu_inst_0xC4, cpu_inst_0xC5, cpu_inst_0xC6, cpu_inst_0xC7,  // 0xC4-0xC7
    cpu_inst_0xC8, cpu_inst_0xC9, cpu_inst_0xCA, cpu_inst_0xCB,  // 0xC8-0xCB
    cpu_inst_0xCC, cpu_inst_0xCD, cpu_inst_0xCE, cpu_inst_0xCF,  // 0xCC-0xCF
    cpu_inst_0xD0, cpu_inst_0xD1, cpu_inst_0xD2, cpu_inst_0xD3,  // 0xD0-0xD3
    cpu_inst_0xD4, cpu_inst_0xD5, cpu_inst_0xD6, cpu_inst_0xD7,  // 0xD4-0xD7
    cpu_inst_0xD8, cpu_inst_0xD9, cpu_inst_0xDA, cpu_inst_0xDB,  // 0xD8-0xDB
    cpu_inst_0xDC, cpu_inst_0xDD, cpu_inst_0xDE, cpu_inst_0xDF,  // 0xDC-0xDF
    cpu_inst_0xE0, cpu_inst_0xE1, cpu_inst_0xE2, cpu_inst_0xE3,  // 0xE0-0xE3
    cpu_inst_0xE4, cpu_inst_0xE5, cpu_inst_0xE6, cpu_inst_0xE7,  // 0xE4-0xE7
    cpu_inst_0xE8, cpu_inst_0xE9, cpu_inst_0xEA, cpu_inst_0xEB,  // 0xE8-0xEB
    cpu_inst_0xEC, cpu_inst_0xED, cpu_inst_0xEE, cpu_inst_0xEF,  // 0xEC-0xEF
    cpu_inst_0xF0, cpu_inst_0xF1, cpu_inst_0xF2, cpu_inst_0xF3,  // 0xF0-0xF3
    cpu_inst_0xF4, cpu_inst_0xF5, cpu_inst_0xF6, cpu_inst_0xF7,  // 0xF4-0xF7
    cpu_inst_0xF8, cpu_inst_0xF9, cpu_inst_0xFA, cpu_inst_0xFB,  // 0xF8-0xFB
    cpu_inst_0xFC, cpu_inst_0xFD, cpu_inst_0xFE, cpu_inst_0xFF   // 0xFC-0xFF
};

} // namespace ZeroPoint
