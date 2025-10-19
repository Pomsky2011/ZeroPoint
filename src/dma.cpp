#include "dma.h"
#include <cstring>

namespace ZeroPoint {

DMAController::DMAController()
    : memoryRead(nullptr)
    , memoryWrite(nullptr)
    , interruptActive(false)
{
    reset();
}

DMAController::~DMAController() {
}

void DMAController::reset() {
    for (auto& channel : channels) {
        channel.state = DMAState::Idle;
        channel.cyclesRemaining = 0;
        channel.bytesTransferred = 0;
        channel.totalBytes = 0;
    }

    // Clear the queue
    while (!pendingQueue.empty()) {
        pendingQueue.pop();
    }

    interruptActive = false;
}

bool DMAController::queueDMA(const uint8_t config[9]) {
    DMAConfig dmaConfig;

    // Parse configuration
    dmaConfig.mode = config[0];

    // Parse 24-bit source address (bytes 1-3, little endian)
    dmaConfig.sourceAddr = config[1] | (config[2] << 8) | (config[3] << 16);

    // Parse 24-bit target address (bytes 4-6, little endian)
    dmaConfig.targetAddr = config[4] | (config[5] << 8) | (config[6] << 16);

    // Parse size multiplier
    dmaConfig.sizeMultiplier = config[7];

    // Parse interrupt/trigger byte
    dmaConfig.interrupt = config[8];

    uint8_t channel = dmaConfig.getChannel();

    // Check if channel is valid
    if (channel >= 16) {
        return false;
    }

    // Check if total size is valid
    if (dmaConfig.getTotalSize() == 0 || dmaConfig.getTotalSize() > 1024) {
        return false;
    }

    // If channel is busy, queue the transfer
    if (isChannelBusy(channel)) {
        pendingQueue.push(dmaConfig);
        return true;
    }

    // If we already have 2 active channels, queue the transfer
    if (getActiveChannelCount() >= MAX_ACTIVE_CHANNELS) {
        pendingQueue.push(dmaConfig);
        return true;
    }

    // Otherwise, start immediately with 8-cycle configuration delay
    startTransfer(channel, dmaConfig);
    return true;
}

void DMAController::tick() {
    // If interrupt is active, pause all DMA
    if (interruptActive) {
        return;
    }

    // Process active channels (max 2 at a time)
    int processedCount = 0;
    for (uint8_t i = 0; i < 16 && processedCount < MAX_ACTIVE_CHANNELS; i++) {
        if (channels[i].state != DMAState::Idle && channels[i].state != DMAState::Complete) {
            processChannel(i);
            processedCount++;
        }
    }

    // Try to start pending transfers if we have room for more active channels
    int activeCount = getActiveChannelCount();
    while (!pendingQueue.empty() && activeCount < MAX_ACTIVE_CHANNELS) {
        DMAConfig& pending = pendingQueue.front();
        uint8_t channel = pending.getChannel();

        // Check if channel is now available
        if (!isChannelBusy(channel)) {
            startTransfer(channel, pending);
            pendingQueue.pop();
            activeCount++;
        } else {
            // Channel still busy, wait
            break;
        }
    }
}

bool DMAController::isChannelBusy(uint8_t channel) const {
    if (channel >= 16) return false;
    return channels[channel].state != DMAState::Idle &&
           channels[channel].state != DMAState::Complete;
}

DMAState DMAController::getChannelState(uint8_t channel) const {
    if (channel >= 16) return DMAState::Idle;
    return channels[channel].state;
}

void DMAController::startTransfer(uint8_t channel, const DMAConfig& config) {
    DMATransfer& transfer = channels[channel];

    transfer.config = config;
    transfer.state = DMAState::Configuring;
    transfer.cyclesRemaining = 8; // 8 cycles to configure
    transfer.bytesTransferred = 0;
    transfer.totalBytes = config.getTotalSize();
    std::memset(transfer.patternBuffer, 0, sizeof(transfer.patternBuffer));
}

void DMAController::processChannel(uint8_t channel) {
    DMATransfer& transfer = channels[channel];

    if (transfer.cyclesRemaining > 0) {
        transfer.cyclesRemaining--;

        // State transition when cycles complete
        if (transfer.cyclesRemaining == 0) {
            switch (transfer.state) {
                case DMAState::Configuring: {
                    // Move to initialization phase
                    uint32_t initCycles = getInitCycles(
                        transfer.config.getMode(),
                        transfer.config.getSizeBits()
                    );

                    if (initCycles > 0) {
                        transfer.state = DMAState::Initializing;
                        transfer.cyclesRemaining = initCycles;

                        // For modes that need initialization, read the pattern
                        DMAMode mode = transfer.config.getMode();
                        if (mode == DMAMode::ConstCopy || mode == DMAMode::ConstRepeat) {
                            // Read constant pattern during init
                            uint32_t patternSize = transfer.config.getPatternSize();
                            for (uint32_t i = 0; i < patternSize && i < 4; i++) {
                                if (memoryRead) {
                                    transfer.patternBuffer[i] = memoryRead(
                                        transfer.config.sourceAddr + i
                                    );
                                }
                            }
                        } else if (mode == DMAMode::RepeatTransfer) {
                            // Read repeat pattern during init
                            uint32_t patternSize = transfer.config.getPatternSize();
                            for (uint32_t i = 0; i < patternSize && i < 4; i++) {
                                if (memoryRead) {
                                    transfer.patternBuffer[i] = memoryRead(
                                        transfer.config.sourceAddr + i
                                    );
                                }
                            }
                        }
                    } else {
                        // No init needed, go straight to transferring
                        transfer.state = DMAState::Transferring;
                        transfer.cyclesRemaining = getCyclesPerByte(transfer.config.getMode());
                    }
                    break;
                }

                case DMAState::Initializing:
                    // Move to transferring
                    transfer.state = DMAState::Transferring;
                    transfer.cyclesRemaining = getCyclesPerByte(transfer.config.getMode());
                    break;

                case DMAState::Transferring:
                    // Execute one byte transfer
                    executeTransfer(transfer);

                    // Check if transfer is complete
                    if (transfer.bytesTransferred >= transfer.totalBytes) {
                        transfer.state = DMAState::Complete;
                        transfer.cyclesRemaining = 0;
                    } else {
                        // Set up for next byte
                        transfer.cyclesRemaining = getCyclesPerByte(transfer.config.getMode());
                    }
                    break;

                default:
                    break;
            }
        }
    }
}

void DMAController::executeTransfer(DMATransfer& transfer) {
    DMAMode mode = transfer.config.getMode();
    uint32_t currentByte = transfer.bytesTransferred;
    uint32_t sourceAddr = transfer.config.sourceAddr;
    uint32_t targetAddr = transfer.config.targetAddr;
    uint32_t patternSize = transfer.config.getPatternSize();

    uint8_t value = 0;

    switch (mode) {
        case DMAMode::DataCopy:
            // Read from source + offset, write to target + offset
            if (memoryRead) {
                value = memoryRead(sourceAddr + currentByte);
            }
            if (memoryWrite) {
                memoryWrite(targetAddr + currentByte, value);
            }
            break;

        case DMAMode::ConstCopy:
            // Write pattern[currentByte % patternSize] to target + offset
            value = transfer.patternBuffer[currentByte % patternSize];
            if (memoryWrite) {
                memoryWrite(targetAddr + currentByte, value);
            }
            break;

        case DMAMode::RepeatTransfer:
            // Write pattern[currentByte % patternSize] to same target address
            value = transfer.patternBuffer[currentByte % patternSize];
            if (memoryWrite) {
                memoryWrite(targetAddr, value);
            }
            break;

        case DMAMode::ConstRepeat:
            // Write pattern[currentByte % patternSize] to same target address
            value = transfer.patternBuffer[currentByte % patternSize];
            if (memoryWrite) {
                memoryWrite(targetAddr, value);
            }
            break;
    }

    transfer.bytesTransferred++;
}

uint32_t DMAController::getCyclesPerByte(DMAMode mode) const {
    switch (mode) {
        case DMAMode::DataCopy:       return 3;
        case DMAMode::ConstCopy:      return 1;
        case DMAMode::RepeatTransfer: return 3;
        case DMAMode::ConstRepeat:    return 2;
        default:                      return 1;
    }
}

uint32_t DMAController::getInitCycles(DMAMode mode, uint8_t sizeBits) const {
    switch (mode) {
        case DMAMode::DataCopy:       return 0;           // No init
        case DMAMode::ConstCopy:      return sizeBits + 1; // S + 1 cycles
        case DMAMode::RepeatTransfer: return sizeBits + 1; // S + 1 cycles
        case DMAMode::ConstRepeat:    return sizeBits + 1; // S + 1 cycles
        default:                      return 0;
    }
}

int DMAController::getActiveChannelCount() const {
    int count = 0;
    for (const auto& channel : channels) {
        if (channel.state != DMAState::Idle && channel.state != DMAState::Complete) {
            count++;
        }
    }
    return count;
}

void DMAController::triggerInterrupt() {
    interruptActive = true;
}

void DMAController::clearInterrupt() {
    interruptActive = false;
}

} // namespace ZeroPoint
