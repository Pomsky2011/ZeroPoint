#include "ppu_instructions.h"
#include "ppu.h"

namespace ZeroPoint {

// ============================================================================
// PPU INSTRUCTION IMPLEMENTATION TEMPLATE
// ============================================================================
//
// PPU Instructions: 16-bit big-endian, 4-bit opcode + 12-bit operand
// Format: [OOOO PPPP PPPP PPPP]
//
// Each handler receives:
// - ppu: Pointer to PPU instance
// - operand: 12-bit operand (bits 0-11 of instruction)
//
// Common operand patterns:
// - regX = (operand >> 6) & 0x3F;  // Upper 6 bits (register 0-63)
// - regY = operand & 0x3F;         // Lower 6 bits (register 0-63)
//
// Access PPU internals:
// - ppu->getRegister(n) / ppu->setRegister(n, value)
// - ppu->readMemory(addr) / ppu->writeMemory(addr, value)
//
// PRESET_E and PRESET_F have sub-opcodes - see docs for details
//
// ============================================================================

// ============================================================================
// Basic Instructions (0x0-0xD)
// ============================================================================

void ppu_exec_0x0(PPU* ppu, uint16_t operand) {  // DEFCALL
    // TODO: IMPLEMENT ME
    // Define callable function
    // Encoding: 0000 XXXXXX YYYYYY
    (void)ppu;
    (void)operand;
}

void ppu_exec_0x1(PPU* ppu, uint16_t operand) {  // MOVXP/NOP
    // TODO: IMPLEMENT ME
    // Encoding: 0001 D 00000 XXXXXX
    // D=0: movxp X - copy EP+2 to register X
    // D=1: nop - do nothing
    (void)ppu;
    (void)operand;
}

void ppu_exec_0x2(PPU* ppu, uint16_t operand) {  // SWAPREG
    // TODO: IMPLEMENT ME
    // swapreg X Y - swap registers X and Y
    // Encoding: 0010 XXXXXX YYYYYY
    uint8_t regX = (operand >> 6) & 0x3F;
    uint8_t regY = operand & 0x3F;

    uint16_t temp = ppu->getRegister(regX);
    ppu->setRegister(regX, ppu->getRegister(regY));
    ppu->setRegister(regY, temp);
}

void ppu_exec_0x3(PPU* ppu, uint16_t operand) {  // CLR
    // TODO: IMPLEMENT ME
    // clr X - clear register X (set to 0)
    // Encoding: 0011 000000 XXXXXX
    uint8_t regX = operand & 0x3F;
    ppu->setRegister(regX, 0);
}

void ppu_exec_0x4(PPU* ppu, uint16_t operand) {  // CMP
    // TODO: IMPLEMENT ME
    // cmp X Y - compare registers X and Y, set flags
    // Encoding: 0100 XXXXXX YYYYYY
    (void)ppu;
    (void)operand;
}

void ppu_exec_0x5(PPU* ppu, uint16_t operand) {  // CLRF
    // TODO: IMPLEMENT ME
    // clrf - clear flags (zero, greater)
    // Encoding: 0101 0000 0000 0000
    (void)ppu;
    (void)operand;
}

void ppu_exec_0x6(PPU* ppu, uint16_t operand) {  // JUMP_ZG
    // TODO: IMPLEMENT ME
    // Encoding: 0110 D AAAAAAAAAAA
    // D=0: jmz (jump if zero)
    // D=1: jmg (jump if greater)
    (void)ppu;
    (void)operand;
}

void ppu_exec_0x7(PPU* ppu, uint16_t operand) {  // JUMP_NZG
    // TODO: IMPLEMENT ME
    // Encoding: 0111 DD AAAAAAAAA
    // DD=00: jnz (jump if not zero)
    // DD=01: jng (jump if not greater)
    // DD=10: jml (jump if less)
    (void)ppu;
    (void)operand;
}

void ppu_exec_0x8(PPU* ppu, uint16_t operand) {  // INC
    // TODO: IMPLEMENT ME
    // inc X - increment register X
    // Encoding: 1000 000000 XXXXXX
    uint8_t regX = operand & 0x3F;
    ppu->setRegister(regX, ppu->getRegister(regX) + 1);
}

void ppu_exec_0x9(PPU* ppu, uint16_t operand) {  // DEC
    // TODO: IMPLEMENT ME
    // dec X - decrement register X
    // Encoding: 1001 000000 XXXXXX
    uint8_t regX = operand & 0x3F;
    ppu->setRegister(regX, ppu->getRegister(regX) - 1);
}

void ppu_exec_0xA(PPU* ppu, uint16_t operand) {  // ADD
    // TODO: IMPLEMENT ME
    // add X Y - X = X + Y
    // Encoding: 1010 XXXXXX YYYYYY
    uint8_t regX = (operand >> 6) & 0x3F;
    uint8_t regY = operand & 0x3F;

    ppu->setRegister(regX, ppu->getRegister(regX) + ppu->getRegister(regY));
}

void ppu_exec_0xB(PPU* ppu, uint16_t operand) {  // SUB
    // TODO: IMPLEMENT ME
    // sub X Y - X = X - Y
    // Encoding: 1011 XXXXXX YYYYYY
    uint8_t regX = (operand >> 6) & 0x3F;
    uint8_t regY = operand & 0x3F;

    ppu->setRegister(regX, ppu->getRegister(regX) - ppu->getRegister(regY));
}

void ppu_exec_0xC(PPU* ppu, uint16_t operand) {  // MUL
    // TODO: IMPLEMENT ME
    // mul X Y - X = X * Y
    // Encoding: 1100 XXXXXX YYYYYY
    uint8_t regX = (operand >> 6) & 0x3F;
    uint8_t regY = operand & 0x3F;

    ppu->setRegister(regX, ppu->getRegister(regX) * ppu->getRegister(regY));
}

void ppu_exec_0xD(PPU* ppu, uint16_t operand) {  // INTDIV
    // TODO: IMPLEMENT ME
    // intdiv X Y - X = X / Y (integer division)
    // Encoding: 1101 XXXXXX YYYYYY
    uint8_t regX = (operand >> 6) & 0x3F;
    uint8_t regY = operand & 0x3F;

    uint16_t divisor = ppu->getRegister(regY);
    if (divisor != 0) {
        ppu->setRegister(regX, ppu->getRegister(regX) / divisor);
    }
}

// ============================================================================
// PRESET_E: Immediate/Byte Operations (0xE)
// ============================================================================

void ppu_exec_0xE(PPU* ppu, uint16_t operand) {  // PRESET_E
    // Sub-opcode in bits 11-10 (2 bits)
    uint8_t subopcode = (operand >> 10) & 0x03;
    uint16_t suboperand = operand & 0x3FF;  // Lower 10 bits

    switch (subopcode) {
        case 0x0:  // TARREG
            // TODO: IMPLEMENT ME
            // tarreg T, Y, X - set target register T to point to reg X, byte Y
            // Encoding: 1110 00 TT Y 0 XXXXXX
            break;

        case 0x1:  // SETBYTE
            // TODO: IMPLEMENT ME
            // setbyte T, 0xXX - set byte in target register to immediate
            // Encoding: 1110 01 TT XXXXXXXX
            break;

        case 0x2:  // BUILD
            // TODO: IMPLEMENT ME
            // build T1, T2, D - build 16-bit value from target regs into D
            // Encoding: 1110 10 TT TT XXXXXX
            break;

        case 0x3:  // CPREG
            // TODO: IMPLEMENT ME
            // cpreg X, Y - copy register X to register Y (using banks)
            // Encoding: 1110 11 00 XXXX YYYY
            break;
    }

    (void)ppu;
    (void)suboperand;
}

// ============================================================================
// PRESET_F: Extended Instructions (0xF)
// ============================================================================

void ppu_exec_0xF(PPU* ppu, uint16_t operand) {  // PRESET_F
    // Sub-opcode in bits 11-8 (4 bits)
    uint8_t subopcode = (operand >> 8) & 0x0F;
    uint8_t suboperand = operand & 0xFF;  // Lower 8 bits

    switch (subopcode) {
        case 0x0:  // SETPOS
            // TODO: IMPLEMENT ME
            // setpos X Y - set position using registers
            break;

        case 0x1:  // SETTILE
            // TODO: IMPLEMENT ME
            // settile X, Y - set tile pointer and mode
            break;

        case 0x2:  // SETDP
            // TODO: IMPLEMENT ME
            // setdp X - set data pointer (R63)
            break;

        case 0x3:  // MOVDP
            // TODO: IMPLEMENT ME
            // movdp X - move from (DP) to register X
            break;

        case 0x4:  // SETRENDMOD
            // TODO: IMPLEMENT ME
            // setrendmod X - set render mode
            break;

        case 0x5:  // PALETTE16_LOAD
            // TODO: IMPLEMENT ME
            // palette16 - load 16-color palette from DP
            break;

        case 0x6:  // PALETTE256_LOAD
            // TODO: IMPLEMENT ME
            // palette256 - load 256-color palette from DP
            break;

        case 0x7:  // JMR
            // TODO: IMPLEMENT ME
            // jmr offset - jump relative to PC
            break;

        case 0x8:  // MOV
            // TODO: IMPLEMENT ME
            // mov X - move to (DP) from register X
            break;

        case 0x9:  // SETREGBANK
            // TODO: IMPLEMENT ME
            // setregbank X Y - set register banks
            break;

        case 0xA:  // CLRTILE
            // TODO: IMPLEMENT ME
            // clrtile - clear current tile
            break;

        case 0xB:  // CLRPALETTE
            // TODO: IMPLEMENT ME
            // clrpalette - clear palette
            break;

        case 0xC:  // TILEDRAW
            // TODO: IMPLEMENT ME
            // tiledraw - draw current tile at position
            break;

        case 0xD:  // RESERVED_D
            // Reserved
            break;

        case 0xE:  // CALL
            // TODO: IMPLEMENT ME
            // call addr - call function at address
            break;

        case 0xF:  // GBLS
            // TODO: IMPLEMENT ME
            // gbls X - get blank status (VBlank/HBlank) into X
            break;
    }

    (void)ppu;
    (void)suboperand;
}

// ============================================================================
// Dispatch Table
// ============================================================================

const PPUInstructionHandler PPU_INSTRUCTION_TABLE[16] = {
    ppu_exec_0x0,  // DEFCALL
    ppu_exec_0x1,  // MOVXP/NOP
    ppu_exec_0x2,  // SWAPREG
    ppu_exec_0x3,  // CLR
    ppu_exec_0x4,  // CMP
    ppu_exec_0x5,  // CLRF
    ppu_exec_0x6,  // JUMP_ZG
    ppu_exec_0x7,  // JUMP_NZG
    ppu_exec_0x8,  // INC
    ppu_exec_0x9,  // DEC
    ppu_exec_0xA,  // ADD
    ppu_exec_0xB,  // SUB
    ppu_exec_0xC,  // MUL
    ppu_exec_0xD,  // INTDIV
    ppu_exec_0xE,  // PRESET_E
    ppu_exec_0xF   // PRESET_F
};

} // namespace ZeroPoint
