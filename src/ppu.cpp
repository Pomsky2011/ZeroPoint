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

    // Initialize palette system (default grayscale palettes)
    for (int i = 0; i < 16; i++) {
        uint8_t gray = (i << 4) | i;  // 0x00, 0x11, 0x22, ... 0xFF
        palette16[i] = ((gray >> 3) << 11) | ((gray >> 3) << 6) | ((gray >> 3) << 1) | 1;  // 16-bit BBGR
    }
    for (int i = 0; i < 256; i++) {
        palette256[i] = (i << 24) | (i << 16) | (i << 8) | 0xFF;  // 32-bit RGBA grayscale
    }
    tileTranslucency.fill(0x0F);  // Fully opaque by default
    tileBlendMode = 0;

    // Initialize VOC registers
    vocRegisters.renderModeControl = 0x00;  // 16-bit mode, auto-roll enabled by default
    vocRegisters.paletteAddrLow = 0x00;
    vocRegisters.paletteAddrHigh = 0x00;
    // Default bank order: 0, 1, 2, 3, 4, 5, 6, 7
    for (int i = 0; i < 8; i++) {
        vocRegisters.bankOrder[i] = i;
    }
    vocRegisters.autoRollToggle = 0x01;  // Auto-roll enabled by default
    vocRegisters.translucency[0] = 0x00;
    vocRegisters.translucency[1] = 0x00;
    vocRegisters.translucency[2] = 0x00;
    vocRegisters.translucency[3] = 0x00;
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
    if (state != PPUState::Running) [[unlikely]] {
        return;
    }

    // Check for VBlank interrupt (R59 + VOC bit 4) - optimized with branch hints
    if (vblank) [[unlikely]] {
        bool vblankIntEnabled = (vocRegisters.renderModeControl & VOC_VBLANK_INT) != 0;
        if (vblankIntEnabled && registers[REG_VBLANK_INT] != 0) [[likely]] {
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
    }

    // Check for HBlank interrupt (R60 + VOC bit 5) - optimized with branch hints
    if (hblank) [[unlikely]] {
        bool hblankIntEnabled = (vocRegisters.renderModeControl & VOC_HBLANK_INT) != 0;
        if (hblankIntEnabled && registers[REG_HBLANK_INT] != 0) [[likely]] {
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
            loadPalette16();
            break;
        }

        case PresetFOpcode::PALETTE256_LOAD: {
            // 256cpaletteload(dp) - load 256-color palette from DP
            loadPalette256();
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
            // clrpalette - clear both palettes to default grayscale
            for (int i = 0; i < 16; i++) {
                uint8_t gray = (i << 4) | i;
                palette16[i] = ((gray >> 3) << 11) | ((gray >> 3) << 6) | ((gray >> 3) << 1) | 1;
            }
            for (int i = 0; i < 256; i++) {
                palette256[i] = (i << 24) | (i << 16) | (i << 8) | 0xFF;
            }
            break;
        }

        case PresetFOpcode::TILEDRAW: {
            // tiledraw - draw tile using position from SETPOS and tile from SETTILE
            // Position stored at memory-mapped I/O (similar to pixel drawing)
            if (display != nullptr) {
                // Get position from memory-mapped region (0x0200-0x0203)
                uint16_t x = memory[0x0200] | (memory[0x0201] << 8);
                uint16_t y = memory[0x0202] | (memory[0x0203] << 8);

                // Tile mode: 0=16BBGR 4bpp, 1=32RGBA 4bpp, 2=16BBGR 8bpp, 3=32RGBA 8bpp
                bool is4bpp = (currentTileMode & 0x02) == 0;
                bool is32bit = (currentTileMode & 0x01) != 0;

                // Draw 8x8 tile
                for (int ty = 0; ty < 8; ty++) {
                    for (int tx = 0; tx < 8; tx++) {
                        uint32_t color = 0;

                        if (is4bpp) {
                            // 4bpp mode: 2 pixels per byte, use palette lookup
                            uint8_t byteIndex = (ty * 8 + tx) / 2;
                            uint8_t pixelByte = tileStorage[currentTileId][byteIndex];

                            // Get 4-bit palette index (high nibble for even pixels, low nibble for odd)
                            uint8_t paletteIndex = (tx & 1) ? (pixelByte & 0x0F) : ((pixelByte >> 4) & 0x0F);

                            if (is32bit) {
                                // 32-bit RGBA mode with 16-color palette
                                // Convert 16-bit palette entry to 32-bit
                                uint16_t pal16 = palette16[paletteIndex];

                                // Extract 5-bit components (BBBBBGGGGGRRRRR-A format)
                                uint8_t r5 = (pal16 >> 1) & 0x1F;
                                uint8_t g5 = (pal16 >> 6) & 0x1F;
                                uint8_t b5 = (pal16 >> 11) & 0x1F;
                                uint8_t a1 = pal16 & 0x01;

                                // Expand 5-bit to 8-bit
                                uint8_t r8 = (r5 << 3) | (r5 >> 2);
                                uint8_t g8 = (g5 << 3) | (g5 >> 2);
                                uint8_t b8 = (b5 << 3) | (b5 >> 2);
                                uint8_t a8 = a1 ? 0xFF : 0x00;

                                color = (r8 << 24) | (g8 << 16) | (b8 << 8) | a8;
                            } else {
                                // 16-bit mode with 16-color palette - use 16-bit palette directly
                                uint16_t pal16 = palette16[paletteIndex];

                                // Extract and expand to 32-bit for drawing
                                uint8_t r5 = (pal16 >> 1) & 0x1F;
                                uint8_t g5 = (pal16 >> 6) & 0x1F;
                                uint8_t b5 = (pal16 >> 11) & 0x1F;
                                uint8_t a1 = pal16 & 0x01;

                                uint8_t r8 = (r5 << 3) | (r5 >> 2);
                                uint8_t g8 = (g5 << 3) | (g5 >> 2);
                                uint8_t b8 = (b5 << 3) | (b5 >> 2);
                                uint8_t a8 = a1 ? 0xFF : 0x00;

                                color = (r8 << 24) | (g8 << 16) | (b8 << 8) | a8;
                            }
                        } else {
                            // 8bpp mode: 1 pixel per byte, use palette lookup
                            uint8_t paletteIndex = tileStorage[currentTileId][ty * 8 + tx];

                            if (is32bit) {
                                // 32-bit RGBA mode with 256-color palette
                                color = palette256[paletteIndex];
                            } else {
                                // 16-bit mode with 256-color palette
                                // Use lower 16 entries or convert to 16-bit format
                                if (paletteIndex < 16) {
                                    uint16_t pal16 = palette16[paletteIndex];

                                    uint8_t r5 = (pal16 >> 1) & 0x1F;
                                    uint8_t g5 = (pal16 >> 6) & 0x1F;
                                    uint8_t b5 = (pal16 >> 11) & 0x1F;
                                    uint8_t a1 = pal16 & 0x01;

                                    uint8_t r8 = (r5 << 3) | (r5 >> 2);
                                    uint8_t g8 = (g5 << 3) | (g5 >> 2);
                                    uint8_t b8 = (b5 << 3) | (b5 >> 2);
                                    uint8_t a8 = a1 ? 0xFF : 0x00;

                                    color = (r8 << 24) | (g8 << 16) | (b8 << 8) | a8;
                                } else {
                                    // Fallback: grayscale for palette indices >= 16
                                    color = (paletteIndex << 24) | (paletteIndex << 16) |
                                           (paletteIndex << 8) | 0xFF;
                                }
                            }
                        }

                        // Apply translucency to the color
                        color = applyTranslucency(color, currentTileId);

                        // Apply blending if mode is set and we have a non-zero blend mode
                        if (tileBlendMode != 0) {
                            // Calculate framebuffer address for this pixel
                            uint16_t pixelX = x + tx;
                            uint16_t pixelY = y + ty;

                            // Read existing pixel from framebuffer
                            uint32_t dstColor = 0;

                            if (display->getRenderMode() == RenderMode::RGBA32) {
                                // 32-bit mode: 4 bytes per pixel, 1024 bytes per scanline
                                uint16_t fbOffset = (pixelY * 1024) + (pixelX * 4);
                                uint16_t fbAddr = 0xE000 + fbOffset;

                                // Read 4 bytes (RGBA) from framebuffer
                                if (fbAddr + 3 <= 0xFFFF) {
                                    uint8_t r = handleMemoryRead(fbAddr + 0);
                                    uint8_t g = handleMemoryRead(fbAddr + 1);
                                    uint8_t b = handleMemoryRead(fbAddr + 2);
                                    uint8_t a = handleMemoryRead(fbAddr + 3);
                                    dstColor = (r << 24) | (g << 16) | (b << 8) | a;
                                }
                            } else {
                                // 16-bit mode: 2 bytes per pixel, 512 bytes per scanline
                                uint16_t fbOffset = (pixelY * 512) + (pixelX * 2);
                                uint16_t fbAddr = 0xE000 + fbOffset;

                                // Read 2 bytes (16-bit BGR) from framebuffer
                                if (fbAddr + 1 <= 0xEFFF) {  // 16-bit mode only uses 0xE000-0xEFFF
                                    uint8_t low = handleMemoryRead(fbAddr);
                                    uint8_t high = handleMemoryRead(fbAddr + 1);
                                    uint16_t rgb16 = (high << 8) | low;

                                    // Convert 16-bit to 32-bit for blending
                                    uint8_t r5 = (rgb16 >> 1) & 0x1F;
                                    uint8_t g5 = (rgb16 >> 6) & 0x1F;
                                    uint8_t b5 = (rgb16 >> 11) & 0x1F;
                                    uint8_t a1 = rgb16 & 0x01;

                                    uint8_t r8 = (r5 << 3) | (r5 >> 2);
                                    uint8_t g8 = (g5 << 3) | (g5 >> 2);
                                    uint8_t b8 = (b5 << 3) | (b5 >> 2);
                                    uint8_t a8 = a1 ? 0xFF : 0x00;

                                    dstColor = (r8 << 24) | (g8 << 16) | (b8 << 8) | a8;
                                }
                            }

                            // Apply blending with the translucency value
                            uint8_t tileSlot = currentTileId & 0x03;
                            uint8_t alpha = tileTranslucency[tileSlot];

                            if (alpha > 0 && dstColor != 0) {
                                color = blendColors(color, dstColor, tileBlendMode, alpha);
                            }
                        }

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
    // VOC registers: 0x00F0-0x00FF
    if (address >= 0x00F0 && address <= 0x00FF) {
        return handleVOCRead(address);
    }

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
    // VOC registers: 0x00F0-0x00FF
    if (address >= 0x00F0 && address <= 0x00FF) {
        handleVOCWrite(address, value);
        return;  // Don't also write to regular memory
    }

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

void PPU::handleVOCWrite(uint16_t address, uint8_t value) {
    uint16_t offset = address - 0x00F0;

    switch (offset) {
        case 0x00:  // $00F0: Render mode control
            vocRegisters.renderModeControl = value;

            // Check for reset switch (bit 0)
            if (value & VOC_RESET) {
                applyVOCReset();
                // Auto-clear reset bit after applying
                vocRegisters.renderModeControl &= ~VOC_RESET;
            }

            // Apply render mode changes
            applyVOCRenderMode();
            break;

        case 0x01:  // $00F1: Palette address low
            vocRegisters.paletteAddrLow = value;
            break;

        case 0x02:  // $00F2: Palette address high
            vocRegisters.paletteAddrHigh = value;
            break;

        case 0x03:  // $00F3: Bank order 0
        case 0x04:  // $00F4: Bank order 1
        case 0x05:  // $00F5: Bank order 2
        case 0x06:  // $00F6: Bank order 3
        case 0x07:  // $00F7: Bank order 4
        case 0x08:  // $00F8: Bank order 5
        case 0x09:  // $00F9: Bank order 6
        case 0x0A:  // $00FA: Bank order 7
            // Only writable when auto-roll is disabled
            if (vocRegisters.autoRollToggle == 0) {
                vocRegisters.bankOrder[offset - 0x03] = value;
            }
            break;

        case 0x0B:  // $00FB: Auto-roll toggle
            vocRegisters.autoRollToggle = value;
            break;

        case 0x0C:  // $00FC: Translucency byte 0 (MM - blend mode)
        case 0x0D:  // $00FD: Translucency byte 1 (PH - push/halt flags)
        case 0x0E:  // $00FE: Translucency byte 2 (TT - translucency values 0-1)
        case 0x0F:  // $00FF: Translucency byte 3 (TT - translucency values 2-3)
            // In 32-bit RGBA mode, translucency is read-only
            if (display != nullptr && display->getRenderMode() == RenderMode::RGBA16) {
                vocRegisters.translucency[offset - 0x0C] = value;

                // Extract settings when byte 1 (PH) is written
                if (offset == 0x0D) {
                    // Check for halt switch (bit 2)
                    if (value & 0x04) {
                        state = PPUState::Halted;
                    }

                    // Check for push flag (bit 3)
                    if (value & 0x08) {
                        // Extract blend mode from byte 0 (bits 0-1)
                        tileBlendMode = vocRegisters.translucency[0] & 0x03;

                        // Extract translucency values from bytes 2-3
                        // Each tile gets 4 bits (0-15 for opacity)
                        uint8_t byte2 = vocRegisters.translucency[2];
                        uint8_t byte3 = vocRegisters.translucency[3];

                        tileTranslucency[0] = byte2 & 0x0F;        // Low nibble of byte 2
                        tileTranslucency[1] = (byte2 >> 4) & 0x0F; // High nibble of byte 2
                        tileTranslucency[2] = byte3 & 0x0F;        // Low nibble of byte 3
                        tileTranslucency[3] = (byte3 >> 4) & 0x0F; // High nibble of byte 3
                    }
                }
            }
            break;
    }
}

uint8_t PPU::handleVOCRead(uint16_t address) const {
    uint16_t offset = address - 0x00F0;

    switch (offset) {
        case 0x00:  // $00F0: Render mode control
            return vocRegisters.renderModeControl;

        case 0x01:  // $00F1: Palette address low
            return vocRegisters.paletteAddrLow;

        case 0x02:  // $00F2: Palette address high
            return vocRegisters.paletteAddrHigh;

        case 0x03:  // $00F3: Bank order 0
        case 0x04:  // $00F4: Bank order 1
        case 0x05:  // $00F5: Bank order 2
        case 0x06:  // $00F6: Bank order 3
        case 0x07:  // $00F7: Bank order 4
        case 0x08:  // $00F8: Bank order 5
        case 0x09:  // $00F9: Bank order 6
        case 0x0A:  // $00FA: Bank order 7
            return vocRegisters.bankOrder[offset - 0x03];

        case 0x0B:  // $00FB: Auto-roll toggle
            return vocRegisters.autoRollToggle;

        case 0x0C:  // $00FC: Translucency byte 0
        case 0x0D:  // $00FD: Translucency byte 1
        case 0x0E:  // $00FE: Translucency byte 2
        case 0x0F:  // $00FF: Translucency byte 3
            return vocRegisters.translucency[offset - 0x0C];

        default:
            return 0x00;
    }
}

void PPU::applyVOCRenderMode() {
    if (display == nullptr) return;

    // Apply color depth setting (bit 7)
    bool is32bit = (vocRegisters.renderModeControl & VOC_COLOR_DEPTH) != 0;
    display->setRenderMode(is32bit ? RenderMode::RGBA32 : RenderMode::RGBA16);

    // TODO: Apply rolling mode setting (bit 6) when display supports it
    // TODO: Apply palette mode setting (bit 3) when palette system is implemented
}

void PPU::applyVOCReset() {
    // Reset PPU state but preserve VRAM (memory)
    // Don't clear memory - only registers and state
    registers.fill(0);
    targetRegisters.fill(0);
    targetBytes.fill(0);
    executionPointer = 0;
    state = PPUState::WaitingForStart;
    flags = PPUFlags();
    cycleCounter = 0;
    regBankX = 0;
    regBankY = 0;

    // Reset tile system
    for (auto& tile : tileStorage) {
        tile.fill(0);
    }
    currentTileId = 0;
    currentTileMode = 0;

    // Don't reset VOC registers themselves (except the reset bit, which is cleared by caller)
}

void PPU::loadPalette16() {
    // Load 16-color palette from memory address pointed to by DP
    uint16_t addr = registers[REG_DP];

    // Read 16 palette entries (16-bit format: BBGGGRRRRR-A)
    for (int i = 0; i < 16; i++) {
        uint8_t low = memory[addr + i * 2];
        uint8_t high = memory[addr + i * 2 + 1];
        palette16[i] = (high << 8) | low;
    }
}

void PPU::loadPalette256() {
    // Load 256-color palette from memory address pointed to by DP
    uint16_t addr = registers[REG_DP];

    // Read 256 palette entries (32-bit format: RRGGBBAA)
    for (int i = 0; i < 256; i++) {
        uint8_t b0 = memory[addr + i * 4];
        uint8_t b1 = memory[addr + i * 4 + 1];
        uint8_t b2 = memory[addr + i * 4 + 2];
        uint8_t b3 = memory[addr + i * 4 + 3];
        palette256[i] = (b3 << 24) | (b2 << 16) | (b1 << 8) | b0;
    }
}

uint32_t PPU::blendColors(uint32_t src, uint32_t dst, uint8_t mode, uint8_t alpha) {
    // Extract RGBA components from source (new pixel)
    uint8_t srcR = (src >> 24) & 0xFF;
    uint8_t srcG = (src >> 16) & 0xFF;
    uint8_t srcB = (src >> 8) & 0xFF;
    uint8_t srcA = src & 0xFF;

    // Extract RGBA components from destination (existing pixel)
    uint8_t dstR = (dst >> 24) & 0xFF;
    uint8_t dstG = (dst >> 16) & 0xFF;
    uint8_t dstB = (dst >> 8) & 0xFF;
    uint8_t dstA = dst & 0xFF;

    uint8_t outR, outG, outB, outA;

    // Apply translucency alpha (0-15 range mapped to 0-255)
    uint16_t alphaFull = (alpha << 4) | alpha;  // 0-15 -> 0-255

    switch (mode) {
        case 0:  // Multiply (darken)
            outR = (srcR * dstR) >> 8;
            outG = (srcG * dstG) >> 8;
            outB = (srcB * dstB) >> 8;
            outA = (srcA * alphaFull) >> 8;
            break;

        case 1:  // Average (50/50 blend)
            outR = (srcR + dstR) >> 1;
            outG = (srcG + dstG) >> 1;
            outB = (srcB + dstB) >> 1;
            outA = (srcA * alphaFull) >> 8;
            break;

        case 2:  // Subtract (color subtraction)
            outR = (dstR > srcR) ? (dstR - srcR) : 0;
            outG = (dstG > srcG) ? (dstG - srcG) : 0;
            outB = (dstB > srcB) ? (dstB - srcB) : 0;
            outA = (srcA * alphaFull) >> 8;
            break;

        case 3:  // Add (lighten/additive)
            outR = std::min(255, srcR + dstR);
            outG = std::min(255, srcG + dstG);
            outB = std::min(255, srcB + dstB);
            outA = (srcA * alphaFull) >> 8;
            break;

        default:
            return src;
    }

    // Alpha blend based on output alpha
    uint16_t blend = alphaFull;
    outR = ((srcR * blend) + (dstR * (255 - blend))) >> 8;
    outG = ((srcG * blend) + (dstG * (255 - blend))) >> 8;
    outB = ((srcB * blend) + (dstB * (255 - blend))) >> 8;

    return (outR << 24) | (outG << 16) | (outB << 8) | outA;
}

uint32_t PPU::applyTranslucency(uint32_t color, uint8_t tileIndex) {
    // Get translucency value for this tile (last 4 tiles are tracked)
    uint8_t tileSlot = tileIndex & 0x03;  // 0-3
    uint8_t alpha = tileTranslucency[tileSlot];

    // If fully opaque (15), return color as-is
    if (alpha == 0x0F) {
        return color;
    }

    // If fully transparent (0), return transparent
    if (alpha == 0x00) {
        return color & 0xFFFFFF00;  // Set alpha to 0
    }

    // Apply alpha to color's alpha channel
    uint8_t colorAlpha = color & 0xFF;
    uint8_t newAlpha = (colorAlpha * ((alpha << 4) | alpha)) >> 8;

    return (color & 0xFFFFFF00) | newAlpha;
}

} // namespace ZeroPoint
