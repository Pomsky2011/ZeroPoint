#ifndef ZEROPOINT_APU_INSTRUCTIONS_H
#define ZEROPOINT_APU_INSTRUCTIONS_H

#include <cstdint>

class APU;  // APU is in global namespace

// ============================================================================
// APU Instruction Dispatch System
// ============================================================================
// APU uses 16-bit big-endian instructions with 5-bit opcode (0x00-0x1F)
// Format: [OOOOO PPPPPPPPPPP] where O=opcode (5 bits), P=operand (11 bits)
//
// To implement an instruction:
// 1. Find the handler function (e.g., apu_exec_0x01 for JMP)
// 2. Decode the operand as needed (11-bit value)
// 3. Implement your logic
// 4. Each instruction takes 4 cycles (handled by APU::step())
// ============================================================================

// Instruction handler function pointer type
using APUInstructionHandler = void (*)(APU* apu, uint16_t operand);

// Dispatch table: 32 instruction handlers (one per opcode 0x00-0x1F)
extern const APUInstructionHandler APU_INSTRUCTION_TABLE[32];

// ============================================================================
// Individual Instruction Handlers (0x00-0x1F)
// ============================================================================
// IMPLEMENT THESE FUNCTIONS IN apu_instructions.cpp

void apu_exec_0x00(APU* apu, uint16_t operand);  // NOP
void apu_exec_0x01(APU* apu, uint16_t operand);  // JMP
void apu_exec_0x02(APU* apu, uint16_t operand);  // JNZ
void apu_exec_0x03(APU* apu, uint16_t operand);  // SRP/SDP
void apu_exec_0x04(APU* apu, uint16_t operand);  // NOR
void apu_exec_0x05(APU* apu, uint16_t operand);  // AND
void apu_exec_0x06(APU* apu, uint16_t operand);  // ADD
void apu_exec_0x07(APU* apu, uint16_t operand);  // SUB
void apu_exec_0x08(APU* apu, uint16_t operand);  // STA/STR
void apu_exec_0x09(APU* apu, uint16_t operand);  // SBF
void apu_exec_0x0A(APU* apu, uint16_t operand);  // SCR
void apu_exec_0x0B(APU* apu, uint16_t operand);  // IOO
void apu_exec_0x0C(APU* apu, uint16_t operand);  // IOI
void apu_exec_0x0D(APU* apu, uint16_t operand);  // ZOR
void apu_exec_0x0E(APU* apu, uint16_t operand);  // ZOA
void apu_exec_0x0F(APU* apu, uint16_t operand);  // LST
void apu_exec_0x10(APU* apu, uint16_t operand);  // LFN
void apu_exec_0x11(APU* apu, uint16_t operand);  // BRT
void apu_exec_0x12(APU* apu, uint16_t operand);  // BRP
void apu_exec_0x13(APU* apu, uint16_t operand);  // IBC
void apu_exec_0x14(APU* apu, uint16_t operand);  // RBC
void apu_exec_0x15(APU* apu, uint16_t operand);  // BEQ
void apu_exec_0x16(APU* apu, uint16_t operand);  // BNE
void apu_exec_0x17(APU* apu, uint16_t operand);  // BLT
void apu_exec_0x18(APU* apu, uint16_t operand);  // BGT
void apu_exec_0x19(APU* apu, uint16_t operand);  // SDB
void apu_exec_0x1A(APU* apu, uint16_t operand);  // WRH
void apu_exec_0x1B(APU* apu, uint16_t operand);  // WRL
void apu_exec_0x1C(APU* apu, uint16_t operand);  // CFN (Callable Function New)
void apu_exec_0x1D(APU* apu, uint16_t operand);  // STACK operations
void apu_exec_0x1E(APU* apu, uint16_t operand);  // CCF
void apu_exec_0x1F(APU* apu, uint16_t operand);  // CMP variants

#endif // ZEROPOINT_APU_INSTRUCTIONS_H
