#include <fstream>
#include <cstring>
#include <iostream>

// .zpb file header structure (64 bytes)
#pragma pack(push, 1)
struct ZPBHeader {
    char magic[4];           // "ZPB\0"
    uint8_t version;         // Format version (currently 1)
    uint8_t reserved;        // Reserved for future use
    uint16_t headerSize;     // Header size (64 bytes)
    uint32_t romSize;        // ROM data size in bytes
    uint32_t entryPoint;     // Entry point address
    char title[32];          // Game title
    char developer[16];      // Developer name
};
#pragma pack(pop)

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <output.zpb>\n";
        return 1;
    }

    const char* outputFile = argv[1];

    // Create header
    ZPBHeader header;
    std::memset(&header, 0, sizeof(header));

    // Set magic number
    std::memcpy(header.magic, "ZPB\0", 4);

    // Set version
    header.version = 1;
    header.reserved = 0;
    header.headerSize = 64;

    // Create simple test ROM (1KB of test data)
    const uint32_t TEST_ROM_SIZE = 1024;
    header.romSize = TEST_ROM_SIZE;
    header.entryPoint = 0x00000000;

    // Set title and developer
    std::strncpy(header.title, "Test ROM", 32);
    std::strncpy(header.developer, "ZeroPoint Team", 16);

    // Write to file
    std::ofstream file(outputFile, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to create file: " << outputFile << "\n";
        return 1;
    }

    // Write header
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));

    // Write test ROM data (simple pattern)
    uint8_t testData[TEST_ROM_SIZE];
    for (uint32_t i = 0; i < TEST_ROM_SIZE; i++) {
        testData[i] = static_cast<uint8_t>(i & 0xFF);
    }
    file.write(reinterpret_cast<const char*>(testData), TEST_ROM_SIZE);

    file.close();

    std::cout << "Created test ROM: " << outputFile << "\n";
    std::cout << "  Title: " << header.title << "\n";
    std::cout << "  Developer: " << header.developer << "\n";
    std::cout << "  ROM Size: " << header.romSize << " bytes\n";
    std::cout << "  Entry Point: 0x" << std::hex << header.entryPoint << "\n";

    return 0;
}
