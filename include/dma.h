#ifndef ZEROPOINT_DMA_H
#define ZEROPOINT_DMA_H

#include <cstdint>
#include <cstddef>
#include <array>
#include <queue>

namespace ZeroPoint {

// DMA transfer modes
enum class DMAMode : uint8_t {
    DataCopy = 0b00,      // Copy memory range (3 cycles/byte, 0 init)
    ConstCopy = 0b01,     // Copy constant to range (1 cycle/byte, S+1 init)
    RepeatTransfer = 0b10,// Repeat pattern to address (3 cycles/byte, S+1 init)
    ConstRepeat = 0b11    // Repeat constant to address (2 cycles/byte, S+1 init)
};

// DMA configuration (9 bytes total)
struct DMAConfig {
    uint8_t mode;           // Byte 0: SS XX YYYY (size, mode, channel)
    uint32_t sourceAddr;    // Bytes 1-3: 24-bit source address
    uint32_t targetAddr;    // Bytes 4-6: 24-bit target address
    uint8_t sizeMultiplier; // Byte 7: size multiplier
    uint8_t interrupt;      // Byte 8: interrupt/trigger byte

    // Parsed fields
    uint8_t getSizeBits() const { return (mode >> 6) & 0x03; }
    DMAMode getMode() const { return static_cast<DMAMode>((mode >> 4) & 0x03); }
    uint8_t getChannel() const { return mode & 0x0F; }

    // Calculate total transfer size
    uint32_t getTotalSize() const {
        return ((getSizeBits() + 1) << 8) * sizeMultiplier;
    }

    // Get pattern size for repeat modes (S + 1)
    uint32_t getPatternSize() const {
        return getSizeBits() + 1;
    }
};

// DMA transfer state
enum class DMAState {
    Idle,
    Configuring,  // 8 cycles to configure
    Initializing, // Mode-specific init cycles
    Transferring, // Active transfer
    Complete
};

// Active DMA transfer
struct DMATransfer {
    DMAConfig config;
    DMAState state;
    uint32_t cyclesRemaining;  // Cycles left in current state
    uint32_t bytesTransferred; // How many bytes transferred
    uint32_t totalBytes;       // Total bytes to transfer
    uint8_t patternBuffer[4];  // Buffer for repeat mode patterns (max S=3, so 4 bytes)

    DMATransfer() : state(DMAState::Idle), cyclesRemaining(0),
                    bytesTransferred(0), totalBytes(0) {}
};

class DMAController {
public:
    DMAController();
    ~DMAController();

    // Queue a DMA transfer from 9-byte configuration
    // Bytes 0-7: Configuration, Byte 8: Interrupt/trigger
    bool queueDMA(const uint8_t config[9]);

    // Tick the DMA controller by one bus cycle
    void tick();

    // Check if a channel is busy
    bool isChannelBusy(uint8_t channel) const;

    // Get current state of a channel
    DMAState getChannelState(uint8_t channel) const;

    // Get number of queued transfers
    size_t getQueuedCount() const { return pendingQueue.size(); }

    // Get number of active channels
    int getActiveChannelCount() const;

    // Interrupt handling - pauses all DMA
    void triggerInterrupt();
    void clearInterrupt();
    bool isInterruptActive() const { return interruptActive; }

    // Reset all DMA transfers
    void reset();

    // Callback for memory access (to be set by memory controller)
    // For now we'll just track what would happen
    using MemoryReadCallback = uint8_t(*)(uint32_t address);
    using MemoryWriteCallback = void(*)(uint32_t address, uint8_t value);

    void setMemoryCallbacks(MemoryReadCallback read, MemoryWriteCallback write) {
        memoryRead = read;
        memoryWrite = write;
    }

private:
    // 16 DMA channels
    std::array<DMATransfer, 16> channels;

    // Queue for pending DMA transfers
    std::queue<DMAConfig> pendingQueue;

    // Memory access callbacks
    MemoryReadCallback memoryRead;
    MemoryWriteCallback memoryWrite;

    // Interrupt state - when active, all DMA is paused
    bool interruptActive;

    // Number of channels in an active (non-Idle, non-Complete) state. Lets
    // tick() early-out in O(1) when nothing is transferring, instead of
    // scanning all 16 channels every call. Incremented on startTransfer,
    // decremented when a channel reaches Complete; reset() zeroes it.
    int busyChannels;

    // Maximum of 2 channels can be active at once
    static constexpr int MAX_ACTIVE_CHANNELS = 2;

    // Process one cycle for a specific channel
    void processChannel(uint8_t channel);

    // Start a DMA transfer on a channel
    void startTransfer(uint8_t channel, const DMAConfig& config);

    // Execute one byte transfer based on mode
    void executeTransfer(DMATransfer& transfer);

    // Get cycles per byte for a mode
    uint32_t getCyclesPerByte(DMAMode mode) const;

    // Get init cycles for a mode
    uint32_t getInitCycles(DMAMode mode, uint8_t sizeBits) const;
};

} // namespace ZeroPoint

#endif // ZEROPOINT_DMA_H
