#include "rom.h"
#include <fstream>
#include <cstring>

namespace ZeroPoint {

ROM::ROM()
    : loaded(false)
    , entryPoint(0)
{
}

ROM::~ROM() {
}

bool ROM::load(const std::string& filename) {
    loaded = false;
    data.clear();
    title.clear();
    developer.clear();
    entryPoint = 0;
    errorMessage.clear();
    signedRom = false;
    std::memset(rawHeader, 0, sizeof(rawHeader));
    trailer.clear();

    // Open file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        setError("Failed to open file: " + filename);
        return false;
    }

    // Read header
    ZPBHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (!file) {
        setError("Failed to read ROM header");
        return false;
    }
    static_assert(sizeof(header) == RAW_HEADER_SIZE, "ZPBHeader must be 64 bytes");
    std::memcpy(rawHeader, &header, RAW_HEADER_SIZE);

    // Validate magic number
    if (std::memcmp(header.magic, "ZPB", 3) != 0 || header.magic[3] != '\0') {
        setError("Invalid ROM file: bad magic number");
        return false;
    }

    // Check version - 1 is unsigned, 2 is zpbuild-signed (ZPSG trailer
    // follows the payload; verified by the boot ROM, not here).
    if (header.version != 1 && header.version != 2) {
        setError("Unsupported ROM version: " + std::to_string(header.version));
        return false;
    }

    // Validate header size
    if (header.headerSize != 64) {
        setError("Invalid header size: " + std::to_string(header.headerSize));
        return false;
    }

    // Check ROM size (max 2 MB)
    const uint32_t MAX_ROM_SIZE = 2 * 1024 * 1024;
    if (header.romSize == 0 || header.romSize > MAX_ROM_SIZE) {
        setError("Invalid ROM size: " + std::to_string(header.romSize));
        return false;
    }

    // Extract metadata
    title = std::string(header.title, strnlen(header.title, 32));
    developer = std::string(header.developer, strnlen(header.developer, 16));
    entryPoint = header.entryPoint;

    // Read ROM data
    data.resize(header.romSize);
    file.read(reinterpret_cast<char*>(data.data()), header.romSize);
    if (!file) {
        setError("Failed to read ROM data");
        data.clear();
        return false;
    }

    if (header.version == 2) {
        // "ZPSG" trailer: magic(4) + version(1) + keysize(1) + siglen(2),
        // then a 32-byte digest, then the RSA signature (siglen bytes).
        // Layout/sizes must match ZPdevtools/src/zpbuild.c exactly.
        const size_t TRAILER_FIXED_SIZE = 8 + 32;
        uint8_t fixedPart[TRAILER_FIXED_SIZE];
        file.read(reinterpret_cast<char*>(fixedPart), TRAILER_FIXED_SIZE);
        if (!file) {
            setError("Failed to read ROM signature trailer");
            data.clear();
            return false;
        }
        if (std::memcmp(fixedPart, "ZPSG", 4) != 0) {
            setError("Invalid ROM file: missing ZPSG trailer for signed ROM");
            data.clear();
            return false;
        }
        uint16_t sigLen = static_cast<uint16_t>(fixedPart[6] | (fixedPart[7] << 8));
        if (sigLen == 0 || sigLen > 4096) {
            setError("Invalid ROM signature length: " + std::to_string(sigLen));
            data.clear();
            return false;
        }
        trailer.assign(fixedPart, fixedPart + TRAILER_FIXED_SIZE);
        trailer.resize(TRAILER_FIXED_SIZE + sigLen);
        file.read(reinterpret_cast<char*>(trailer.data() + TRAILER_FIXED_SIZE), sigLen);
        if (!file) {
            setError("Failed to read ROM signature");
            data.clear();
            trailer.clear();
            return false;
        }
        signedRom = true;
    }

    loaded = true;
    return true;
}

void ROM::setError(const std::string& error) {
    errorMessage = error;
}

} // namespace ZeroPoint
