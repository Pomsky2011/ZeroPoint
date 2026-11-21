#include "movie.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

// SHA-256 for pinning ROM images (vendored from ZPdevtools, see src/boot_hle/).
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "boot_hle/zpsha256.h"
#pragma GCC diagnostic pop

namespace ZeroPoint {

// ---- SHA-256 -------------------------------------------------------------------------------------------------------

Sha256 sha256Of(const uint8_t* data, size_t size) {
    Sha256 out{};
    zp_sha256 sh;
    zp_sha256_init(&sh);
    // Feed in chunks: the vendored code counts length in unsigned long (32-bit on Windows).
    for (size_t off = 0; off < size;) {
        const size_t n = std::min<size_t>(size - off, 1u << 24);
        zp_sha256_update(&sh, data + off, (unsigned long)n);
        off += n;
    }
    zp_sha256_final(&sh, out.data());
    return out;
}

std::string toHex(const Sha256& h) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (uint8_t b : h) { s += d[b >> 4]; s += d[b & 15]; }
    return s;
}

bool fromHex(const std::string& s, Sha256& out) {
    if (s.size() != 64) return false;
    for (size_t i = 0; i < 32; i++) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = nib(s[2 * i]), lo = nib(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)(hi * 16 + lo);
    }
    return true;
}

namespace {

uint64_t fnv(const uint8_t* p, size_t n) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

// ---- binary reader/writer (little-endian, LEB128 varints) ---------------------------------------------------------------

struct Writer {
    std::vector<uint8_t>& out;
    void u8(uint8_t v) { out.push_back(v); }
    void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
    void u32(uint32_t v) { for (int i = 0; i < 4; i++) u8((v >> (8 * i)) & 0xFF); }
    void u64(uint64_t v) { for (int i = 0; i < 8; i++) u8((v >> (8 * i)) & 0xFF); }
    void varint(uint64_t v) { while (v >= 0x80) { u8((uint8_t)(v | 0x80)); v >>= 7; } u8((uint8_t)v); }
    void bytes(const uint8_t* p, size_t n) { out.insert(out.end(), p, p + n); }
    void str(const std::string& s) { varint(s.size()); bytes(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }
};

struct Reader {
    const uint8_t* p;
    size_t n, pos = 0;
    bool ok = true;
    bool need(size_t k) { if (!ok || n - pos < k) { ok = false; return false; } return true; }
    uint8_t u8() { return need(1) ? p[pos++] : 0; }
    uint16_t u16() { uint16_t a = u8(); a |= (uint16_t)u8() << 8; return a; }
    uint32_t u32() { uint32_t v = 0; for (int i = 0; i < 4; i++) v |= (uint32_t)u8() << (8 * i); return v; }
    uint64_t u64() { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)u8() << (8 * i); return v; }
    uint64_t varint() {
        uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const uint8_t b = u8();
            if (!ok) return 0;
            v |= (uint64_t)(b & 0x7F) << shift;
            if (!(b & 0x80)) return v;
        }
        ok = false;   // over-long varint
        return 0;
    }
    void bytes(uint8_t* dst, size_t k) { if (need(k)) { std::memcpy(dst, p + pos, k); pos += k; } }
    std::string str() {
        const uint64_t len = varint();
        if (!ok || len > n - pos) { ok = false; return std::string(); }
        std::string s(reinterpret_cast<const char*>(p + pos), (size_t)len);
        pos += (size_t)len;
        return s;
    }
};

constexpr uint8_t MAGIC[4] = {'Z', 'P', 'M', 0x1A};
constexpr uint16_t FORMAT_VERSION = 1;

}  // namespace

// ---- Movie: binary ---------------------------------------------------------------------------------------------------

std::vector<uint8_t> Movie::serialize() const {
    std::vector<uint8_t> out;
    Writer w{out};
    w.bytes(MAGIC, 4);
    w.u16(FORMAT_VERSION);
    w.u32(coreRevision);
    w.u64(lengthCycles);
    w.u8((verifySignatures ? 1 : 0) | (hleBoot ? 2 : 0) | (hleApuBios ? 4 : 0) | (devUnit ? 8 : 0));
    if (!hleBoot) w.bytes(bootRom.data(), 32);
    if (!hleApuBios) w.bytes(apuBios.data(), 32);
    w.varint((uint64_t)(initialRom + 1));
    w.varint(roms.size());
    for (const MovieRom& r : roms) { w.bytes(r.sha.data(), 32); w.str(r.title); }
    w.str(author);
    w.str(description);
    w.varint(checkpointInterval);
    w.varint(events.size());
    uint64_t prev = 0;
    for (const MovieEvent& e : events) {
        w.varint(e.cycle - prev);
        prev = e.cycle;
        w.u8(e.type);
        switch (e.type) {
            case MovieEvent::Input: w.u8(e.port); w.u8(e.dir); w.u8(e.ctrl); w.u8(e.btn); break;
            case MovieEvent::Insert: w.varint(e.rom); break;
            case MovieEvent::SerialIn: w.u8(e.data); break;
            case MovieEvent::Checkpoint:
                w.u8((uint8_t)e.parts.size());
                for (uint64_t v : e.parts) w.u64(v);
                break;
            default: break;
        }
    }
    w.u64(fnv(out.data(), out.size()));
    return out;
}

bool Movie::parse(const uint8_t* data, size_t size, Movie& out, std::string& error) {
    out = Movie();
    auto fail = [&](const char* what) { error = what; return false; };
    if (size < 4 + 2 + 8) return fail("not a movie (too short)");
    if (std::memcmp(data, MAGIC, 4) != 0) return fail("not a movie (bad magic)");
    Reader r{data, size - 8};   // the last 8 bytes are the checksum
    {
        Reader c{data + size - 8, 8};
        const uint64_t want = c.u64();
        if (fnv(data, size - 8) != want) return fail("movie is corrupt (checksum mismatch)");
    }
    r.pos = 4;
    if (r.u16() != FORMAT_VERSION) return fail("unsupported movie format version");
    out.coreRevision = r.u32();
    out.lengthCycles = r.u64();
    const uint8_t flags = r.u8();
    out.verifySignatures = flags & 1;
    out.hleBoot = flags & 2;
    out.hleApuBios = flags & 4;
    out.devUnit = flags & 8;
    if (flags & ~15) return fail("unknown flag bits");
    if (out.devUnit && out.coreRevision < 11) return fail("a dev-unit movie needs core revision 11 or later");
    if (!out.hleBoot) r.bytes(out.bootRom.data(), 32);
    if (!out.hleApuBios) r.bytes(out.apuBios.data(), 32);
    const uint64_t initial = r.varint();
    const uint64_t nroms = r.varint();
    if (!r.ok || nroms > (r.n - r.pos) / 33) return fail("bad ROM table");
    if (initial > nroms) return fail("initial ROM index out of range");
    out.initialRom = (int32_t)initial - 1;
    for (uint64_t i = 0; i < nroms; i++) {
        MovieRom m;
        r.bytes(m.sha.data(), 32);
        m.title = r.str();
        out.roms.push_back(m);
    }
    out.author = r.str();
    out.description = r.str();
    out.checkpointInterval = r.varint();
    const uint64_t nev = r.varint();
    if (!r.ok || nev > r.n - r.pos) return fail("bad event count");   // every event is at least 2 bytes
    uint64_t cycle = 0;
    for (uint64_t i = 0; i < nev; i++) {
        MovieEvent e;
        const uint64_t delta = r.varint();
        if (delta > UINT64_MAX - cycle) return fail("event cycle overflow");
        cycle += delta;
        e.cycle = cycle;
        const uint8_t type = r.u8();
        if (type < MovieEvent::Input || type > MovieEvent::SerialIn) return fail("unknown event type");
        if (type == MovieEvent::SerialIn && out.coreRevision < 11) return fail("serial input needs core revision 11 or later");
        e.type = (MovieEvent::Type)type;
        switch (e.type) {
            case MovieEvent::Input:
                e.port = r.u8(); e.dir = r.u8(); e.ctrl = r.u8(); e.btn = r.u8();
                if (e.port > 1) return fail("bad controller port");
                break;
            case MovieEvent::Insert: {
                const uint64_t idx = r.varint();
                if (idx >= nroms) return fail("insert references a missing ROM");
                e.rom = (uint32_t)idx;
                break;
            }
            case MovieEvent::SerialIn: e.data = r.u8(); break;
            case MovieEvent::Checkpoint: {
                const uint8_t n = r.u8();
                for (uint8_t k = 0; k < n && r.ok; k++) e.parts.push_back(r.u64());
                break;
            }
            default: break;
        }
        if (!r.ok) return fail("movie is truncated");
        out.events.push_back(std::move(e));
    }
    if (!r.ok || r.pos != r.n) return fail("movie has trailing garbage or is truncated");
    return true;
}

bool Movie::save(const std::string& path, std::string& error) const {
    const std::vector<uint8_t> bytes = serialize();
    std::ofstream f(path, std::ios::binary);
    if (!f) { error = "cannot write " + path; return false; }
    f.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
    return f.good() ? true : (error = "write failed: " + path, false);
}

bool Movie::load(const std::string& path, Movie& out, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "cannot open " + path; return false; }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return parse(bytes.data(), bytes.size(), out, error);
}

// ---- Movie: text ---------------------------------------------------------------------------------------------------------

namespace {

std::string hex8(unsigned v) { char b[8]; std::snprintf(b, sizeof b, "0x%02X", v & 0xFF); return b; }
std::string hex64(uint64_t v) { char b[24]; std::snprintf(b, sizeof b, "0x%016llX", (unsigned long long)v); return b; }

bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

}  // namespace

std::string Movie::toText() const {
    std::ostringstream o;
    o << "zpm-text 1\n";
    o << "core " << coreRevision << "\n";
    o << "length " << lengthCycles << "\n";
    o << "verify " << (verifySignatures ? "on" : "off") << "\n";
    if (devUnit) o << "devunit on\n";
    o << "boot " << (hleBoot ? "hle" : "sha256:" + toHex(bootRom)) << "\n";
    o << "apubios " << (hleApuBios ? "hle" : "sha256:" + toHex(apuBios)) << "\n";
    for (size_t i = 0; i < roms.size(); i++) o << "rom " << i << " sha256:" << toHex(roms[i].sha) << " " << roms[i].title << "\n";
    o << "initial " << (initialRom < 0 ? std::string("none") : std::to_string(initialRom)) << "\n";
    o << "checkpoint-interval " << checkpointInterval << "\n";
    if (!author.empty()) o << "author " << author << "\n";
    if (!description.empty()) o << "description " << description << "\n";
    for (const MovieEvent& e : events) {
        o << "@" << e.cycle << " ";
        switch (e.type) {
            case MovieEvent::Input: o << "input " << (int)e.port + 1 << " " << hex8(e.dir) << " " << hex8(e.ctrl) << " " << hex8(e.btn); break;
            case MovieEvent::SoftReset: o << "softreset"; break;
            case MovieEvent::HardReset: o << "hardreset"; break;
            case MovieEvent::Yank: o << "yank"; break;
            case MovieEvent::Insert: o << "insert " << e.rom; break;
            case MovieEvent::SerialIn: o << "serial " << hex8(e.data); break;
            case MovieEvent::Checkpoint:
                o << "checkpoint";
                for (uint64_t v : e.parts) o << " " << hex64(v);
                break;
        }
        o << "\n";
    }
    return o.str();
}

bool Movie::fromText(const std::string& text, Movie& out, std::string& error) {
    out = Movie();
    out.coreRevision = System::CORE_REVISION;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    bool sawHeader = false;
    auto fail = [&](const std::string& what) { error = "line " + std::to_string(lineNo) + ": " + what; return false; };
    // Resolves "hle", "sha256:<hex>" or a file path to a pin.
    auto pin = [&](const std::string& what, bool* hle, Sha256& sha, std::string* title) -> bool {
        if (what == "hle") { *hle = true; return true; }
        *hle = false;
        if (what.compare(0, 7, "sha256:") == 0) return fromHex(what.substr(7), sha);
        std::vector<uint8_t> data;
        if (!readFile(what, data)) return false;
        sha = sha256Of(data.data(), data.size());
        if (title) *title = std::filesystem::path(what).filename().string();
        return true;
    };
    while (std::getline(in, line)) {
        lineNo++;
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        std::string word;
        if (!(ls >> word)) continue;
        std::string rest;
        auto restOfLine = [&]() { std::getline(ls, rest); size_t a = rest.find_first_not_of(" \t"); rest = a == std::string::npos ? "" : rest.substr(a); return rest; };
        if (word == "zpm-text") { sawHeader = true; continue; }
        if (!sawHeader) return fail("expected 'zpm-text 1' first");
        if (word == "core") { unsigned long long v; ls >> v; out.coreRevision = (uint32_t)v; }
        else if (word == "length") { unsigned long long v; ls >> v; out.lengthCycles = v; }
        else if (word == "verify") { std::string v; ls >> v; out.verifySignatures = v != "off"; }
        else if (word == "devunit") { std::string v; ls >> v; out.devUnit = v == "on"; }
        else if (word == "boot") { std::string v; ls >> v; if (!pin(v, &out.hleBoot, out.bootRom, nullptr)) return fail("cannot resolve boot ROM '" + v + "'"); }
        else if (word == "apubios") { std::string v; ls >> v; if (!pin(v, &out.hleApuBios, out.apuBios, nullptr)) return fail("cannot resolve APU BIOS '" + v + "'"); }
        else if (word == "rom") {
            size_t idx; std::string v;
            if (!(ls >> idx >> v)) return fail("rom needs an index and a path or sha256");
            if (idx != out.roms.size()) return fail("rom indices must be 0, 1, 2, ... in order");
            MovieRom m;
            bool hle;
            std::string title;
            if (!pin(v, &hle, m.sha, &title) || hle) return fail("cannot resolve ROM '" + v + "'");
            std::string given = restOfLine();
            m.title = given.empty() ? title : given;
            out.roms.push_back(m);
        }
        else if (word == "initial") { std::string v; ls >> v; out.initialRom = v == "none" ? -1 : std::atoi(v.c_str()); }
        else if (word == "checkpoint-interval") { unsigned long long v; ls >> v; out.checkpointInterval = v; }
        else if (word == "author") out.author = restOfLine();
        else if (word == "description") out.description = restOfLine();
        else if (word[0] == '@') {
            MovieEvent e;
            e.cycle = std::strtoull(word.c_str() + 1, nullptr, 0);
            std::string kind;
            if (!(ls >> kind)) return fail("event needs a kind");
            if (kind == "input") {
                int port; std::string d, c, b;
                if (!(ls >> port >> d >> c >> b) || (port != 1 && port != 2)) return fail("input <1|2> <dir> <ctrl> <btn>");
                e.type = MovieEvent::Input;
                e.port = (uint8_t)(port - 1);
                e.dir = (uint8_t)std::strtoul(d.c_str(), nullptr, 0);
                e.ctrl = (uint8_t)std::strtoul(c.c_str(), nullptr, 0);
                e.btn = (uint8_t)std::strtoul(b.c_str(), nullptr, 0);
            } else if (kind == "softreset") e.type = MovieEvent::SoftReset;
            else if (kind == "hardreset") e.type = MovieEvent::HardReset;
            else if (kind == "yank") e.type = MovieEvent::Yank;
            else if (kind == "serial") { std::string b; if (!(ls >> b)) return fail("serial <byte>"); e.type = MovieEvent::SerialIn; e.data = (uint8_t)std::strtoul(b.c_str(), nullptr, 0); }
            else if (kind == "insert") { unsigned idx; if (!(ls >> idx)) return fail("insert <rom index>"); e.type = MovieEvent::Insert; e.rom = idx; }
            else if (kind == "checkpoint") {
                e.type = MovieEvent::Checkpoint;
                std::string v;
                while (ls >> v) e.parts.push_back(std::strtoull(v.c_str(), nullptr, 0));
            } else return fail("unknown event '" + kind + "'");
            if (!out.events.empty() && e.cycle < out.events.back().cycle) return fail("events must be in cycle order");
            out.events.push_back(std::move(e));
        }
        else return fail("unknown directive '" + word + "'");
    }
    if (!sawHeader) { error = "empty script"; return false; }
    for (const MovieEvent& e : out.events)
        if (e.type == MovieEvent::Insert && e.rom >= out.roms.size()) { error = "insert refers to a rom that isn't declared"; return false; }
    if (out.initialRom >= (int32_t)out.roms.size()) { error = "initial refers to a rom that isn't declared"; return false; }
    if (out.lengthCycles == 0 && !out.events.empty()) out.lengthCycles = out.events.back().cycle;
    return true;
}

// ---- RomLibrary ----------------------------------------------------------------------------------------------------------

void RomLibrary::add(const std::string& fileOrDirectory) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto addFile = [&](const fs::path& p) {
        std::vector<uint8_t> data;
        if (!readFile(p.string(), data) || data.empty()) return;
        files.push_back({sha256Of(data.data(), data.size()), p.string()});
    };
    if (fs::is_directory(fileOrDirectory, ec)) {
        for (auto it = fs::recursive_directory_iterator(fileOrDirectory, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            std::error_code e2;
            if (it->is_regular_file(e2) && it->file_size(e2) <= (64u << 20)) addFile(it->path());
        }
    } else if (fs::is_regular_file(fileOrDirectory, ec)) {
        addFile(fileOrDirectory);
    }
}

std::string RomLibrary::pathOf(const Sha256& sha) const {
    for (const Entry& e : files) if (e.sha == sha) return e.path;
    return std::string();
}

bool RomLibrary::read(const Sha256& sha, std::vector<uint8_t>& out) const {
    const std::string path = pathOf(sha);
    if (path.empty() || !readFile(path, out)) return false;
    return sha256Of(out.data(), out.size()) == sha;   // the file may have changed since it was scanned
}

// ---- MovieSession --------------------------------------------------------------------------------------------------------

bool MovieSession::setupMachine(const RecordSetup& s, const std::vector<uint8_t>* bootImage,
                                const std::vector<uint8_t>* apuImage, const std::vector<uint8_t>* cartImage,
                                std::string& error) {
    (void)s;
    // Start from a power cycle whatever the System was doing (a frontend reuses one across ROM loads): the right boot
    // ROM and APU BIOS, the right cartridge (or none) in the slot, every byte of state at its power-on value.
    if (bootImage) { if (!sys->loadBootROMImage(*bootImage)) { error = "cannot load the boot ROM image"; return false; } }
    else sys->useHLEBootROM();
    if (apuImage) { if (!sys->loadAPUBiosImage(*apuImage)) { error = "cannot load the APU BIOS image"; return false; } }
    else sys->useHLEApuBios();
    sys->setVerifySignatures(movie.verifySignatures);
    sys->setDevUnit(movie.devUnit);
    sys->ejectCartridge();
    if (cartImage && !sys->loadROMImage(*cartImage)) { error = "cannot load the cartridge image"; return false; }
    sys->hardReset();
    // The host side of the machine starts from a defined state too: player 1 plugged in and idle, player 2 unplugged.
    sys->getCPU().setPlayerInput(0, 0, PlayerInput::CTRL_CONNECTION, 0);
    sys->getCPU().setPlayerInput(1, 0, 0, 0);
    return true;
}

bool MovieSession::beginRecording(System& system, const RecordSetup& setup, std::string& error) {
    sys = &system;
    movie = Movie();
    movie.verifySignatures = setup.verifySignatures;
    movie.devUnit = setup.devUnit;
    movie.checkpointInterval = setup.checkpointInterval;
    movie.author = setup.author;
    movie.description = setup.description;
    std::vector<uint8_t> boot, apu, cart;
    if (!setup.bootRomPath.empty()) {
        if (!readFile(setup.bootRomPath, boot)) { error = "cannot read " + setup.bootRomPath; return false; }
        movie.hleBoot = false;
        movie.bootRom = sha256Of(boot.data(), boot.size());
    }
    if (!setup.apuBiosPath.empty()) {
        if (!readFile(setup.apuBiosPath, apu)) { error = "cannot read " + setup.apuBiosPath; return false; }
        movie.hleApuBios = false;
        movie.apuBios = sha256Of(apu.data(), apu.size());
    }
    if (!setup.cartridgePath.empty()) {
        if (!readFile(setup.cartridgePath, cart)) { error = "cannot read " + setup.cartridgePath; return false; }
        movie.initialRom = addRom(cart, std::filesystem::path(setup.cartridgePath).filename().string());
    }
    if (!setupMachine(setup, boot.empty() ? nullptr : &boot, apu.empty() ? nullptr : &apu, cart.empty() ? nullptr : &cart, error))
        return false;
    movieCycle = 0;
    nextCheckpoint = setup.checkpointInterval;
    currentMode = Mode::Recording;
    return true;
}

bool MovieSession::beginPlayback(System& system, const Movie& m, const RomLibrary& lib, std::string& error,
                                 bool ignoreCoreRevision) {
    sys = &system;
    movie = m;
    library = &lib;
    // A revision from MIN_COMPATIBLE_REVISION up replays bit-for-bit (a newer core with its additions switched off is the
    // older machine); anything older than that, or newer than this core, does not.
    if ((m.coreRevision < System::MIN_COMPATIBLE_REVISION || m.coreRevision > System::CORE_REVISION) && !ignoreCoreRevision) {
        error = "movie was made with core revision " + std::to_string(m.coreRevision) + ", this core is revision " +
                std::to_string(System::CORE_REVISION) + " and replays revisions " + std::to_string(System::MIN_COMPATIBLE_REVISION) +
                "-" + std::to_string(System::CORE_REVISION) + " (emulation changed; the movie will not replay)";
        return false;
    }
    std::vector<uint8_t> boot, apu, cart;
    if (!m.hleBoot && !lib.read(m.bootRom, boot)) { error = "the boot ROM this movie pins (sha256 " + toHex(m.bootRom) + ") is not in the ROM library"; return false; }
    if (!m.hleApuBios && !lib.read(m.apuBios, apu)) { error = "the APU BIOS this movie pins (sha256 " + toHex(m.apuBios) + ") is not in the ROM library"; return false; }
    if (m.initialRom >= 0) {
        if ((size_t)m.initialRom >= m.roms.size()) { error = "initial ROM index out of range"; return false; }
        if (!lib.read(m.roms[m.initialRom].sha, cart)) {
            error = "the cartridge this movie starts with (" + m.roms[m.initialRom].title + ", sha256 " +
                    toHex(m.roms[m.initialRom].sha) + ") is not in the ROM library";
            return false;
        }
    }
    RecordSetup unused;
    if (!setupMachine(unused, boot.empty() ? nullptr : &boot, apu.empty() ? nullptr : &apu, cart.empty() ? nullptr : &cart, error))
        return false;
    movieCycle = 0;
    nextEvent = 0;
    desync = false;
    desyncText.clear();
    failureText.clear();
    verified = 0;
    currentMode = Mode::Playing;
    return true;
}

int MovieSession::addRom(const std::vector<uint8_t>& image, const std::string& title) {
    const Sha256 sha = sha256Of(image.data(), image.size());
    for (size_t i = 0; i < movie.roms.size(); i++) if (movie.roms[i].sha == sha) return (int)i;
    MovieRom r;
    r.sha = sha;
    r.title = title;
    movie.roms.push_back(r);
    romImages.push_back(image);
    return (int)movie.roms.size() - 1;
}

bool MovieSession::applyEvent(const MovieEvent& e) {
    switch (e.type) {
        case MovieEvent::Input: sys->getCPU().setPlayerInput(e.port, e.dir, e.ctrl, e.btn); return true;
        case MovieEvent::SoftReset: sys->softReset(); return true;
        case MovieEvent::HardReset: sys->hardReset(); return true;
        case MovieEvent::Yank: sys->ejectCartridge(); return true;
        case MovieEvent::SerialIn: sys->serialReceive(e.data); return true;   // (a byte the full queue drops is dropped on replay too)
        case MovieEvent::Insert: {
            std::vector<uint8_t> image;
            if (e.rom >= movie.roms.size()) { failureText = "insert event references a missing ROM"; return false; }
            if (currentMode == Mode::Recording && e.rom < romImages.size()) image = romImages[e.rom];
            else if (!library || !library->read(movie.roms[e.rom].sha, image)) {
                failureText = "the cartridge swapped in at cycle " + std::to_string(e.cycle) + " (" + movie.roms[e.rom].title +
                              ", sha256 " + toHex(movie.roms[e.rom].sha) + ") is not in the ROM library";
                return false;
            }
            if (!sys->insertCartridge(image)) { failureText = "the swapped-in cartridge is not a valid ROM image"; return false; }
            return true;
        }
        case MovieEvent::Checkpoint: {
            if (currentMode != Mode::Playing) return true;
            const auto now = sys->machineHashParts();
            bool same = now.size() == e.parts.size();
            std::string who;
            for (size_t i = 0; i < now.size() && i < e.parts.size(); i++)
                if (now[i].second != e.parts[i]) { same = false; who += std::string(who.empty() ? "" : ", ") + now[i].first; }
            if (same) { verified++; return true; }
            desync = true;
            desyncText = "DESYNC at movie cycle " + std::to_string(e.cycle) + " (" + std::to_string(e.cycle / 68011355ull) + "." +
                         std::to_string(1000 + (e.cycle % 68011355ull) * 1000 / 68011355ull).substr(1) +
                         " s of console time): " + (who.empty() ? "checkpoint has a different number of parts (core change?)" : "state differs in: " + who) +
                         "; " + std::to_string(verified) + " earlier checkpoints matched";
            return false;
        }
    }
    return false;
}

uint64_t MovieSession::runMachine(uint64_t cycles) {
    if (cycles) sys->runCycles(cycles);
    return cycles;
}

uint64_t MovieSession::run(uint64_t cycles) {
    if (!sys || currentMode == Mode::Idle) return 0;
    if (currentMode == Mode::Playing && (desync || !failureText.empty())) return 0;
    uint64_t target = movieCycle + cycles;
    if (currentMode == Mode::Playing && target > movie.lengthCycles) target = movie.lengthCycles;
    const uint64_t start = movieCycle;
    auto applyDue = [&]() {
        while (nextEvent < movie.events.size() && movie.events[nextEvent].cycle <= movieCycle) {
            if (!applyEvent(movie.events[nextEvent++])) return false;
        }
        return true;
    };
    while (movieCycle < target) {
        if (currentMode == Mode::Playing && !applyDue()) return movieCycle - start;
        uint64_t seg = target;
        if (currentMode == Mode::Playing && nextEvent < movie.events.size() && movie.events[nextEvent].cycle < seg)
            seg = movie.events[nextEvent].cycle;
        if (currentMode == Mode::Recording && nextCheckpoint && nextCheckpoint < seg) seg = nextCheckpoint;
        runMachine(seg - movieCycle);
        movieCycle = seg;
        if (currentMode == Mode::Recording && nextCheckpoint && movieCycle == nextCheckpoint) {
            MovieEvent c;
            c.cycle = movieCycle;
            c.type = MovieEvent::Checkpoint;
            for (const auto& p : sys->machineHashParts()) c.parts.push_back(p.second);
            movie.events.push_back(std::move(c));
            nextCheckpoint += movie.checkpointInterval;
        }
    }
    // Events due at a boundary apply when the next run() starts, so a checkpoint recorded at cycle c (taken when run()
    // returns at c) sees the state before the events at c. Only at the movie's very end are they applied here (they
    // would otherwise never be: the closing checkpoint sits after them).
    if (currentMode == Mode::Playing && movieCycle >= movie.lengthCycles) applyDue();
    return movieCycle - start;
}

void MovieSession::log(MovieEvent e) {
    e.cycle = movieCycle;
    if (!applyEvent(e)) return;
    movie.events.push_back(std::move(e));
}

void MovieSession::input(int port, uint8_t dir, uint8_t ctrl, uint8_t btn) {
    if (currentMode != Mode::Recording || (port != 0 && port != 1)) return;
    uint8_t* last = lastInput[port];
    if (last[0] == dir && last[1] == ctrl && last[2] == btn) return;
    last[0] = dir; last[1] = ctrl; last[2] = btn;
    MovieEvent e;
    e.type = MovieEvent::Input;
    e.port = (uint8_t)port;
    e.dir = dir; e.ctrl = ctrl; e.btn = btn;
    log(e);
}

void MovieSession::serialIn(uint8_t byte) {
    if (currentMode != Mode::Recording || !sys->isDevUnit()) return;
    MovieEvent e;
    e.type = MovieEvent::SerialIn;
    e.data = byte;
    log(e);
}

void MovieSession::softReset() {
    if (currentMode != Mode::Recording) return;
    MovieEvent e; e.type = MovieEvent::SoftReset; log(e);
}

void MovieSession::hardReset() {
    if (currentMode != Mode::Recording) return;
    MovieEvent e; e.type = MovieEvent::HardReset; log(e);
}

void MovieSession::yank() {
    if (currentMode != Mode::Recording) return;
    MovieEvent e; e.type = MovieEvent::Yank; log(e);
}

bool MovieSession::insert(const std::vector<uint8_t>& image, const std::string& title) {
    if (currentMode != Mode::Recording) return false;
    ROM probe;
    if (!probe.loadMemory(image.data(), image.size())) return false;   // never log what the machine would refuse
    MovieEvent e;
    e.type = MovieEvent::Insert;
    e.rom = (uint32_t)addRom(image, title);
    log(e);
    return true;
}

Movie MovieSession::finishRecording() {
    if (currentMode == Mode::Recording) {
        movie.lengthCycles = movieCycle;
        if (movie.checkpointInterval && (movie.events.empty() || movie.events.back().type != MovieEvent::Checkpoint ||
                                         movie.events.back().cycle != movieCycle)) {
            MovieEvent c;   // a closing checkpoint, so the very end is verified too
            c.cycle = movieCycle;
            c.type = MovieEvent::Checkpoint;
            for (const auto& p : sys->machineHashParts()) c.parts.push_back(p.second);
            movie.events.push_back(std::move(c));
        }
    }
    currentMode = Mode::Idle;
    return movie;
}

}  // namespace ZeroPoint
