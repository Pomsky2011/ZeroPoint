#ifndef ZEROPOINT_PPU_INSTRUCTIONS_H
#define ZEROPOINT_PPU_INSTRUCTIONS_H

#include <cstdint>

namespace ZeroPoint {

class PPU;

// ============================================================================
// PPU Instruction Dispatch System
// ============================================================================
// PPU uses 16-bit big-endian instructions with 4-bit opcode (0x0-0xF)
// Format: [OOOO PPPP PPPP PPPP] where O=opcode, P=operand (12 bits)
//
// To implement an instruction:
// 1. Find the handler function (e.g., ppu_exec_0xE for PRESET_E)
// 2. Decode the operand as needed (already extracted as uint16_t)
// 3. Implement your logic
// 4. Each instruction takes 1 cycle (implicit)
// ============================================================================

// Instruction handler function pointer type
using PPUInstructionHandler = void (*)(PPU* ppu, uint16_t operand);

// Dispatch table: 16 instruction handlers (one per opcode 0x0-0xF)
extern const PPUInstructionHandler PPU_INSTRUCTION_TABLE[16];

// ============================================================================
// Individual Instruction Handlers (0x0-0xF)
// ============================================================================
// IMPLEMENT THESE FUNCTIONS IN ppu_instructions.cpp

void ppu_exec_0x0(PPU* ppu, uint16_t operand);  // DEFCALL
void ppu_exec_0x1(PPU* ppu, uint16_t operand);  // MOVXP/NOP
void ppu_exec_0x2(PPU* ppu, uint16_t operand);  // SWAPREG
void ppu_exec_0x3(PPU* ppu, uint16_t operand);  // CLR
void ppu_exec_0x4(PPU* ppu, uint16_t operand);  // CMP
void ppu_exec_0x5(PPU* ppu, uint16_t operand);  // CLRF
void ppu_exec_0x6(PPU* ppu, uint16_t operand);  // JUMP_ZG (JMZ/JMG)
void ppu_exec_0x7(PPU* ppu, uint16_t operand);  // JUMP_NZG (JNZ/JNG/JML)
void ppu_exec_0x8(PPU* ppu, uint16_t operand);  // INC
void ppu_exec_0x9(PPU* ppu, uint16_t operand);  // DEC
void ppu_exec_0xA(PPU* ppu, uint16_t operand);  // ADD
void ppu_exec_0xB(PPU* ppu, uint16_t operand);  // SUB
void ppu_exec_0xC(PPU* ppu, uint16_t operand);  // MUL
void ppu_exec_0xD(PPU* ppu, uint16_t operand);  // INTDIV
void ppu_exec_0xE(PPU* ppu, uint16_t operand);  // PRESET_E (immediate ops)
void ppu_exec_0xF(PPU* ppu, uint16_t operand);  // PRESET_F (extended ops)

} // namespace ZeroPoint

#endif // ZEROPOINT_PPU_INSTRUCTIONS_H
