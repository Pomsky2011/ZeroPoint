#include "ppu.h"
#include <cstring>

namespace ZeroPoint {

PPU::PPU()
    : executionPointer(0)
    , state(PPUState::WaitingForInterrupts)
    , vblankInterruptAddr(0)
    , hblankInterruptAddr(0)
    , vblank(false)
    , hblank(false)
    , renderMode(RenderMode::RGBA16)
    , regBankX(0)
    , regBankY(0)
    , cycleCounter(0)
    , initStage(0)
{
    reset();
}

PPU::~PPU() {
}

void PPU::reset() {
    // Clear all registers
    registers.fill(0);

    // Clear memory
    memory.fill(0);

    // Reset state
    executionPointer = 0;
    state = PPUState::WaitingForInterrupts;
    flags = PPUFlags();
    cycleCounter = 0;
    initStage = 0;

    vblankInterruptAddr = 0;
    hblankInterruptAddr = 0;
    vblank = false;
    hblank = false;
    renderMode = RenderMode::RGBA16;
    regBankX = 0;
    regBankY = 0;
}

void PPU::loadMicrocode(const uint8_t* code, size_t length, uint16_t offset) {
    size_t copyLength = (length + offset > 65536) ? (65536 - offset) : length;
    std::memcpy(&memory[offset], code, copyLength);
}

void PPU::setVBlankInterrupt(uint16_t address) {
    vblankInterruptAddr = address;
    if (state == PPUState::WaitingForInterrupts && initStage == 0) {
        initStage = 1;
    }
}

void PPU::setHBlankInterrupt(uint16_t address) {
    hblankInterruptAddr = address;
    if (state == PPUState::WaitingForInterrupts && initStage == 1) {
        initStage = 2;
        // Move to boot ROM stage
        state = PPUState::RunningBootROM;
        // TODO: Actually run boot ROM animation
        // For now, immediately move to waiting for start
        state = PPUState::WaitingForStart;
    }
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
            // defcall 0xXX - define callable at address XX
            // For now, this is a no-op marker
            break;
        }

        case PPUOpcode::ENDDEFCALL: {
            // enddefcall - end function definition
            // No-op marker
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

        case PPUOpcode::HALT: {
            // halt - stop execution
            state = PPUState::Halted;
            break;
        }

        case PPUOpcode::PRESET: {
            // preset Z W - extended instructions
            uint8_t subopcode = (operand >> 8) & 0x0F;
            uint8_t suboperand = operand & 0xFF;
            executePresetF(subopcode, suboperand);
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
            // settile W - set tile ID
            // uint8_t tileId = operand;
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
            }
            break;
        }

        case PresetFOpcode::SETRENDMOD: {
            // setrendmod - set render mode
            bool is32bit = (operand & 0x80) != 0;
            renderMode = is32bit ? RenderMode::RGBA32 : RenderMode::RGBA16;
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
                registers[regX] = memory[addr] | (memory[addr + 1] << 8);
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
            // clrtile - clear current tile
            // Placeholder
            break;
        }

        case PresetFOpcode::CLRPALETTE: {
            // clrpalette - clear palette
            // Placeholder
            break;
        }

        case PresetFOpcode::STRTDEFTILE: {
            // strtdeftile - start defining tile
            // Placeholder
            break;
        }

        case PresetFOpcode::ENDDEFTILE: {
            // enddeftile - end defining tile
            // Placeholder
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

} // namespace ZeroPoint
