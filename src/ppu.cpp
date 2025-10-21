#include "ppu.h"
#include "display.h"
#include <cstring>
#include <iostream>

namespace ZeroPoint {

PPU::PPU()
    : executionPointer(0)
    , state(PPUState::WaitingForStart)
    , vblank(false)
    , hblank(false)
    , regBankX(0)
    , regBankY(0)
    , cycleCounter(0)
    , display(nullptr)
    , currentTileId(0)
    , currentTileMode(0)
{
    reset();
}

PPU::~PPU() {
}

void PPU::reset() {
    // Clear all registers (including R59/R60 interrupt addresses)
    registers.fill(0);

    // Clear target registers
    targetRegisters.fill(0);
    targetBytes.fill(0);

    // Clear memory
    memory.fill(0);

    // Reset state
    executionPointer = 0;
    state = PPUState::WaitingForStart;
    flags = PPUFlags();
    cycleCounter = 0;

    vblank = false;
    hblank = false;
    regBankX = 0;
    regBankY = 0;

    // Reset tile system
    for (auto& tile : tileStorage) {
        tile.fill(0);
    }
    currentTileId = 0;
    currentTileMode = 0;
}

void PPU::loadMicrocode(const uint8_t* code, size_t length, uint16_t offset) {
    size_t copyLength = (length + offset > 65536) ? (65536 - offset) : length;
    std::memcpy(&memory[offset], code, copyLength);
}

void PPU::start() {
    if (state == PPUState::WaitingForStart) {
        state = PPUState::Running;
        executionPointer = 0;
    }
}

void PPU::tick() {
    if (state != PPUState::Running) {
        return;
    }

    // Check for VBlank interrupt (R59)
    if (vblank && registers[REG_VBLANK_INT] != 0) {
        // Push return address onto stack (little-endian)
        // Stack grows downward, so decrement BEFORE writing
        uint16_t returnAddr = executionPointer;
        registers[REG_SP] -= 2;
        memory[registers[REG_SP]] = returnAddr & 0xFF;        // Low byte
        memory[registers[REG_SP] + 1] = (returnAddr >> 8) & 0xFF;  // High byte

        // Jump to interrupt handler
        executionPointer = registers[REG_VBLANK_INT];
        return;
    }

    // Check for HBlank interrupt (R60)
    if (hblank && registers[REG_HBLANK_INT] != 0) {
        // Push return address onto stack (little-endian)
        // Stack grows downward, so decrement BEFORE writing
        uint16_t returnAddr = executionPointer;
        registers[REG_SP] -= 2;
        memory[registers[REG_SP]] = returnAddr & 0xFF;        // Low byte
        memory[registers[REG_SP] + 1] = (returnAddr >> 8) & 0xFF;  // High byte

        // Jump to interrupt handler
        executionPointer = registers[REG_HBLANK_INT];
        return;
    }

    // Execute 1 instruction per cycle
    executeInstruction();
}

uint16_t PPU::fetchInstruction() {
    uint16_t instruction = (memory[executionPointer] << 8) | memory[executionPointer + 1];
    executionPointer += 2;
    return instruction;
}

void PPU::executeInstruction() {
    uint16_t instruction = fetchInstruction();

    // Decode: 4-bit opcode, 12-bit operand
    uint8_t opcode = (instruction >> 12) & 0x0F;
    uint16_t operand = instruction & 0x0FFF;

    switch (static_cast<PPUOpcode>(opcode)) {
        case PPUOpcode::DEFCALL: {
            // defcall X, Y - define callable function
            // Encoding: 0000 XXXXXX YYYYYY
            // Address in register X, ID in LSB of register Y
            uint8_t regX = (operand >> 6) & 0x3F;
            uint8_t regY = operand & 0x3F;
            // uint16_t address = registers[regX];
            // uint8_t callID = registers[regY] & 0xFF;
            // For now, this is a no-op marker
            // TODO: Implement call table registration
            break;
        }

        case PPUOpcode::MOVXP_NOP: {
            // Encoding: 0001 D 00000 XXXXXX
            // D=0: movxp X - copy execution pointer + 2 to register X
            // D=1: nop - do nothing
            bool isNop = (operand >> 11) & 0x01;
            if (!isNop) {
                // MOVXP X - copy EP+2 to register X
                uint8_t regX = operand & 0x3F;
                registers[regX] = executionPointer + 2;
            }
            // If isNop=true, do nothing
            break;
        }

        case PPUOpcode::SWAPREG: {
            // swapreg X Y - swap registers X and Y
            uint8_t regX = (operand >> 6) & 0x3F;
            uint8_t regY = operand & 0x3F;
            uint16_t temp = registers[regX];
            registers[regX] = registers[regY];
            registers[regY] = temp;
            break;
        }

        case PPUOpcode::CLR: {
            // clr X - clear register X
            uint8_t regX = operand & 0x3F;
            registers[regX] = 0;
            break;
        }

        case PPUOpcode::CMP: {
            // cmp X Y - compare X and Y
            uint8_t regX = (operand >> 6) & 0x3F;
            uint8_t regY = operand & 0x3F;
            updateFlags(registers[regX] - registers[regY], registers[regX], registers[regY]);
            break;
        }

        case PPUOpcode::CLRF: {
            // clrf - clear flags
            clearFlags();
            break;
        }

        case PPUOpcode::JUMP_ZG: {
            // Bit 11: 0 = jmz, 1 = jmg
            bool isGreater = (operand & 0x800) != 0;
            if (isGreater) {
                // jmg (pc) - jump if greater
                if (flags.greater) {
                    executionPointer = registers[REG_PC];
                }
            } else {
                // jmz (pc) - jump if zero
                if (flags.zero) {
                    executionPointer = registers[REG_PC];
                }
            }
            break;
        }

        case PPUOpcode::JUMP_NZG: {
            // Bit 11: 0 = jnz, 1 = jng (jml)
            bool isNotGreater = (operand & 0x800) != 0;
            if (isNotGreater) {
                // jng (pc) / jml (pc) - jump if not greater (less or equal)
                if (!flags.greater) {
                    executionPointer = registers[REG_PC];
                }
            } else {
                // jnz (pc) - jump if not zero
                if (!flags.zero) {
                    executionPointer = registers[REG_PC];
                }
            }
            break;
        }

        case PPUOpcode::INC: {
            // inc X - increment register X
            uint8_t regX = operand & 0x3F;
            registers[regX]++;
            break;
        }

        case PPUOpcode::DEC: {
            // dec X - decrement register X
            uint8_t regX = operand & 0x3F;
            registers[regX]--;
            break;
        }

        case PPUOpcode::ADD: {
            // add X Y - X = X + Y
            uint8_t regX = (operand >> 6) & 0x3F;
            uint8_t regY = operand & 0x3F;
            registers[regX] += registers[regY];
            break;
        }

        case PPUOpcode::SUB: {
            // sub X Y - X = X - Y
            uint8_t regX = (operand >> 6) & 0x3F;
            uint8_t regY = operand & 0x3F;
            registers[regX] -= registers[regY];
            break;
        }

        case PPUOpcode::MUL: {
            // mul X Y - X = X * Y
            uint8_t regX = (operand >> 6) & 0x3F;
            uint8_t regY = operand & 0x3F;
            registers[regX] *= registers[regY];
            break;
        }

        case PPUOpcode::INTDIV: {
            // intdiv X Y - X = X / Y (integer division)
            uint8_t regX = (operand >> 6) & 0x3F;
            uint8_t regY = operand & 0x3F;
            if (registers[regY] != 0) {
                registers[regX] /= registers[regY];
            }
            break;
        }

        case PPUOpcode::PRESET_E: {
            // preset E Z W - immediate/byte operations
            uint8_t subopcode = (operand >> 10) & 0x03;  // 2 bits for sub-opcode (bits 11-10)
            uint16_t suboperand = operand & 0x3FF;        // 10 bits for operand (bits 9-0)
            executePresetE(subopcode, suboperand);
            break;
        }

        case PPUOpcode::PRESET_F: {
            // preset F Z W - extended instructions
            uint8_t subopcode = (operand >> 8) & 0x0F;  // 4 bits for sub-opcode
            uint8_t suboperand = operand & 0xFF;
            executePresetF(subopcode, suboperand);
            break;
        }
    }
}

void PPU::executePresetE(uint8_t subopcode, uint16_t operand) {
    switch (static_cast<PresetEOpcode>(subopcode)) {
        case PresetEOpcode::TARREG: {
            // tarreg T, Y, X - set target register T to point to register X, target byte Y
            // Encoding: 00 TT Y 0 XXXXXX (bits 11-10: subop, bits 9-8: TT, bit 7: Y, bit 6: unused, bits 5-0: XXXXXX)
            uint8_t targetReg = (operand >> 8) & 0x03;     // 2 bits for target register (bits 9-8)
            uint8_t targetByte = (operand >> 7) & 0x01;    // 1 bit for byte select (bit 7)
            uint8_t sourceReg = operand & 0x3F;            // 6 bits for source register (bits 5-0)

            targetRegisters[targetReg] = sourceReg;
            targetBytes[targetReg] = targetByte;
            break;
        }

        case PresetEOpcode::SETBYTE: {
            // setbyte T, 0xXX - set byte in target register to immediate value
            // Encoding: 01 TT XXXXXXXX (bits 11-10: subop, bits 9-8: TT, bits 7-0: XXXXXXXX)
            uint8_t targetReg = (operand >> 8) & 0x03;     // 2 bits for target register (bits 9-8)
            uint8_t immediate = operand & 0xFF;            // 8 bits immediate value (bits 7-0)

            // Get the register pointed to by target register
            uint8_t regIndex = targetRegisters[targetReg];
            uint8_t byteSel = targetBytes[targetReg];

            if (byteSel == 0) {
                // LSB
                registers[regIndex] = (registers[regIndex] & 0xFF00) | immediate;
            } else {
                // MSB
                registers[regIndex] = (registers[regIndex] & 0x00FF) | (immediate << 8);
            }
            break;
        }

        case PresetEOpcode::BUILD: {
            // build T1, T2, X - set register X's MSB from target T1, LSB from target T2
            // Encoding: 10 TT TT XXXXXX
            uint8_t targetReg1 = (operand >> 6) & 0x03;    // First target (MSB source)
            uint8_t targetReg2 = (operand >> 4) & 0x03;    // Second target (LSB source)
            uint8_t destReg = operand & 0x3F;              // Destination register (0-63)

            // Get values from the registers pointed to by target registers
            uint8_t regIndex1 = targetRegisters[targetReg1];
            uint8_t regIndex2 = targetRegisters[targetReg2];
            uint8_t byteSel1 = targetBytes[targetReg1];
            uint8_t byteSel2 = targetBytes[targetReg2];

            // Extract the bytes
            uint8_t msb = (byteSel1 == 0) ? (registers[regIndex1] & 0xFF) : ((registers[regIndex1] >> 8) & 0xFF);
            uint8_t lsb = (byteSel2 == 0) ? (registers[regIndex2] & 0xFF) : ((registers[regIndex2] >> 8) & 0xFF);

            // Build the 16-bit value
            registers[destReg] = (msb << 8) | lsb;
            break;
        }

        case PresetEOpcode::CPREG: {
            // cpreg X, Y - copy register X to register Y (using register banks)
            // Encoding: 11 00 XXXX YYYY
            uint8_t regX = ((operand >> 4) & 0x0F) | (regBankX << 4);  // 4-bit reg ID with bank
            uint8_t regY = (operand & 0x0F) | (regBankY << 4);         // 4-bit reg ID with bank

            registers[regY] = registers[regX];
            break;
        }
    }
}

void PPU::executePresetF(uint8_t subopcode, uint8_t operand) {
    switch (static_cast<PresetFOpcode>(subopcode)) {
        case PresetFOpcode::SETPOS: {
            // setpos X Y - set position using 4-bit register IDs from banks
            uint8_t regXId = ((operand >> 4) & 0x0F) | (regBankX << 4);
            uint8_t regYId = (operand & 0x0F) | (regBankY << 4);
            // Position would be stored somewhere - for now just a placeholder
            // uint16_t x = registers[regXId];
            // uint16_t y = registers[regYId];
            break;
        }

        case PresetFOpcode::SETTILE: {
            // settile X, Y - set tile pointed to by register X, in mode Y
            // Encoding: 0001 XXXXXX YY
            uint8_t regX = (operand >> 2) & 0x3F;   // 6 bits for register
            uint8_t mode = operand & 0x03;          // 2 bits for mode (0-3)

            currentTileId = registers[regX] & 0xFF;
            currentTileMode = mode;
            break;
        }

        case PresetFOpcode::SETDP: {
            // setdp X - set DP register
            uint8_t regX = (operand >> 2) & 0x3F;
            registers[REG_DP] = registers[regX];
            break;
        }

        case PresetFOpcode::MOVDP: {
            // mov(dp) X - move register X to address pointed by DP
            uint8_t regX = (operand >> 2) & 0x3F;
            uint16_t addr = registers[REG_DP];
            if (addr < 65535) {
                memory[addr] = registers[regX] & 0xFF;
                memory[addr + 1] = (registers[regX] >> 8) & 0xFF;

                // Check for memory-mapped I/O
                handleMemoryWrite(addr, memory[addr]);
                handleMemoryWrite(addr + 1, memory[addr + 1]);
            }
            break;
        }

        case PresetFOpcode::SETRENDMOD: {
            // setrendmod - set render mode
            if (display != nullptr) {
                bool is32bit = (operand & 0x80) != 0;
                display->setRenderMode(is32bit ? RenderMode::RGBA32 : RenderMode::RGBA16);
            }
            break;
        }

        case PresetFOpcode::PALETTE16_LOAD: {
            // 16cpaletteload(dp) - load 16-color palette from DP
            // Size: 32 bytes (16-bit) or 64 bytes (32-bit)
            // Placeholder - would load into palette memory
            break;
        }

        case PresetFOpcode::PALETTE256_LOAD: {
            // 256cpaletteload(dp) - load 256-color palette from DP
            // Size: 512 bytes (16-bit) or 1024 bytes (32-bit)
            // Placeholder - would load into palette memory
            break;
        }

        case PresetFOpcode::JMR: {
            // jmr (pc) - jump relative to PC
            executionPointer = registers[REG_PC];
            break;
        }

        case PresetFOpcode::MOV: {
            // mov X (dp) - move from address pointed by DP to register X
            uint8_t regX = (operand >> 2) & 0x3F;
            uint16_t addr = registers[REG_DP];
            if (addr < 65535) {
                uint8_t low = handleMemoryRead(addr);
                uint8_t high = handleMemoryRead(addr + 1);
                registers[regX] = low | (high << 8);
            }
            break;
        }

        case PresetFOpcode::SETREGBANK: {
            // setregbank Z W - set register banks for setpos
            regBankX = (operand >> 4) & 0x03;
            regBankY = (operand >> 2) & 0x03;
            break;
        }

        case PresetFOpcode::CLRTILE: {
            // clrtile - clear tile storage
            for (auto& tile : tileStorage) {
                tile.fill(0);
            }
            break;
        }

        case PresetFOpcode::CLRPALETTE: {
            // clrpalette - clear palette
            // Placeholder - not implemented yet
            break;
        }

        case PresetFOpcode::TILEDRAW: {
            // tiledraw - draw tile using position from SETPOS and tile from SETTILE
            // Position stored at memory-mapped I/O (similar to pixel drawing)
            if (display != nullptr) {
                // Get position from memory-mapped region (0x0200-0x0203)
                uint16_t x = memory[0x0200] | (memory[0x0201] << 8);
                uint16_t y = memory[0x0202] | (memory[0x0203] << 8);

                // Draw 8x8 tile
                for (int ty = 0; ty < 8; ty++) {
                    for (int tx = 0; tx < 8; tx++) {
                        uint8_t pixelValue = tileStorage[currentTileId][ty * 8 + tx];

                        // For now, treat as grayscale (mode handling can be added later)
                        uint32_t color = (pixelValue << 24) |  // R
                                        (pixelValue << 16) |   // G
                                        (pixelValue << 8) |    // B
                                        0xFF;                  // A = 255

                        display->setPixel32(x + tx, y + ty, color);
                    }
                }
            }
            break;
        }

        case PresetFOpcode::RESERVED_D: {
            // Reserved - no-op
            break;
        }

        case PresetFOpcode::CALL: {
            // call X - call function at address X (from defcall)
            // Would need a call stack - placeholder
            // registers[REG_PC] = operand;
            break;
        }

        case PresetFOpcode::GBLS: {
            // gbls - get blank status
            uint8_t regX = (operand >> 2) & 0x3F;
            if (vblank) {
                registers[regX] = 0;
            } else if (hblank) {
                registers[regX] = 1;
            } else {
                registers[regX] = 65535;
            }
            break;
        }
    }
}

void PPU::updateFlags(uint16_t result, uint16_t left, uint16_t right) {
    flags.zero = (result == 0);
    flags.greater = (left > right);
}

void PPU::clearFlags() {
    flags.zero = false;
    flags.greater = false;
}

uint8_t PPU::handleMemoryRead(uint16_t address) const {
    // Framebuffer region: 0xE000-0xFFFF (8 KiB max)
    if (address >= 0xE000 && address <= 0xFFFF && display != nullptr) {
        uint16_t offset = address - 0xE000;

        if (display->getRenderMode() == RenderMode::RGBA16) {
            // 4 KiB framebuffer (0xE000-0xEFFF)
            if (offset < 0x1000) {
                const uint8_t* fbBytes = reinterpret_cast<const uint8_t*>(display->getFramebuffer16());
                return fbBytes[offset];
            }
        } else {
            // 8 KiB framebuffer (0xE000-0xFFFF)
            if (offset < 0x2000) {
                const uint8_t* fbBytes = reinterpret_cast<const uint8_t*>(display->getFramebuffer32());
                return fbBytes[offset];
            }
        }
        return 0; // Out of bounds
    }

    // Normal memory
    return memory[address];
}

void PPU::handleMemoryWrite(uint16_t address, uint8_t value) {
    // Framebuffer region: 0xE000-0xFFFF (8 KiB max)
    if (address >= 0xE000 && address <= 0xFFFF && display != nullptr) {
        uint16_t offset = address - 0xE000;

        if (display->getRenderMode() == RenderMode::RGBA16) {
            // 4 KiB framebuffer (0xE000-0xEFFF)
            if (offset < 0x1000) {
                uint8_t* fbBytes = reinterpret_cast<uint8_t*>(display->getFramebuffer16());
                fbBytes[offset] = value;
            }
        } else {
            // 8 KiB framebuffer (0xE000-0xFFFF)
            if (offset < 0x2000) {
                uint8_t* fbBytes = reinterpret_cast<uint8_t*>(display->getFramebuffer32());
                fbBytes[offset] = value;
            }
        }
        return; // Don't also write to regular memory
    }

    // Memory-mapped display I/O region: 0x0100-0x010B (word-aligned for MOVDP)
    // 0x0100-0x0101: X position (16-bit)
    // 0x0102-0x0103: Y position (16-bit)
    // 0x0104-0x0105: Red component (16-bit, low byte used)
    // 0x0106-0x0107: Green component (16-bit, low byte used)
    // 0x0108-0x0109: Blue component (16-bit, low byte used)
    // 0x010A-0x010B: Alpha component (16-bit, low byte used) - writing triggers pixel draw

    if (address == 0x010A && display != nullptr) {
        // Extract position and color from memory (little-endian 16-bit values)
        uint16_t x = memory[0x0100] | (memory[0x0101] << 8);
        uint16_t y = memory[0x0102] | (memory[0x0103] << 8);

        // Build RGBA32 color (RRGGBBAA format) - use low bytes of 16-bit values
        uint32_t color = (memory[0x0104] << 24) |  // R
                        (memory[0x0106] << 16) |   // G
                        (memory[0x0108] << 8) |    // B
                        memory[0x010A];            // A

        // Draw pixel to display
        display->setPixel32(x, y, color);
    }

    // Tile data region: 0x0300-0x033F (64 bytes for direct tile storage access)
    // Can be written to directly for tile definition
    if (address >= 0x0300 && address <= 0x033F) {
        uint8_t offset = address - 0x0300;
        if (offset < TILE_SIZE && currentTileId < MAX_TILES) {
            tileStorage[currentTileId][offset] = value;
        }
    }

    // Tile position region: 0x0200-0x0203 (word-aligned)
    // 0x0200-0x0201: X position (16-bit) for TILEDRAW
    // 0x0202-0x0203: Y position (16-bit) for TILEDRAW
    // Position is used by TILEDRAW instruction in Preset F
}

} // namespace ZeroPoint
