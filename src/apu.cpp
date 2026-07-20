#include "apu.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <iostream>

APU::APU()
    : pc(0), sp(0), rp(0), dp(0), db(0), flags(0), bf(false),
      romBank(0), ioBank(0),
      cycleCount(0), instructionCount(0), halted(false),
      reserved1(0)
{
    std::fill(std::begin(reserved2), std::end(reserved2), 0);
    reset();
}

APU::~APU() {
}

void APU::reset() {
    memory.fill(0);
    arom.fill(0);
    registers.fill(0);

    // A user-loaded internal BIOS (loadBIOS()) has no dedicated memory
    // region to preserve across the fill(0) above - APU memory is one flat
    // array, unlike the CPU's bank map - so it's kept separately and
    // reapplied here.
    if (!customBios.empty()) {
        std::memcpy(&memory[BIOS_BASE], customBios.data(), customBios.size());
    }

    pc = 0x8000;  // Start at BIOS
    sp = 0x0000;  // Stack pointer (should be initialized by BSP instruction)
    rp = 0x80;
    dp = 0x00;
    db = 0x00;
    bf = false;
    flags = 0;

    romBank = 0;
    ioBank = 0;

    for (auto& loop : loops) {
        loop.active = false;
        loop.count = 0;
        loop.remaining = 0;
        loop.startAddress = 0;
        loop.endAddress = 0;
    }

    for (auto& func : functions) {
        func.defined = false;
        func.address = 0;
    }

    callStack.clear();

    cycleCount = 0;
    instructionCount = 0;
    halted = false;

    resetMMP();
}

void APU::resetMMP() {
    for (auto& ch : mmpChannels) {
        ch.pitch = 0x1000;   // 1.0× speed
        ch.volume = 255;
        ch.pan = 128;        // center
        ch.reserved0 = 0;
        ch.stlAddress = 0;
        ch.samplePosition = 0;
        ch.active = false;
        ch.sampleDataAddress = 0;
        ch.loopAddress = 0;
        ch.sampleCache.clear();
    }

    reserved1 = 0;
    std::fill(std::begin(reserved2), std::end(reserved2), 0);
}

void APU::loadROM(const uint8_t* data, size_t size, uint16_t address) {
    if (address >= AROM_BASE) {
        // Load into AROM
        size_t aromOffset = address - AROM_BASE;
        size_t copySize = std::min(size, AROM_SIZE - aromOffset);
        std::memcpy(&arom[aromOffset], data, copySize);
    } else {
        // Load into main memory
        size_t copySize = std::min(size, MEMORY_SIZE - address);
        std::memcpy(&memory[address], data, copySize);
    }
}

void APU::loadBIOS(const uint8_t* data, size_t size) {
    size_t copySize = std::min(size, (size_t)BIOS_SIZE);
    customBios.assign(data, data + copySize);
    std::memcpy(&memory[BIOS_BASE], customBios.data(), customBios.size());
}

uint8_t APU::readByte(uint16_t address) {
    // AROM banking
    if (address >= AROM_BASE) {
        size_t aromOffset = (romBank * AROM_BANK_SIZE) + (address - AROM_BASE);
        if (aromOffset < AROM_SIZE) {
            return arom[aromOffset];
        }
        return 0;
    }

    // MMP registers (special handling for reads)
    if (address >= MMP_BASE && address < MMP_BASE + MMP_SIZE) {
        // Return current MMP state
        return memory[address];
    }

    return memory[address];
}

void APU::writeByte(uint16_t address, uint8_t value) {
    // ROM regions are read-only
    if ((address >= BIOS_BASE && address < BIOS_BASE + BIOS_SIZE) ||
        (address >= AROM_BASE)) {
        return;
    }

    // MMP registers (special handling for writes)
    // Register map:
    //   $0000-$001F  Pitch       ch0-15, 2 bytes each
    //   $0020-$002F  Volume      ch0-15, 1 byte each
    //   $0030-$003F  Pan         ch0-15, 1 byte each
    //   $0040-$004F  Reserved0   ch0-15, 1 byte each
    //   $0050-$006F  STL address ch0-15, 2 bytes each
    //   $0070-$007F  Reserved1   (global, 16 bytes)
    //   $0080-$00FF  Reserved2   (128 bytes)
    if (address >= MMP_BASE && address < MMP_BASE + MMP_SIZE) {
        memory[address] = value;

        // Pitch: $0000-$001F, 2 bytes per channel
        if (address < 0x0020 && (address & 1) == 1) {
            int channel = address / 2;
            if (channel < 16)
                mmpChannels[channel].pitch = readWord(address & ~1);
        }

        // Volume: $0020-$002F, 1 byte per channel
        else if (address >= 0x0020 && address < 0x0030) {
            int channel = address - 0x0020;
            mmpChannels[channel].volume = value;
        }

        // Pan: $0030-$003F, 1 byte per channel
        else if (address >= 0x0030 && address < 0x0040) {
            int channel = address - 0x0030;
            mmpChannels[channel].pan = value;
        }

        // Reserved0: $0040-$004F, 1 byte per channel (write through to memory only)
        else if (address >= 0x0040 && address < 0x0050) {
            int channel = address - 0x0040;
            mmpChannels[channel].reserved0 = value;
        }

        // STL address: $0050-$006F, 2 bytes per channel
        else if (address >= 0x0050 && address < 0x0070 && (address & 1) == 1) {
            int channel = (address - 0x0050) / 2;
            if (channel < 16) {
                uint16_t stlAddr = readWord(address & ~1);
                bool addressChanged = (mmpChannels[channel].stlAddress != stlAddr);

                if (addressChanged) {
                    mmpChannels[channel].sampleCache.clear();
                    mmpChannels[channel].stlAddress = stlAddr;

                    if (stlAddr == 0) {
                        mmpChannels[channel].active = false;
                    } else {
                        mmpChannels[channel].active = true;
                        mmpChannels[channel].samplePosition = 0;

                        if (stlAddr >= STL_BASE && stlAddr < STL_BASE + STL_SIZE) {
                            mmpChannels[channel].sampleDataAddress = readWord(stlAddr);
                            mmpChannels[channel].loopAddress = readWord(stlAddr + 2);
                        }
                    }
                }
            }
        }

        return;
    }

    memory[address] = value;
}

uint16_t APU::readWord(uint16_t address) {
    uint8_t low = readByte(address);
    uint8_t high = readByte(address + 1);
    return (high << 8) | low;
}

void APU::writeWord(uint16_t address, uint16_t value) {
    writeByte(address, value & 0xFF);
    writeByte(address + 1, (value >> 8) & 0xFF);
}

uint8_t APU::getRegister(uint8_t reg) {
    return registers[reg];
}

void APU::setRegister(uint8_t reg, uint8_t value) {
    registers[reg] = value;
}

void APU::setROMBank(uint16_t bank) {
    if (bank < AROM_BANKS) {
        romBank = bank;
    }
}

void APU::setIOBank(uint16_t bank) {
    ioBank = bank;
}

void APU::writeDEF88186Input(uint16_t offset, uint8_t value) {
    if (offset < DEF88186_INPUT_SIZE) {
        memory[DEF88186_INPUT_BASE + offset] = value;
    }
}

void APU::step() {
    if (halted) {
        cycleCount += 4;
        return;
    }

    // Fetch instruction (16-bit big-endian)
    uint16_t instruction = (readByte(pc) << 8) | readByte(pc + 1);
    pc += 2;

    // Execute
    executeInstruction(instruction);

    // Each instruction takes 4 cycles
    cycleCount += 4;
    instructionCount++;
}

void APU::run(uint64_t cycles) {
    uint64_t targetCycles = cycleCount + cycles;
    while (cycleCount < targetCycles && !halted) {
        step();
    }
}

void APU::executeInstruction(uint16_t instruction) {
    uint8_t opcode = (instruction >> 11) & 0x1F;  // 5-bit opcode
    uint16_t operand = instruction & 0x7FF;       // 11-bit operand

    switch (opcode) {
        case 0x00: execNOP(operand); break;
        case 0x01: execJMP(operand); break;
        case 0x02: {
            if (operand & 0x100) {
                // JZ — jump if register == 0
                uint8_t reg = (operand >> 10) & 1;
                bool mode   = (operand >> 9)  & 1;
                uint8_t offset = operand & 0xFF;
                if (registers[reg ? 1 : 0] == 0) {
                    pc = mode ? ((uint16_t)rp << 8) | offset : 0x8000 | offset;
                }
            } else {
                execJNZ(operand);
            }
            break;
        }
        case 0x03: {
            // SRP or SDP based on bit 8
            if (operand & 0x400) {  // Bit 10 set = SDP
                execSDP(operand);
            } else {  // SRP
                execSRP(operand);
            }
            break;
        }
        case 0x04: execNOR(operand); break;
        case 0x05: execAND(operand); break;
        case 0x06: execADD(operand); break;
        case 0x07: execSUB(operand); break;
        case 0x08: {
            // STA or STR based on bit 9
            if (operand & 0x200) {  // Bit 9 set = STA
                execSTA(operand);
            } else {  // STR
                execSTR(operand);
            }
            break;
        }
        case 0x09: {
            // LDA/STA via data pointer — bits 9-8 = subop
            uint8_t subop = (operand >> 8) & 0x03;
            bool regSel = (operand >> 10) & 1;  // 0=X, 1=Y
            uint16_t addr = ((uint16_t)dp << 8) | db;
            switch (subop) {
                case 0x00:  // LDA X/Y — load (dp:db) into register
                    registers[regSel ? 1 : 0] = readByte(addr);
                    break;
                case 0x01:  // STA X/Y — store register to (dp:db)
                    writeByte(addr, registers[regSel ? 1 : 0]);
                    break;
                case 0x02:  // STA $XX — store immediate to (dp:db)
                    writeByte(addr, operand & 0xFF);
                    break;
                default:
                    break;
            }
            break;
        }
        case 0x0A: execSCR(operand); break;
        case 0x0B: {
            uint8_t subop = (operand >> 8) & 0x07;
            uint8_t mask  = (operand >> 4) & 0x0F;
            switch (subop) {
                case 0x1:  // SFR — set flag register to value
                    flags = mask;
                    break;
                case 0x2:  // CF — clear flags by mask
                    flags &= ~mask;
                    break;
                case 0x3:  // SF — set flags by mask
                    flags |= mask;
                    break;
                case 0x4:  // STF — store flags in upper nybble of X/Y
                case 0x5: {
                    bool reg = (operand >> 8) & 1;
                    registers[reg ? 1 : 0] = (registers[reg ? 1 : 0] & 0x0F) | (flags << 4);
                    break;
                }
                default: break;
            }
            break;
        }
        case 0x0C: {
            uint8_t subop = (operand >> 8) & 0x07;
            switch (subop) {
                case 0x0: execZOR(operand); break;  // ZOR: zero register
                case 0x2: execZOA(operand); break;  // ZOA $XX: zero word at dp:XX
                case 0x3:                            // ZOA DP: zero byte at (dp:db)
                    writeByte(((uint16_t)dp << 8) | db, 0);
                    break;
                default: break;
            }
            break;
        }
        case 0x0D: {
            // LST or LFN based on bit 10
            if (operand & 0x400) {
                execLFN(operand);
            } else {
                execLST(operand);
            }
            break;
        }
        case 0x0E: execBRT(operand); break;
        case 0x0F: execBRP(operand); break;
        case 0x10: execADC(operand); break;
        case 0x11: execSBC(operand); break;
        case 0x12: {
            // BEQ or BNE based on bit 8
            if (operand & 0x100) {
                execBNE(operand);
            } else {
                execBEQ(operand);
            }
            break;
        }
        case 0x13: {
            // BLT or BGT based on bit 8
            if (operand & 0x100) {
                execBGT(operand);
            } else {
                execBLT(operand);
            }
            break;
        }
        case 0x14: execSDB(operand); break;
        case 0x15: execJMS(operand); break;
        case 0x16: execINC(operand); break;
        case 0x17: execSTACK(operand); break;
        case 0x18: {
            if (operand & 0x100) {
                // EXC — exchange X and Y
                uint8_t tmp = registers[0];
                registers[0] = registers[1];
                registers[1] = tmp;
            } else {
                // MOV src, dst — copy src register to dst register
                bool src = (operand >> 10) & 1;
                bool dst = (operand >> 9)  & 1;
                registers[dst ? 1 : 0] = registers[src ? 1 : 0];
            }
            break;
        }
        case 0x19: {
            // CME/CMN/CML/CMG based on bits 7-6
            uint8_t subOp = (operand >> 6) & 0x03;
            switch (subOp) {
                case 0: execCME(operand); break;
                case 1: execCMN(operand); break;
                case 2: execCMG(operand); break;
                case 3: execCML(operand); break;
            }
            break;
        }
        case 0x1B: {
            // XOR — same operand layout as CRB
            bool rx    = (operand >> 10) & 1;
            bool ry    = (operand >> 9)  & 1;
            bool toMem = (operand >> 8)  & 1;
            uint8_t result = registers[rx ? 1 : 0] ^ registers[ry ? 1 : 0];
            flags = result ? 0 : FLAG_Z;
            if (!toMem) {
                bool rz = (operand >> 7) & 1;
                registers[rz ? 1 : 0] = result;
            } else {
                writeByte(((uint16_t)dp << 8) | (operand & 0xFF), result);
            }
            break;
        }
        case 0x1D: {
            // STRX/STAX — indexed load/store: bit9 selects STAX(1)/STRX(0)
            if (operand & 0x200) {
                execSTAX(operand);
            } else {
                execSTRX(operand);
            }
            break;
        }
        case 0x1E: {
            // JMX/JSX — indexed jump: bit10 selects JSX(1, pushes return addr)/JMX(0)
            if (operand & 0x400) {
                execJSX(operand);
            } else {
                execJMX(operand);
            }
            break;
        }
        case 0x1A: {
            // CRB — conditional right/left shift via signed nybble
            bool inputReg = (operand >> 10) & 1;  // 0=X, 1=Y
            bool shiftReg = (operand >> 9)  & 1;  // 0=X, 1=Y
            bool toMem    = (operand >> 8)  & 1;  // 0=register, 1=memory

            uint8_t value = registers[inputReg ? 1 : 0];
            uint8_t raw   = registers[shiftReg ? 1 : 0] & 0x0F;
            int8_t  shift = (raw & 0x08) ? (int8_t)(raw | 0xF0) : (int8_t)raw;

            uint8_t result = (shift >= 0) ? (value >> shift) : (value << (-shift));
            flags = result ? 0 : FLAG_Z;

            if (!toMem) {
                bool outReg = (operand >> 7) & 1;
                registers[outReg ? 1 : 0] = result;
            } else {
                writeByte(((uint16_t)dp << 8) | (operand & 0xFF), result);
            }
            break;
        }
        default:
            // Unknown opcode, halt
            halted = true;
            break;
    }
}

// Instruction implementations

void APU::execNOP(uint16_t operand) {
    // NOP with stall: stall for operand cycles
    cycleCount += operand;
}

void APU::execJMP(uint16_t operand) {
    bool direction = (operand >> 9) & 1;  // Bit 9
    bool mode = (operand >> 8) & 1;        // Bit 8
    uint8_t offset = operand & 0xFF;

    // Push return address onto stack before jumping
    pushWord(pc);

    if ((operand & 0x300) == 0) {
        // Relative jump
        if (direction) {
            pc -= (offset * 2);  // Jump backward
        } else {
            pc += (offset * 2);  // Jump forward
        }
    } else {
        // Absolute jump
        if (mode) {
            pc = (rp << 8) | offset;  // Jump to $RPZZ
        } else {
            pc = 0x8000 | offset;     // Jump to $80ZZ (BIOS)
        }
    }
}

void APU::execJNZ(uint16_t operand) {
    uint8_t reg = (operand >> 10) & 0x01;
    bool mode = (operand >> 8) & 1;
    uint8_t offset = operand & 0xFF;

    if (registers[reg] != 0) {
        // Push return address only if jump is taken
        pushWord(pc);

        if (mode) {
            pc = (rp << 8) | offset;
        } else {
            pc = 0x8000 | offset;
        }
    }
}

void APU::execSRP(uint16_t operand) {
    rp = operand & 0xFF;
}

void APU::execSDP(uint16_t operand) {
    dp = operand & 0xFF;
}

void APU::execNOR(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool toMem = !(operand & 0x100);

    uint8_t result = ~(registers[regX] | registers[regY]);
    flags = result ? 0 : FLAG_Z;

    if (toMem) {
        uint8_t offset = operand & 0xFF;
        uint16_t address = (dp << 8) | offset;
        if (bf) {
            // High byte
            uint8_t low = readByte(address);
            writeWord(address, (result << 8) | low);
        } else {
            // Low byte
            uint8_t high = readByte(address + 1);
            writeWord(address, (high << 8) | result);
        }
    } else {
        uint8_t regZ = operand & 0x7F;
        registers[regZ] = result;
    }
}

void APU::execAND(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool toMem = !(operand & 0x100);

    uint8_t result = registers[regX] & registers[regY];
    flags = result ? 0 : FLAG_Z;

    if (toMem) {
        uint8_t offset = operand & 0xFF;
        uint16_t address = (dp << 8) | offset;
        if (bf) {
            uint8_t low = readByte(address);
            writeWord(address, (result << 8) | low);
        } else {
            uint8_t high = readByte(address + 1);
            writeWord(address, (high << 8) | result);
        }
    } else {
        uint8_t regZ = operand & 0x7F;
        registers[regZ] = result;
    }
}

void APU::execADD(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool toMem = !(operand & 0x100);

    uint16_t full = (uint16_t)registers[regX] + registers[regY];
    uint8_t result = full & 0xFF;
    flags = 0;
    if (!result)     flags |= FLAG_Z;
    if (full > 0xFF) flags |= FLAG_C;

    if (toMem) {
        uint8_t offset = operand & 0xFF;
        uint16_t address = (dp << 8) | offset;
        if (bf) {
            uint8_t low = readByte(address);
            writeWord(address, (result << 8) | low);
        } else {
            uint8_t high = readByte(address + 1);
            writeWord(address, (high << 8) | result);
        }
    } else {
        uint8_t regZ = operand & 0x7F;
        registers[regZ] = result;
    }
}

void APU::execSUB(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool toMem = !(operand & 0x100);

    uint8_t a = registers[regX], b = registers[regY];
    uint8_t result = a - b;
    flags = 0;
    if (!result) flags |= FLAG_Z;
    if (a < b)   flags |= FLAG_C | FLAG_L;
    if (a > b)   flags |= FLAG_G;

    if (toMem) {
        uint8_t offset = operand & 0xFF;
        uint16_t address = (dp << 8) | offset;
        if (bf) {
            uint8_t low = readByte(address);
            writeWord(address, (result << 8) | low);
        } else {
            uint8_t high = readByte(address + 1);
            writeWord(address, (high << 8) | result);
        }
    } else {
        uint8_t regZ = operand & 0x7F;
        registers[regZ] = result;
    }
}

void APU::execSTA(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t offset = operand & 0xFF;
    uint16_t address = (dp << 8) | offset;

    if (bf) {
        // Write to high byte
        uint8_t low = readByte(address);
        writeWord(address, (registers[regX] << 8) | low);
    } else {
        // Write to low byte
        uint8_t high = readByte(address + 1);
        writeWord(address, (high << 8) | registers[regX]);
    }
}

void APU::execSTR(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t offset = operand & 0xFF;
    uint16_t address = (dp << 8) | offset;

    if (bf) {
        // Read from high byte
        registers[regX] = readByte(address + 1);
    } else {
        // Read from low byte
        registers[regX] = readByte(address);
    }
}

void APU::execSCR(uint16_t operand) {
    uint8_t reg = (operand >> 10) & 0x01;
    uint8_t value = operand & 0xFF;
    registers[reg] = value;
}

void APU::execZOR(uint16_t operand) {
    uint8_t reg = operand & 0x3F;
    registers[reg] = 0;
}

void APU::execZOA(uint16_t operand) {
    uint8_t offset = operand & 0xFF;
    uint16_t address = (dp << 8) | offset;
    writeWord(address, 0);
}

void APU::execLST(uint16_t operand) {
    uint8_t loopId = (operand >> 6) & 0x0F;
    uint8_t count = operand & 0x3F;

    if (loopId < 16) {
        loops[loopId].active = true;
        loops[loopId].count = count;
        loops[loopId].remaining = count;
        loops[loopId].startAddress = pc;
    }
}

void APU::execLFN(uint16_t operand) {
    uint8_t loopId = (operand >> 6) & 0x0F;

    if (loopId < 16 && loops[loopId].active) {
        loops[loopId].endAddress = pc;

        if (loops[loopId].remaining > 0) {
            loops[loopId].remaining--;
            if (loops[loopId].remaining > 0) {
                pc = loops[loopId].startAddress;
            } else {
                loops[loopId].active = false;
            }
        }
    }
}

void APU::execBRT(uint16_t operand) {
    // Break from highest ID active loop
    int highestLoop = -1;
    for (int i = 15; i >= 0; i--) {
        if (loops[i].active) {
            highestLoop = i;
            break;
        }
    }

    if (highestLoop >= 0) {
        loops[highestLoop].active = false;
        pc = loops[highestLoop].endAddress + (operand * 2);
    }
}

void APU::execBRP(uint16_t operand) {
    (void)operand;  // Break to re-enter - implementation specific
}

void APU::execADC(uint16_t operand) {
    // ADC — add with carry
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool toMem   = !(operand & 0x100);

    uint16_t full = (uint16_t)registers[regX] + registers[regY] + ((flags & FLAG_C) ? 1 : 0);
    uint8_t result = full & 0xFF;
    flags = 0;
    if (!result)     flags |= FLAG_Z;
    if (full > 0xFF) flags |= FLAG_C;

    if (toMem) {
        uint16_t address = ((uint16_t)dp << 8) | (operand & 0xFF);
        writeByte(address, result);
    } else {
        registers[operand & 0x7F] = result;
    }
}

void APU::execSBC(uint16_t operand) {
    // SBC — subtract with borrow
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool toMem   = !(operand & 0x100);

    uint8_t a = registers[regX];
    uint8_t b = registers[regY] + ((flags & FLAG_C) ? 1 : 0);
    uint8_t result = a - b;
    flags = 0;
    if (!result) flags |= FLAG_Z;
    if (a < b)   flags |= FLAG_C | FLAG_L;
    if (a > b)   flags |= FLAG_G;

    if (toMem) {
        uint16_t address = ((uint16_t)dp << 8) | (operand & 0xFF);
        writeByte(address, result);
    } else {
        registers[operand & 0x7F] = result;
    }
}

void APU::execBEQ(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    uint8_t offset = operand & 0xFF;

    if (registers[regX] == registers[regY]) {
        pc = (rp << 8) | offset;
    }
}

void APU::execBNE(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    uint8_t offset = operand & 0xFF;

    if (registers[regX] != registers[regY]) {
        pc = (rp << 8) | offset;
    }
}

void APU::execBLT(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    uint8_t offset = operand & 0xFF;

    if (registers[regX] < registers[regY]) {
        pc = (rp << 8) | offset;
    }
}

void APU::execBGT(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    uint8_t offset = operand & 0xFF;

    if (registers[regX] > registers[regY]) {
        pc = (rp << 8) | offset;
    }
}

void APU::execSDB(uint16_t operand) {
    db = operand & 0xFF;
}

void APU::execJMS(uint16_t operand) {
    bool useDP  = (operand >> 10) & 1;
    bool doCall = (operand >> 9)  & 1;
    uint16_t target = useDP ? ((uint16_t)dp << 8) | db
                            : ((uint16_t)rp << 8) | (operand & 0xFF);
    if (doCall) pushWord(pc);
    pc = target;
}

void APU::execINC(uint16_t operand) {
    // Increment/Decrement: bit 10 = DEC(1)/INC(0), bits 9-8 = X(0)/Y(1)/DP(2)/SP(3)
    bool dec = (operand >> 10) & 1;
    uint8_t target = (operand >> 8) & 0x03;
    switch (target) {
        case 0: dec ? registers[0]-- : registers[0]++; break;
        case 1: dec ? registers[1]-- : registers[1]++; break;
        case 2: dec ? dp-- : dp++; break;
        case 3: dec ? sp-- : sp++; break;
    }
}

void APU::execWRH(uint16_t operand) {
    // reserved
    (void)operand;
}

void APU::execWRL(uint16_t operand) {
    // reserved
    (void)operand;
}

void APU::execCFN(uint16_t operand) {
    // reserved
    (void)operand;
}

void APU::execSTACK(uint16_t operand) {
    // Stack operations based on operand pattern
    // 10111 D SS 0 XXXXXXX
    bool D = (operand >> 10) & 1;
    uint8_t SS = (operand >> 8) & 3;

    if (!D) {
        if (operand & 0x200) {
            // sdp b,$XX — set byte b of data pointer (b=0: DB/low, b=1: DP/high)
            bool b = (operand >> 8) & 1;
            uint8_t value = operand & 0xFF;
            if (b) dp = value; else db = value;
        } else if ((operand & 0xFF) == 0x42) {
            // PODP: Pop 16-bit dp:db from stack
            db = popByte();
            dp = popByte();
        } else if ((operand & 0xFF) == 0x41) {
            // PUDP: Push 16-bit dp:db onto stack
            pushByte(dp);
            pushByte(db);
        } else if ((operand & 0xFF) == 0x40) {
            // RET: Pop 16-bit PC from stack
            pc = popWord();
        } else {
            // BSP: Build Stack Pointer from X (LSB) and Y (MSB)
            sp = registers[0] | (registers[1] << 8);
        }
    } else {
        // D=1: Push/Pop operations
        switch (SS) {
            case 0: // PUX: Push X
                pushByte(registers[0]);
                break;
            case 1: // PUY: Push Y
                pushByte(registers[1]);
                break;
            case 2: // POX: Pop to X
                registers[0] = popByte();
                break;
            case 3: // POY: Pop to Y
                registers[1] = popByte();
                break;
        }
    }
}

void APU::execCCF(uint16_t operand) {
    // reserved - opcode 0x18 available for redesign
    (void)operand;
}

void APU::execCME(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool direction = (operand >> 6) & 1;
    uint8_t offset = operand & 0x3F;

    uint8_t cma = registers[regX], cmb = registers[regY];
    flags = 0;
    if (cma == cmb) flags |= FLAG_Z;
    if (cma > cmb)  flags |= FLAG_G;
    if (cma < cmb)  flags |= FLAG_L;

    if (registers[regX] == registers[regY]) {
        if (direction) {
            pc -= (offset * 2);
        } else {
            pc += (offset * 2);
        }
    }
}

void APU::execCMN(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool direction = (operand >> 6) & 1;
    uint8_t offset = operand & 0x3F;

    uint8_t cma = registers[regX], cmb = registers[regY];
    flags = 0;
    if (cma == cmb) flags |= FLAG_Z;
    if (cma > cmb)  flags |= FLAG_G;
    if (cma < cmb)  flags |= FLAG_L;

    if (registers[regX] != registers[regY]) {
        if (direction) {
            pc -= (offset * 2);
        } else {
            pc += (offset * 2);
        }
    }
}

void APU::execCMG(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool direction = (operand >> 6) & 1;
    uint8_t offset = operand & 0x3F;

    uint8_t cma = registers[regX], cmb = registers[regY];
    flags = 0;
    if (cma == cmb) flags |= FLAG_Z;
    if (cma > cmb)  flags |= FLAG_G;
    if (cma < cmb)  flags |= FLAG_L;

    if (registers[regX] > registers[regY]) {
        if (direction) {
            pc -= (offset * 2);
        } else {
            pc += (offset * 2);
        }
    }
}

void APU::execCML(uint16_t operand) {
    uint8_t regX = (operand >> 10) & 0x01;
    uint8_t regY = (operand >> 9)  & 0x01;
    bool direction = (operand >> 6) & 1;
    uint8_t offset = operand & 0x3F;

    uint8_t cma = registers[regX], cmb = registers[regY];
    flags = 0;
    if (cma == cmb) flags |= FLAG_Z;
    if (cma > cmb)  flags |= FLAG_G;
    if (cma < cmb)  flags |= FLAG_L;

    if (registers[regX] < registers[regY]) {
        if (direction) {
            pc -= (offset * 2);
        } else {
            pc += (offset * 2);
        }
    }
}

void APU::execSTRX(uint16_t operand) {
    // Indexed load: dataReg = memory[(dp<<8) | registers[offsetReg]]
    uint8_t dataReg   = (operand >> 10) & 0x01;
    uint8_t offsetReg = (operand >> 8)  & 0x01;
    uint16_t address = ((uint16_t)dp << 8) | registers[offsetReg];
    registers[dataReg] = readByte(address);
}

void APU::execSTAX(uint16_t operand) {
    // Indexed store: memory[(dp<<8) | registers[offsetReg]] = dataReg
    uint8_t dataReg   = (operand >> 10) & 0x01;
    uint8_t offsetReg = (operand >> 8)  & 0x01;
    uint16_t address = ((uint16_t)dp << 8) | registers[offsetReg];
    writeByte(address, registers[dataReg]);
}

void APU::execJMX(uint16_t operand) {
    (void)operand;
    pc = ((uint16_t)registers[1] << 8) | registers[0];
}

void APU::execJSX(uint16_t operand) {
    (void)operand;
    uint16_t target = ((uint16_t)registers[1] << 8) | registers[0];
    pushWord(pc);
    pc = target;
}

// Stack operations

void APU::pushByte(uint8_t value) {
    writeByte(sp, value);
    sp++;
}

uint8_t APU::popByte() {
    sp--;
    return readByte(sp);
}

void APU::pushWord(uint16_t value) {
    // Push high byte first, then low byte
    pushByte((value >> 8) & 0xFF);
    pushByte(value & 0xFF);
}

uint16_t APU::popWord() {
    // Pop low byte first, then high byte
    uint8_t low = popByte();
    uint8_t high = popByte();
    return (high << 8) | low;
}

// MMP implementation (simplified for now)

void APU::updateMMP() {
    // Called at audio sample rate (e.g., 48kHz)
    // Advances all active channels by one sample; pitch is fixed-point (0x1000 = 1.0×)
    for (auto& ch : mmpChannels) {
        if (ch.active && ch.stlAddress != 0 && !ch.sampleCache.empty()) {
            ch.samplePosition += ch.pitch;
            uint32_t maxPosition = static_cast<uint32_t>(ch.sampleCache.size()) << 12;
            if (ch.samplePosition >= maxPosition)
                ch.samplePosition %= maxPosition;
        }
    }
}

int16_t APU::mixChannels(bool right) {
    int32_t mixed = 0;

    for (int i = 0; i < 16; i++) {
        auto& ch = mmpChannels[i];
        if (!ch.active || ch.stlAddress == 0)
            continue;

        processChannel(i);

        if (ch.sampleCache.empty())
            continue;

        size_t cacheSize = ch.sampleCache.size();
        size_t idx  = (ch.samplePosition >> 12) % cacheSize;
        size_t prev = (idx + cacheSize - 1) % cacheSize;
        size_t next = (idx + 1) % cacheSize;
        size_t next2 = (idx + 2) % cacheSize;

        int8_t s0 = static_cast<int8_t>(ch.sampleCache[prev]);
        int8_t s1 = static_cast<int8_t>(ch.sampleCache[idx]);
        int8_t s2 = static_cast<int8_t>(ch.sampleCache[next]);
        int8_t s3 = static_cast<int8_t>(ch.sampleCache[next2]);

        uint32_t frac = ch.samplePosition & 0xFFF;
        int32_t interpolated = cubicInterpolate(s0, s1, s2, s3, frac);

        // Scale interpolated 8-bit-range sample to 16-bit (may exceed +/-128
        // range due to cubic overshoot; the final mix clamp below handles it)
        int32_t sample16 = interpolated * 256;

        // Apply volume (0 = silent, 255 = max)
        int32_t amplified = (sample16 * ch.volume) / 255;

        // Apply pan: pan=0 → full left, pan=128 → center, pan=255 → full right
        int32_t panGain = right ? ch.pan : (255 - ch.pan);
        mixed += (amplified * panGain) / 255;
    }

    if (mixed > 32767)  mixed = 32767;
    if (mixed < -32768) mixed = -32768;
    return static_cast<int16_t>(mixed);
}

int16_t APU::getMixedSampleLeft()  { return mixChannels(false); }
int16_t APU::getMixedSampleRight() { return mixChannels(true);  }

void APU::processChannel(int channel) {
    auto& ch = mmpChannels[channel];

    // Load sample data from SST on first use
    if (ch.sampleCache.empty() &&
        ch.sampleDataAddress >= SST_BASE &&
        ch.sampleDataAddress < SST_BASE + STL_SIZE) {

        uint16_t blockAddr = ch.sampleDataAddress;
        bool finalBlock = false;

        while (!finalBlock && blockAddr < SST_BASE + SST_SIZE) {
            // SST block header byte 1: upper nybble = config (W bit = final block), lower = loop start
            uint8_t header1 = readByte(blockAddr + 1);
            finalBlock = ((header1 >> 4) & 0x04) != 0;

            for (int i = 0; i < 12; i++)
                ch.sampleCache.push_back(readByte(blockAddr + 4 + i));

            blockAddr += 16;
        }
    }
}

// Catmull-Rom cubic spline through s0..s3, interpolating between s1 and s2.
// frac is Q12 fixed-point (0..4095) position between s1 (frac=0) and s2 (frac=4096).
int16_t APU::cubicInterpolate(int8_t s0, int8_t s1, int8_t s2, int8_t s3, uint32_t frac) {
    float t = static_cast<float>(frac) / 4096.0f;
    float a0 = -0.5f * s0 + 1.5f * s1 - 1.5f * s2 + 0.5f * s3;
    float a1 = static_cast<float>(s0) - 2.5f * s1 + 2.0f * s2 - 0.5f * s3;
    float a2 = -0.5f * s0 + 0.5f * s2;
    float a3 = s1;
    return static_cast<int16_t>(((a0 * t + a1) * t + a2) * t + a3);
}
