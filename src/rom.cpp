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
    trailerVersion = 0;
    codeSize = 0;
    chunkCount = 0;

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

    // Check ROM size (max 8 MB)
    const uint32_t MAX_ROM_SIZE = 8 * 1024 * 1024;
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
        trailerVersion = fixedPart[4];
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

        // Trailer version 2 (code/data-split signing, see
        // docs/zpb-format.md) appends a 4-byte little-endian codeSize
        // field after the signature - the boot ROM's rsa_verify uses it to
        // split the payload into a SHA-256/RSA-signed code region and a
        // BLAKE2s-hashed data region. Appended AFTER the signature (not
        // inserted earlier in the trailer) so the fixed offsets rsa_verify
        // already hardcodes for the digest/signature don't move.
        if (trailerVersion >= 2) {
            uint8_t codeSizeBytes[4];
            file.read(reinterpret_cast<char*>(codeSizeBytes), 4);
            if (!file) {
                setError("Failed to read ROM trailer codeSize field");
                data.clear();
                trailer.clear();
                return false;
            }
            trailer.insert(trailer.end(), codeSizeBytes, codeSizeBytes + 4);

            // Trailer version 3 (code + chunked-data-manifest signing, see
            // docs/zpb-format.md) appends a per-chunk BLAKE2s manifest right
            // after codeSize: one 32-byte digest per CHUNK_SIZE-byte data
            // chunk (last chunk short), covering payload[codeSize..romSize).
            // chunkCount is NOT itself stored - it's derived here exactly
            // the way zpbuild.c and rsa_verify_composite_manifest do, from
            // codeSize and romSize, so it can't be tampered independently
            // of the digest that actually gets re-checked at boot.
            if (trailerVersion >= 3) {
                codeSize = static_cast<uint32_t>(codeSizeBytes[0])
                    | (static_cast<uint32_t>(codeSizeBytes[1]) << 8)
                    | (static_cast<uint32_t>(codeSizeBytes[2]) << 16)
                    | (static_cast<uint32_t>(codeSizeBytes[3]) << 24);
                if (codeSize > header.romSize) {
                    setError("Invalid ROM trailer: codeSize exceeds romSize");
                    data.clear();
                    trailer.clear();
                    return false;
                }
                const uint32_t CHUNK_SIZE = 16384;
                const uint32_t dataSize = header.romSize - codeSize;
                chunkCount = (dataSize + CHUNK_SIZE - 1) / CHUNK_SIZE;
                if (chunkCount > 0) {
                    std::vector<uint8_t> manifest(static_cast<size_t>(chunkCount) * 32);
                    file.read(reinterpret_cast<char*>(manifest.data()), manifest.size());
                    if (!file) {
                        setError("Failed to read ROM trailer manifest");
                        data.clear();
                        trailer.clear();
                        return false;
                    }
                    trailer.insert(trailer.end(), manifest.begin(), manifest.end());
                }
            }
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
