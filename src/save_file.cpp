#include "save_file.h"
#include "rom.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace ZeroPoint {

std::string SaveFile::pathFor(const std::string& romPath) {
    std::filesystem::path p(romPath);
    p.replace_extension(".sav");
    return p.string();
}

size_t SaveFile::sizeFor(const std::vector<uint8_t>& romImage) {
    ROM rom;
    return rom.loadMemory(romImage.data(), romImage.size()) ? rom.getSaveSize() : 0;
}

bool SaveFile::load(const std::string& path, size_t expectedSize, std::vector<uint8_t>& out, std::string& note) {
    out.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() != expectedSize) {
        note = path + " is " + std::to_string(bytes.size()) + " bytes but this cartridge's save RAM is " + std::to_string(expectedSize) +
               ": not using it";
        return false;
    }
    out = std::move(bytes);
    return true;
}

void SaveFile::attach(System& sys, const std::string& path, std::string& note) {
    detach();
    if (path.empty() || sys.saveSize() == 0) return;
    std::vector<uint8_t> bytes;
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        if (!load(path, sys.saveSize(), bytes, note)) return;   // wrong size (or unreadable): leave the file alone
        sys.setSave(bytes);
    }
    sys.takeSaveDirty();   // what was just loaded is not a change
    filePath = path;
    bound = true;
    pending = false;
    lastWrite = -1e9;
    lastError.clear();
}

void SaveFile::detach() {
    bound = false;
    pending = false;
    filePath.clear();
}

bool SaveFile::flush(System& sys, bool force) {
    if (!bound) return true;
    if (sys.takeSaveDirty()) pending = true;
    if (!pending && !force) return true;
    if (sys.saveSize() == 0) return true;   // the cartridge left (a yank): nothing of it to write
    const std::string tmp = filePath + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        const std::vector<uint8_t>& data = sys.saveData();
        f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        f.flush();
        if (!f) {
            lastError = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, filePath, ec);
    if (ec) {
        lastError = "cannot replace " + filePath + ": " + ec.message();
        return false;
    }
    pending = false;
    lastError.clear();
    return true;
}

void SaveFile::poll(System& sys, double nowSeconds) {
    if (!bound) return;
    if (sys.takeSaveDirty()) pending = true;
    if (pending && nowSeconds - lastWrite >= FLUSH_INTERVAL_SECONDS) {
        lastWrite = nowSeconds;   // (also paces the retries of a write that fails)
        flush(sys);
    }
}

}  // namespace ZeroPoint
