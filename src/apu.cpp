#include "apu.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <cmath>

APU::APU()
    : pc(0), rp(0), dp(0), db(0), bf(false),
      romBank(0), ioBank(0),
      cycleCount(0), instructionCount(0), halted(false),
      reverbFlagsLeft(0), reverbFlagsRight(0),
      echoFlagsLeft(0), echoFlagsRight(0),
      gaussianInterpolationLeft(false), gaussianInterpolationRight(false),
      echoBufferPosition(0)
{
    reset();
}

APU::~APU() {
}

void APU::reset() {
    memory.fill(0);
    arom.fill(0);
    registers.fill(0);

    pc = 0x8000;  // Start at BIOS
    rp = 0x80;
    dp = 0x00;
    db = 0x00;
    bf = false;

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
    for (auto& channel : mmpChannels) {
        channel.pitch = 0x1000;  // 1.0× speed
        channel.volume = 128;
        channel.stlAddress = 0;
        channel.samplePosition = 0;
        channel.active = false;
        channel.sampleDataAddress = 0;
        channel.loopAddress = 0;
        channel.sampleCache.clear();
    }

    reverbFlagsLeft = 0;
    reverbFlagsRight = 0;
    echoFlagsLeft = 0;
    echoFlagsRight = 0;
    gaussianInterpolationLeft = false;
    gaussianInterpolationRight = false;

    echoDelays.fill(0);

    // Allocate echo buffers (1 second at 48kHz)
    leftEchoBuffer.resize(48000, 0);
    rightEchoBuffer.resize(48000, 0);
    echoBufferPosition = 0;
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

uint8_t APU::readByte(uint16_t address) {
    // AROM banking
    if (address >= AROM_BASE && address < 0x10000) {
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
        (address >= AROM_BASE && address < 0x10000)) {
        return;
    }

    // MMP registers (special handling for writes)
    if (address >= MMP_BASE && address < MMP_BASE + MMP_SIZE) {
        memory[address] = value;

        // Update MMP channel state when STL address is written
        // Channels 0-15 left ear: STL at $0054-$0073
        // Channels 16-31 right ear: STL at $00D4-$00F3
        if ((address >= 0x0054 && address < 0x0074) ||
            (address >= 0x00D4 && address < 0x00F4)) {

            int baseChannel = (address >= 0x00D4) ? 16 : 0;
            int channelOffset = (address >= 0x00D4) ? (address - 0x00D4) : (address - 0x0054);
            int channel = baseChannel + (channelOffset / 2);

            if (channel < 32) {
                // STL address was written, load sample data
                uint16_t stlAddr = readWord(address & ~1);
                mmpChannels[channel].stlAddress = stlAddr;

                if (stlAddr == 0) {
                    mmpChannels[channel].active = false;
                } else {
                    mmpChannels[channel].active = true;
                    mmpChannels[channel].samplePosition = 0;

                    // Load sample info from STL
                    if (stlAddr >= STL_BASE && stlAddr < STL_BASE + STL_SIZE) {
                        mmpChannels[channel].sampleDataAddress = readWord(stlAddr);
                        mmpChannels[channel].loopAddress = readWord(stlAddr + 2);
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
    if (reg < 256) {
        return registers[reg];
    }
    return 0;
}

void APU::setRegister(uint8_t reg, uint8_t value) {
    if (reg < 256) {
        registers[reg] = value;
    }
}

void APU::setROMBank(uint16_t bank) {
    if (bank < AROM_BANKS) {
        romBank = bank;
    }
}

void APU::setIOBank(uint16_t bank) {
    ioBank = bank;
}

uint8_t APU::readDEF88186Input(uint16_t offset) {
    if (offset < DEF88186_INPUT_SIZE) {
        return memory[DEF88186_INPUT_BASE + offset];
    }
    return 0;
}

void APU::writeDEF88186Input(uint16_t offset, uint8_t value) {
    if (offset < DEF88186_INPUT_SIZE) {
        memory[DEF88186_INPUT_BASE + offset] = value;
    }
}

uint8_t APU::readDEF88186Output(uint16_t offset) {
    if (offset < DEF88186_OUTPUT_SIZE) {
        return memory[DEF88186_OUTPUT_BASE + offset];
    }
    return 0;
}

void APU::writeDEF88186Output(uint16_t offset, uint8_t value) {
    if (offset < DEF88186_OUTPUT_SIZE) {
        memory[DEF88186_OUTPUT_BASE + offset] = value;
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
        case 0x02: execJNZ(operand); break;
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
        case 0x09: execSBF(operand); break;
        case 0x0A: execSCR(operand); break;
        case 0x0B: {
            // IOO or IOI based on bit 8
            if (operand & 0x100) {
                execIOI(operand);
            } else {
                execIOO(operand);
            }
            break;
        }
        case 0x0C: {
            // ZOR or ZOA based on pattern
            if ((operand & 0x7C0) == 0x080) {  // ZOR pattern
                execZOR(operand);
            } else {  // ZOA
                execZOA(operand);
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
        case 0x10: execIBC(operand); break;
        case 0x11: execRBC(operand); break;
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
        case 0x15: {
            // WRH or WRL based on bit 8
            if (operand & 0x100) {
                execWRL(operand);
            } else {
                execWRH(operand);
            }
            break;
        }
        case 0x16: execCFS(operand); break;
        case 0x17: execCFE(operand); break;
        case 0x18: execCCF(operand); break;
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
    uint8_t reg = (operand >> 9) & 0x01;  // Register X (simplified)
    bool mode = (operand >> 8) & 1;
    uint8_t offset = operand & 0xFF;

    if (registers[reg] != 0) {
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
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    bool toMem = !(operand & 0x100);

    uint8_t result = ~(registers[regX] | registers[regY]);

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
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    bool toMem = !(operand & 0x100);

    uint8_t result = registers[regX] & registers[regY];

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
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    bool toMem = !(operand & 0x100);

    uint8_t result = registers[regX] + registers[regY];

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
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    bool toMem = !(operand & 0x100);

    uint8_t result = registers[regX] - registers[regY];

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
    uint8_t regX = (operand >> 9) & 0x01;
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
    uint8_t regX = (operand >> 9) & 0x01;
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

void APU::execSBF(uint16_t operand) {
    bf = (operand & 0x01) != 0;
}

void APU::execSCR(uint16_t operand) {
    uint8_t reg = (operand >> 8) & 0x01;
    uint8_t value = operand & 0xFF;
    registers[reg] = value;
}

void APU::execIOO(uint16_t operand) {
    // I/O output - implementation specific
    // For now, just a placeholder
}

void APU::execIOI(uint16_t operand) {
    // I/O input - implementation specific
    // For now, just a placeholder
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
    // Break to re-enter - implementation specific
}

void APU::execIBC(uint16_t operand) {
    ioBank = operand & 0x7FF;
}

void APU::execRBC(uint16_t operand) {
    setROMBank(operand & 0x7FF);
}

void APU::execBEQ(uint16_t operand) {
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    uint8_t offset = operand & 0xFF;

    if (registers[regX] == registers[regY]) {
        pc = (rp << 8) | offset;
    }
}

void APU::execBNE(uint16_t operand) {
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    uint8_t offset = operand & 0xFF;

    if (registers[regX] != registers[regY]) {
        pc = (rp << 8) | offset;
    }
}

void APU::execBLT(uint16_t operand) {
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    uint8_t offset = operand & 0xFF;

    if (registers[regX] < registers[regY]) {
        pc = (rp << 8) | offset;
    }
}

void APU::execBGT(uint16_t operand) {
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    uint8_t offset = operand & 0xFF;

    if (registers[regX] > registers[regY]) {
        pc = (rp << 8) | offset;
    }
}

void APU::execSDB(uint16_t operand) {
    db = operand & 0xFF;
}

void APU::execWRH(uint16_t operand) {
    uint8_t value = operand & 0xFF;
    uint16_t address = (dp << 8) | db;
    uint8_t low = readByte(address);
    writeWord(address, (value << 8) | low);
}

void APU::execWRL(uint16_t operand) {
    uint8_t value = operand & 0xFF;
    uint16_t address = (dp << 8) | db;
    uint8_t high = readByte(address + 1);
    writeWord(address, (high << 8) | value);
}

void APU::execCFS(uint16_t operand) {
    uint16_t funcId = operand & 0x7FF;
    if (funcId < 2048) {
        functions[funcId].defined = true;
        functions[funcId].address = pc;
    }
}

void APU::execCFE(uint16_t operand) {
    // Return from function
    if (!callStack.empty()) {
        pc = callStack.back();
        callStack.pop_back();
    }
}

void APU::execCCF(uint16_t operand) {
    uint16_t funcId = operand & 0x7FF;
    if (funcId < 2048 && functions[funcId].defined) {
        callStack.push_back(pc);
        pc = functions[funcId].address;
    }
}

void APU::execCME(uint16_t operand) {
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    bool direction = (operand >> 6) & 1;
    uint8_t offset = operand & 0x3F;

    if (registers[regX] == registers[regY]) {
        if (direction) {
            pc -= (offset * 2);
        } else {
            pc += (offset * 2);
        }
    }
}

void APU::execCMN(uint16_t operand) {
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    bool direction = (operand >> 6) & 1;
    uint8_t offset = operand & 0x3F;

    if (registers[regX] != registers[regY]) {
        if (direction) {
            pc -= (offset * 2);
        } else {
            pc += (offset * 2);
        }
    }
}

void APU::execCMG(uint16_t operand) {
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    bool direction = (operand >> 6) & 1;
    uint8_t offset = operand & 0x3F;

    if (registers[regX] > registers[regY]) {
        if (direction) {
            pc -= (offset * 2);
        } else {
            pc += (offset * 2);
        }
    }
}

void APU::execCML(uint16_t operand) {
    uint8_t regX = (operand >> 9) & 0x01;
    uint8_t regY = (operand >> 8) & 0x01;
    bool direction = (operand >> 6) & 1;
    uint8_t offset = operand & 0x3F;

    if (registers[regX] < registers[regY]) {
        if (direction) {
            pc -= (offset * 2);
        } else {
            pc += (offset * 2);
        }
    }
}

// MMP implementation (simplified for now)

void APU::updateMMP() {
    // This would be called at audio sample rate (e.g., 48kHz)
    // For now, just a placeholder
}

int16_t APU::getMixedSampleLeft() {
    int32_t mixed = 0;

    for (int i = 0; i < 16; i++) {
        if (mmpChannels[i].active && mmpChannels[i].stlAddress != 0) {
            // Simple mixing (real implementation would do resampling, volume, etc.)
            mixed += 0;  // Placeholder
        }
    }

    // Clamp to 16-bit range
    if (mixed > 32767) mixed = 32767;
    if (mixed < -32768) mixed = -32768;

    return static_cast<int16_t>(mixed);
}

int16_t APU::getMixedSampleRight() {
    int32_t mixed = 0;

    for (int i = 16; i < 32; i++) {
        if (mmpChannels[i].active && mmpChannels[i].stlAddress != 0) {
            // Simple mixing
            mixed += 0;  // Placeholder
        }
    }

    // Clamp to 16-bit range
    if (mixed > 32767) mixed = 32767;
    if (mixed < -32768) mixed = -32768;

    return static_cast<int16_t>(mixed);
}

void APU::processChannel(int channel, bool rightEar) {
    // Placeholder for per-channel processing
}

int16_t APU::resampleSample(uint8_t sample, uint16_t pitch) {
    // Convert 8-bit unsigned to 16-bit signed
    int16_t signed_sample = static_cast<int16_t>(sample) - 128;
    signed_sample *= 256;  // Scale to 16-bit range

    // Apply pitch (resampling) - simplified
    // Real implementation would use interpolation

    return signed_sample;
}
