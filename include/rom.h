#ifndef ZEROPOINT_ROM_H
#define ZEROPOINT_ROM_H

#include <cstdint>
#include <string>
#include <vector>

namespace ZeroPoint {

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

static_assert(sizeof(ZPBHeader) == 64, "ZPBHeader must be 64 bytes");

class ROM {
public:
    ROM();
    ~ROM();

    // Load a .zpb ROM file
    bool load(const std::string& filename);

    // Check if ROM is loaded
    bool isLoaded() const { return loaded; }

    // Get ROM data
    const uint8_t* getData() const { return data.data(); }
    size_t getSize() const { return data.size(); }

    // Get ROM information
    const std::string& getTitle() const { return title; }
    const std::string& getDeveloper() const { return developer; }
    uint32_t getEntryPoint() const { return entryPoint; }

    // Get last error message
    const std::string& getError() const { return errorMessage; }

private:
    bool loaded;
    std::vector<uint8_t> data;
    std::string title;
    std::string developer;
    uint32_t entryPoint;
    std::string errorMessage;

    void setError(const std::string& error);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_ROM_H
