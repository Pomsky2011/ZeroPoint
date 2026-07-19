#include "ppu.h"
#include "display.h"
#include <cstring>
#include <iostream>

namespace ZeroPoint {

// --- PPU instruction cycle-cost model ---------------------------------------
// The PPU issues one instruction at a time, but instructions are not all
// single-cycle on real silicon. These costs model that: a register/ALU datapath
// op is 1 cycle, multi-step arithmetic and pipeline-flushing branches cost a
// few, and memory-streaming ops (palette loads, the TILEDRAW blitter) cost
// roughly one bus cycle per element they touch. After issuing, tick() stalls
// the PPU for (cost - 1) cycles so the rest of the system sees realistic pacing.
// All values are tunable knobs for the fantasy hardware, not measured silicon.
namespace {
constexpr uint16_t CYC_BASE          = 1;   // register/ALU datapath op
constexpr uint16_t CYC_MUL           = 4;   // 16-bit multiply
constexpr uint16_t CYC_DIV           = 16;  // 16-bit integer divide (iterative)
constexpr uint16_t CYC_BRANCH_TAKEN  = 3;   // taken branch: instruction refetch
constexpr uint16_t CYC_BRANCH_NOT    = 1;   // untaken branch: no penalty
constexpr uint16_t CYC_MEM           = 3;   // single word load/store (MOV/MOVDP)
constexpr uint16_t CYC_PRESET_E      = 2;   // preset-E: extra decode stage
constexpr uint16_t CYC_PRESET_F      = 2;   // preset-F default: extra decode stage
constexpr uint16_t CYC_INT_ENTRY     = 4;   // interrupt entry: push PC + vector
constexpr uint16_t CYC_CALL          = 4;   // CALL: push return address + table lookup

// Memory-streaming ops: base + one bus cycle per element touched.
constexpr uint16_t CYC_PAL_BASE      = 2;
constexpr uint16_t CYC_PAL_PER_ENTRY = 1;   // per palette word streamed from DP
constexpr uint16_t CYC_CLRTILE       = 8;   // bulk tile-RAM clear (fixed-function)
constexpr uint16_t CYC_CLRPAL        = 8;   // bulk palette reset

// TILEDRAW blitter: setup + per-pixel bus cost over the 8x8 (64 px) tile.
// Opaque pixel = read src + write dst; blended = read src + read dst + write dst.
constexpr uint16_t CYC_TILE_BASE     = 8;
constexpr uint16_t CYC_TILE_PX       = 4;   // opaque pixel
constexpr uint16_t CYC_TILE_PX_BLEND = 6;   // blended pixel (extra dst read)
constexpr uint16_t TILE_PIXELS       = 64;
} // namespace

PPU::PPU()
    : executionPointer(0)
    , state(PPUState::WaitingForStart)
    , vblank(false)
    , hblank(false)
    , prevVblank(false)
    , prevHblank(false)
    , vblankPending(false)
    , hblankPending(false)
    , cpuIrqPending(false)
    , cpuIrqVector(0)
    , regBankX(0)
    , regBankY(0)
    , cycleCounter(0)
    , stallCycles(0)
    , instrCycles(CYC_BASE)
    , display(nullptr)
    , currentTileId(0)
    , currentTileBank(0)
    , currentTileMode(0)
{
    reset();
}

PPU::~PPU() {
}

void PPU::reset() {
    // Clear all registers (including R59/R60 interrupt addresses)
    registers.fill(0);
    // SP defaults into low RAM rather than 0: address 0 is the program's own
    // entry point, so a push before firmware explicitly sets up its stack
    // would otherwise corrupt the code it's about to execute.
    registers[REG_SP] = 0x8000;

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
    stallCycles = 0;
    instrCycles = CYC_BASE;

    vblank = false;
    hblank = false;
    prevVblank = false;
    prevHblank = false;
    vblankPending = false;
    hblankPending = false;
    cpuIrqPending = false;
    cpuIrqVector = 0;
    regBankX = 0;
    regBankY = 0;
    callTable.fill(0);

    // Reset tile system
    for (auto& bank : tileStorage) {
        for (auto& tile : bank) {
            tile.fill(0);
        }
    }
    currentTileId = 0;
    currentTileBank = 0;
    currentTileMode = 0;
    for (auto& ch : blitChannels) {
        ch = BlitChannel();
    }

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

    // Resync the Display to these defaults. A no-op the first time this runs
    // (during PPU construction, before System has wired up the display
    // pointer via setDisplay()); on every later reset/hot-swap it prevents
    // the Display from being stuck in whatever rolling/color mode a prior
    // $00F0 write left it in.
    applyVOCRenderMode();
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

void PPU::latchInterruptEdges() {
    // Edge-detect the blank lines every cycle - only the 0->1 transition arms
    // an interrupt. This must run every cycle, including while stallCycles>0,
    // not just at instruction boundaries: a blank pulse can be shorter than a
    // single stalled instruction (PALETTE16/INTDIV/etc. inside an ISR body can
    // easily run 20-50+ cycles), so sampling only at boundaries could see the
    // line go high and back low again entirely within one stall and never
    // notice the pulse - silently dropping that interrupt. The *sticky*
    // vblankPending/hblankPending latches (set here, cleared in
    // serviceInterrupts()) are what actually survive until the next boundary.
    bool vblankEdge = vblank && !prevVblank;
    bool hblankEdge = hblank && !prevHblank;
    prevVblank = vblank;
    prevHblank = hblank;
    if (vblankEdge) vblankPending = true;
    if (hblankEdge) hblankPending = true;
}

bool PPU::serviceInterrupts() {
    // Highest priority: a CPU/host-raised interrupt (e.g. "your code is about to
    // be swapped"). One-shot, fires regardless of blanking. Same upward-growing
    // push convention as the blank interrupts so RET stays balanced.
    if (cpuIrqPending) [[unlikely]] {
        cpuIrqPending = false;
        pushReturnAddress(executionPointer);
        executionPointer = cpuIrqVector;
        stallCycles = CYC_INT_ENTRY - 1;
        return true;
    }

    // Check for VBlank interrupt (R59 + VOC bit 4) - optimized with branch hints
    if (vblankPending) [[unlikely]] {
        vblankPending = false;
        bool vblankIntEnabled = (vocRegisters.renderModeControl & VOC_VBLANK_INT) != 0;
        if (vblankIntEnabled && registers[REG_VBLANK_INT] != 0) [[likely]] {
            // Push return address. The stack grows UPWARD to match the
            // ppuasm PUSH/RET shorthands (PUSH does INC SP; RET pops with
            // DEC SP), so an ISR that ends in RET doesn't leak SP.
            pushReturnAddress(executionPointer);

            // Jump to interrupt handler
            executionPointer = registers[REG_VBLANK_INT];
            stallCycles = CYC_INT_ENTRY - 1;
            return true;
        }
    }

    // Check for HBlank interrupt (R60 + VOC bit 5) - optimized with branch hints
    if (hblankPending) [[unlikely]] {
        hblankPending = false;
        bool hblankIntEnabled = (vocRegisters.renderModeControl & VOC_HBLANK_INT) != 0;
        if (hblankIntEnabled && registers[REG_HBLANK_INT] != 0) [[likely]] {
            // Push return address. Stack grows UPWARD to match the ppuasm
            // PUSH/RET shorthands, so an ISR ending in RET stays balanced
            // (see V-blank push above).
            pushReturnAddress(executionPointer);

            // Jump to interrupt handler
            executionPointer = registers[REG_HBLANK_INT];
            stallCycles = CYC_INT_ENTRY - 1;
            return true;
        }
    }

    return false;
}

void PPU::tick() {
    if (state != PPUState::Running) [[unlikely]] {
        return;
    }

    // Blit channels run concurrently with the main instruction stream (see
    // BlitChannel's comment in ppu.h) - advance them every cycle regardless
    // of whether the PPU itself is stalled on something else.
    advanceBlitChannels(1);

    // Latch blank-line edges every cycle, even mid-stall (see
    // latchInterruptEdges()'s comment) - a short blank pulse could otherwise
    // toggle on and back off entirely within one stalled instruction and
    // never get noticed.
    latchInterruptEdges();

    // Busy finishing a multi-cycle instruction: burn one cycle and wait. This
    // is what makes MUL/DIV, branches, and palette loads actually occupy the
    // PPU for their full duration. Actually *servicing* a pending interrupt
    // (pushing PC, jumping) still only happens at an instruction boundary.
    if (stallCycles > 0) {
        stallCycles--;
        return;
    }

    // At an instruction boundary: an interrupt may pre-empt the next instruction.
    if (serviceInterrupts()) {
        return;
    }

    // Issue one instruction: apply its effect now, then stall for the rest of
    // its cycle cost. executeInstruction() sets instrCycles for the op it ran.
    executeInstruction();
    stallCycles = instrCycles - 1;
}

uint32_t PPU::runBatch(uint32_t cycles) {
    uint32_t remaining = cycles;
    while (remaining > 0 && state == PPUState::Running) {
        // Collapse a multi-cycle stall in one step instead of spinning one
        // iteration per cycle. Blit channels must see the same elapsed time
        // as tick() would have given them one cycle at a time, so advance
        // them by the same `consumed` amount here. Latching the edge once per
        // collapsed segment (rather than once per cycle within it) is safe
        // for runBatch's current caller: vblank/hblank are driven externally
        // by Display::tick(), which nothing inside this loop invokes, so the
        // line can't actually change mid-collapse the way it can across
        // System's per-cycle tick()/Display::tick() interleaving - see
        // tick()'s call to latchInterruptEdges() for the case that matters.
        if (stallCycles > 0) {
            uint32_t consumed = stallCycles < remaining ? stallCycles : remaining;
            stallCycles -= consumed;
            remaining -= consumed;
            advanceBlitChannels(consumed);
            latchInterruptEdges();
            continue;
        }

        latchInterruptEdges();

        // Instruction boundary: same interrupt/issue logic as tick(), one cycle.
        if (serviceInterrupts()) {
            remaining--;
            advanceBlitChannels(1);
            continue;
        }

        executeInstruction();
        stallCycles = instrCycles - 1;
        remaining--;
        advanceBlitChannels(1);
    }
    return cycles - remaining;
}

void PPU::executeInstruction() {
    // OPTIMIZED: Inline fetch and decode
    // Fetch instruction (big-endian, 2 bytes)
    uint16_t ep = executionPointer;
    uint16_t instruction = (memory[ep] << 8) | memory[ep + 1];
    executionPointer = ep + 2;

    // Decode: 4-bit opcode, 12-bit operand
    uint8_t opcode = instruction >> 12;  // No need to mask, only 4 bits
    uint16_t operand = instruction & 0x0FFF;

    // Pre-extract common operand patterns (for most instructions)
    // This avoids re-extracting in each case
    uint8_t regX = (operand >> 6) & 0x3F;
    uint8_t regY = operand & 0x3F;

    // Default cost: a single-cycle datapath op. Multi-cycle ops (MUL/DIV,
    // branches, presets) override this below; tick() reads it after we return.
    instrCycles = CYC_BASE;

    switch (static_cast<PPUOpcode>(opcode)) {
        case PPUOpcode::DEFCALL: {
            // defcall X, Y - register registers[Y] as the entry point for call
            // slot registers[X] (masked to the table's 8-bit range, matching
            // CALL's 8-bit operand). Encoding: 0000 XXXXXX YYYYYY.
            callTable[registers[regX] & 0xFF] = registers[regY];
            break;
        }

        case PPUOpcode::MOVXP_NOP: {
            // Encoding: 0001 D 00000 XXXXXX
            // D=0: movxp X - copy execution pointer + 2 to register X
            // D=1: nop - do nothing
            if ((operand & 0x800) == 0) [[likely]] {  // Likely MOVXP
                // MOVXP X - copy EP+2 to register X
                registers[regY] = executionPointer + 2;  // Uses regY (lower 6 bits)
            }
            // If bit 11 set, it's NOP - do nothing
            break;
        }

        case PPUOpcode::SWAPREG: {
            // swapreg X Y - swap registers X and Y (regX, regY pre-extracted)
            uint16_t temp = registers[regX];
            registers[regX] = registers[regY];
            registers[regY] = temp;
            break;
        }

        case PPUOpcode::CLR: {
            // clr X - clear register X (regY = lower 6 bits)
            registers[regY] = 0;
            break;
        }

        case PPUOpcode::CMP: {
            // cmp X Y - compare X and Y (regX, regY pre-extracted)
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
            bool taken = isGreater ? flags.greater : flags.zero;
            if (taken) {
                executionPointer = registers[REG_PC];
            }
            instrCycles = taken ? CYC_BRANCH_TAKEN : CYC_BRANCH_NOT;
            break;
        }

        case PPUOpcode::JUMP_NZG: {
            // Bit 11: 0 = jnz, 1 = jng (jml)
            bool isNotGreater = (operand & 0x800) != 0;
            // jng/jml: taken if not greater (<=); jnz: taken if not zero.
            bool taken = isNotGreater ? !flags.greater : !flags.zero;
            if (taken) {
                executionPointer = registers[REG_PC];
            }
            instrCycles = taken ? CYC_BRANCH_TAKEN : CYC_BRANCH_NOT;
            break;
        }

        case PPUOpcode::INC: {
            // inc X - increment register X (regY = lower 6 bits)
            registers[regY]++;
            break;
        }

        case PPUOpcode::DEC: {
            // dec X - decrement register X (regY = lower 6 bits)
            registers[regY]--;
            break;
        }

        case PPUOpcode::ADD: {
            // add X Y - X = X + Y (regX, regY pre-extracted)
            registers[regX] += registers[regY];
            break;
        }

        case PPUOpcode::SUB: {
            // sub X Y - X = X - Y (regX, regY pre-extracted)
            registers[regX] -= registers[regY];
            break;
        }

        case PPUOpcode::MUL: {
            // mul X Y - X = X * Y (regX, regY pre-extracted)
            registers[regX] *= registers[regY];
            instrCycles = CYC_MUL;
            break;
        }

        case PPUOpcode::INTDIV: {
            // intdiv X Y - X = X / Y (regX, regY pre-extracted)
            if (registers[regY] != 0) [[likely]] {  // Division by zero is unlikely
                registers[regX] /= registers[regY];
            }
            instrCycles = CYC_DIV;
            break;
        }

        case PPUOpcode::PRESET_E: {
            // preset E Z W - immediate/byte operations
            uint8_t subopcode = (operand >> 10) & 0x03;  // 2 bits for sub-opcode (bits 11-10)
            uint16_t suboperand = operand & 0x3FF;        // 10 bits for operand (bits 9-0)
            instrCycles = CYC_PRESET_E;  // extra decode stage; handler may override
            executePresetE(subopcode, suboperand);
            break;
        }

        case PPUOpcode::PRESET_F: {
            // preset F Z W - extended instructions
            uint8_t subopcode = (operand >> 8) & 0x0F;  // 4 bits for sub-opcode
            uint8_t suboperand = operand & 0xFF;
            instrCycles = CYC_PRESET_F;  // extra decode stage; handler may override
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
            // Encoding: 10 TT TT XXXXXX (T1 at bits 9-8, T2 at bits 7-6, dest at
            // bits 5-0 of the 10-bit suboperand - matches ppuasm.c's encoder:
            // suboperand = (T1<<8)|(T2<<6)|(dest&0x3F). This used to read T1/T2
            // two bits too low (>>6/>>4), which both silently corrupted T1 and
            // made dest's low 2 bits alias T2's field - see ucode.txt's changelog.
            uint8_t targetReg1 = (operand >> 8) & 0x03;    // First target (MSB source)
            uint8_t targetReg2 = (operand >> 6) & 0x03;    // Second target (LSB source)
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
            // setpos X, Y - set tile-draw position from two 4-bit register
            // IDs, extended via SETREGBANK the same way CPREG extends its
            // operands. Encoding: 0000 XXXX YYYY.
            // Writes into the same memory-mapped position cells TILEDRAW
            // reads ($0200-$0203), so SETPOS and a raw poke to that region
            // are interchangeable.
            uint8_t regX = ((operand >> 4) & 0x0F) | (regBankX << 4);
            uint8_t regY = (operand & 0x0F) | (regBankY << 4);
            uint16_t x = registers[regX];
            uint16_t y = registers[regY];
            memory[0x0200] = x & 0xFF;
            memory[0x0201] = (x >> 8) & 0xFF;
            memory[0x0202] = y & 0xFF;
            memory[0x0203] = (y >> 8) & 0xFF;
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
            instrCycles = CYC_MEM;  // word store to (DP)
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
            instrCycles = CYC_PAL_BASE + 16 * CYC_PAL_PER_ENTRY;  // stream 16 words
            break;
        }

        case PresetFOpcode::PALETTE256_LOAD: {
            // 256cpaletteload(dp) - load 256-color palette from DP
            loadPalette256();
            instrCycles = CYC_PAL_BASE + 256 * CYC_PAL_PER_ENTRY;  // stream 256 words
            break;
        }

        case PresetFOpcode::JMR: {
            // jmr (pc) - jump relative to PC
            executionPointer = registers[REG_PC];
            instrCycles = CYC_BRANCH_TAKEN;  // unconditional jump: always taken
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
            instrCycles = CYC_MEM;  // word load from (DP)
            break;
        }

        case PresetFOpcode::SETREGBANK: {
            // setregbank Z W - set register banks for setpos
            regBankX = (operand >> 4) & 0x03;
            regBankY = (operand >> 2) & 0x03;
            break;
        }

        case PresetFOpcode::CLRTILE: {
            // clrtile - clear tile storage (all banks)
            for (auto& bank : tileStorage) {
                for (auto& tile : bank) {
                    tile.fill(0);
                }
            }
            instrCycles = CYC_CLRTILE;  // bulk fixed-function clear
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
            instrCycles = CYC_CLRPAL;  // bulk fixed-function reset
            break;
        }

        case PresetFOpcode::TILEDRAW: {
            // tiledraw - snapshot the current draw state (position, tile,
            // mode, blend settings) into a free blit channel and dispatch
            // it to run concurrently - see BlitChannel's comment in ppu.h.
            // The actual pixel writes happen in executeBlit() when the
            // channel's own cyclesRemaining reaches 0, not here.
            int freeChannel = -1;
            for (size_t i = 0; i < NUM_BLIT_CHANNELS; i++) {
                if (!blitChannels[i].busy) {
                    freeChannel = static_cast<int>(i);
                    break;
                }
            }

            uint32_t blitCost = CYC_TILE_BASE +
                                 TILE_PIXELS * (tileBlendMode != 0 ? CYC_TILE_PX_BLEND
                                                                   : CYC_TILE_PX);

            if (freeChannel >= 0) {
                BlitChannel& ch = blitChannels[freeChannel];
                ch.busy = true;
                ch.cyclesRemaining = blitCost;
                ch.x = memory[0x0200] | (memory[0x0201] << 8);
                ch.y = memory[0x0202] | (memory[0x0203] << 8);
                ch.tileBank = currentTileBank;
                ch.tileId = currentTileId;
                ch.tileMode = currentTileMode;
                ch.blendMode = tileBlendMode;
                ch.translucency = tileTranslucency;

                // Dispatch-only cost: the PPU is free to issue its next
                // instruction (e.g. scheduling another TILEDRAW into a
                // different channel) while this one runs in the background.
                instrCycles = CYC_TILE_BASE;
            } else {
                // All channels busy: fall back to the old fully-synchronous
                // blit rather than drop this draw or corrupt an in-flight
                // one. Never worse than pre-concurrency behavior, and with
                // sane pacing (channels free up faster than a well-tuned
                // scheduler re-issues TILEDRAW) this path shouldn't trigger.
                BlitChannel synchronous;
                synchronous.x = memory[0x0200] | (memory[0x0201] << 8);
                synchronous.y = memory[0x0202] | (memory[0x0203] << 8);
                synchronous.tileBank = currentTileBank;
                synchronous.tileId = currentTileId;
                synchronous.tileMode = currentTileMode;
                synchronous.blendMode = tileBlendMode;
                synchronous.translucency = tileTranslucency;
                executeBlit(synchronous);
                instrCycles = blitCost;
            }
            break;
        }

        case PresetFOpcode::SETTILEBANK: {
            // settilebank X - select tile bank from register X. Same
            // encoding shape as SETDP/MOVDP (0001 XXXXXX --): a plain
            // register operand in bits 2-7, low 2 bits unused. Widens total
            // addressable tiles past the 8-bit SETTILE ID field by paging,
            // not by changing that field's width - see tileStorage's comment.
            uint8_t regX = (operand >> 2) & 0x3F;
            currentTileBank = static_cast<uint8_t>(registers[regX] % MAX_TILE_BANKS);
            break;
        }

        case PresetFOpcode::CALL: {
            // call X - call the function registered under slot X (via DEFCALL).
            // Pushes the return address the same way blank/CPU interrupt entry
            // does (SP grows upward), so the ppuasm RET shorthand
            // (SETDP SP; DEC SP; DEC SP; MOV PC; JMR) can pop it back.
            pushReturnAddress(executionPointer);
            executionPointer = callTable[operand];
            instrCycles = CYC_CALL;
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

// updateFlags and clearFlags are now inline in ppu.h

uint8_t PPU::handleMemoryRead(uint16_t address) const {
    // Current scanline (read-only), 16-bit little-endian at $00E0-$00E1. Lets
    // beam-synced firmware (e.g. an H-blank ISR) know which line the display is
    // on, so it can pick the correct rolling-buffer slot to paint. Without this
    // the PPU only exposes blank/active status (GBLS), which isn't enough.
    if ((address == 0x00E0 || address == 0x00E1) && display != nullptr) {
        int sl = display->getCurrentScanline();
        return (address == 0x00E0) ? (sl & 0xFF) : ((sl >> 8) & 0xFF);
    }

    // VOC registers: 0x00F0-0x00FF
    if (address >= 0x00F0 && address <= 0x00FF) {
        return handleVOCRead(address);
    }

    // Framebuffer region: 0xE000-0xFFFF (8 KiB max)
    if (address >= 0xE000 && display != nullptr) {
        uint16_t offset = address - 0xE000;

        // Rolling buffer is always 8 KiB ($E000-$FFFF) in both modes.
        if (offset < 0x2000) {
            const uint8_t* fbBytes = reinterpret_cast<const uint8_t*>(display->getFramebuffer16());
            return fbBytes[offset];
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
    if (address >= 0xE000 && display != nullptr) {
        uint16_t offset = address - 0xE000;

        // Rolling buffer is always 8 KiB ($E000-$FFFF) in both modes.
        if (offset < 0x2000) {
            uint8_t* fbBytes = reinterpret_cast<uint8_t*>(display->getFramebuffer16());
            fbBytes[offset] = value;
        }
        return; // Don't also write to regular memory
    }

    // Memory-mapped display I/O region: 0x0100-0x010B (word-aligned for MOVDP)
    // 0x0100-0x0101: X position (16-bit)
    // 0x0102-0x0103: Y position (16-bit)
    // 0x0104-0x0105: Palette index (16-bit, low byte used) - see VOC_PALETTE_MODE
    // 0x0106-0x0109: unused
    // 0x010A-0x010B: (16-bit, low byte used) - writing triggers the pixel draw

    if (address == 0x010A && display != nullptr) {
        // Extract position from memory (little-endian 16-bit values)
        uint16_t x = memory[0x0100] | (memory[0x0101] << 8);
        uint16_t y = memory[0x0102] | (memory[0x0103] << 8);

        // The pixel-draw port is palette-indexed, like tile drawing. VOC bit 3
        // (VOC_PALETTE_MODE) picks which table the index in $0104 selects.
        uint32_t color;
        if ((vocRegisters.renderModeControl & VOC_PALETTE_MODE) != 0) {
            color = palette256[memory[0x0104]];
        } else {
            color = expandPalette16(palette16[memory[0x0104] & 0x0F]);
        }

        // Draw pixel to display
        display->setPixel32(x, y, color);
    }

    // Tile data region: 0x0300-0x033F (64 bytes for direct tile storage access)
    // Can be written to directly for tile definition
    if (address >= 0x0300 && address <= 0x033F) {
        uint8_t offset = address - 0x0300;
        if (offset < TILE_SIZE && currentTileId < MAX_TILES && currentTileBank < MAX_TILE_BANKS) {
            tileStorage[currentTileBank][currentTileId][offset] = value;
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

    // Apply rolling mode setting (bit 6): 0=block, 1=single.
    bool singleMode = (vocRegisters.renderModeControl & VOC_ROLLING_MODE) != 0;
    display->setRollingMode(!singleMode);

    // Palette mode (bit 3) is applied at the $0100-$010B pixel-draw port
    // (handleMemoryWrite), which reads vocRegisters.renderModeControl
    // directly rather than caching it here.
}

void PPU::applyVOCReset() {
    // Reset PPU state but preserve VRAM (memory)
    // Don't clear memory - only registers and state
    registers.fill(0);
    registers[REG_SP] = 0x8000;  // see PPU::reset() - avoid a stray push corrupting code at 0
    targetRegisters.fill(0);
    targetBytes.fill(0);
    executionPointer = 0;
    state = PPUState::WaitingForStart;
    flags = PPUFlags();
    cycleCounter = 0;
    stallCycles = 0;
    instrCycles = CYC_BASE;
    regBankX = 0;
    regBankY = 0;
    callTable.fill(0);

    // Reset tile system
    for (auto& bank : tileStorage) {
        for (auto& tile : bank) {
            tile.fill(0);
        }
    }
    currentTileId = 0;
    currentTileBank = 0;
    currentTileMode = 0;
    for (auto& ch : blitChannels) {
        ch = BlitChannel();
    }

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

    // Read 256 palette entries (32-bit format: RRGGBBAA, b0=R first in memory)
    for (int i = 0; i < 256; i++) {
        uint8_t b0 = memory[addr + i * 4];
        uint8_t b1 = memory[addr + i * 4 + 1];
        uint8_t b2 = memory[addr + i * 4 + 2];
        uint8_t b3 = memory[addr + i * 4 + 3];
        palette256[i] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
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

    uint8_t blendR, blendG, blendB, outA;

    // Apply translucency alpha (0-15 range mapped to 0-255)
    uint16_t alphaFull = (alpha << 4) | alpha;  // 0-15 -> 0-255

    switch (mode) {
        case 0:  // Multiply (darken)
            blendR = (srcR * dstR) >> 8;
            blendG = (srcG * dstG) >> 8;
            blendB = (srcB * dstB) >> 8;
            outA = (srcA * alphaFull) >> 8;
            break;

        case 1:  // Average (50/50 blend)
            blendR = (srcR + dstR) >> 1;
            blendG = (srcG + dstG) >> 1;
            blendB = (srcB + dstB) >> 1;
            outA = (srcA * alphaFull) >> 8;
            break;

        case 2:  // Subtract (color subtraction)
            blendR = (dstR > srcR) ? (dstR - srcR) : 0;
            blendG = (dstG > srcG) ? (dstG - srcG) : 0;
            blendB = (dstB > srcB) ? (dstB - srcB) : 0;
            outA = (srcA * alphaFull) >> 8;
            break;

        case 3:  // Add (lighten/additive)
            blendR = std::min(255, srcR + dstR);
            blendG = std::min(255, srcG + dstG);
            blendB = std::min(255, srcB + dstB);
            outA = (srcA * alphaFull) >> 8;
            break;

        default:
            return src;
    }

    // Alpha blend the mode-specific result over the destination pixel.
    uint16_t blend = alphaFull;
    uint8_t outR = ((blendR * blend) + (dstR * (255 - blend))) >> 8;
    uint8_t outG = ((blendG * blend) + (dstG * (255 - blend))) >> 8;
    uint8_t outB = ((blendB * blend) + (dstB * (255 - blend))) >> 8;

    return (outR << 24) | (outG << 16) | (outB << 8) | outA;
}

void PPU::advanceBlitChannels(uint32_t elapsed) {
    for (auto& ch : blitChannels) {
        if (!ch.busy) continue;
        if (elapsed >= ch.cyclesRemaining) {
            ch.cyclesRemaining = 0;
        } else {
            ch.cyclesRemaining -= elapsed;
        }
        if (ch.cyclesRemaining == 0) {
            executeBlit(ch);
            ch.busy = false;
        }
    }
}

void PPU::executeBlit(const BlitChannel& channel) {
    // The pixel loop TILEDRAW used to run inline and synchronously - now
    // driven off a channel's snapshotted state (see TILEDRAW's dispatch
    // case) so it can complete long after the instruction that issued it.
    if (display == nullptr) return;

    uint16_t x = channel.x;
    uint16_t y = channel.y;

    // Tile mode: 0=16BBGR 4bpp, 1=32RGBA 4bpp, 2=16BBGR 8bpp, 3=32RGBA 8bpp
    bool is4bpp = (channel.tileMode & 0x02) == 0;
    bool is32bit = (channel.tileMode & 0x01) != 0;

    for (int ty = 0; ty < 8; ty++) {
        for (int tx = 0; tx < 8; tx++) {
            uint32_t color = 0;

            if (is4bpp) {
                // 4bpp mode: 2 pixels per byte, use palette lookup
                uint8_t byteIndex = (ty * 8 + tx) / 2;
                uint8_t pixelByte = tileStorage[channel.tileBank][channel.tileId][byteIndex];

                // Get 4-bit palette index (high nibble for even pixels, low nibble for odd)
                uint8_t paletteIndex = (tx & 1) ? (pixelByte & 0x0F) : ((pixelByte >> 4) & 0x0F);

                // Both is32bit and !is32bit draw from the 16-color
                // palette; the entry is always 16-bit, so expansion
                // to 32-bit RGBA is identical either way.
                color = expandPalette16(palette16[paletteIndex]);
            } else {
                // 8bpp mode: 1 pixel per byte, use palette lookup
                uint8_t paletteIndex = tileStorage[channel.tileBank][channel.tileId][ty * 8 + tx];

                if (is32bit) {
                    // 32-bit RGBA mode with 256-color palette
                    color = palette256[paletteIndex];
                } else {
                    // 16-bit mode with 256-color palette
                    // Use lower 16 entries or convert to 16-bit format
                    if (paletteIndex < 16) {
                        color = expandPalette16(palette16[paletteIndex]);
                    } else {
                        // Fallback: grayscale for palette indices >= 16
                        color = (paletteIndex << 24) | (paletteIndex << 16) |
                               (paletteIndex << 8) | 0xFF;
                    }
                }
            }

            // Apply translucency to the color
            color = applyTranslucency(color, channel.tileId);

            // Apply blending if mode is set and we have a non-zero blend mode
            if (channel.blendMode != 0) {
                // Calculate framebuffer address for this pixel
                uint16_t pixelX = x + tx;
                uint16_t pixelY = y + ty;

                // Read existing pixel from framebuffer
                uint32_t dstColor = 0;

                if (display->getRenderMode() == RenderMode::RGBA32) {
                    // 32-bit: 4 bytes/pixel. The rolling buffer only
                    // holds 8 scanlines, so index by (pixelY % 8) to
                    // stay within the $E000-$FFFF window.
                    uint16_t row = pixelY % FB_SCANLINES_32BIT;
                    uint16_t fbAddr = 0xE000 + (row * 1024) + (pixelX * 4);

                    // Read 4 bytes (RGBA) from framebuffer
                    if (fbAddr + 3 <= 0xFFFF) {
                        uint8_t r = handleMemoryRead(fbAddr + 0);
                        uint8_t g = handleMemoryRead(fbAddr + 1);
                        uint8_t b = handleMemoryRead(fbAddr + 2);
                        uint8_t a = handleMemoryRead(fbAddr + 3);
                        dstColor = (r << 24) | (g << 16) | (b << 8) | a;
                    }
                } else {
                    // 16-bit: 2 bytes/pixel, 16-scanline rolling window;
                    // index by (pixelY % 16) within the 8 KiB buffer.
                    uint16_t row = pixelY % FB_SCANLINES_16BIT;
                    uint16_t fbAddr = 0xE000 + (row * 512) + (pixelX * 2);

                    // Read 2 bytes (16-bit BGR) from framebuffer
                    if (fbAddr + 1 <= 0xFFFF) {
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
                uint8_t tileSlot = channel.tileId & 0x03;
                uint8_t alpha = channel.translucency[tileSlot];

                if (alpha > 0 && dstColor != 0) {
                    color = blendColors(color, dstColor, channel.blendMode, alpha);
                }
            }

            display->setPixel32(x + tx, y + ty, color);
        }
    }
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
