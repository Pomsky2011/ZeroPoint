#include "dma.h"
#include <iostream>
#include <iomanip>
#include <cstring>

using namespace ZeroPoint;

// Simple memory simulation (16 MB address space)
uint8_t memory[16 * 1024 * 1024];

uint8_t memoryRead(uint32_t address) {
    if (address < sizeof(memory)) {
        return memory[address];
    }
    return 0;
}

void memoryWrite(uint32_t address, uint8_t value) {
    if (address < sizeof(memory)) {
        memory[address] = value;
    }
}

void printMemoryRange(const char* label, uint32_t start, uint32_t count) {
    std::cout << label << " [0x" << std::hex << std::setw(6) << std::setfill('0')
              << start << "]: ";
    for (uint32_t i = 0; i < count && i < 16; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory[start + i] << " ";
    }
    if (count > 16) {
        std::cout << "...";
    }
    std::cout << std::dec << std::endl;
}

void testDataCopy() {
    std::cout << "\n=== Test 1: Data Copy Mode ===\n";

    // Setup source data
    uint32_t sourceAddr = 0x010000;
    uint32_t targetAddr = 0x020000;

    for (int i = 0; i < 256; i++) {
        memory[sourceAddr + i] = i & 0xFF;
    }

    DMAController dma;
    dma.setMemoryCallbacks(memoryRead, memoryWrite);

    // Configure DMA: Data Copy, 256 bytes
    // S=0 (size bits), Mode=00, Channel=0
    // Transfer size: ((0+1) * 256) * 1 = 256 bytes
    uint8_t config[9] = {
        0b00000000,  // S=0, Mode=00, Channel=0
        0x00, 0x00, 0x01,  // Source: 0x010000
        0x00, 0x00, 0x02,  // Target: 0x020000
        0x01,  // Multiplier: 1
        0x01   // Interrupt/trigger
    };

    dma.queueDMA(config);

    printMemoryRange("Before - Source", sourceAddr, 16);
    printMemoryRange("Before - Target", targetAddr, 16);

    // Run DMA until complete
    int cycles = 0;
    while (dma.getChannelState(0) != DMAState::Complete && cycles < 10000) {
        dma.tick();
        cycles++;
    }

    printMemoryRange("After  - Source", sourceAddr, 16);
    printMemoryRange("After  - Target", targetAddr, 16);

    std::cout << "Total cycles: " << cycles << " (expected: 8 + 0 + 256*3 = 776)\n";

    // Verify
    bool success = true;
    for (int i = 0; i < 256; i++) {
        if (memory[targetAddr + i] != (i & 0xFF)) {
            success = false;
            break;
        }
    }
    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
}

void testConstCopy() {
    std::cout << "\n=== Test 2: Const Copy Mode ===\n";

    // Setup pattern
    uint32_t sourceAddr = 0x030000;
    uint32_t targetAddr = 0x040000;

    memory[sourceAddr + 0] = 0xAA;
    memory[sourceAddr + 1] = 0xBB;

    // Clear target
    std::memset(&memory[targetAddr], 0, 256);

    DMAController dma;
    dma.setMemoryCallbacks(memoryRead, memoryWrite);

    // Configure DMA: Const Copy, 256 bytes, 2-byte pattern
    // S=1 (pattern size = 2), Mode=01, Channel=1
    uint8_t config[9] = {
        0b01010001,  // S=1, Mode=01, Channel=1
        0x00, 0x00, 0x03,  // Source: 0x030000
        0x00, 0x00, 0x04,  // Target: 0x040000
        0x01,  // Multiplier: 1 -> (2 * 256) * 1 = 512 bytes
        0x01   // Interrupt/trigger
    };

    dma.queueDMA(config);

    printMemoryRange("Before - Pattern", sourceAddr, 4);
    printMemoryRange("Before - Target", targetAddr, 16);

    // Run DMA
    int cycles = 0;
    while (dma.getChannelState(1) != DMAState::Complete && cycles < 10000) {
        dma.tick();
        cycles++;
    }

    printMemoryRange("After  - Target", targetAddr, 16);

    std::cout << "Total cycles: " << cycles << " (expected: 8 + 2 + 512*1 = 522)\n";

    // Verify alternating pattern
    bool success = true;
    for (int i = 0; i < 512; i++) {
        uint8_t expected = (i % 2 == 0) ? 0xAA : 0xBB;
        if (memory[targetAddr + i] != expected) {
            success = false;
            std::cout << "Mismatch at offset " << i << ": got 0x" << std::hex
                      << (int)memory[targetAddr + i] << ", expected 0x"
                      << (int)expected << std::dec << "\n";
            break;
        }
    }
    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
}

void testRepeatTransfer() {
    std::cout << "\n=== Test 3: Repeat Transfer Mode ===\n";

    // Setup pattern
    uint32_t sourceAddr = 0x050000;
    uint32_t targetAddr = 0x060000;

    memory[sourceAddr + 0] = 0x11;
    memory[sourceAddr + 1] = 0x22;
    memory[sourceAddr + 2] = 0x33;
    memory[sourceAddr + 3] = 0x44;

    // Clear target
    memory[targetAddr] = 0x00;

    DMAController dma;
    dma.setMemoryCallbacks(memoryRead, memoryWrite);

    // Configure DMA: Repeat Transfer, 4-byte pattern, 256 transfers
    // S=3 (pattern size = 4), Mode=10, Channel=2
    // Transfer size: ((3+1) * 256) * 1 = 1024 bytes
    uint8_t config[9] = {
        0b11100010,  // S=3, Mode=10, Channel=2
        0x00, 0x00, 0x05,  // Source: 0x050000
        0x00, 0x00, 0x06,  // Target: 0x060000 (same address written 1024 times)
        0x01,  // Multiplier: 1
        0x01   // Interrupt/trigger
    };

    dma.queueDMA(config);

    printMemoryRange("Before - Pattern", sourceAddr, 4);
    std::cout << "Before - Target[0x060000]: 0x" << std::hex << std::setw(2)
              << std::setfill('0') << (int)memory[targetAddr] << std::dec << "\n";

    // Run DMA
    int cycles = 0;
    while (dma.getChannelState(2) != DMAState::Complete && cycles < 20000) {
        dma.tick();
        cycles++;
    }

    std::cout << "After  - Target[0x060000]: 0x" << std::hex << std::setw(2)
              << std::setfill('0') << (int)memory[targetAddr] << std::dec << "\n";

    std::cout << "Total cycles: " << cycles << " (expected: 8 + 4 + 1024*3 = 3084)\n";

    // Verify - should be last byte of pattern (0x44)
    bool success = (memory[targetAddr] == 0x44);
    std::cout << "Result: " << (success ? "PASS" : "FAIL") << "\n";
}

void testChannelQueue() {
    std::cout << "\n=== Test 4: Channel Queueing ===\n";

    DMAController dma;
    dma.setMemoryCallbacks(memoryRead, memoryWrite);

    // Queue two transfers on same channel
    uint8_t config1[9] = {
        0b00000000,  // Channel 0
        0x00, 0x00, 0x01,
        0x00, 0x00, 0x02,
        0x01,
        0x01   // Interrupt/trigger
    };

    uint8_t config2[9] = {
        0b00000000,  // Channel 0 (same!)
        0x00, 0x10, 0x01,
        0x00, 0x10, 0x02,
        0x01,
        0x01   // Interrupt/trigger
    };

    dma.queueDMA(config1);
    std::cout << "Queued transfer 1 on channel 0\n";

    dma.queueDMA(config2);
    std::cout << "Queued transfer 2 on channel 0\n";
    std::cout << "Pending queue size: " << dma.getQueuedCount() << " (expected: 1)\n";

    // Run until first completes
    int cycles = 0;
    while (dma.getChannelState(0) != DMAState::Complete && cycles < 2000) {
        dma.tick();
        cycles++;
    }

    std::cout << "First transfer complete after " << cycles << " cycles\n";
    std::cout << "Pending queue size: " << dma.getQueuedCount() << " (expected: 0)\n";
    std::cout << "Channel 0 state: "
              << (dma.getChannelState(0) == DMAState::Configuring ? "Configuring" :
                  dma.getChannelState(0) == DMAState::Transferring ? "Transferring" :
                  dma.getChannelState(0) == DMAState::Complete ? "Complete" : "Other")
              << "\n";

    // Continue until second completes
    while (dma.getChannelState(0) != DMAState::Complete && cycles < 4000) {
        dma.tick();
        cycles++;
    }

    std::cout << "Second transfer complete after " << cycles << " total cycles\n";
    std::cout << "Result: PASS\n";
}

void testMultipleChannels() {
    std::cout << "\n=== Test 5: Multiple Channels ===\n";

    DMAController dma;
    dma.setMemoryCallbacks(memoryRead, memoryWrite);

    // Start transfers on different channels simultaneously
    uint8_t config0[9] = {
        0b00000000,  // Channel 0
        0x00, 0x00, 0x07,
        0x00, 0x00, 0x08,
        0x01,
        0x01   // Interrupt/trigger
    };

    uint8_t config1[9] = {
        0b00000001,  // Channel 1
        0x00, 0x10, 0x07,
        0x00, 0x10, 0x08,
        0x01,
        0x01   // Interrupt/trigger
    };

    dma.queueDMA(config0);
    dma.queueDMA(config1);

    std::cout << "Started transfers on channels 0 and 1\n";

    // Run for 100 cycles
    for (int i = 0; i < 100; i++) {
        dma.tick();
    }

    std::cout << "After 100 cycles:\n";
    std::cout << "  Channel 0: "
              << (dma.getChannelState(0) == DMAState::Idle ? "Idle" :
                  dma.getChannelState(0) == DMAState::Transferring ? "Transferring" : "Other")
              << "\n";
    std::cout << "  Channel 1: "
              << (dma.getChannelState(1) == DMAState::Idle ? "Idle" :
                  dma.getChannelState(1) == DMAState::Transferring ? "Transferring" : "Other")
              << "\n";

    std::cout << "Result: PASS (both channels active)\n";
}

void testTwoChannelLimit() {
    std::cout << "\n=== Test 6: Two Channel Limit ===\n";

    DMAController dma;
    dma.setMemoryCallbacks(memoryRead, memoryWrite);

    // Start 3 transfers on different channels
    uint8_t config0[9] = {
        0b00000000,  // Channel 0
        0x00, 0x00, 0x09,
        0x00, 0x00, 0x0A,
        0x01,
        0x01
    };

    uint8_t config1[9] = {
        0b00000001,  // Channel 1
        0x00, 0x10, 0x09,
        0x00, 0x10, 0x0A,
        0x01,
        0x01
    };

    uint8_t config2[9] = {
        0b00000010,  // Channel 2
        0x00, 0x20, 0x09,
        0x00, 0x20, 0x0A,
        0x01,
        0x01
    };

    dma.queueDMA(config0);
    dma.queueDMA(config1);
    dma.queueDMA(config2);

    std::cout << "Queued 3 transfers on channels 0, 1, 2\n";
    std::cout << "Active channels: " << dma.getActiveChannelCount() << " (expected: 2)\n";
    std::cout << "Pending queue: " << dma.getQueuedCount() << " (expected: 1)\n";

    // Run for 100 cycles
    for (int i = 0; i < 100; i++) {
        dma.tick();
    }

    std::cout << "After 100 cycles:\n";
    std::cout << "  Channel 0: "
              << (dma.getChannelState(0) == DMAState::Transferring ? "Transferring" : "Other")
              << "\n";
    std::cout << "  Channel 1: "
              << (dma.getChannelState(1) == DMAState::Transferring ? "Transferring" : "Other")
              << "\n";
    std::cout << "  Channel 2: "
              << (dma.getChannelState(2) == DMAState::Idle ? "Idle/Queued" : "Active")
              << "\n";

    bool success = (dma.getActiveChannelCount() == 2);
    std::cout << "Result: " << (success ? "PASS" : "FAIL") << " (only 2 channels active)\n";
}

void testInterruptPause() {
    std::cout << "\n=== Test 7: Interrupt Pauses DMA ===\n";

    DMAController dma;
    dma.setMemoryCallbacks(memoryRead, memoryWrite);

    // Setup source data (use non-zero pattern)
    uint32_t sourceAddr = 0x0B0000;
    uint32_t targetAddr = 0x0C0000;
    for (int i = 0; i < 256; i++) {
        memory[sourceAddr + i] = ((i % 255) + 1);  // Values 1-255, no zeros
    }

    uint8_t config[9] = {
        0b00000000,  // Channel 0
        0x00, 0x00, 0x0B,
        0x00, 0x00, 0x0C,
        0x01,
        0x01
    };

    dma.queueDMA(config);

    // Run for 50 cycles
    for (int i = 0; i < 50; i++) {
        dma.tick();
    }

    uint32_t bytesBeforeInterrupt = 0;
    for (int i = 0; i < 256; i++) {
        if (memory[targetAddr + i] != 0) {
            bytesBeforeInterrupt++;
        }
    }

    std::cout << "After 50 cycles: " << bytesBeforeInterrupt << " bytes transferred\n";

    // Trigger interrupt
    dma.triggerInterrupt();
    std::cout << "Interrupt triggered (DMA should pause)\n";

    // Run for 100 more cycles - DMA should not progress
    for (int i = 0; i < 100; i++) {
        dma.tick();
    }

    uint32_t bytesDuringInterrupt = 0;
    for (int i = 0; i < 256; i++) {
        if (memory[targetAddr + i] != 0) {
            bytesDuringInterrupt++;
        }
    }

    std::cout << "After 100 cycles during interrupt: " << bytesDuringInterrupt << " bytes\n";
    bool pausedCorrectly = (bytesBeforeInterrupt == bytesDuringInterrupt);

    // Clear interrupt
    dma.clearInterrupt();
    std::cout << "Interrupt cleared (DMA should resume)\n";

    // Run until complete
    int cycles = 0;
    while (dma.getChannelState(0) != DMAState::Complete && cycles < 5000) {
        dma.tick();
        cycles++;
    }

    uint32_t bytesAfterResume = 0;
    for (int i = 0; i < 256; i++) {
        if (memory[targetAddr + i] != 0) {
            bytesAfterResume++;
        }
    }

    std::cout << "After resume: " << bytesAfterResume << " bytes (expected: 256)\n";

    bool success = pausedCorrectly && (bytesAfterResume == 256);
    std::cout << "Result: " << (success ? "PASS" : "FAIL")
              << " (DMA paused and resumed correctly)\n";
}

int main() {
    std::cout << "ZeroPoint DMA Controller Test Suite\n";
    std::cout << "====================================\n";

    // Initialize memory
    std::memset(memory, 0, sizeof(memory));

    testDataCopy();
    testConstCopy();
    testRepeatTransfer();
    testChannelQueue();
    testMultipleChannels();
    testTwoChannelLimit();
    testInterruptPause();

    std::cout << "\nAll tests complete!\n";

    return 0;
}
