#include "ppu.h"
#include "display.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <type_traits>

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

// Line blitter: setup + one cycle per source pixel (the engine streams the run, nothing is re-decoded per pixel).
constexpr uint16_t CYC_LINE_BASE     = 8;
constexpr uint16_t CYC_LINE_PX       = 1;
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
    , decoded(65536)
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
    for (auto& d : decoded) d.fn = nullptr;

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
    resetBlitMirrors();
    busyBlitChannelCount = 0;
    blitClock = 0;
    blitNextDone = UINT64_MAX;
    lastTileCompleteAt = 0;
    lineOverrun = false;
    resetQueue();

    // Initialize palette system (default grayscale palettes)
    for (int i = 0; i < 16; i++) {
        uint8_t gray = (i << 4) | i;  // 0x00, 0x11, 0x22, ... 0xFF
        palette16[i] = ((gray >> 3) << 11) | ((gray >> 3) << 6) | ((gray >> 3) << 1) | 1;  // 16-bit BBGR
    }
    refreshPalette16Expanded();
    for (int i = 0; i < 256; i++) {
        palette256[i] = (i << 24) | (i << 16) | (i << 8) | 0xFF;  // 32-bit RGBA grayscale
    }
    tileTranslucency.fill(0x0F);  // Fully opaque by default
    dropTileLuts();   // (also after palette256, which the table below it is derived from)
    tileBlendMode = 0;
    tileAttrs = 0;

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
    codeWrittenRange(offset, static_cast<uint32_t>(copyLength));
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
    if (tqPending != 0) [[unlikely]] stepQueueEngine();
    tqHold = false;   // only tickMany/runBatch skip held cycles, and they start by looking again

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

void PPU::resetBlitMirrors() {
    blitDoneAt.fill(UINT64_MAX);
    tileBusyMask = 0;
}

void PPU::burnBlitCycles(uint32_t k) {
    // k cycles' worth of advanceBlitChannels(1), without the per-cycle call: blitClock moves by k unless a blit
    // completes inside them, and a completion draws pixels and must run in its own cycle (the one that brings
    // blitClock to blitNextDone), so stop one short of it and take that cycle singly.
    while (k > 0 && busyBlitChannelCount != 0) {
        const uint64_t room = blitNextDone - blitClock;   // advances until the next completion cycle
        if (room > k) {
            blitClock += k;
            return;
        }
        if (room > 1) blitClock += room - 1;
        advanceBlitChannels(1);
        k -= room > 1 ? static_cast<uint32_t>(room) : 1;
    }
}

void PPU::tickMany(uint32_t n) {
    // tick() n times, assuming the blank lines don't change meanwhile, one instruction at a time instead of one
    // cycle at a time. Per cycle tick() does, in this order: advance the blit channels, latch the blank edges,
    // then burn a stall cycle / take an interrupt / issue an instruction. The blank lines are constant, so the
    // edge latch is the same on every cycle and can come first; a stall is just that many blit-advancing cycles
    // with nothing else happening; and interrupts can only be pending from that latch or from before the call, so
    // whether any is pending is a local that only a serviced interrupt can change.
    if (n == 0 || state != PPUState::Running) [[unlikely]] return;
    latchInterruptEdges();
    tqHold = false;   // the display may have moved since the last call: look at the queue again
    blitOwed = 0;
    blitHorizon = blitQuietHorizon();
    bool pending = cpuIrqPending | vblankPending | hblankPending;
    uint32_t stall = stallCycles;
    for (;;) {
        // The stall's cycles and the boundary cycle after them see no instruction in between, so they are one
        // advance of the blits and the queue.
        uint32_t owed = 0;
        if (stall > 0) {
            owed = stall < n ? stall : n;
            stall -= owed;
            n -= owed;
            if (n == 0) { blitOwed += owed; break; }
        }
        // An instruction boundary: this cycle advances the blits, then takes an interrupt or issues. While nothing
        // can happen to a blit or the queue (blitHorizon) the cycles are only owed: whatever an instruction touches
        // that depends on them settles first (settleBlits()), and tickMany pays up before it returns.
        n--;
        if (static_cast<uint64_t>(blitOwed) + owed + 1 <= blitHorizon) {
            blitOwed += owed + 1;
        } else {
            const uint32_t due = blitOwed + owed + 1;
            blitOwed = 0;
            advanceBlitsAndQueue(due);
            blitHorizon = blitQuietHorizon();
        }
        if (pending) [[unlikely]] {
            stallCycles = 0;
            const bool taken = serviceInterrupts();
            pending = cpuIrqPending | vblankPending | hblankPending;
            if (taken) {
                stall = stallCycles;
                if (n == 0) break;
                continue;
            }
        }
        executeInstruction();
        stall = instrCycles - 1;
        if (state != PPUState::Running) [[unlikely]] break;   // an instruction (a write to the VOC halt bit) stopped it
        if (n == 0) break;
    }
    if (blitOwed != 0) {
        const uint32_t due = blitOwed;
        blitOwed = 0;
        advanceBlitsAndQueue(due);
    }
    stallCycles = stall;
}

uint32_t PPU::blitQuietHorizon() const {
    // How many cycles advanceBlitsAndQueue() could advance right now without any of them being more than a count-down:
    // up to the cycle before a blit completes, and (cells pending) through the cooldown or, while a cell is held, for
    // as long as it is.
    uint64_t h = UINT64_MAX;
    if (busyBlitChannelCount != 0) {
        const uint64_t room = blitNextDone - blitClock;
        h = room > 0 ? room - 1 : 0;
    }
    if (tqPending != 0) {
        if (tqCooldown > 0) h = std::min<uint64_t>(h, tqCooldown);
        else if (!tqHold) h = 0;
    }
    return h > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(h);
}

void PPU::settleBlits() {
    // Called first by everything an instruction does to the blit channels, the queue or the window they wait on: the
    // cycles tickMany has only owed so far happen now, so the change lands at the cycle it would have, and the horizon
    // is no longer known.
    if (blitOwed != 0) {
        const uint32_t due = blitOwed;
        blitOwed = 0;
        advanceBlitsAndQueue(due);
    }
    blitHorizon = 0;
}

uint32_t PPU::runBatch(uint32_t cycles) {
    uint32_t remaining = cycles;
    tqHold = false;
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
            advanceBlitsAndQueue(consumed);
            latchInterruptEdges();
            continue;
        }

        latchInterruptEdges();

        // Instruction boundary: same interrupt/issue logic as tick(), one cycle.
        if (serviceInterrupts()) {
            remaining--;
            advanceBlitsAndQueue(1);
            continue;
        }

        executeInstruction();
        stallCycles = instrCycles - 1;
        remaining--;
        advanceBlitsAndQueue(1);
    }
    return cycles - remaining;
}

// ---------------------------------------------------------------------------------------------------------------
// Instruction semantics. Each PPU instruction is decoded once, the first time it runs from an address, into a
// DecodedOp (handler + the fields its encoding carries + its base cycle cost) cached per code address; anything
// that writes PPU memory drops the cached decodes covering the written byte (codeWritten()). Handlers are the
// only definition of what an instruction does: tick(), tickMany() and runBatch() all go through
// executeInstruction().
// ---------------------------------------------------------------------------------------------------------------

void PPU::opDEFCALL(const DecodedOp& d) { callTable[registers[d.a] & 0xFF] = registers[d.b]; }
// movxp Y: copy execution pointer + 2 to register Y (the pointer has already moved past this instruction)
void PPU::opMOVXP(const DecodedOp& d) { registers[d.b] = executionPointer + 2; }
void PPU::opNOP(const DecodedOp&) {}
void PPU::opSWAPREG(const DecodedOp& d) {
    uint16_t temp = registers[d.a];
    registers[d.a] = registers[d.b];
    registers[d.b] = temp;
}
void PPU::opCLR(const DecodedOp& d) { registers[d.b] = 0; }
void PPU::opCMP(const DecodedOp& d) { updateFlags(registers[d.a] - registers[d.b], registers[d.a], registers[d.b]); }
void PPU::opCLRF(const DecodedOp&) { clearFlags(); }
void PPU::opJMZ(const DecodedOp&) {
    const bool taken = flags.zero;
    if (taken) executionPointer = registers[REG_PC];
    instrCycles = taken ? CYC_BRANCH_TAKEN : CYC_BRANCH_NOT;
}
void PPU::opJMG(const DecodedOp&) {
    const bool taken = flags.greater;
    if (taken) executionPointer = registers[REG_PC];
    instrCycles = taken ? CYC_BRANCH_TAKEN : CYC_BRANCH_NOT;
}
void PPU::opJNZ(const DecodedOp&) {
    const bool taken = !flags.zero;
    if (taken) executionPointer = registers[REG_PC];
    instrCycles = taken ? CYC_BRANCH_TAKEN : CYC_BRANCH_NOT;
}
// jng/jml: taken if not greater (<=)
void PPU::opJNG(const DecodedOp&) {
    const bool taken = !flags.greater;
    if (taken) executionPointer = registers[REG_PC];
    instrCycles = taken ? CYC_BRANCH_TAKEN : CYC_BRANCH_NOT;
}
void PPU::opINC(const DecodedOp& d) { registers[d.b]++; }
void PPU::opDEC(const DecodedOp& d) { registers[d.b]--; }
void PPU::opADD(const DecodedOp& d) { registers[d.a] += registers[d.b]; }
void PPU::opSUB(const DecodedOp& d) { registers[d.a] -= registers[d.b]; }
// cost CYC_MUL: the decoded base cost. Wraps at 16 bits; done in uint32_t because uint16_t * uint16_t promotes to int,
// where 65535 * 65535 is signed overflow (undefined behaviour a guest PPU program can reach).
void PPU::opMUL(const DecodedOp& d) { registers[d.a] = static_cast<uint16_t>(static_cast<uint32_t>(registers[d.a]) * registers[d.b]); }
void PPU::opINTDIV(const DecodedOp& d) {                                    // cost CYC_DIV: the decoded base cost
    if (registers[d.b] != 0) [[likely]] registers[d.a] /= registers[d.b];   // division by zero leaves X alone
}

// Preset E. a/b/operand carry the fields of the encoding, see decodeOp().
void PPU::opTARREG(const DecodedOp& d) {
    // tarreg T, Y, X: a = T, operand = Y (byte select), b = X (source register)
    targetRegisters[d.a] = d.b;
    targetBytes[d.a] = static_cast<uint8_t>(d.operand);
}
void PPU::opSETBYTE(const DecodedOp& d) {
    // setbyte T, imm: a = T, b = immediate
    const uint8_t regIndex = targetRegisters[d.a];
    if (targetBytes[d.a] == 0) {
        registers[regIndex] = (registers[regIndex] & 0xFF00) | d.b;
    } else {
        registers[regIndex] = (registers[regIndex] & 0x00FF) | (d.b << 8);
    }
}
void PPU::opBUILD(const DecodedOp& d) {
    // build T1, T2, X: a = T1 (MSB source), b = T2 (LSB source), operand = X (destination register)
    const uint8_t regIndex1 = targetRegisters[d.a];
    const uint8_t regIndex2 = targetRegisters[d.b];
    const uint8_t msb = (targetBytes[d.a] == 0) ? (registers[regIndex1] & 0xFF) : ((registers[regIndex1] >> 8) & 0xFF);
    const uint8_t lsb = (targetBytes[d.b] == 0) ? (registers[regIndex2] & 0xFF) : ((registers[regIndex2] >> 8) & 0xFF);
    registers[d.operand] = (msb << 8) | lsb;
}
void PPU::opCPREG(const DecodedOp& d) {
    // cpreg X, Y: a = X, b = Y, both 4-bit register ids extended by the register banks (SETREGBANK)
    const uint8_t regX = d.a | (regBankX << 4);
    const uint8_t regY = d.b | (regBankY << 4);
    registers[regY] = registers[regX];
}

// Preset F.
void PPU::opSETPOS(const DecodedOp& d) {
    // setpos X, Y: a, b = 4-bit register ids extended by the register banks. Writes the same memory-mapped position
    // cells TILEDRAW reads ($0200-$0203), so SETPOS and a raw poke to that region are interchangeable.
    const uint8_t regX = d.a | (regBankX << 4);
    const uint8_t regY = d.b | (regBankY << 4);
    const uint16_t x = registers[regX];
    const uint16_t y = registers[regY];
    memory[0x0200] = x & 0xFF;
    memory[0x0201] = (x >> 8) & 0xFF;
    memory[0x0202] = y & 0xFF;
    memory[0x0203] = (y >> 8) & 0xFF;
    codeWrittenRange(0x0200, 4);
}
void PPU::opSETTILE(const DecodedOp& d) {
    currentTileId = registers[d.a] & 0xFF;
    currentTileMode = static_cast<uint8_t>(d.b);
}
void PPU::opSETDP(const DecodedOp& d) { registers[REG_DP] = registers[d.a]; }
void PPU::opMOVDP(const DecodedOp& d) {
    // mov(dp) X: move register X to the address DP points at
    const uint16_t addr = registers[REG_DP];
    if (addr < 65535) {
        memory[addr] = registers[d.a] & 0xFF;
        memory[addr + 1] = (registers[d.a] >> 8) & 0xFF;
        codeWritten(addr);
        codeWritten(addr + 1);

        // Check for memory-mapped I/O
        handleMemoryWrite(addr, memory[addr]);
        handleMemoryWrite(addr + 1, memory[addr + 1]);
    }
    instrCycles = CYC_MEM;  // word store to (DP)
}
void PPU::opSETRENDMOD(const DecodedOp& d) {
    settleBlits();
    if (display != nullptr) {
        const bool is32bit = (d.operand & 0x80) != 0;
        display->setRenderMode(is32bit ? RenderMode::RGBA32 : RenderMode::RGBA16);
        tqHold = false;   // the window the queue waits on may have changed
    }
}
void PPU::opPALETTE16_LOAD(const DecodedOp&) {
    loadPalette16();
    instrCycles = CYC_PAL_BASE + 16 * CYC_PAL_PER_ENTRY;  // stream 16 words
}
void PPU::opPALETTE256_LOAD(const DecodedOp&) {
    loadPalette256();
    instrCycles = CYC_PAL_BASE + 256 * CYC_PAL_PER_ENTRY;  // stream 256 words
}
void PPU::opJMR(const DecodedOp&) {
    executionPointer = registers[REG_PC];
    instrCycles = CYC_BRANCH_TAKEN;  // unconditional jump: always taken
}
void PPU::opMOV(const DecodedOp& d) {
    // mov X (dp): move from the address DP points at to register X
    const uint16_t addr = registers[REG_DP];
    if (addr < 65535) {
        const uint8_t low = handleMemoryRead(addr);
        const uint8_t high = handleMemoryRead(addr + 1);
        registers[d.a] = low | (high << 8);
    }
    instrCycles = CYC_MEM;  // word load from (DP)
}
void PPU::opSETREGBANK(const DecodedOp& d) {
    regBankX = d.a;
    regBankY = d.b;
}
void PPU::opCLRTILE(const DecodedOp&) {
        // clrtile - clear tile storage (all banks)
        for (auto& bank : tileStorage) {
            for (auto& tile : bank) {
                tile.fill(0);
            }
        }
        instrCycles = CYC_CLRTILE;  // bulk fixed-function clear
}
void PPU::opCLRPALETTE(const DecodedOp&) {
        // clrpalette - clear both palettes to default grayscale
        for (int i = 0; i < 16; i++) {
            uint8_t gray = (i << 4) | i;
            palette16[i] = ((gray >> 3) << 11) | ((gray >> 3) << 6) | ((gray >> 3) << 1) | 1;
        }
        refreshPalette16Expanded();
        for (int i = 0; i < 256; i++) {
            palette256[i] = (i << 24) | (i << 16) | (i << 8) | 0xFF;
        }
        dropTileLuts();
        instrCycles = CYC_CLRPAL;  // bulk fixed-function reset
}
void PPU::opTILEDRAW(const DecodedOp&) {
        settleBlits();
        // tiledraw - snapshot the current draw state (position, tile,
        // mode, blend settings) into a free blit channel and dispatch
        // it to run concurrently - see BlitChannel's comment in ppu.h.
        // The actual pixel writes happen in executeBlit() when the
        // channel's own completeAt is reached, not here.
        int freeChannel = -1;
        for (size_t i = 0; i < NUM_BLIT_CHANNELS; i++) {
            if (!blitChannels[i].busy) {
                freeChannel = static_cast<int>(i);
                break;
            }
        }

        // TILE_ATTR_CLIP: the blit writes only the rows of [$0204, $0206) and is charged only for those rows
        const uint8_t attrs = tileAttrs;
        const uint16_t clipY0 = memory[0x0204] | (memory[0x0205] << 8);
        const uint16_t clipY1 = memory[0x0206] | (memory[0x0207] << 8);
        uint32_t rows = 8;
        if (attrs & TILE_ATTR_CLIP) {
            const uint32_t y = memory[0x0202] | (memory[0x0203] << 8);
            rows = 0;
            for (uint32_t ty = 0; ty < 8; ty++) rows += (y + ty >= clipY0 && y + ty < clipY1);
        }
        uint32_t blitCost = CYC_TILE_BASE +
                             (rows * (TILE_PIXELS / 8)) * (tileBlendMode != 0 ? CYC_TILE_PX_BLEND
                                                                              : CYC_TILE_PX);

        if (freeChannel >= 0) {
            BlitChannel& ch = blitChannels[freeChannel];
            ch.busy = true;
            ch.fromQueue = false;
            busyBlitChannelCount++;
            // Blits retire (draw) in issue order, whatever they cost: a clipped, cheaper tile issued after a full one
            // would otherwise land first and then be overwritten by the older, longer blit.
            ch.completeAt = std::max(blitClock + blitCost, lastTileCompleteAt + 1);
            lastTileCompleteAt = ch.completeAt;
            if (ch.completeAt < blitNextDone) blitNextDone = ch.completeAt;
            blitDoneAt[freeChannel] = ch.completeAt;
            tileBusyMask |= 1u << freeChannel;
            ovRowLo[freeChannel] = INT64_MIN; ovRowHi[freeChannel] = INT64_MAX;
            ovColLo[freeChannel] = INT32_MIN; ovColHi[freeChannel] = INT32_MAX;
            ch.x = memory[0x0200] | (memory[0x0201] << 8);
            ch.y = memory[0x0202] | (memory[0x0203] << 8);
            ch.tileBank = currentTileBank;
            ch.tileId = currentTileId;
            ch.tileMode = currentTileMode;
            ch.blendMode = tileBlendMode;
            ch.attrs = attrs;
            ch.clipY0 = clipY0; ch.clipY1 = clipY1;
            ch.translucency = tileTranslucency;

            // Dispatch-only cost: the PPU is free to issue its next
            // instruction (e.g. scheduling another TILEDRAW into a
            // different channel) while this one runs in the background.
            instrCycles = CYC_TILE_BASE;
        } else {
            // Every channel is busy: wait for one. Run this TILEDRAW again next cycle (the pointer already stepped past
            // it) until a channel frees, like a stalled blitter queue. It used to draw synchronously right here, ahead
            // of the older blits still in flight, which turned a layered scene inside out.
            executionPointer = static_cast<uint16_t>(executionPointer - 2);
            instrCycles = 1;
        }
}
void PPU::opSETTILEBANK(const DecodedOp& d) {
    currentTileBank = static_cast<uint8_t>(registers[d.a] % MAX_TILE_BANKS);
}
void PPU::opCALL(const DecodedOp& d) {
    // call X: call the function registered under slot X (via DEFCALL). Pushes the return address the same way blank
    // interrupt entry does, so the ppuasm RET shorthand can pop it back.
    pushReturnAddress(executionPointer);
    executionPointer = callTable[d.operand];
    instrCycles = CYC_CALL;
}
void PPU::opGBLS(const DecodedOp& d) {
    if (vblank) {
        registers[d.a] = 0;
    } else if (hblank) {
        registers[d.a] = 1;
    } else {
        registers[d.a] = 65535;
    }
}

// A handler as a plain function pointer, so dispatch is one indirect call to a direct, inlined body: the trampoline
// binds the member function at compile time.
#define ZP_OP(h) (&PPU::opTrampoline<&PPU::h>)

PPU::DecodedOp PPU::decodeOp(uint16_t ep) const {
    const uint16_t instruction = (memory[ep] << 8) | memory[(ep + 1) & 0xFFFF];
    const uint8_t opcode = instruction >> 12;
    const uint16_t operand = instruction & 0x0FFF;
    const uint8_t regX = (operand >> 6) & 0x3F;
    const uint8_t regY = operand & 0x3F;

    DecodedOp d;
    d.operand = 0;
    d.a = d.b = 0;
    d.cost = CYC_BASE;    // a single-cycle datapath op; multi-cycle ones override it here or in their handler
    switch (static_cast<PPUOpcode>(opcode)) {
        case PPUOpcode::DEFCALL:   d.fn = ZP_OP(opDEFCALL); d.a = regX; d.b = regY; break;
        case PPUOpcode::MOVXP_NOP:
            if ((operand & 0x800) == 0) { d.fn = ZP_OP(opMOVXP); d.b = regY; }
            else d.fn = ZP_OP(opNOP);
            break;
        case PPUOpcode::SWAPREG:   d.fn = ZP_OP(opSWAPREG); d.a = regX; d.b = regY; break;
        case PPUOpcode::CLR:       d.fn = ZP_OP(opCLR); d.b = regY; break;
        case PPUOpcode::CMP:       d.fn = ZP_OP(opCMP); d.a = regX; d.b = regY; break;
        case PPUOpcode::CLRF:      d.fn = ZP_OP(opCLRF); break;
        case PPUOpcode::JUMP_ZG:   d.fn = (operand & 0x800) ? ZP_OP(opJMG) : ZP_OP(opJMZ); break;
        case PPUOpcode::JUMP_NZG:  d.fn = (operand & 0x800) ? ZP_OP(opJNG) : ZP_OP(opJNZ); break;
        case PPUOpcode::INC:       d.fn = ZP_OP(opINC); d.b = regY; break;
        case PPUOpcode::DEC:       d.fn = ZP_OP(opDEC); d.b = regY; break;
        case PPUOpcode::ADD:       d.fn = ZP_OP(opADD); d.a = regX; d.b = regY; break;
        case PPUOpcode::SUB:       d.fn = ZP_OP(opSUB); d.a = regX; d.b = regY; break;
        case PPUOpcode::MUL:       d.fn = ZP_OP(opMUL); d.a = regX; d.b = regY; d.cost = CYC_MUL; break;
        case PPUOpcode::INTDIV:    d.fn = ZP_OP(opINTDIV); d.a = regX; d.b = regY; d.cost = CYC_DIV; break;
        case PPUOpcode::PRESET_E: {
            const uint8_t subopcode = (operand >> 10) & 0x03;   // bits 11-10
            const uint16_t sub = operand & 0x3FF;               // bits 9-0
            d.cost = CYC_PRESET_E;                              // extra decode stage; a handler may override
            switch (static_cast<PresetEOpcode>(subopcode)) {
                case PresetEOpcode::TARREG:
                    d.fn = ZP_OP(opTARREG);
                    d.a = (sub >> 8) & 0x03;                    // target register
                    d.operand = (sub >> 7) & 0x01;              // byte select
                    d.b = sub & 0x3F;                           // source register
                    break;
                case PresetEOpcode::SETBYTE:
                    d.fn = ZP_OP(opSETBYTE);
                    d.a = (sub >> 8) & 0x03;
                    d.b = sub & 0xFF;                           // immediate
                    break;
                case PresetEOpcode::BUILD:
                    d.fn = ZP_OP(opBUILD);
                    d.a = (sub >> 8) & 0x03;                    // MSB source target
                    d.b = (sub >> 6) & 0x03;                    // LSB source target
                    d.operand = sub & 0x3F;                     // destination register
                    break;
                case PresetEOpcode::CPREG:
                    d.fn = ZP_OP(opCPREG);
                    d.a = (sub >> 4) & 0x0F;
                    d.b = sub & 0x0F;
                    break;
            }
            break;
        }
        case PPUOpcode::PRESET_F: {
            const uint8_t subopcode = (operand >> 8) & 0x0F;
            const uint8_t sub = operand & 0xFF;
            d.cost = CYC_PRESET_F;                              // extra decode stage; a handler may override
            switch (static_cast<PresetFOpcode>(subopcode)) {
                case PresetFOpcode::SETPOS:          d.fn = ZP_OP(opSETPOS); d.a = (sub >> 4) & 0x0F; d.b = sub & 0x0F; break;
                case PresetFOpcode::SETTILE:         d.fn = ZP_OP(opSETTILE); d.a = (sub >> 2) & 0x3F; d.b = sub & 0x03; break;
                case PresetFOpcode::SETDP:           d.fn = ZP_OP(opSETDP); d.a = (sub >> 2) & 0x3F; break;
                case PresetFOpcode::MOVDP:           d.fn = ZP_OP(opMOVDP); d.a = (sub >> 2) & 0x3F; break;
                case PresetFOpcode::SETRENDMOD:      d.fn = ZP_OP(opSETRENDMOD); d.operand = sub; break;
                case PresetFOpcode::PALETTE16_LOAD:  d.fn = ZP_OP(opPALETTE16_LOAD); break;
                case PresetFOpcode::PALETTE256_LOAD: d.fn = ZP_OP(opPALETTE256_LOAD); break;
                case PresetFOpcode::JMR:             d.fn = ZP_OP(opJMR); break;
                case PresetFOpcode::MOV:             d.fn = ZP_OP(opMOV); d.a = (sub >> 2) & 0x3F; break;
                case PresetFOpcode::SETREGBANK:      d.fn = ZP_OP(opSETREGBANK); d.a = (sub >> 4) & 0x03; d.b = (sub >> 2) & 0x03; break;
                case PresetFOpcode::CLRTILE:         d.fn = ZP_OP(opCLRTILE); break;
                case PresetFOpcode::CLRPALETTE:      d.fn = ZP_OP(opCLRPALETTE); break;
                case PresetFOpcode::TILEDRAW:        d.fn = ZP_OP(opTILEDRAW); break;
                case PresetFOpcode::SETTILEBANK:     d.fn = ZP_OP(opSETTILEBANK); d.a = (sub >> 2) & 0x3F; break;
                case PresetFOpcode::CALL:            d.fn = ZP_OP(opCALL); d.operand = sub; break;
                case PresetFOpcode::GBLS:            d.fn = ZP_OP(opGBLS); d.a = (sub >> 2) & 0x3F; break;
            }
            break;
        }
    }
    return d;
}

#undef ZP_OP

void PPU::executeInstruction() {
    // Fetch: the cached decode of the instruction at the execution pointer, decoding it on first use.
    const uint16_t ep = executionPointer;
    DecodedOp* d = &decoded[ep];
    if (!d->fn) [[unlikely]] {
        *d = decodeOp(ep);
    }
    executionPointer = ep + 2;
    instrCycles = d->cost;   // handlers that cost more than their base override it
    d->fn(*this, *d);
}


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

    if (address == LINE_PORT_STATUS) {
        uint8_t status = lineOverrun ? LINE_STATUS_OVERRUN : 0;
        for (size_t i = 0; i < NUM_LINE_CHANNELS; i++)
            if (blitChannels[NUM_BLIT_CHANNELS + i].busy) status |= static_cast<uint8_t>(1u << i);
        return status;
    }

    // Tile-row queue: $0360 reads the cells still pending as a clean word, $0364 the sticky late/overflow bits.
    if (address == TQ_GO) return static_cast<uint8_t>(tqPending);
    if (address == TQ_GO + 1) return 0;
    if (address == TQ_STATUS) return static_cast<uint8_t>((tqLate ? TQ_STATUS_LATE : 0) | (tqOverflow ? TQ_STATUS_OVERFLOW : 0));
    if (address == TQ_STATUS + 1) return 0;
    // $0366: the frame the hardware is on (its frame-row offset / 256, bumped at scanline 257).
    if (address == TQ_HWFRAME || address == TQ_HWFRAME + 1) {
        const uint16_t frame = display != nullptr ? static_cast<uint16_t>(display->getFrameRowOffset() / VISIBLE_SCANLINES) : 0;
        return static_cast<uint8_t>(address == TQ_HWFRAME ? frame & 0xFF : frame >> 8);
    }

    // Write-ahead mode: the framebuffer ($E000-$FFFF) and the queue's backing store ($C000-$DFFF) are not on the
    // PPU's bus - they read open bus ($FF, like the CPU's) and ignore writes.
    if (writeAhead && address >= 0xC000) return 0xFF;

    // Framebuffer region: 0xE000-0xFFFF (8 KiB max)
    if (address >= 0xE000 && display != nullptr) {
        return framebufferByte(address);
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

    if (writeAhead && address >= 0xC000 && address != TQ_MODE) return;   // open bus: see handleMemoryRead

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
        uint32_t color = 0;
        if ((vocRegisters.renderModeControl & VOC_PALETTE_MODE) != 0) {
            color = palette256[memory[0x0104]];
        } else {
            color = palette16Expanded[memory[0x0104] & 0x0F];
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

    // Line-blit port: the other port words are plain memory the launch reads, so $0348 (the low byte of the
    // word a MOVDP stores) is the trigger; a write to $034A clears the sticky overrun flag.
    if (address == LINE_PORT_GO) startLineBlit();
    if (address == LINE_PORT_STATUS) lineOverrun = false;

    // Tile-row queue: $0360 launches, a write to $0364/$0365 clears the sticky bits, $0362 bit 0 is write-ahead mode.
    if (address == TQ_GO) startRowCommand();
    if (address == TQ_STATUS || address == TQ_STATUS + 1) { tqLate = false; tqOverflow = false; }
    if (address == TQ_MODE) writeAhead = (value & 1) != 0;
}

void PPU::resetQueue() {
    settleBlits();
    tqHead = tqCount = tqPending = tqCooldown = 0;
    tqHold = false;
    tqLate = tqOverflow = writeAhead = false;
    for (auto& c : tqRing) c = RowCmd();
}

uint8_t PPU::framebufferByte(uint16_t address) const {
    const uint16_t offset = address - 0xE000;
    if (display == nullptr || offset >= 0x2000) return 0;
    return reinterpret_cast<const uint8_t*>(display->getFramebuffer16())[offset];
}

void PPU::advanceBlitsAndQueue(uint32_t cycles) {
    // `cycles` of advanceBlitChannels(1) + (while cells are pending) stepQueueEngine(), with the stretches where
    // neither does anything skipped: nothing ran in between, so each of those cycles is a no-op that only counts
    // down - blitClock moves by one unless a blit completes, the cooldown ticks, and a held cell stays held (its
    // inputs - the window, the channels - only change on a completion, which is a cycle taken singly below and
    // looks at the cell again, or outside this call, which clears tqHold).
    while (cycles > 0) {
        if (tqPending == 0) { burnBlitCycles(cycles); return; }
        uint32_t quiet = cycles;
        if (busyBlitChannelCount != 0) {
            const uint64_t room = blitNextDone - blitClock;   // the room-th cycle from now completes a blit
            const uint64_t before = room > 0 ? room - 1 : 0;
            if (before < quiet) quiet = static_cast<uint32_t>(before);
        }
        if (tqCooldown > 0) {
            if (tqCooldown < quiet) quiet = tqCooldown;
        } else if (!tqHold) {
            quiet = 0;   // the engine looks at the cell this cycle
        }
        if (quiet > 0) {
            if (busyBlitChannelCount != 0) blitClock += quiet;
            if (tqCooldown > 0) tqCooldown -= quiet;
            cycles -= quiet;
            continue;
        }
        advanceBlitChannels(1);
        stepQueueEngine();
        cycles--;
    }
}

void PPU::startRowCommand() {
    settleBlits();
    // Snapshot the port into the queue. Cells are read from PPU memory when they are dispatched, not now, so the
    // tilemap must stay put until they have been. A row that would take the pending count past TQ_CAPACITY is
    // dropped whole and the sticky overflow bit set (a write cannot stall the instruction that made it).
    const uint16_t count = memory[TQ_COUNT] | (memory[TQ_COUNT + 1] << 8);
    if (count == 0) return;
    if (tqPending + count > TQ_CAPACITY) { tqOverflow = true; return; }
    RowCmd& c = tqRing[(tqHead + tqCount) % TQ_CAPACITY];
    c = RowCmd();
    c.src = memory[TQ_SRC] | (memory[TQ_SRC + 1] << 8);
    c.count = count;
    c.x = memory[TQ_X] | (memory[TQ_X + 1] << 8);
    c.flags = memory[TQ_FLAGS] | (memory[TQ_FLAGS + 1] << 8);
    // Rows are absolute: 256 x the frame the row belongs to + the row within it. The program names the frame ($035E, a
    // 16-bit frame number that wraps; it is placed relative to the hardware's own frame), so a launch that blocks for a
    // long time before it is written still lands in the frame it was meant for - the program never has to guess which
    // frame "now" is in. A frame the beam has already passed is simply late. y and the clip rows are signed: a tile may
    // start above row 0 (its top rows clipped away) for a fine scroll.
    const int64_t fro = display != nullptr ? display->getFrameRowOffset() : 0;
    const int64_t hw = fro / VISIBLE_SCANLINES;
    const int16_t ahead = static_cast<int16_t>(static_cast<uint16_t>(memory[TQ_FRAME] | (memory[TQ_FRAME + 1] << 8)) - static_cast<uint16_t>(hw & 0xFFFF));
    const int64_t base = fro + static_cast<int64_t>(ahead) * VISIBLE_SCANLINES;
    c.absY = base + static_cast<int16_t>(memory[TQ_Y] | (memory[TQ_Y + 1] << 8));
    c.absClip0 = base + static_cast<int16_t>(memory[TQ_CLIP0] | (memory[TQ_CLIP0 + 1] << 8));
    c.absClip1 = base + static_cast<int16_t>(memory[TQ_CLIP1] | (memory[TQ_CLIP1 + 1] << 8));
    c.blendMode = tileBlendMode;
    c.translucency = tileTranslucency;
    tqCount++;
    tqPending += count;
    tqHold = false;
}

void PPU::stepQueueEngine() {
    // One queued cell per TQ_DISPATCH_CYCLES at most: read its two words, decide whether its rows are writable yet
    // (hold until they are - the point of the queue), and dispatch it onto a free tile channel. Dispatch order is
    // queue order and the channels retire in issue order, so queue order is z-order, same as TILEDRAW.
    if (tqCooldown > 0) { tqCooldown--; return; }
    tqHold = false;
    RowCmd& cmd = tqRing[tqHead];
    const uint32_t k = cmd.next;
    const uint32_t idx = (cmd.flags & TQ_F_FLIPX) ? cmd.count - 1u - k : k;
    const uint16_t addr = (cmd.flags & TQ_F_WRAP) ? static_cast<uint16_t>((cmd.src & 0xFF00) | ((cmd.src + 4 * idx) & 0x00FF))
                                                  : static_cast<uint16_t>(cmd.src + 4 * idx);
    const uint16_t w0 = memory[addr] | (memory[(addr + 1) & 0xFFFF] << 8);
    const uint16_t w1 = memory[(addr + 2) & 0xFFFF] | (memory[(addr + 3) & 0xFFFF] << 8);

    uint8_t attrs = static_cast<uint8_t>(w1 & TILE_ATTR_MASK & ~TILE_ATTR_CLIP);
    if (cmd.flags & TQ_F_FLIPX) attrs ^= TILE_ATTR_HFLIP;
    if (cmd.flags & TQ_F_FLIPY) attrs ^= TILE_ATTR_VFLIP;
    if (cmd.flags & TQ_F_TRANSPARENT) attrs |= TILE_ATTR_TRANSPARENT;
    if (cmd.flags & TQ_F_CLIP) attrs |= TILE_ATTR_CLIP;

    // The rows this cell writes, absolute: the tile's eight, cut down to the clip window when there is one.
    int64_t r0 = cmd.absY, r1 = cmd.absY + 7;
    if (cmd.flags & TQ_F_CLIP) { r0 = std::max(r0, cmd.absClip0); r1 = std::min(r1, cmd.absClip1 - 1); }
    const bool empty = r1 < r0;
    if (!empty) {
        if (display != nullptr && !display->rowIsClear(r1)) { tqHold = true; return; }   // its slot is not free yet: hold
        const uint32_t freeMask = ~tileBusyMask & ((1u << NUM_BLIT_CHANNELS) - 1);
        if (freeMask == 0) { tqHold = true; return; }
        const int freeChannel = __builtin_ctz(freeMask);
        const uint32_t rows = static_cast<uint32_t>(r1 - r0 + 1);
        const uint32_t blitCost = CYC_TILE_BASE + (rows * (TILE_PIXELS / 8)) * (cmd.blendMode != 0 ? CYC_TILE_PX_BLEND : CYC_TILE_PX);
        BlitChannel& ch = blitChannels[freeChannel];
        ch = BlitChannel();
        ch.busy = true;
        busyBlitChannelCount++;
        ch.x = static_cast<uint16_t>(static_cast<int16_t>(cmd.x) + 8 * static_cast<int>(k));
        ch.fromQueue = true;
        ch.rowLo = r0; ch.rowHi = r1;
        ch.colLo = static_cast<int16_t>(ch.x); ch.colHi = ch.colLo + 7;
        // Retire after every older in-flight blit this one overlaps. A blit the queue did not dispatch (TILEDRAW) has no
        // known extent, so it counts as overlapping everything.
        uint64_t after = 0;
        for (size_t i = 0; i < NUM_BLIT_CHANNELS; i++) {   // (this channel is not in the mirrors yet, and is not idle there either)
            const uint64_t done = blitDoneAt[i];           // UINT64_MAX: idle
            const bool overlaps = (ovRowLo[i] <= ch.rowHi) & (ch.rowLo <= ovRowHi[i]) & (ovColLo[i] <= ch.colHi) & (ch.colLo <= ovColHi[i]);
            const uint64_t cand = (overlaps & (done != UINT64_MAX)) ? done : 0;
            after = cand > after ? cand : after;
        }
        ch.completeAt = std::max(blitClock + blitCost, after + 1);
        blitDoneAt[freeChannel] = ch.completeAt;
        tileBusyMask |= 1u << freeChannel;
        ovRowLo[freeChannel] = ch.rowLo; ovRowHi[freeChannel] = ch.rowHi;
        ovColLo[freeChannel] = ch.colLo; ovColHi[freeChannel] = ch.colHi;
        if (ch.completeAt > lastTileCompleteAt) lastTileCompleteAt = ch.completeAt;   // a later TILEDRAW still retires after this
        if (ch.completeAt < blitNextDone) blitNextDone = ch.completeAt;
        ch.tileBank = static_cast<uint8_t>(w1 & 3);
        ch.tileId = static_cast<uint8_t>(w0 & 0xFF);
        ch.tileMode = static_cast<uint8_t>((cmd.flags >> 4) & 3);
        ch.blendMode = cmd.blendMode;
        ch.attrs = attrs;
        ch.translucency = cmd.translucency;
        ch.absY = cmd.absY; ch.absClip0 = cmd.absClip0; ch.absClip1 = cmd.absClip1;
        tqCooldown = TQ_DISPATCH_CYCLES;
    }
    tqPending--;
    if (++cmd.next == cmd.count) {
        tqHead = (tqHead + 1) % TQ_CAPACITY;
        tqCount--;
    }
    if (tqPending == 0) tqCooldown = 0;
}

void PPU::startLineBlit() {
    settleBlits();
    // Snapshot the port into a free line channel; the run is written when the channel completes, like a tile blit.
    // All four busy: the launch is dropped and the sticky overrun flag set (a write has no way to stall the
    // instruction that made it, and a program that paces its launches never sees this).
    for (size_t i = NUM_BLIT_CHANNELS; i < blitChannels.size(); i++) {
        BlitChannel& ch = blitChannels[i];
        if (ch.busy) continue;
        ch = BlitChannel();
        ch.kind = 1;
        ch.lineSrc = memory[LINE_PORT_SRC] | (memory[LINE_PORT_SRC + 1] << 8);
        ch.x = memory[LINE_PORT_X] | (memory[LINE_PORT_X + 1] << 8);
        ch.y = memory[LINE_PORT_Y] | (memory[LINE_PORT_Y + 1] << 8);
        ch.lineCount = memory[LINE_PORT_COUNT] | (memory[LINE_PORT_COUNT + 1] << 8);
        ch.busy = true;
        busyBlitChannelCount++;
        ch.completeAt = blitClock + CYC_LINE_BASE + static_cast<uint64_t>(ch.lineCount) * CYC_LINE_PX;
        blitDoneAt[i] = ch.completeAt;
        if (ch.completeAt < blitNextDone) blitNextDone = ch.completeAt;
        return;
    }
    lineOverrun = true;
}

void PPU::executeLineBlit(const BlitChannel& ch) {
    // `lineCount` 4bpp palette indices (high nibble first, like a tile row) from PPU memory at `lineSrc`, through
    // the live 16-colour palette, along row y from x (signed, so a run can start off the left edge). An entry
    // with alpha 0 is transparent and leaves the framebuffer alone; pixels off the scanline or outside the
    // rolling window are dropped by the same checks the tile blitter uses.
    if (display == nullptr) return;
    const int x0 = static_cast<int16_t>(ch.x);
    const int y = ch.y;
    const int count = ch.lineCount;
    const bool mode16 = display->getRenderMode() == RenderMode::RGBA16;
    uint8_t* span = display->rowSpan(x0, y, count);
    for (int i = 0; i < count; i++) {
        const uint8_t pair = memory[static_cast<uint16_t>(ch.lineSrc + (i >> 1))];
        const uint32_t color = palette16Expanded[(i & 1) ? (pair & 0x0F) : (pair >> 4)];
        if ((color & 0xFF) == 0) continue;
        if (span != nullptr) {
            if (mode16) {
                const uint16_t c16 = Display::color32To16(color);
                span[2 * i] = c16 & 0xFF;
                span[2 * i + 1] = c16 >> 8;
            } else {
                span[4 * i] = color >> 24;
                span[4 * i + 1] = (color >> 16) & 0xFF;
                span[4 * i + 2] = (color >> 8) & 0xFF;
                span[4 * i + 3] = color & 0xFF;
            }
        } else {
            display->setPixel32(x0 + i, y, color);
        }
    }
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
            // The per-blit attribute bits ($00FC bits 2-5) are writable in either color depth, and take effect on
            // their own push flag ($00FD bit 4), separate from the blend mode's (bit 3) so changing a flip doesn't
            // re-latch the translucency bytes. A stray byte written alone (a program image DMA'd over the VOC window)
            // changes nothing.
            if (offset == 0x0C) {
                vocRegisters.translucency[0] = (vocRegisters.translucency[0] & ~TILE_ATTR_MASK) | (value & TILE_ATTR_MASK);
            }
            if (offset == 0x0D && (value & 0x10)) {
                tileAttrs = vocRegisters.translucency[0] & TILE_ATTR_MASK;
            }
            // In 32-bit RGBA mode, the rest of translucency is read-only
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
                        dropTileLuts();
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
    settleBlits();
    if (display == nullptr) return;

    // Apply color depth setting (bit 7)
    bool is32bit = (vocRegisters.renderModeControl & VOC_COLOR_DEPTH) != 0;
    display->setRenderMode(is32bit ? RenderMode::RGBA32 : RenderMode::RGBA16);

    // Apply rolling mode setting (bit 6): 0=block, 1=single.
    bool singleMode = (vocRegisters.renderModeControl & VOC_ROLLING_MODE) != 0;
    display->setRollingMode(!singleMode);
    tqHold = false;   // ditto: rowIsClear() depends on both modes

    // Palette mode (bit 3) is applied at the $0100-$010B pixel-draw port
    // (handleMemoryWrite), which reads vocRegisters.renderModeControl
    // directly rather than caching it here.
}

void PPU::applyVOCReset() {
    settleBlits();
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
    resetBlitMirrors();
    busyBlitChannelCount = 0;
    blitClock = 0;
    blitNextDone = UINT64_MAX;
    lastTileCompleteAt = 0;
    lineOverrun = false;
    resetQueue();

    // Don't reset VOC registers themselves (except the reset bit, which is cleared by caller)
}

void PPU::loadPalette16() {
    // Load 16-color palette from memory address pointed to by DP
    uint16_t addr = registers[REG_DP];

    // Read 16 palette entries (16-bit format: BBGGGRRRRR-A). The address wraps at 16 bits like the PPU stack does
    // (a DP near $FFFF used to read past the end of `memory`).
    for (int i = 0; i < 16; i++) {
        uint8_t low = memory[(addr + i * 2) & 0xFFFF];
        uint8_t high = memory[(addr + i * 2 + 1) & 0xFFFF];
        palette16[i] = (high << 8) | low;
    }
    refreshPalette16Expanded();
}

void PPU::refreshPalette16Expanded() {
    for (int i = 0; i < 16; i++) {
        palette16Expanded[i] = expandPalette16(palette16[i]);
    }
    dropTileLuts();
}

const PPU::TileLut& PPU::tileLut(uint8_t tileId) {
    const unsigned slot = tileId & 0x03;
    TileLut& t = tileLutCache[slot];
    if (!(tileLutValid & (1u << slot))) {
        for (int i = 0; i < 16; i++) {
            t.c32[i] = applyTranslucency(palette16Expanded[i], tileId);
            t.c16[i] = Display::color32To16(t.c32[i]);
            t.opq[i] = (t.c32[i] & 0xFF) != 0;
        }
        tileLutValid |= static_cast<uint8_t>(1u << slot);
    }
    return t;
}

const PPU::TileLut256* PPU::tileLut256(uint8_t tileId) {
    const unsigned slot = tileId & 0x03;
    if (!(tileLut256Valid & (1u << slot))) {
        if (tileLut256Warmup < LUT256_WARMUP_BLITS) { tileLut256Warmup++; return nullptr; }
        TileLut256& t = tileLut256Cache[slot];
        for (int i = 0; i < 256; i++) {
            t.c32[i] = applyTranslucency(palette256[i], tileId);
            t.c16[i] = Display::color32To16(t.c32[i]);
            t.opq[i] = (t.c32[i] & 0xFF) != 0;
        }
        tileLut256Valid |= static_cast<uint8_t>(1u << slot);
    }
    return &tileLut256Cache[slot];
}

void PPU::loadPalette256() {
    // Load 256-color palette from memory address pointed to by DP
    uint16_t addr = registers[REG_DP];
    dropTileLuts();

    // 16-bit render mode: 256 entries of 16-bit BBGR like PALETTE16's (512 bytes), expanded to the 32-bit form the
    // blitters work in. 32-bit render mode: 256 entries of RRGGBBAA, b0=R first in memory (1024 bytes).
    // (Wraps at 16 bits, see loadPalette16.)
    if (display == nullptr || display->getRenderMode() != RenderMode::RGBA32) {
        for (int i = 0; i < 256; i++) {
            const uint8_t low = memory[(addr + i * 2) & 0xFFFF];
            const uint8_t high = memory[(addr + i * 2 + 1) & 0xFFFF];
            palette256[i] = expandPalette16((high << 8) | low);
        }
        return;
    }
    for (int i = 0; i < 256; i++) {
        uint8_t b0 = memory[(addr + i * 4) & 0xFFFF];
        uint8_t b1 = memory[(addr + i * 4 + 1) & 0xFFFF];
        uint8_t b2 = memory[(addr + i * 4 + 2) & 0xFFFF];
        uint8_t b3 = memory[(addr + i * 4 + 3) & 0xFFFF];
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

    uint8_t blendR = 0, blendG = 0, blendB = 0, outA = 0;

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
    // Common case: nothing in flight. Called every single PPU cycle, so this
    // skips everything for the vast majority of calls.
    if (busyBlitChannelCount == 0) return;

    blitClock += elapsed;
    if (blitClock < blitNextDone) return;     // nothing finishes yet

    // Everything due retires in completion order (channel order would draw a later blit first whenever one batched
    // advance covers several). Tile blits' completion times already follow issue order, see opTILEDRAW.
    // One pass finds the earliest due blit and the earliest completion among the rest; with exactly one due (nearly
    // always) that is also the new blitNextDone, so there is no second look.
    for (;;) {
        // One branch-free pass over the finish times: the earliest due blit (the first channel on a tie), how many are
        // due, and the earliest completion among the rest. An idle channel is UINT64_MAX: never due, never earlier.
        uint64_t dueAt = UINT64_MAX, next = UINT64_MAX;
        unsigned dueIdx = 0, dueCount = 0;
        for (unsigned i = 0; i < NUM_ALL_CHANNELS; i++) {
            const uint64_t d = blitDoneAt[i];
            const bool isDue = d <= blitClock;
            const bool better = isDue & (d < dueAt);
            dueAt = better ? d : dueAt;
            dueIdx = better ? i : dueIdx;
            dueCount += isDue;
            const uint64_t pending = isDue ? UINT64_MAX : d;
            next = pending < next ? pending : next;
        }
        if (dueCount == 0) { blitNextDone = next; return; }
        BlitChannel& due = blitChannels[dueIdx];
        executeBlit(due);
        due.busy = false;
        busyBlitChannelCount--;
        blitDoneAt[dueIdx] = UINT64_MAX;
        if (dueIdx < NUM_BLIT_CHANNELS) tileBusyMask &= ~(1u << dueIdx);
        if (dueCount == 1) { blitNextDone = next; return; }
    }
}

namespace {

// Everything a no-blend tile blit needs once its palette has been turned into per-index tables.
struct BlitRows {
    Display* display;
    const uint8_t* tile;                 // 4 bytes per row (4bpp) or 8 (8bpp)
    const uint16_t* c16;                 // the color of each palette index, as RGBA16 stores it...
    const uint32_t* c32;                 // ...and as RGBA32 does (translucency already applied)
    const uint8_t* opq;                  // 1 where the color's alpha is not 0 (what a transparent blit stores)
    int x, y;                            // destination of the tile's top-left pixel (x signed)
    int t0, t1;                          // tile rows to draw: [t0, t1) after the row clip
    int c0, c1;                          // tile columns on the scanline: [c0, c1)
    bool vflip;
};

// RGBA16 pixels sit in the framebuffer low byte first. On a little-endian host that is the host's own layout, so a
// pixel (or a whole row of them) moves as one store instead of a pair of byte stores per pixel.
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
constexpr bool kHostLittleEndian = false;
#else
constexpr bool kHostLittleEndian = true;
#endif
inline void put16(uint8_t* d, uint16_t v) {
    if (kHostLittleEndian) std::memcpy(d, &v, sizeof v);
    else { d[0] = v & 0xFF; d[1] = v >> 8; }
}
inline uint16_t get16(const uint8_t* d) {
    if (kHostLittleEndian) { uint16_t v; std::memcpy(&v, d, sizeof v); return v; }
    return static_cast<uint16_t>(d[0] | (d[1] << 8));
}
inline void put32(uint8_t* d, uint32_t v) { d[0] = v >> 24; d[1] = (v >> 16) & 0xFF; d[2] = (v >> 8) & 0xFF; d[3] = v & 0xFF; }
inline uint32_t get32(const uint8_t* d) { return (uint32_t(d[0]) << 24) | (uint32_t(d[1]) << 16) | (uint32_t(d[2]) << 8) | d[3]; }

// A row at a time: decode the 8 palette indices, look their colors up, work out which columns are stored (all of
// them, unless the blit is transparent or hangs off an edge) and store. A row that is stored whole is one run of
// stores; a row that is only partly stored merges with what is already there (stored columns replaced, the rest
// rewritten with the bytes they hold), which is the same framebuffer as storing the columns one by one but has no
// branch per pixel for the data to mispredict. The same pixels in the same order as the per-pixel loop in executeBlit.
template <bool MODE16, bool IS4, bool TRANSP, bool HFLIP>
void blitRows(const BlitRows& a) {
    constexpr int BPP = MODE16 ? 2 : 4;
    using Px = std::conditional_t<MODE16, uint16_t, uint32_t>;
    const Px* table = nullptr;
    if constexpr (MODE16) table = a.c16; else table = a.c32;
    const unsigned colMask = ((1u << a.c1) - 1) & ~((1u << a.c0) - 1);
    for (int ty = a.t0; ty < a.t1; ty++) {
        uint8_t* row = a.display->rowBase(a.y + ty);
        if (row == nullptr) continue;   // this row is outside the window: dropped, like setPixel32 drops it
        const int sy = a.vflip ? 7 - ty : ty;

        unsigned idx[8];
        if constexpr (IS4) {
            const uint8_t* s = a.tile + sy * 4;
            for (int k = 0; k < 4; k++) {
                idx[HFLIP ? 7 - 2 * k : 2 * k] = s[k] >> 4;
                idx[HFLIP ? 6 - 2 * k : 2 * k + 1] = s[k] & 0x0F;
            }
        } else {
            const uint8_t* s = a.tile + sy * 8;
            for (int k = 0; k < 8; k++) idx[HFLIP ? 7 - k : k] = s[k];
        }

        unsigned mask = colMask;
        if constexpr (TRANSP) {
            unsigned opaque = 0;
            for (int tx = 0; tx < 8; tx++) opaque |= static_cast<unsigned>(a.opq[idx[tx]]) << tx;
            mask &= opaque;
        }
        if (mask == 0) continue;

        Px px[8];
        for (int tx = 0; tx < 8; tx++) px[tx] = table[idx[tx]];

        // The address of tile column c0, the first one on the scanline (so it is never before the row).
        uint8_t* dst = row + static_cast<ptrdiff_t>(a.x + a.c0) * BPP;
        if (mask == 0xFF) {   // every column, so c0 is 0
            if constexpr (MODE16 && kHostLittleEndian) {
                std::memcpy(dst, px, sizeof px);
            } else {
                for (int tx = 0; tx < 8; tx++) {
                    if constexpr (MODE16) put16(dst + 2 * tx, px[tx]); else put32(dst + 4 * tx, px[tx]);
                }
            }
        } else if (a.c0 == 0 && a.c1 == 8) {
            for (int tx = 0; tx < 8; tx++) {
                const Px keep = static_cast<Px>(0) - static_cast<Px>((mask >> tx) & 1u);   // all ones where stored
                if constexpr (MODE16) put16(dst + 2 * tx, (px[tx] & keep) | (get16(dst + 2 * tx) & ~keep));
                else put32(dst + 4 * tx, (px[tx] & keep) | (get32(dst + 4 * tx) & ~keep));
            }
        } else {
            for (int tx = a.c0; tx < a.c1; tx++) {
                if (!(mask & (1u << tx))) continue;
                if constexpr (MODE16) put16(dst + 2 * (tx - a.c0), px[tx]); else put32(dst + 4 * (tx - a.c0), px[tx]);
            }
        }
    }
}

// Whether every pixel of the tile is the same palette index, and if so which: a blank tile (the usual filler of a
// transparent layer) is a handful of word compares instead of 64 lookups. Bails out at the first word that differs.
inline bool uniformTile(const uint8_t* tile, bool is4bpp, unsigned& index) {
    const uint8_t first = tile[0];
    if (is4bpp && (first >> 4) != (first & 0x0F)) return false;
    const uint64_t pattern = 0x0101010101010101ull * first;
    const size_t bytes = is4bpp ? 32 : 64;
    for (size_t i = 0; i < bytes; i += 8) {
        uint64_t w;
        std::memcpy(&w, tile + i, sizeof w);
        if (w != pattern) return false;
    }
    index = is4bpp ? (first >> 4) : first;
    return true;
}

template <bool MODE16, bool IS4>
void blitRowsFor(const BlitRows& a, bool transparent, bool hflip) {
    if (transparent) {
        if (hflip) blitRows<MODE16, IS4, true, true>(a); else blitRows<MODE16, IS4, true, false>(a);
    } else {
        if (hflip) blitRows<MODE16, IS4, false, true>(a); else blitRows<MODE16, IS4, false, false>(a);
    }
}

} // namespace

void PPU::executeBlit(const BlitChannel& channel) {
    // The pixel loop TILEDRAW used to run inline and synchronously - now
    // driven off a channel's snapshotted state (see TILEDRAW's dispatch
    // case) so it can complete long after the instruction that issued it.
    if (display == nullptr) return;
    if (channel.kind == 1) { executeLineBlit(channel); return; }

    // x is signed (like the line blitter's), so a tile can hang off the left edge; the display clips it per pixel.
    const int x = static_cast<int16_t>(channel.x);
    // Queued blits keep absolute rows (see BlitChannel::fromQueue); everything below works in local rows.
    struct AbsoluteRows {   // a queued blit's rows may be in the next frame's space: see Display::setAbsoluteRows
        Display* d;
        explicit AbsoluteRows(Display* display, bool on) : d(on ? display : nullptr) { if (d) d->setAbsoluteRows(true); }
        ~AbsoluteRows() { if (d) d->setAbsoluteRows(false); }
    } absoluteRowsGuard(display, channel.fromQueue);
    const int64_t frameOffset = display->getFrameRowOffset();
    const int y = channel.fromQueue ? static_cast<int>(channel.absY - frameOffset) : channel.y;
    const int clip0 = channel.fromQueue ? static_cast<int>(channel.absClip0 - frameOffset) : channel.clipY0;
    const int clip1 = channel.fromQueue ? static_cast<int>(channel.absClip1 - frameOffset) : channel.clipY1;

    // Tile mode: 0=16BBGR 4bpp, 1=32RGBA 4bpp, 2=16BBGR 8bpp, 3=32RGBA 8bpp. Bit 0 picks nothing: 4bpp tiles read the
    // 16-entry palette and 8bpp tiles the 256-entry one (palette256, loaded in the render mode's entry format).
    const bool is4bpp = (channel.tileMode & 0x02) == 0;
    const bool transparent = (channel.attrs & TILE_ATTR_TRANSPARENT) != 0;
    const bool hflip = (channel.attrs & TILE_ATTR_HFLIP) != 0;
    const bool vflip = (channel.attrs & TILE_ATTR_VFLIP) != 0;
    const bool clipRows = (channel.attrs & TILE_ATTR_CLIP) != 0;

    // The tile rows that are drawn: all eight, or those inside the row clip.
    int t0 = 0, t1 = 8;
    if (clipRows) {
        t0 = std::clamp(clip0 - y, 0, 8);
        t1 = std::clamp(clip1 - y, 0, 8);
    }

    // A queued blit whose rows have already left the window (the PPU queued it too late) is lost; remember it in the
    // sticky late bit instead of drawing a torn frame silently. The window is a run of rows, so when the first and
    // last drawn rows are both in it (and not negative, where a row's slot goes by its signed remainder) so are all
    // the rows between; anything else is looked at row by row.
    if (channel.fromQueue && t0 < t1 &&
        !(y + t0 >= 0 && display->isRowWritable(y + t0) && display->isRowWritable(y + t1 - 1))) {
        for (int ty = t0; ty < t1; ty++) {
            if (!display->isRowWritable(y + ty)) { tqLate = true; break; }
        }
    }

    // Without blending nothing reads the framebuffer back, so a blit is a pure function of its tile and palette:
    // translucency depends only on the color, so the palette is translated once (see tileLut / tileLut256: 16 entries
    // for 4bpp, 256 for 8bpp once its palette has settled - until then 8bpp translates per pixel, below).
    if (channel.blendMode == 0) {
        const TileLut* tl = is4bpp ? &tileLut(channel.tileId) : nullptr;
        const TileLut256* t8 = is4bpp ? nullptr : tileLut256(channel.tileId);
        if (tl != nullptr || t8 != nullptr) {
            if (t0 >= t1) return;
            // Columns on the scanline: a tile hanging off an edge is clipped per pixel.
            const int c0 = std::clamp(-x, 0, 8);
            const int c1 = std::clamp(FB_WIDTH - x, 0, 8);
            if (c0 >= c1) return;
            BlitRows args;
            args.display = display;
            args.tile = tileStorage[channel.tileBank][channel.tileId].data();
            args.c16 = tl ? tl->c16 : t8->c16;
            args.c32 = tl ? tl->c32 : t8->c32;
            args.opq = tl ? tl->opq : t8->opq;
            // A transparent blit of a tile whose every pixel is a transparent color stores nothing.
            unsigned blankIndex;
            if (transparent && uniformTile(args.tile, is4bpp, blankIndex) && !args.opq[blankIndex]) return;
            args.x = x; args.y = y;
            args.t0 = t0; args.t1 = t1;
            args.c0 = c0; args.c1 = c1;
            args.vflip = vflip;
            const bool mode16 = display->getRenderMode() == RenderMode::RGBA16;
            if (mode16) {
                if (is4bpp) blitRowsFor<true, true>(args, transparent, hflip); else blitRowsFor<true, false>(args, transparent, hflip);
            } else {
                if (is4bpp) blitRowsFor<false, true>(args, transparent, hflip); else blitRowsFor<false, false>(args, transparent, hflip);
            }
            return;
        }
    }

    // The per-pixel reference: blending (it reads the framebuffer back) and 8bpp while its palette is still settling.
    for (int ty = 0; ty < 8; ty++) {
        const int sy = vflip ? 7 - ty : ty;   // source row: the destination row is always (y + ty)
        if (clipRows && (y + ty < clip0 || y + ty >= clip1)) continue;
        for (int tx = 0; tx < 8; tx++) {
            const int sx = hflip ? 7 - tx : tx;
            uint32_t color = 0;

            if (is4bpp) {
                // 4bpp mode: 2 pixels per byte, use palette lookup
                uint8_t byteIndex = (sy * 8 + sx) / 2;
                uint8_t pixelByte = tileStorage[channel.tileBank][channel.tileId][byteIndex];

                // Get 4-bit palette index (high nibble for even pixels, low nibble for odd)
                uint8_t paletteIndex = (sx & 1) ? (pixelByte & 0x0F) : ((pixelByte >> 4) & 0x0F);
                color = palette16Expanded[paletteIndex];
            } else {
                // 8bpp mode: 1 pixel per byte, 256-entry palette
                color = palette256[tileStorage[channel.tileBank][channel.tileId][sy * 8 + sx]];
            }

            // Apply translucency to the color
            color = applyTranslucency(color, channel.tileId);

            // Transparent blits leave the framebuffer alone for alpha-0 pixels (blending included: no read-back)
            if (transparent && (color & 0xFF) == 0) continue;

            // Apply blending if mode is set and we have a non-zero blend mode
            if (channel.blendMode != 0) {
                // Calculate framebuffer address for this pixel
                const int pixelX = x + tx;
                const int pixelY = y + ty;
                if (pixelX < 0 || pixelX >= FB_WIDTH) continue;   // off the scanline: setPixel32 would drop it

                // Read existing pixel from framebuffer
                uint32_t dstColor = 0;

                if (display->getRenderMode() == RenderMode::RGBA32) {
                    // 32-bit: 4 bytes/pixel. The rolling buffer only
                    // holds 8 scanlines, so index by (pixelY % 8) to
                    // stay within the $E000-$FFFF window.
                    const uint16_t row = static_cast<uint16_t>(pixelY % FB_SCANLINES_32BIT);
                    uint16_t fbAddr = 0xE000 + (row * 1024) + (pixelX * 4);

                    // Read 4 bytes (RGBA) from framebuffer
                    if (fbAddr + 3 <= 0xFFFF) {
                        uint8_t r = framebufferByte(fbAddr + 0);
                        uint8_t g = framebufferByte(fbAddr + 1);
                        uint8_t b = framebufferByte(fbAddr + 2);
                        uint8_t a = framebufferByte(fbAddr + 3);
                        dstColor = (r << 24) | (g << 16) | (b << 8) | a;
                    }
                } else {
                    // 16-bit: 2 bytes/pixel, 16-scanline rolling window;
                    // index by (pixelY % 16) within the 8 KiB buffer.
                    const uint16_t row = static_cast<uint16_t>(pixelY % FB_SCANLINES_16BIT);
                    uint16_t fbAddr = 0xE000 + (row * 512) + (pixelX * 2);

                    // Read 2 bytes (16-bit BGR) from framebuffer
                    if (fbAddr + 1 <= 0xFFFF) {
                        uint8_t low = framebufferByte(fbAddr);
                        uint8_t high = framebufferByte(fbAddr + 1);
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

namespace ZeroPoint {

namespace {
inline uint64_t fnv(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}
template <typename T> inline uint64_t fnvv(uint64_t h, const T& v) { return fnv(h, &v, sizeof v); }
}  // namespace

uint64_t PPU::stateHash() const {
    uint64_t h = 1469598103934665603ull;
    h = fnv(h, memory.data(), memory.size());
    h = fnv(h, registers.data(), sizeof(uint16_t) * registers.size());
    h = fnv(h, targetRegisters.data(), sizeof(uint16_t) * targetRegisters.size());
    h = fnv(h, targetBytes.data(), targetBytes.size());
    h = fnvv(h, executionPointer);
    h = fnvv(h, flags.zero); h = fnvv(h, flags.greater);
    h = fnvv(h, static_cast<int>(state));
    h = fnvv(h, vblank); h = fnvv(h, hblank); h = fnvv(h, prevVblank); h = fnvv(h, prevHblank);
    h = fnvv(h, vblankPending); h = fnvv(h, hblankPending);
    h = fnvv(h, cpuIrqPending); h = fnvv(h, cpuIrqVector);
    h = fnvv(h, regBankX); h = fnvv(h, regBankY);
    h = fnv(h, callTable.data(), sizeof(uint16_t) * callTable.size());
    h = fnvv(h, cycleCounter); h = fnvv(h, stallCycles); h = fnvv(h, instrCycles);
    h = fnv(h, tileStorage.data(), sizeof tileStorage);
    h = fnvv(h, currentTileId); h = fnvv(h, currentTileBank); h = fnvv(h, currentTileMode);
    for (const auto& c : blitChannels) {
        h = fnvv(h, c.busy); h = fnvv(h, c.busy ? c.completeAt - blitClock : 0ull); h = fnvv(h, c.x); h = fnvv(h, c.y);
        h = fnvv(h, c.tileBank); h = fnvv(h, c.tileId); h = fnvv(h, c.tileMode); h = fnvv(h, c.blendMode); h = fnvv(h, c.attrs); h = fnvv(h, c.clipY0); h = fnvv(h, c.clipY1);
        h = fnv(h, c.translucency.data(), c.translucency.size());
        h = fnvv(h, c.kind); h = fnvv(h, c.lineSrc); h = fnvv(h, c.lineCount);
        h = fnvv(h, c.fromQueue); h = fnvv(h, c.absY); h = fnvv(h, c.absClip0); h = fnvv(h, c.absClip1);
        h = fnvv(h, c.rowLo); h = fnvv(h, c.rowHi); h = fnvv(h, c.colLo); h = fnvv(h, c.colHi);
    }
    h = fnvv(h, tqPending); h = fnvv(h, tqCount); h = fnvv(h, tqCooldown);
    h = fnvv(h, tqLate); h = fnvv(h, tqOverflow); h = fnvv(h, writeAhead);
    for (uint32_t i = 0; i < tqCount; i++) {
        const RowCmd& c = tqRing[(tqHead + i) % TQ_CAPACITY];
        h = fnvv(h, c.src); h = fnvv(h, c.count); h = fnvv(h, c.x); h = fnvv(h, c.flags); h = fnvv(h, c.next);
        h = fnvv(h, c.absY); h = fnvv(h, c.absClip0); h = fnvv(h, c.absClip1); h = fnvv(h, c.blendMode);
        h = fnv(h, c.translucency.data(), c.translucency.size());
    }
    h = fnvv(h, lastTileCompleteAt > blitClock ? lastTileCompleteAt - blitClock : 0ull);
    h = fnvv(h, lineOverrun);
    h = fnvv(h, busyBlitChannelCount);
    h = fnv(h, palette16.data(), sizeof(uint16_t) * palette16.size());
    h = fnv(h, palette256.data(), sizeof(uint32_t) * palette256.size());
    h = fnv(h, tileTranslucency.data(), tileTranslucency.size());
    h = fnvv(h, tileBlendMode);
    h = fnvv(h, tileAttrs);
    h = fnvv(h, vocRegisters);
    return h;
}

}  // namespace ZeroPoint
