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

    // Validate magic number
    if (std::memcmp(header.magic, "ZPB", 3) != 0 || header.magic[3] != '\0') {
        setError("Invalid ROM file: bad magic number");
        return false;
    }

    // Check version
    if (header.version != 1) {
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

    loaded = true;
    return true;
}

void ROM::setError(const std::string& error) {
    errorMessage = error;
}

} // namespace ZeroPoint
