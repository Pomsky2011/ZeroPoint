#ifndef ZEROPOINT_CPU_INSTRUCTIONS_H
#define ZEROPOINT_CPU_INSTRUCTIONS_H

#include <cstdint>

namespace ZeroPoint {

class CPU;

// ============================================================================
// CPU Instruction Dispatch Table
// ============================================================================
// This file defines all 256 CPU instruction handlers.
// Each instruction is implemented as a separate function for easy modification.
//
// To add/modify an instruction:
// 1. Find the instruction handler function (e.g., cpu_inst_0x42)
// 2. Implement your logic in cpu_instructions.cpp
// 3. The instruction will automatically be dispatched via the table
// ============================================================================

// Instruction handler function pointer type
// Each handler receives a pointer to the CPU instance
using CPUInstructionHandler = void (*)(CPU* cpu);

// Dispatch table: 256 instruction handlers (one per opcode 0x00-0xFF)
extern const CPUInstructionHandler CPU_INSTRUCTION_TABLE[256];

// ============================================================================
// Individual Instruction Handlers (0x00-0xFF)
// ============================================================================
// IMPLEMENT THESE FUNCTIONS IN cpu_instructions.cpp
// Each function represents one opcode and contains its complete implementation

void cpu_inst_0x00(CPU* cpu);  // NOP
void cpu_inst_0x01(CPU* cpu);  // BIT #const
void cpu_inst_0x02(CPU* cpu);  // BIT addr
void cpu_inst_0x03(CPU* cpu);  // BIT addr,X
void cpu_inst_0x04(CPU* cpu);  // BIT dp
void cpu_inst_0x05(CPU* cpu);  // BIT dp,X
void cpu_inst_0x06(CPU* cpu);  // BMI
void cpu_inst_0x07(CPU* cpu);  // BRA
void cpu_inst_0x08(CPU* cpu);  // BRL
void cpu_inst_0x09(CPU* cpu);  // BVS
void cpu_inst_0x0A(CPU* cpu);  // BCS/BGE
void cpu_inst_0x0B(CPU* cpu);  // BEQ
void cpu_inst_0x0C(CPU* cpu);  // JMP (addr,X)
void cpu_inst_0x0D(CPU* cpu);  // JMP (addr)
void cpu_inst_0x0E(CPU* cpu);  // JMP [addr]
void cpu_inst_0x0F(CPU* cpu);  // JMP addr
void cpu_inst_0x10(CPU* cpu);  // JMP long
void cpu_inst_0x11(CPU* cpu);  // JSR (addr,X)
void cpu_inst_0x12(CPU* cpu);  // JSR addr
void cpu_inst_0x13(CPU* cpu);  // LOOP
void cpu_inst_0x14(CPU* cpu);  // LPEND
void cpu_inst_0x15(CPU* cpu);  // CALL
void cpu_inst_0x16(CPU* cpu);  // RET
void cpu_inst_0x17(CPU* cpu);  // RTI
void cpu_inst_0x18(CPU* cpu);  // RTL
void cpu_inst_0x19(CPU* cpu);  // RTS
void cpu_inst_0x1A(CPU* cpu);  // SEP
void cpu_inst_0x1B(CPU* cpu);  // SDB
void cpu_inst_0x1C(CPU* cpu);  // WAI
void cpu_inst_0x1D(CPU* cpu);  // TDC
void cpu_inst_0x1E(CPU* cpu);  // TSC
void cpu_inst_0x1F(CPU* cpu);  // TCS
void cpu_inst_0x20(CPU* cpu);  // TAX
void cpu_inst_0x21(CPU* cpu);  // TXA
void cpu_inst_0x22(CPU* cpu);  // TAY
void cpu_inst_0x23(CPU* cpu);  // TCD
void cpu_inst_0x24(CPU* cpu);  // TXY
void cpu_inst_0x25(CPU* cpu);  // TYA
void cpu_inst_0x26(CPU* cpu);  // TYX
void cpu_inst_0x27(CPU* cpu);  // REP
void cpu_inst_0x28(CPU* cpu);  // CLC
void cpu_inst_0x29(CPU* cpu);  // SEC
void cpu_inst_0x2A(CPU* cpu);  // CLI
void cpu_inst_0x2B(CPU* cpu);  // SEI
void cpu_inst_0x2C(CPU* cpu);  // CLD
void cpu_inst_0x2D(CPU* cpu);  // SED
void cpu_inst_0x2E(CPU* cpu);  // CLV
void cpu_inst_0x2F(CPU* cpu);  // PHA
void cpu_inst_0x30(CPU* cpu);  // PLA
void cpu_inst_0x31(CPU* cpu);  // POPF
void cpu_inst_0x32(CPU* cpu);  // PHX
void cpu_inst_0x33(CPU* cpu);  // PHY
void cpu_inst_0x34(CPU* cpu);  // PHP
void cpu_inst_0x35(CPU* cpu);  // PHB
void cpu_inst_0x36(CPU* cpu);  // PHD
void cpu_inst_0x37(CPU* cpu);  // PHK
void cpu_inst_0x38(CPU* cpu);  // PUSH
void cpu_inst_0x39(CPU* cpu);  // PEA
void cpu_inst_0x3A(CPU* cpu);  // PEI
void cpu_inst_0x3B(CPU* cpu);  // PER
void cpu_inst_0x3C(CPU* cpu);  // BRK
void cpu_inst_0x3D(CPU* cpu);  // COP
void cpu_inst_0x3E(CPU* cpu);  // HLT
void cpu_inst_0x3F(CPU* cpu);  // Reserved
void cpu_inst_0x40(CPU* cpu);  // LDA #const
void cpu_inst_0x41(CPU* cpu);  // LDA addr
void cpu_inst_0x42(CPU* cpu);  // LDA long
void cpu_inst_0x43(CPU* cpu);  // LDA addr,X
void cpu_inst_0x44(CPU* cpu);  // LDA addr,Y
void cpu_inst_0x45(CPU* cpu);  // LDA long,X
void cpu_inst_0x46(CPU* cpu);  // LDA dp
void cpu_inst_0x47(CPU* cpu);  // LDA dp,X
void cpu_inst_0x48(CPU* cpu);  // LDA (dp)
void cpu_inst_0x49(CPU* cpu);  // LDA [dp]
void cpu_inst_0x4A(CPU* cpu);  // LDA (dp,X)
void cpu_inst_0x4B(CPU* cpu);  // LDA (dp),Y
void cpu_inst_0x4C(CPU* cpu);  // LDA [dp],Y
void cpu_inst_0x4D(CPU* cpu);  // LDA sr,S
void cpu_inst_0x4E(CPU* cpu);  // LDA (sr,S),Y
void cpu_inst_0x4F(CPU* cpu);  // Reserved
void cpu_inst_0x50(CPU* cpu);  // STA addr
void cpu_inst_0x51(CPU* cpu);  // STA long
void cpu_inst_0x52(CPU* cpu);  // STA addr,X
void cpu_inst_0x53(CPU* cpu);  // STA addr,Y
void cpu_inst_0x54(CPU* cpu);  // STA long,X
void cpu_inst_0x55(CPU* cpu);  // STA dp
void cpu_inst_0x56(CPU* cpu);  // STA dp,X
void cpu_inst_0x57(CPU* cpu);  // STA (dp)
void cpu_inst_0x58(CPU* cpu);  // STA [dp]
void cpu_inst_0x59(CPU* cpu);  // STA (dp,X)
void cpu_inst_0x5A(CPU* cpu);  // STA (dp),Y
void cpu_inst_0x5B(CPU* cpu);  // STA [dp],Y
void cpu_inst_0x5C(CPU* cpu);  // STA sr,S
void cpu_inst_0x5D(CPU* cpu);  // STA (sr,S),Y
void cpu_inst_0x5E(CPU* cpu);  // Reserved
void cpu_inst_0x5F(CPU* cpu);  // Reserved
void cpu_inst_0x60(CPU* cpu);  // LDX #const
void cpu_inst_0x61(CPU* cpu);  // LDX addr
void cpu_inst_0x62(CPU* cpu);  // LDX addr,Y
void cpu_inst_0x63(CPU* cpu);  // LDX dp
void cpu_inst_0x64(CPU* cpu);  // LDX dp,Y
void cpu_inst_0x65(CPU* cpu);  // STX addr
void cpu_inst_0x66(CPU* cpu);  // STX dp
void cpu_inst_0x67(CPU* cpu);  // STX dp,Y
void cpu_inst_0x68(CPU* cpu);  // LDY #const
void cpu_inst_0x69(CPU* cpu);  // LDY addr
void cpu_inst_0x6A(CPU* cpu);  // LDY addr,X
void cpu_inst_0x6B(CPU* cpu);  // LDY dp
void cpu_inst_0x6C(CPU* cpu);  // LDY dp,X
void cpu_inst_0x6D(CPU* cpu);  // STY addr
void cpu_inst_0x6E(CPU* cpu);  // STY dp
void cpu_inst_0x6F(CPU* cpu);  // STY dp,X
void cpu_inst_0x70(CPU* cpu);  // STZ addr
void cpu_inst_0x71(CPU* cpu);  // STZ addr,X
void cpu_inst_0x72(CPU* cpu);  // STZ dp
void cpu_inst_0x73(CPU* cpu);  // STZ dp,X
void cpu_inst_0x74(CPU* cpu);  // CMP #const
void cpu_inst_0x75(CPU* cpu);  // CMP addr
void cpu_inst_0x76(CPU* cpu);  // CMP long
void cpu_inst_0x77(CPU* cpu);  // CMP addr,X
void cpu_inst_0x78(CPU* cpu);  // CMP addr,Y
void cpu_inst_0x79(CPU* cpu);  // CMP long,X
void cpu_inst_0x7A(CPU* cpu);  // CMP dp
void cpu_inst_0x7B(CPU* cpu);  // CMP dp,X
void cpu_inst_0x7C(CPU* cpu);  // CMP (dp)
void cpu_inst_0x7D(CPU* cpu);  // CMP [dp]
void cpu_inst_0x7E(CPU* cpu);  // CMP (dp,X)
void cpu_inst_0x7F(CPU* cpu);  // CMP (dp),Y
void cpu_inst_0x80(CPU* cpu);  // CMP [dp],Y
void cpu_inst_0x81(CPU* cpu);  // CMP sr,S
void cpu_inst_0x82(CPU* cpu);  // CMP (sr,S),Y
void cpu_inst_0x83(CPU* cpu);  // CPX #const
void cpu_inst_0x84(CPU* cpu);  // CPX addr
void cpu_inst_0x85(CPU* cpu);  // CPX dp
void cpu_inst_0x86(CPU* cpu);  // CPY #const
void cpu_inst_0x87(CPU* cpu);  // CPY addr
void cpu_inst_0x88(CPU* cpu);  // CPY dp
void cpu_inst_0x89(CPU* cpu);  // INC
void cpu_inst_0x8A(CPU* cpu);  // DEC
void cpu_inst_0x8B(CPU* cpu);  // INC addr
void cpu_inst_0x8C(CPU* cpu);  // INC addr,X
void cpu_inst_0x8D(CPU* cpu);  // INC dp
void cpu_inst_0x8E(CPU* cpu);  // INC dp,X
void cpu_inst_0x8F(CPU* cpu);  // DEC addr
void cpu_inst_0x90(CPU* cpu);  // DEC addr,X
void cpu_inst_0x91(CPU* cpu);  // DEC dp
void cpu_inst_0x92(CPU* cpu);  // DEC dp,X
void cpu_inst_0x93(CPU* cpu);  // INX
void cpu_inst_0x94(CPU* cpu);  // DEX
void cpu_inst_0x95(CPU* cpu);  // INY
void cpu_inst_0x96(CPU* cpu);  // DEY
void cpu_inst_0x97(CPU* cpu);  // ADC #const
void cpu_inst_0x98(CPU* cpu);  // ADC addr
void cpu_inst_0x99(CPU* cpu);  // ADC long
void cpu_inst_0x9A(CPU* cpu);  // ADC addr,X
void cpu_inst_0x9B(CPU* cpu);  // ADC addr,Y
void cpu_inst_0x9C(CPU* cpu);  // ADC long,X
void cpu_inst_0x9D(CPU* cpu);  // ADC dp
void cpu_inst_0x9E(CPU* cpu);  // ADC dp,X
void cpu_inst_0x9F(CPU* cpu);  // ADC (dp)
void cpu_inst_0xA0(CPU* cpu);  // ADC [dp]
void cpu_inst_0xA1(CPU* cpu);  // ADC (dp,X)
void cpu_inst_0xA2(CPU* cpu);  // ADC (dp),Y
void cpu_inst_0xA3(CPU* cpu);  // ADC [dp],Y
void cpu_inst_0xA4(CPU* cpu);  // ADC sr,S
void cpu_inst_0xA5(CPU* cpu);  // ADC (sr,S),Y
void cpu_inst_0xA6(CPU* cpu);  // SBC #const
void cpu_inst_0xA7(CPU* cpu);  // SBC addr
void cpu_inst_0xA8(CPU* cpu);  // SBC long
void cpu_inst_0xA9(CPU* cpu);  // SBC addr,X
void cpu_inst_0xAA(CPU* cpu);  // SBC addr,Y
void cpu_inst_0xAB(CPU* cpu);  // SBC long,X
void cpu_inst_0xAC(CPU* cpu);  // SBC dp
void cpu_inst_0xAD(CPU* cpu);  // SBC dp,X
void cpu_inst_0xAE(CPU* cpu);  // SBC (dp)
void cpu_inst_0xAF(CPU* cpu);  // SBC [dp]
void cpu_inst_0xB0(CPU* cpu);  // SBC (dp,X)
void cpu_inst_0xB1(CPU* cpu);  // SBC (dp),Y
void cpu_inst_0xB2(CPU* cpu);  // SBC [dp],Y
void cpu_inst_0xB3(CPU* cpu);  // SBC sr,S
void cpu_inst_0xB4(CPU* cpu);  // SBC (sr,S),Y
void cpu_inst_0xB5(CPU* cpu);  // AND #const
void cpu_inst_0xB6(CPU* cpu);  // AND addr
void cpu_inst_0xB7(CPU* cpu);  // AND long
void cpu_inst_0xB8(CPU* cpu);  // AND addr,X
void cpu_inst_0xB9(CPU* cpu);  // AND addr,Y
void cpu_inst_0xBA(CPU* cpu);  // AND long,X
void cpu_inst_0xBB(CPU* cpu);  // AND dp
void cpu_inst_0xBC(CPU* cpu);  // AND dp,X
void cpu_inst_0xBD(CPU* cpu);  // AND (dp)
void cpu_inst_0xBE(CPU* cpu);  // AND [dp]
void cpu_inst_0xBF(CPU* cpu);  // AND (dp,X)
void cpu_inst_0xC0(CPU* cpu);  // AND (dp),Y
void cpu_inst_0xC1(CPU* cpu);  // AND [dp],Y
void cpu_inst_0xC2(CPU* cpu);  // AND sr,S
void cpu_inst_0xC3(CPU* cpu);  // AND (sr,S),Y
void cpu_inst_0xC4(CPU* cpu);  // ORA #const
void cpu_inst_0xC5(CPU* cpu);  // ORA addr
void cpu_inst_0xC6(CPU* cpu);  // ORA long
void cpu_inst_0xC7(CPU* cpu);  // ORA addr,X
void cpu_inst_0xC8(CPU* cpu);  // ORA addr,Y
void cpu_inst_0xC9(CPU* cpu);  // ORA long,X
void cpu_inst_0xCA(CPU* cpu);  // ORA dp
void cpu_inst_0xCB(CPU* cpu);  // ORA dp,X
void cpu_inst_0xCC(CPU* cpu);  // ORA (dp)
void cpu_inst_0xCD(CPU* cpu);  // ORA [dp]
void cpu_inst_0xCE(CPU* cpu);  // ORA (dp,X)
void cpu_inst_0xCF(CPU* cpu);  // ORA (dp),Y
void cpu_inst_0xD0(CPU* cpu);  // ORA [dp],Y
void cpu_inst_0xD1(CPU* cpu);  // ORA sr,S
void cpu_inst_0xD2(CPU* cpu);  // ORA (sr,S),Y
void cpu_inst_0xD3(CPU* cpu);  // XOR #const
void cpu_inst_0xD4(CPU* cpu);  // XOR addr
void cpu_inst_0xD5(CPU* cpu);  // XOR long
void cpu_inst_0xD6(CPU* cpu);  // XOR addr,X
void cpu_inst_0xD7(CPU* cpu);  // XOR addr,Y
void cpu_inst_0xD8(CPU* cpu);  // XOR long,X
void cpu_inst_0xD9(CPU* cpu);  // XOR dp
void cpu_inst_0xDA(CPU* cpu);  // XOR dp,X
void cpu_inst_0xDB(CPU* cpu);  // XOR (dp)
void cpu_inst_0xDC(CPU* cpu);  // XOR [dp]
void cpu_inst_0xDD(CPU* cpu);  // XOR (dp,X)
void cpu_inst_0xDE(CPU* cpu);  // XOR (dp),Y
void cpu_inst_0xDF(CPU* cpu);  // XOR [dp],Y
void cpu_inst_0xE0(CPU* cpu);  // XOR sr,S
void cpu_inst_0xE1(CPU* cpu);  // XOR (sr,S),Y
void cpu_inst_0xE2(CPU* cpu);  // ASL
void cpu_inst_0xE3(CPU* cpu);  // ASL addr
void cpu_inst_0xE4(CPU* cpu);  // ASL addr,X
void cpu_inst_0xE5(CPU* cpu);  // ASL dp
void cpu_inst_0xE6(CPU* cpu);  // ASL dp,X
void cpu_inst_0xE7(CPU* cpu);  // LSR
void cpu_inst_0xE8(CPU* cpu);  // LSR addr
void cpu_inst_0xE9(CPU* cpu);  // LSR addr,X
void cpu_inst_0xEA(CPU* cpu);  // LSR dp
void cpu_inst_0xEB(CPU* cpu);  // LSR dp,X
void cpu_inst_0xEC(CPU* cpu);  // ROL
void cpu_inst_0xED(CPU* cpu);  // ROL addr
void cpu_inst_0xEE(CPU* cpu);  // ROL addr,X
void cpu_inst_0xEF(CPU* cpu);  // ROL dp
void cpu_inst_0xF0(CPU* cpu);  // ROL dp,X
void cpu_inst_0xF1(CPU* cpu);  // ROR
void cpu_inst_0xF2(CPU* cpu);  // ROR addr
void cpu_inst_0xF3(CPU* cpu);  // ROR addr,X
void cpu_inst_0xF4(CPU* cpu);  // ROR dp
void cpu_inst_0xF5(CPU* cpu);  // ROR dp,X
void cpu_inst_0xF6(CPU* cpu);  // SHL
void cpu_inst_0xF7(CPU* cpu);  // SHLY
void cpu_inst_0xF8(CPU* cpu);  // SHR
void cpu_inst_0xF9(CPU* cpu);  // SHRY
void cpu_inst_0xFA(CPU* cpu);  // RCL
void cpu_inst_0xFB(CPU* cpu);  // MUL
void cpu_inst_0xFC(CPU* cpu);  // DIV
void cpu_inst_0xFD(CPU* cpu);  // MVN
void cpu_inst_0xFE(CPU* cpu);  // MVP
void cpu_inst_0xFF(CPU* cpu);  // Reserved

} // namespace ZeroPoint

#endif // ZEROPOINT_CPU_INSTRUCTIONS_H
