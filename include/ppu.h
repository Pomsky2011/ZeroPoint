#ifndef ZEROPOINT_PPU_H
#define ZEROPOINT_PPU_H

#include <cstdint>
#include <cstddef>
#include <array>
#include <vector>

namespace ZeroPoint {

// Forward declaration
class Display;

// PPU operates at 68.011355 MHz (master clock, NTSC colorburst x19)
// Executes 1 instruction per cycle
constexpr uint32_t PPU_CLOCK_HZ = 68011355;
constexpr uint32_t PPU_CYCLES_PER_INSTRUCTION = 1;

// PPU opcodes (4-bit)
enum class PPUOpcode : uint8_t {
    DEFCALL    = 0x0,  // Define callable function
    MOVXP_NOP  = 0x1,  // Move execution pointer / NOP
    SWAPREG    = 0x2,  // Swap two registers
    CLR        = 0x3,  // Clear register
    CMP        = 0x4,  // Compare registers
    CLRF       = 0x5,  // Clear flags
    JUMP_ZG    = 0x6,  // Jump if zero/greater
    JUMP_NZG   = 0x7,  // Jump if not zero/not greater
    INC        = 0x8,  // Increment register
    DEC        = 0x9,  // Decrement register
    ADD        = 0xA,  // Add registers
    SUB        = 0xB,  // Subtract registers
    MUL        = 0xC,  // Multiply registers
    INTDIV     = 0xD,  // Integer division
    PRESET_E   = 0xE,  // Preset E: immediate operations
    PRESET_F   = 0xF   // Preset F: extended instructions
};

// Preset E sub-opcodes (immediate/byte operations)
enum class PresetEOpcode : uint8_t {
    TARREG     = 0x0,  // Set target register
    SETBYTE    = 0x1,  // Set byte in target register
    BUILD      = 0x2,  // Build register from target registers
    CPREG      = 0x3   // Copy register to register
};

// Preset F sub-opcodes
enum class PresetFOpcode : uint8_t {
    SETPOS           = 0x0,  // Set position
    SETTILE          = 0x1,  // Set tile with mode
    SETDP            = 0x2,  // Set data pointer
    MOVDP            = 0x3,  // Move to (DP)
    SETRENDMOD       = 0x4,  // Set render mode (16/32-bit RGBA)
    PALETTE16_LOAD   = 0x5,  // Load 16-color palette from DP
    PALETTE256_LOAD  = 0x6,  // Load 256-color palette from DP
    JMR              = 0x7,  // Jump relative (PC)
    MOV              = 0x8,  // Move from (DP)
    SETREGBANK       = 0x9,  // Set register bank
    CLRTILE          = 0xA,  // Clear tile
    CLRPALETTE       = 0xB,  // Clear palette
    TILEDRAW         = 0xC,  // Draw tile at position
    SETTILEBANK      = 0xD,  // Select tile bank (widens addressable tile count
                             // past the 8-bit tile-ID field without changing
                             // its encoding - see PPU::tileStorage)
    CALL             = 0xE,  // Call function
    GBLS             = 0xF   // Get blank status
};

// Comparison flags
struct PPUFlags {
    bool zero;     // Result was zero
    bool greater;  // Left operand > right operand

    PPUFlags() : zero(false), greater(false) {}
};

// PPU state
enum class PPUState {
    WaitingForStart,       // Waiting for start signal
    Running,               // Executing microcode
    Halted                 // Halted by instruction
};

class PPU {
public:
    PPU();
    ~PPU();

    // Clock the PPU by one cycle
    void tick();

    // Exactly n calls to tick(), assuming the blank lines (setBlankStatus) don't change meanwhile: whole stretches
    // of a multi-cycle stall with no blit completing in them are collapsed into one step, and every other
    // cycle just calls tick(), so the result is state-identical to ticking n times.
    // Returns how many of the n cycles it ran: all of them, unless a watch (armWatch) fired, in which case it stops right
    // after the cycle whose instruction changed a watched byte.
    uint32_t tickMany(uint32_t n);

    // Run up to `cycles` PPU cycles in one call and return how many were
    // actually consumed. Semantically identical to calling tick() `cycles`
    // times, but collapses multi-cycle stalls into a single step instead of
    // spinning one function call per stalled cycle. This is the fast interpreter
    // path used by the standalone runner/JIT executor.
    uint32_t runBatch(uint32_t cycles);

    // Load microcode into memory
    void loadMicrocode(const uint8_t* code, size_t length, uint16_t offset = 0);

    // Raise a CPU-driven interrupt on the PPU: at its next instruction boundary
    // the PPU pushes the return address and vectors to `vector`. Highest priority
    // (above V/H-blank), one-shot. Used by the host/boot ROM to notify a running
    // PPU program that its code is about to be switched so it can react.
    void raiseCpuInterrupt(uint16_t vector) {
        cpuIrqPending = true;
        cpuIrqVector = vector;
    }

    // Start execution
    void start();

    // Reset PPU
    void reset();

    // Get current state
    PPUState getState() const { return state; }

    // Set blank status (from display controller)
    void setBlankStatus(bool vblank, bool hblank) {
        this->vblank = vblank;
        this->hblank = hblank;
    }

    bool blankStatusIs(bool vblank, bool hblank) const { return this->vblank == vblank && this->hblank == hblank; }

    // Set display pointer (for memory-mapped display I/O)
    void setDisplay(Display* disp) {
        display = disp;
    }

    // Get register value (for debugging)
    uint16_t getRegister(uint8_t reg) const {
        if (reg < 64) return registers[reg];
        return 0;
    }

    // Get execution pointer (for HLT detection)
    uint16_t getExecutionPointer() const {
        return executionPointer;
    }

    // FNV-1a hash of every piece of emulated state (memory, registers, flags, pending interrupts, stall,
    // blit channels, tiles, palettes, VOC). For regression tests: two runs that must be state-identical
    // (e.g. System's per-cycle path vs its batched one) must hash equal at every checkpoint.
    uint64_t stateHash() const;

    // Direct memory access (for debugging and CPU window)
    uint8_t readMemory(uint16_t address) const {
        return handleMemoryRead(address);
    }

    void writeMemory(uint16_t address, uint8_t value) {
        storeByte(address, value);
        handleMemoryWrite(address, value);
    }

    // Idle-poll skip (docs/idle-poll-skip.md, phase 3): watch up to 8 PPU memory bytes. While armed, a store that
    // *changes* one of them (the instruction stores, the CPU window, DMA, a reset or a program load all go through
    // storeByte() or say so) sets watchFired(), and tickMany()/System::tickVideoTo() stop right after that cycle.
    // Host-side bookkeeping: not machine state, not hashed.
    void armWatch(const uint16_t* addrs, int n) {
        watchCount = static_cast<uint8_t>(n > 8 ? 8 : n);
        for (int i = 0; i < watchCount; i++) watchAddr[i] = addrs[i];
        watchHit = false;
    }
    void disarmWatch() { watchCount = 0; watchHit = false; }
    bool watchFired() const { return watchHit; }

    // Register access for CPU I/O
    void setRegister(uint8_t reg, uint16_t value) {
        if (reg < 64) registers[reg] = value;
    }

    // Set the live execution pointer (CPU I/O write to PPUPC). Also mirrors
    // into R62 so reads via getRegister(REG_PC) stay consistent.
    void setExecutionPointer(uint16_t value) {
        executionPointer = value;
        registers[REG_PC] = value;
    }

private:
    // 64 registers, 16-bit each
    std::array<uint16_t, 64> registers{};

    // Special register indices
    static constexpr uint8_t REG_DP = 63;  // Data Pointer
    static constexpr uint8_t REG_PC = 62;  // Program Counter
    static constexpr uint8_t REG_SP = 61;  // Stack Pointer
    static constexpr uint8_t REG_HBLANK_INT = 60;  // H-Blank Interrupt Address
    static constexpr uint8_t REG_VBLANK_INT = 59;  // V-Blank Interrupt Address

    // Target registers for Preset E (4 target registers, each 16-bit)
    std::array<uint16_t, 4> targetRegisters{};
    std::array<uint8_t, 4> targetBytes{};  // 0 = LSB, 1 = MSB

    // 64 KiB memory (shared with ROM)
    std::array<uint8_t, 65536> memory{};

    // Execution pointer (actual instruction pointer)
    uint16_t executionPointer;

    // Comparison flags
    PPUFlags flags;

    // Current state
    PPUState state;

    // Blank status
    bool vblank;
    bool hblank;

    // Previous-cycle blank state, sampled every cycle (see latchInterruptEdges())
    // regardless of whether the PPU is mid-stall - not just at instruction
    // boundaries. Blank interrupts are edge-triggered: they fire once on the
    // 0->1 transition, not continuously while the blank line is asserted.
    bool prevVblank;
    bool prevHblank;

    // Edge-latched, not yet serviced. A blank pulse can be shorter than a
    // single stalled instruction (e.g. PALETTE16/INTDIV inside an ISR body) -
    // sampling edges only at instruction boundaries, as this used to do, could
    // see the line go high and back low again entirely within one stall and
    // never notice the pulse at all, silently dropping that interrupt. Latching
    // every cycle instead means the edge is caught the instant it happens;
    // serviceInterrupts() then just drains whatever's pending at the next
    // instruction boundary.
    bool vblankPending;
    bool hblankPending;

    // CPU-raised (host/boot ROM) interrupt: pending flag + vector. Serviced at
    // the next instruction boundary with priority over the blank interrupts.
    bool cpuIrqPending;
    uint16_t cpuIrqVector;

    // Register banks for preset F setpos (4-bit register IDs)
    uint8_t regBankX;
    uint8_t regBankY;

    // Call table (DEFCALL/CALL): DEFCALL registers an entry-point address
    // under an 8-bit slot number; CALL's 8-bit operand looks the address up
    // and jumps to it, matching PresetFOpcode::CALL's addressing range.
    std::array<uint16_t, 256> callTable{};

    // Cycle counter
    uint32_t cycleCounter;

    // Multi-cycle instruction pacing. An instruction applies its effect on the
    // cycle it is issued, then occupies the PPU for the remaining cycles of its
    // cost. While stallCycles > 0 the PPU is "busy" and tick() does nothing but
    // count down. instrCycles is the cost of the instruction most recently
    // issued (set during executeInstruction / the op handlers).
    uint32_t stallCycles;
    uint16_t instrCycles;

    // Display pointer (for memory-mapped I/O)
    Display* display;

    // Tile system (8x8 tiles, 64 bytes each, up to 256 tiles per bank). The
    // tile-ID field (SETTILE, the $0300-$033F definition window) is a plain
    // 8-bit value, same as always - MAX_TILE_BANKS widens total addressable
    // tiles by paging, the way real 16-bit-era hardware banked CHR/VRAM
    // beyond what a single ID field could reach, rather than by widening the
    // ID itself. SETTILEBANK selects which bank subsequent SETTILE/tile-def
    // writes and TILEDRAW reads target; it does not affect the 8-bit ID.
    static constexpr size_t TILE_SIZE = 64;      // 8x8 pixels
    static constexpr size_t MAX_TILES = 256;     // per bank
    static constexpr size_t MAX_TILE_BANKS = 4;  // 1024 tiles total
    std::array<std::array<std::array<uint8_t, TILE_SIZE>, MAX_TILES>, MAX_TILE_BANKS> tileStorage{};
    uint8_t currentTileId;                        // Currently selected tile for drawing
    uint8_t currentTileBank;                      // Currently selected tile bank (0..MAX_TILE_BANKS-1)
    uint8_t currentTileMode;                      // Tile mode (0-3): 16BBGR 4bpp, 32RGBA 4bpp, 16BBGR 8bpp, 32RGBA 8bpp

    // Concurrent blitter: up to NUM_BLIT_CHANNELS TILEDRAWs can be in flight
    // at once. TILEDRAW no longer stalls the PPU for the whole ~264-392 cycle
    // blit cost - it snapshots the live draw state (position, tile, mode,
    // blend settings) into a free channel and returns almost immediately;
    // the channel then runs down its own cycle cost independently of what
    // the PPU issues next, and performs the actual pixel writes when it
    // completes. This is the same "several transfers make independent
    // progress every tick" idea as DMAController's concurrent channels
    // (src/dma.cpp), reimplemented PPU-side rather than routed through the
    // real DMA controller: DMA only understands CPU-bus-addressable memory
    // (see CPU::readByteForDMA/writeByteForDMA), with no concept of tile
    // storage, palettes, or blend modes, so a tile blit isn't representable
    // as one of its existing transfer modes without duplicating all of this
    // state inside dma.cpp for no benefit over keeping it where the state
    // already lives. If every channel is busy when TILEDRAW issues, it waits
    // (re-executes each cycle) until one frees, and blits retire in issue
    // order - see opTILEDRAW and retireFront().
    struct BlitChannel {
        bool busy = false;
        uint64_t completeAt = 0;     // `clk` value at which this blit finishes (see runEvents)
        uint16_t x = 0, y = 0;
        uint8_t tileBank = 0, tileId = 0, tileMode = 0;
        uint8_t blendMode = 0;
        uint8_t attrs = 0;           // TILE_ATTR_* snapshot at dispatch ($00FC bits 2-5)
        uint16_t clipY0 = 0, clipY1 = 0;   // TILE_ATTR_CLIP: only rows in [clipY0, clipY1) are written ($0204/$0206 at dispatch)
        std::array<uint8_t, 4> translucency{};   // tileTranslucency snapshot at dispatch
        // Line blits (channels NUM_BLIT_CHANNELS..): kind 1, x/y above are the destination pixel (x signed), the
        // source is `lineCount` 4bpp palette indices at PPU address `lineSrc`. See startLineBlit().
        uint8_t kind = 0;            // 0 = tile blit, 1 = line blit
        uint16_t lineSrc = 0, lineCount = 0;
        // Blits dispatched by the tile-row queue keep their rows in the absolute row space (frameRowOffset + y), so a
        // blit that completes after the frame boundary still lands in the frame it was queued for; x/y above are unused for y.
        bool fromQueue = false;
        int64_t absY = 0, absClip0 = 0, absClip1 = 0;
        // The rectangle a queued blit writes (absolute rows, screen columns). A queued blit only has to retire after
        // the older in-flight blits it overlaps - ones that touch other pixels cannot change what it draws - so
        // alternating clipped halves of different cost no longer hold each other's channels up (see stepQueueEngine).
        int64_t rowLo = 0, rowHi = 0;
        int32_t colLo = 0, colHi = 0;
    };
    // 8, not 4: the boot ROM splash's band-0 vblank-tail dispatch (see
    // ZPbootROM/ppu/code/gen_mosaic.py) issues 8 TILEDRAWs per H-blank call
    // to fit all 32 of a band's columns into the 4 scanlines the vblank tail
    // gives it. With only 4 channels, the 5th-8th dispatch in that call had to
    // wait for a channel (a ~264-cycle stall each when this was a synchronous
    // draw, not a cheap dispatch) and blew the 340-cycle H-blank budget (the line was 340 cycles before core revision 13), corrupting the ISR
    // via re-entry. 8 channels means all 8 dispatches stay on the cheap
    // async path - strictly more headroom than 4 gave the existing 4-per-
    // H-blank general schedule too (bands 1-31), never less.
    static constexpr size_t NUM_BLIT_CHANNELS = 8;
    // Line-blit channels live after the tile channels in the same array, so the completion bookkeeping below is
    // shared; TILEDRAW only ever looks at the first NUM_BLIT_CHANNELS, line launches only at the last four.
    static constexpr size_t NUM_LINE_CHANNELS = 4;
    static constexpr size_t NUM_ALL_CHANNELS = NUM_BLIT_CHANNELS + NUM_LINE_CHANNELS;
    std::array<BlitChannel, NUM_ALL_CHANNELS> blitChannels;
    // Derived state, not hashed: set where a channel is dispatched or retired and in resetBlitMirrors(), never read for
    // anything the struct has. The busy channels, sorted by (completeAt, channel index): the order they retire in. Each
    // entry is `completeAt << 4 | channel`; flight[0] is the next blit to finish and flight[nFlight - 1] the last, so a
    // completion is a pop from the front and "when is the last of them done" costs nothing.
    std::array<uint64_t, NUM_ALL_CHANNELS> flight{};
    uint32_t nFlight = 0;
    uint32_t tileBusyMask = 0;                             // bit i: tile channel i is busy
    // The rectangle a tile channel's blit writes, as the queue's overlap test wants it; a TILEDRAW blit (no known extent)
    // is the whole plane, so it overlaps everything, like `!fromQueue` used to.
    std::array<int64_t, NUM_BLIT_CHANNELS> ovRowLo{}, ovRowHi{};
    std::array<int32_t, NUM_BLIT_CHANNELS> ovColLo{}, ovColHi{};
    void resetBlitMirrors();
    void flightInsert(uint64_t completeAt, unsigned channel);
    void retireFront();
    // Line-blit port ($0340-$034F, plain PPU memory the launch reads): src, x, y, count (words), $0348 launches.
    // $034A reads back busy/overrun status. The splash's gen_mosaic.py and docs/ppu.md describe the contract.
    static constexpr uint16_t LINE_PORT_SRC = 0x0340, LINE_PORT_X = 0x0342, LINE_PORT_Y = 0x0344,
                              LINE_PORT_COUNT = 0x0346, LINE_PORT_GO = 0x0348, LINE_PORT_STATUS = 0x034A;
    static constexpr uint8_t LINE_STATUS_OVERRUN = 0x80;   // a launch found all four line channels busy and was dropped
    bool lineOverrun = false;

    // Tile-row queue ("write-ahead", docs/ppu.md): a program queues whole rows of tilemap cells ahead of the beam and
    // the hardware dispatches them onto the blit channels when their rows become writable. Port words are plain PPU
    // memory the launch reads, $0360 launches (the low byte of the word MOVDP stores); capacity is counted in cells.
    static constexpr uint16_t TQ_SRC = 0x0350, TQ_COUNT = 0x0352, TQ_X = 0x0354, TQ_Y = 0x0356, TQ_CLIP0 = 0x0358,
                              TQ_CLIP1 = 0x035A, TQ_FLAGS = 0x035C, TQ_FRAME = 0x035E, TQ_GO = 0x0360, TQ_MODE = 0x0362, TQ_STATUS = 0x0364,
                              TQ_HWFRAME = 0x0366;   // $0360: a write launches, a read is the cells still pending; $0366 reads the hardware's frame
    static constexpr uint16_t TQ_F_CLIP = 0x01, TQ_F_FLIPX = 0x02, TQ_F_FLIPY = 0x04, TQ_F_WRAP = 0x08,
                              TQ_F_TRANSPARENT = 0x40;   // bits 4-5: tile mode
    static constexpr uint8_t TQ_STATUS_LATE = 0x01, TQ_STATUS_OVERFLOW = 0x02;
    static constexpr uint32_t TQ_CAPACITY = 128;          // cells pending across all queued rows
    static constexpr uint32_t TQ_DISPATCH_CYCLES = 8;     // hardware cost of reading a cell and dispatching it
    struct RowCmd {
        uint16_t src = 0, count = 0, x = 0, flags = 0;
        uint16_t next = 0;                                // cells already dispatched
        int64_t absY = 0, absClip0 = 0, absClip1 = 0;
        uint8_t blendMode = 0;
        std::array<uint8_t, 4> translucency{};
        // What every cell of the row shares, worked out once when it is launched (derived, not hashed): the rows a cell
        // writes (the tile's eight cut down to the clip window - none at all makes the cell empty), what its blit costs,
        // and how the flags turn a cell's attribute word into the blit's attributes: (w1 & mask) ^ xorAttrs | orAttrs.
        int64_t r0 = 0, r1 = 0;
        uint32_t blitCost = 0;
        uint8_t xorAttrs = 0, orAttrs = 0, tileMode = 0;
        bool empty = false;
    };
    std::array<RowCmd, TQ_CAPACITY> tqRing;
    uint32_t tqHead = 0, tqCount = 0;                     // queued rows (ring), and how many
    uint32_t tqPending = 0;                               // cells not yet dispatched, across them
    // The engine looks at the head cell on the first cycle >= tqAt in which it could dispatch it (a tile channel is free,
    // or the cell is empty and needs none): tqAt is one past a dispatch plus TQ_DISPATCH_CYCLES, one past an empty cell,
    // one past the launch that found the queue empty. A cell whose rows are not writable yet is held (tqHeld) until the
    // display moves on (revalidateHold()) or a render-mode change; the row it waits on is tqHeldRow.
    uint64_t tqAt = 0;
    bool tqHeld = false;
    int64_t tqHeldRow = 0;
    bool tqLate = false, tqOverflow = false;              // sticky status bits ($0364)
    bool writeAhead = false;                              // $0362 bit 0: the $C000-$FFFF windows read open bus
    void resetQueue();
    void startRowCommand();                               // the $035E write
    void stepQueueEngine();                               // the head cell, at cycle `clk`: hold it or dispatch it onto a free tile channel
    void releaseHold();                                   // look at a held cell again from the next cycle on (a mode change may free it)
    // PPU time. Blits and the queue run on the cycle count `clk` (cycles taken, only while Running): every event at or
    // before it has happened, and evAt is the cycle of the next one (a blit completing, the queue's next look), so an
    // instruction sees the blits and the queue exactly as they are on its cycle with nothing to catch up on, and a
    // stretch of cycles with no event costs one compare. Nothing here moves while the PPU is not Running.
    uint64_t clk = 0;
    uint64_t evAt = UINT64_MAX;
    void runEvents(uint64_t upTo);                        // everything that happens on cycles clk+1 .. upTo, in order; clk = upTo
    void refreshEvent();                                  // evAt after anything changed
    void revalidateHold();                                // the display may have moved since the last call: free a held cell it unblocked
    uint32_t finishTickMany(uint32_t total, uint32_t left, uint32_t stall, uint64_t now);
    uint8_t framebufferByte(uint16_t address) const;      // the raw rolling buffer, unaffected by writeAhead (blits read it back)
    void startLineBlit();                       // the $0348 write: snapshot the port into a free line channel
    void executeLineBlit(const BlitChannel& channel);
    // Count of currently-busy blitChannels entries, kept in lockstep with each ch.busy flip (a dispatch sets it,
    // retireFront() clears it on completion).
    int busyBlitChannelCount = 0;
    // Blit timing is by absolute time (clk, below), not per-channel countdowns: each channel finishes at its own
    // completeAt, and the sorted `flight` list says which is next. Blits finish in completeAt order, channel index order
    // within a cycle.
    uint64_t lastTileCompleteAt = 0;   // completeAt of the newest tile blit: the next one retires strictly after it (issue order)

    // Palette system
    std::array<uint16_t, 16> palette16{};           // 16-color palette (16-bit BBGR format)
    std::array<uint32_t, 256> palette256{};         // 256-color palette (32-bit RGBA format)
    // expandPalette16(palette16[i]) precomputed for all 16 entries. The
    // 4bpp TILEDRAW blit path (executeBlit) looks up this cache instead of
    // recomputing the RGBA expansion per pixel - only 16 distinct palette
    // entries exist, so a 64-pixel tile draw would otherwise redo the same
    // ~16 expansions up to 4x each. Refreshed at every palette16 write site
    // (reset(), CLRPALETTE, loadPalette16()) - see refreshPalette16Expanded().
    std::array<uint32_t, 16> palette16Expanded{};
    void refreshPalette16Expanded();
    std::array<uint8_t, 4> tileTranslucency{};      // Translucency for last 4 tiles (4 bits each)
    // applyTranslucency(palette16Expanded[i], tile) for the 16 entries, per translucency slot (tile & 3), in both the
    // 32-bit form and the 16-bit one the framebuffer stores. Derived from palette16Expanded + tileTranslucency (not
    // hashed): rebuilt lazily by tileLut(), dropped by dropTileLuts() wherever either of those changes.
    struct TileLut { uint32_t c32[16]; uint16_t c16[16]; uint8_t opq[16]; };   // opq: alpha != 0 (a transparent blit stores it)
    std::array<TileLut, 4> tileLutCache{};
    uint8_t tileLutValid = 0;                         // bit s: tileLutCache[s] is current
    // The same for the 256-entry palette (8bpp tiles). A rebuild is 256 entries, more than one tile reads, so after a
    // palette change the first few blits take the direct path and only a palette that stays put gets a table.
    struct TileLut256 { uint32_t c32[256]; uint16_t c16[256]; uint8_t opq[256]; };
    std::array<TileLut256, 4> tileLut256Cache{};
    uint8_t tileLut256Valid = 0;
    uint8_t tileLut256Warmup = 0;                     // blits since the last change that did without a table
    static constexpr uint8_t LUT256_WARMUP_BLITS = 2;
    void dropTileLuts() { tileLutValid = 0; tileLut256Valid = 0; tileLut256Warmup = 0; }
    const TileLut& tileLut(uint8_t tileId);
    const TileLut256* tileLut256(uint8_t tileId);   // nullptr while the palette is still settling
    uint8_t tileBlendMode = 0;                       // Blend mode for tiles (0-3)
    uint8_t tileAttrs = 0;                            // TILE_ATTR_* in force, latched from $00FC by its own push flag ($00FD bit 4)

    // Video Output Coprocessor (VOC) registers at $00F0-$00FF
    struct VOCRegisters {
        uint8_t renderModeControl;      // $00F0: CRHVPLIW
        uint8_t paletteAddrLow;         // $00F1: Palette address LSB
        uint8_t paletteAddrHigh;        // $00F2: Palette address MSB
        uint8_t bankOrder[8];           // $00F3-$00FA: Framebuffer bank order
        uint8_t autoRollToggle;         // $00FB: Auto-roll toggle
        uint8_t translucency[4];        // $00FC-$00FF: Tile translucency (32-bit)
    } vocRegisters = {};

    // VOC bit masks for $00F0
    static constexpr uint8_t VOC_COLOR_DEPTH = 0x80;   // Bit 7: 0=16-bit, 1=32-bit
    static constexpr uint8_t VOC_ROLLING_MODE = 0x40;  // Bit 6: 0=block, 1=single
    static constexpr uint8_t VOC_HBLANK_INT = 0x20;    // Bit 5: H-blank interrupt enable
    static constexpr uint8_t VOC_VBLANK_INT = 0x10;    // Bit 4: V-blank interrupt enable
    static constexpr uint8_t VOC_PALETTE_MODE = 0x08;  // Bit 3: 0=16 color, 1=256 color
    static constexpr uint8_t VOC_RESET = 0x01;         // Bit 0: Reset switch

    // Per-blit tile attributes: $00FC bits 2-5, latched by their own push flag ($00FD bit 4), separate from the blend mode's, and
    // writable in either color depth, unlike the rest of $00FC-$00FF. Stray bytes there (a program image DMA'd over the
    // VOC window) do nothing until a push, which is why they are not sampled live. docs/ppu.md "Tile attributes".
    static constexpr uint8_t TILE_ATTR_TRANSPARENT = 0x04;  // Bit 2: palette entries with alpha 0 leave the framebuffer alone
    static constexpr uint8_t TILE_ATTR_HFLIP = 0x08;        // Bit 3: mirror left-right
    static constexpr uint8_t TILE_ATTR_VFLIP = 0x10;        // Bit 4: mirror top-bottom
    static constexpr uint8_t TILE_ATTR_CLIP = 0x20;         // Bit 5: write only the rows in [$0204, $0206) (screen rows)
    static constexpr uint8_t TILE_ATTR_MASK = TILE_ATTR_TRANSPARENT | TILE_ATTR_HFLIP | TILE_ATTR_VFLIP | TILE_ATTR_CLIP;

    // Execution methods
    void executeInstruction();
    // Decoded-instruction cache (see the "Instruction semantics" block in ppu.cpp): one entry per code address
    // (an instruction is 2 bytes at any address), filled on first execution, dropped when a byte of it is written.
    struct DecodedOp;
    using OpFn = void (*)(PPU&, const DecodedOp&);
    struct DecodedOp {
        OpFn fn = nullptr;       // null = not decoded (or invalidated)
        uint16_t operand = 0;    // a field that does not fit a/b, per instruction
        uint8_t a = 0, b = 0;    // the instruction's register/immediate fields
        uint16_t cost = 1;       // base cycle cost; a handler that costs more overrides instrCycles itself
    };
    template <void (PPU::*Handler)(const DecodedOp&)>
    static void opTrampoline(PPU& ppu, const DecodedOp& d) { (ppu.*Handler)(d); }
    std::vector<DecodedOp> decoded;
    DecodedOp decodeOp(uint16_t ep) const;
    // The one place an instruction or the bus writes a byte of PPU memory: the decoded-instruction cache, and the watch.
    uint8_t watchCount = 0;
    bool watchHit = false;
    uint16_t watchAddr[8] = {};
    void noteStore(uint16_t address, uint8_t value) {
        if (memory[address] == value) return;
        for (int i = 0; i < watchCount; i++)
            if (watchAddr[i] == address) { watchHit = true; return; }
    }
    void storeByte(uint16_t address, uint8_t value) {
        if (watchCount) [[unlikely]] noteStore(address, value);
        memory[address] = value;
        codeWritten(address);
    }
    void codeWritten(uint16_t address) {
        decoded[address].fn = nullptr;
        decoded[(address - 1) & 0xFFFF].fn = nullptr;
    }
    void codeWrittenRange(uint32_t first, uint32_t count) {
        for (uint32_t i = 0; i <= count; i++) decoded[(first + i - 1) & 0xFFFF].fn = nullptr;
    }
#define ZP_PPU_OPS(X) X(DEFCALL) X(MOVXP) X(NOP) X(SWAPREG) X(CLR) X(CMP) X(CLRF) X(JMZ) X(JMG) X(JNZ) X(JNG) \
    X(INC) X(DEC) X(ADD) X(SUB) X(MUL) X(INTDIV) X(TARREG) X(SETBYTE) X(BUILD) X(CPREG) X(SETPOS) X(SETTILE) \
    X(SETDP) X(MOVDP) X(SETRENDMOD) X(PALETTE16_LOAD) X(PALETTE256_LOAD) X(JMR) X(MOV) X(SETREGBANK) X(CLRTILE) \
    X(CLRPALETTE) X(TILEDRAW) X(SETTILEBANK) X(CALL) X(GBLS)
#define ZP_DECLARE_OP(name) void op##name(const DecodedOp& d);
    ZP_PPU_OPS(ZP_DECLARE_OP)
#undef ZP_DECLARE_OP
#undef ZP_PPU_OPS


    // Edge-detect vblank/hblank and latch into vblankPending/hblankPending.
    // Must be called every cycle, including mid-stall - see its own comment
    // in ppu.cpp for why sampling only at instruction boundaries can drop a
    // short blank pulse entirely.
    void latchInterruptEdges();

    // Recognize a pending V/H-blank interrupt at an instruction boundary. If one
    // fires, pushes the return address, redirects the execution pointer to the
    // handler, sets the interrupt-entry stall, and returns true. Shared by tick()
    // and runBatch() so their boundary behavior can never drift apart.
    bool serviceInterrupts();

    // Move PPU time to cycle `to` (see runEvents()): called from every place tick()/runBatch() advances time, so a
    // blit's completion timing is identical however the PPU's own instruction stream is interleaved around it.
    void advanceTo(uint64_t to) {
        if (to >= evAt) runEvents(to); else clk = to;
    }

    // Perform one channel's blit (the pixel loop TILEDRAW used to run
    // synchronously), reading only the snapshotted state captured at
    // dispatch time - never the live current* tile/position registers,
    // which may have moved on to a different draw by the time this runs.
    void executeBlit(const BlitChannel& channel);

    // Push a return address onto the PPU stack (SP grows upward, matching the
    // ppuasm PUSH/RET shorthands). Shared by every interrupt-entry path and
    // CALL. Wraps SP and the memory index at 16 bits rather than indexing past
    // the end of `memory` when SP is 0xFFFF.
    void pushReturnAddress(uint16_t returnAddr) {
        registers[REG_SP] += 2;
        storeByte(registers[REG_SP], returnAddr & 0xFF);
        storeByte((registers[REG_SP] + 1) & 0xFFFF, (returnAddr >> 8) & 0xFF);
    }

    // Helper methods
    // Inline hot-path flag functions to eliminate function call overhead
    inline void updateFlags(uint16_t result, uint16_t left, uint16_t right) {
        flags.zero = (result == 0);
        flags.greater = (left > right);
    }

    inline void clearFlags() {
        flags.zero = false;
        flags.greater = false;
    }

    // Memory-mapped I/O handlers
    void handleMemoryWrite(uint16_t address, uint8_t value);
    uint8_t handleMemoryRead(uint16_t address) const;

    // VOC helper methods
    void handleVOCWrite(uint16_t address, uint8_t value);
    uint8_t handleVOCRead(uint16_t address) const;
    void applyVOCRenderMode();
    void applyVOCReset();

    // Palette helper methods
    void loadPalette16();
    void loadPalette256();
    uint32_t applyTranslucency(uint32_t color, uint8_t tileIndex);

    // Expand a 16-bit palette entry (BBBBBGGGGGRRRRR-A) to 32-bit RGBA,
    // shared by TILEDRAW's 16-color paths and the palette-indexed pixel-draw
    // port.
    static inline uint32_t expandPalette16(uint16_t pal16) {
        uint8_t r5 = (pal16 >> 1) & 0x1F;
        uint8_t g5 = (pal16 >> 6) & 0x1F;
        uint8_t b5 = (pal16 >> 11) & 0x1F;
        uint8_t a1 = pal16 & 0x01;
        uint8_t r8 = (r5 << 3) | (r5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        uint8_t a8 = a1 ? 0xFF : 0x00;
        return (r8 << 24) | (g8 << 16) | (b8 << 8) | a8;
    }
    uint32_t blendColors(uint32_t src, uint32_t dst, uint8_t mode, uint8_t alpha);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_PPU_H
