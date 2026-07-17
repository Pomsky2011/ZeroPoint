#ifndef ZEROPOINT_ROM_H
#define ZEROPOINT_ROM_H

#include <cstdint>
#include <cstddef>
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

    // True for a version-2 (zpbuild-signed) ROM: a "ZPSG" trailer (32-byte
    // SHA-256 digest + 256-byte RSA-2048 signature) follows the payload on
    // disk. The loader only checks the trailer's shape (magic/siglen) - it
    // does not verify the signature itself; that is the boot ROM's job
    // (ZPbootROM/def88186/rsa.def), using the raw header and trailer bytes
    // below (which System maps into CPU memory at bank $E1).
    bool isSigned() const { return signedRom; }

    // Exact 64 bytes read from disk, verbatim - the boot ROM re-hashes these
    // (not the parsed fields) together with the payload.
    const uint8_t* getRawHeader() const { return rawHeader; }
    static constexpr size_t RAW_HEADER_SIZE = 64;

    // "ZPSG" trailer as read from disk: magic(4)+version(1)+keysize(1)+
    // siglen(2) [8 bytes], then a 32-byte digest, then a 256-byte RSA
    // signature (296 bytes total for trailer version 1). Trailer version 2
    // (code/data-split composite signing, see docs/zpb-format.md) appends
    // a further 4-byte little-endian codeSize field after the signature
    // (300 bytes total) - ZPbootROM/def88186/rsa.def's rsa_verify_composite
    // uses it to split the payload into a SHA-256-signed code region and a
    // BLAKE2s-hashed data region. Trailer version 3 (code + chunked-data-
    // manifest signing) appends a further per-16384-byte-chunk BLAKE2s
    // manifest after codeSize (chunkCount derived from codeSize/romSize,
    // not stored) - ZPbootROM/def88186/rsa.def's
    // rsa_verify_composite_manifest verifies the manifest itself
    // synchronously at boot, deferring each chunk's own check to whenever
    // cartridge code is about to load it. Empty when isSigned() is false.
    const std::vector<uint8_t>& getTrailer() const { return trailer; }

    // Trailer version byte (fixedPart[4] above): 0 when unsigned, 1/2/3 for
    // the single-region/composite/chunked-manifest signing schemes in
    // docs/zpb-format.md. codeSize/chunkCount are only meaningful for
    // trailerVersion >= 2/3 respectively (0 otherwise).
    uint8_t getTrailerVersion() const { return trailerVersion; }
    uint32_t getCodeSize() const { return codeSize; }
    uint32_t getChunkCount() const { return chunkCount; }

    // Get last error message
    const std::string& getError() const { return errorMessage; }

private:
    bool loaded;
    std::vector<uint8_t> data;
    std::string title;
    std::string developer;
    uint32_t entryPoint;
    std::string errorMessage;
    bool signedRom = false;
    uint8_t rawHeader[RAW_HEADER_SIZE] = {};
    std::vector<uint8_t> trailer;
    uint8_t trailerVersion = 0;
    uint32_t codeSize = 0;
    uint32_t chunkCount = 0;

    void setError(const std::string& error);
};

} // namespace ZeroPoint

#endif // ZEROPOINT_ROM_H
