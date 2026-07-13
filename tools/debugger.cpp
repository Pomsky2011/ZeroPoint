// Basic interactive debugger for the ZeroPoint system: CPU/PPU/APU register
// inspection, single-instruction stepping, breakpoints on CPU PC, and raw
// memory read/write. Headless (no SDL/Qt/Vulkan) - links only zeropoint_core.
//
// Usage: debugger <game.rom> [boot_rom.bin]
//
// Commands (type "help" at the prompt for the full list): regs, ppu, apu,
// step [n], continue, break <bank:addr>, delete <n>, mem <bank:addr> [len],
// write <bank:addr> <byte>, pmem/pwrite (PPU), amem/awrite (APU), reset, quit.
#include "system.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>

using namespace ZeroPoint;

// Mnemonic + addressing-mode text per opcode, extracted from the comments
// src/cpu_instructions.cpp emits when tools/gen_cpu_instructions.py generates
// it from the authoritative ../ZPdevtools/docs/cpu/DEF88186.csv. This is the
// same table the real dispatch table is built from, so it stays accurate as
// long as this file is regenerated whenever the CSV changes (unlike the
// standalone ZPdevtools cpudisasm.c table, which only covers ~80 of the 256
// opcodes and isn't kept in sync with the emulator - don't reuse that one
// here). Operand length isn't tracked: several modes (#const, SEP/REP, etc.)
// are 8 or 16-bit depending on the live M/X flags, so a fixed-length decode
// would silently misalign on real ROMs. This table is used only to annotate
// the single opcode at PC, not to walk a multi-instruction listing.
static const char* const CPU_MNEMONIC[256] = {
    "NOP", "BIT #const", "BIT addr", "BIT addr,X", "BIT dp", "BIT dp,X",
    "BMI long", "BRA long", "BRL label", "BVS long", "BCS long", "BEQ long",
    "JMP (addr,X)", "JMP (addr)", "JMP [addr]", "JMP addr", "JMP long",
    "JSR (addr,X))", "JSR addr", "LOOP word", "LPEND", "CALL long", "RET",
    "RTI", "RTL", "RTS", "SEP #const", "SDB byte", "WAI", "TDC", "TSC", "TCS",
    "TAX", "TXA", "TAY", "TCD", "TXY", "TYA", "TYX", "PUSH word", "PEA addr",
    "PEI (dp)", "PER label", "PHA", "PHB", "PHD", "PHK", "PHP", "PHX", "PHY",
    "PLA", "POPF", "LDA (sr,S),Y", "LDA [dp]", "LDA [dp],Y", "LDA #const",
    "LDA addr", "LDA addr,X", "LDA addr,Y", "LDA dp", "LDA dp,X", "LDA long",
    "LDA long,X", "LDA sr,S", "LDX #const", "LDX addr", "LDX addr,Y", "LDX dp",
    "LDX dp,Y", "LDY #const", "LDY addr", "LDY addr,X", "LDY dp", "LDY dp,X",
    "STA _dp_X", "STA (dp,X)", "STA (dp)", "STA (dp),Y", "STA (sr,S),Y",
    "STA [dp]", "STA [dp],Y", "STA addr", "STA addr,X", "STA addr,Y",
    "STA dp", "STA long", "STA long,X", "STA sr,S", "STX addr", "STX dp",
    "STX dp,Y", "STY addr", "STY dp", "STY dp,X", "STZ addr", "STZ addr,X",
    "STZ dp", "STZ dp,X", "BRK", "SEC", "SED", "SEI", "ADC dp",
    "MVN srcbk,destbk", "MVP srcbk,destbk", "INC A", "INC addr",
    "INC addr,X", "INC dp", "INC dp,X", "INC long", "INX", "INY", "DEC A",
    "DEC addr", "DEC addr,X", "DEC dp", "DEC dp,X", "DEC long", "DEX", "DEY",
    "CPX addr", "CPY addr", "CLD", "CLI", "CLV", "CLC", "REP #const",
    "ROL A", "ROL addr", "ROL addr,X", "ROL dp", "ROR A", "ROR addr",
    "ROR addr,X", "ROR dp", "SHL X", "SHL Y", "SHR X", "SHR Y", "RCL A",
    "LSR A", "LSR addr", "LSR addr,X", "LSR dp", "LSR dp,X", "DIV X,Y",
    "DIV long,X", "DIV long,Y", "EOR addr", "EOR addr,X", "EOR addr,Y",
    "ASL A", "ASL addr", "ASL addr,X", "ASL dp", "ASL dp,X", "XOR (dp,X)",
    "XOR (dp)", "XOR (dp),Y", "CMP (dp,X)", "CMP (dp)", "CMP (dp),Y",
    "CMP (sr,S),Y", "CMP [dp]", "CMP [dp],Y", "CMP #const", "CMP addr",
    "CMP addr,X", "CMP addr,Y", "CMP dp", "CMP dp,X", "CMP long",
    "CMP long,X", "CMP sr,S", "COP #const", "ADC ( dp),Y", "ADC (dp,X)",
    "ADC (dp)", "ADC (sr,S),Y", "ADC [dp]", "ADC [dp],Y", "ADC #const",
    "ADC addr", "ADC addr,X", "ADC addr,Y", "ADC dp,X", "ADC long",
    "ADC long,X", "ADC sr,S", "AND (dp,X)", "AND (dp)", "AND (dp),Y",
    "AND (sr,S),Y", "AND [dp]", "AND [dp],Y", "AND #const", "AND addr",
    "AND addr,X", "AND addr,Y", "AND dp", "AND dp,X", "AND long",
    "AND long,X", "AND sr,S", "ORA (dp,X)", "ORA (dp)", "ORA (dp),Y",
    "ORA (sr,S),Y", "ORA [dp]", "ORA [dp],Y", "ORA #const", "ORA addr",
    "ORA addr,X", "ORA addr,Y", "ORA dp", "ORA dp,X", "ORA long",
    "ORA long,X", "ORA sr,S", "SBC (dp,X)", "SBC (dp),Y", "SBC (sr,S),Y",
    "SBC [dp]", "SBC [dp],Y", "SBC #const", "SBC addr", "SBC addr,X",
    "SBC addr,Y", "SBC dp", "SBC dp,X", "SBC long", "SBC long,X", "SBC sr,S",
    "MUL (dp,X)", "MUL (dp),Y", "MUL (sr,S),Y", "MUL [dp]", "MUL [dp],Y",
    "MUL #const", "MUL addr", "MUL addr,X", "MUL addr,Y", "MUL dp",
    "MUL dp,X", "MUL long", "MUL long,X", "MUL sr,S", "XOR (sr,S),Y",
    "XOR #const", "XOR dp", "XOR dp,X", "XOR long", "XOR long,X", "XOR sr,S",
    "HLT",
};

namespace {

volatile std::sig_atomic_t g_interrupted = 0;
void onSigint(int) { g_interrupted = 1; }

std::string formatAddr(uint8_t bank, uint16_t offset) {
    std::ostringstream os;
    os << std::hex << std::uppercase << std::setfill('0')
       << std::setw(2) << (int)bank << ":" << std::setw(4) << (int)offset;
    return os.str();
}

// Parse "BB:OOOO" or a plain "OOOO" (bank defaults to defaultBank) into a
// flat 24-bit address (bank<<16 | offset). Hex only, optional "0x"/"$" prefix.
bool parseAddr(const std::string& tok, uint8_t defaultBank, uint32_t& outAddr) {
    std::string s = tok;
    uint32_t bank = defaultBank;
    std::string offsetPart = s;
    size_t colon = s.find(':');
    if (colon != std::string::npos) {
        std::string bankPart = s.substr(0, colon);
        offsetPart = s.substr(colon + 1);
        if (bankPart.size() > 1 && bankPart[0] == '$') bankPart = bankPart.substr(1);
        char* end = nullptr;
        bank = std::strtoul(bankPart.c_str(), &end, 16);
        if (end == bankPart.c_str()) return false;
    }
    if (offsetPart.size() > 1 && offsetPart[0] == '$') offsetPart = offsetPart.substr(1);
    if (offsetPart.size() > 2 && offsetPart[0] == '0' && (offsetPart[1] == 'x' || offsetPart[1] == 'X'))
        offsetPart = offsetPart.substr(2);
    char* end = nullptr;
    uint32_t offset = std::strtoul(offsetPart.c_str(), &end, 16);
    if (end == offsetPart.c_str()) return false;
    outAddr = ((bank & 0xFF) << 16) | (offset & 0xFFFF);
    return true;
}

bool parseU32(const std::string& tok, uint32_t& out) {
    if (tok.empty()) return false;
    std::string s = tok;
    if (s.size() > 1 && s[0] == '$') s = s.substr(1);
    char* end = nullptr;
    out = std::strtoul(s.c_str(), &end, 16);
    return end != s.c_str();
}

void printFlags(const CPUFlags& p) {
    std::cout << (p.N ? 'N' : 'n') << (p.V ? 'V' : 'v') << (p.M ? 'M' : 'm')
              << (p.X ? 'X' : 'x') << (p.D ? 'D' : 'd') << (p.I ? 'I' : 'i')
              << (p.Z ? 'Z' : 'z') << (p.C ? 'C' : 'c');
}

const char* cpuStateName(CPUState s) {
    switch (s) {
        case CPUState::Running: return "Running";
        case CPUState::Halted:  return "Halted";
        case CPUState::Waiting: return "Waiting (WAI)";
    }
    return "?";
}

void printNextInstruction(System& sys) {
    CPU& cpu = sys.getCPU();
    uint8_t bank = cpu.getPB();
    uint16_t pc = cpu.getPC();
    uint32_t flat = (static_cast<uint32_t>(bank) << 16) | pc;
    uint8_t opcode = cpu.readByte(flat);
    std::cout << "  " << formatAddr(bank, pc) << "  op=$"
               << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
               << (int)opcode << std::dec << "  " << CPU_MNEMONIC[opcode] << "\n";
}

void printCPURegs(System& sys) {
    CPU& cpu = sys.getCPU();
    std::cout << std::hex << std::uppercase << std::setfill('0');
    std::cout << "CPU  A=" << std::setw(4) << cpu.getA()
              << "  X=" << std::setw(4) << cpu.getX()
              << "  Y=" << std::setw(4) << cpu.getY()
              << "  SP=" << std::setw(4) << cpu.getSP()
              << "  D=" << std::setw(4) << cpu.getD() << "\n";
    std::cout << "     PB=" << std::setw(2) << (int)cpu.getPB()
              << "  DB=" << std::setw(2) << (int)cpu.getDB()
              << "  PC=" << std::setw(4) << cpu.getPC()
              << "  P=";
    printFlags(cpu.getFlags());
    std::cout << std::dec << "  state=" << cpuStateName(cpu.getState()) << "\n";
    std::cout << "     cycles=" << cpu.getCycleCount()
              << "  instructions=" << cpu.getInstructionCount() << "\n";
    printNextInstruction(sys);
}

void printPPURegs(System& sys) {
    PPU& ppu = sys.getPPU();
    std::cout << std::hex << std::uppercase << std::setfill('0');
    for (int row = 0; row < 64; row += 8) {
        std::cout << "R" << std::dec << std::setfill(' ') << std::setw(2) << row
                   << std::setfill('0') << std::hex << ": ";
        for (int i = 0; i < 8; i++) {
            std::cout << std::setw(4) << ppu.getRegister(row + i) << " ";
        }
        std::cout << "\n";
    }
    std::cout << std::dec;
    std::cout << "  DP(R63)=" << std::hex << std::setw(4) << ppu.getRegister(63)
              << "  SP(R61)=" << std::setw(4) << ppu.getRegister(61)
              << "  PC(R62)=" << std::setw(4) << ppu.getExecutionPointer()
              << std::dec << "\n";
    const char* stateName = ppu.getState() == PPUState::Running ? "Running"
                            : ppu.getState() == PPUState::Halted ? "Halted"
                            : "WaitingForStart";
    std::cout << "  state=" << stateName << "\n";
}

void printAPURegs(System& sys) {
    APU& apu = sys.getAPU();
    std::cout << std::hex << std::uppercase << std::setfill('0');
    std::cout << "APU  PC=" << std::setw(4) << apu.getPC()
              << "  SP=" << std::setw(4) << apu.getSP() << "\n";
    std::cout << "  R0-R7: ";
    for (int i = 0; i < 8; i++)
        std::cout << std::setw(2) << (int)apu.getRegister(i) << " ";
    std::cout << "\n";
    std::cout << std::dec << "  halted=" << (apu.isHalted() ? "yes" : "no")
              << "  cycles=" << apu.getCycleCount()
              << "  instructions=" << apu.getInstructionCount() << "\n";
}

void hexDumpCPU(System& sys, uint32_t startAddr, size_t len) {
    CPU& cpu = sys.getCPU();
    std::cout << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < len; i += 16) {
        uint32_t base = startAddr + static_cast<uint32_t>(i);
        std::cout << std::setw(2) << ((base >> 16) & 0xFF) << ":"
                   << std::setw(4) << (base & 0xFFFF) << "  ";
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            std::cout << std::setw(2) << (int)cpu.readByte(base + static_cast<uint32_t>(j)) << " ";
        }
        std::cout << "\n";
    }
    std::cout << std::dec;
}

void hexDumpPPU(System& sys, uint16_t startAddr, size_t len) {
    PPU& ppu = sys.getPPU();
    std::cout << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < len; i += 16) {
        uint32_t base = startAddr + i;
        std::cout << std::setw(4) << (base & 0xFFFF) << "  ";
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            std::cout << std::setw(2) << (int)ppu.readMemory(static_cast<uint16_t>(base + j)) << " ";
        }
        std::cout << "\n";
    }
    std::cout << std::dec;
}

void hexDumpAPU(System& sys, uint16_t startAddr, size_t len) {
    APU& apu = sys.getAPU();
    std::cout << std::hex << std::uppercase << std::setfill('0');
    for (size_t i = 0; i < len; i += 16) {
        uint32_t base = startAddr + i;
        std::cout << std::setw(4) << (base & 0xFFFF) << "  ";
        for (size_t j = 0; j < 16 && i + j < len; j++) {
            std::cout << std::setw(2) << (int)apu.readByte(static_cast<uint16_t>(base + j)) << " ";
        }
        std::cout << "\n";
    }
    std::cout << std::dec;
}

enum class StepResult { Advanced, Halted, Waiting, TimedOut };

// Advance the whole System (master-clock-accurate: PPU/APU/DMA/Display all
// keep ticking) until exactly one more CPU instruction retires. Caps at
// maxCycles master cycles so a WAI with no pending interrupt (or a runaway
// loop) returns control to the prompt instead of hanging the debugger.
StepResult stepOneInstruction(System& sys, uint64_t maxCycles = 4'000'000) {
    CPU& cpu = sys.getCPU();
    if (cpu.getState() == CPUState::Halted) return StepResult::Halted;
    uint64_t startInstr = cpu.getInstructionCount();
    for (uint64_t n = 0; n < maxCycles; n++) {
        sys.step();
        if (cpu.getInstructionCount() != startInstr) return StepResult::Advanced;
        if (cpu.getState() == CPUState::Halted) return StepResult::Halted;
    }
    return cpu.getState() == CPUState::Waiting ? StepResult::Waiting : StepResult::TimedOut;
}

void printHelp() {
    std::cout <<
        "Commands:\n"
        "  regs, r                  Show CPU registers + next instruction\n"
        "  ppu                      Show PPU registers (R0-R63) and state\n"
        "  apu                      Show APU registers and state\n"
        "  step [n], s [n]          Execute n CPU instructions (default 1)\n"
        "  continue, c              Run until a breakpoint, halt, or Ctrl-C\n"
        "  break BB:OOOO, b ...     Set a breakpoint on CPU PC (bank optional,\n"
        "                           defaults to current PB)\n"
        "  breakpoints, bl          List breakpoints\n"
        "  delete N, bd N           Remove breakpoint N (see breakpoints list)\n"
        "  mem BB:OOOO [len], m     Hex-dump CPU memory (default len 64)\n"
        "  write BB:OOOO VV, w      Write byte VV to CPU memory\n"
        "  pmem OOOO [len], pm      Hex-dump PPU memory\n"
        "  pwrite OOOO VV, pw       Write byte VV to PPU memory\n"
        "  amem OOOO [len], am      Hex-dump APU memory\n"
        "  awrite OOOO VV, aw       Write byte VV to APU memory\n"
        "  reset                    Hard reset (System::reset())\n"
        "  help, h, ?               Show this text\n"
        "  quit, q, exit            Exit the debugger\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <game.rom> [boot_rom.bin]\n", argv[0]);
        return 1;
    }

    System sys;
    if (!sys.loadROM(argv[1])) {
        std::fprintf(stderr, "Failed to load ROM: %s\n", argv[1]);
        return 1;
    }
    if (argc >= 3) {
        if (!sys.loadBootROM(argv[2])) {
            std::fprintf(stderr, "Failed to load boot ROM: %s\n", argv[2]);
            return 1;
        }
    }
    sys.powerOn();

    std::cout << "ZeroPoint debugger - ROM '" << sys.getROMTitle() << "' by '"
              << sys.getROMDeveloper() << "', entry $"
              << std::hex << std::uppercase << sys.getEntryPoint() << std::dec << "\n";
    std::cout << "Type 'help' for commands.\n";
    printCPURegs(sys);

    std::signal(SIGINT, onSigint);

    std::vector<uint32_t> breakpoints;
    std::string line;
    while (true) {
        std::cout << "(zpdbg) " << std::flush;
        if (!std::getline(std::cin, line)) {
            std::cout << "\n";
            break;
        }
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        std::vector<std::string> args;
        std::string tok;
        while (iss >> tok) args.push_back(tok);

        if (cmd == "quit" || cmd == "q" || cmd == "exit") {
            break;
        } else if (cmd == "help" || cmd == "h" || cmd == "?") {
            printHelp();
        } else if (cmd == "regs" || cmd == "r") {
            printCPURegs(sys);
        } else if (cmd == "ppu") {
            printPPURegs(sys);
        } else if (cmd == "apu") {
            printAPURegs(sys);
        } else if (cmd == "reset") {
            sys.reset();
            std::cout << "System reset.\n";
            printCPURegs(sys);
        } else if (cmd == "step" || cmd == "s") {
            uint32_t count = 1;
            if (!args.empty()) parseU32(args[0], count);
            for (uint32_t i = 0; i < count; i++) {
                StepResult res = stepOneInstruction(sys);
                if (res == StepResult::Halted) {
                    std::cout << "CPU halted.\n";
                    break;
                } else if (res == StepResult::Waiting) {
                    std::cout << "CPU waiting for interrupt (WAI), no progress.\n";
                    break;
                } else if (res == StepResult::TimedOut) {
                    std::cout << "Step exceeded cycle budget without retiring an instruction.\n";
                    break;
                }
            }
            printNextInstruction(sys);
        } else if (cmd == "continue" || cmd == "c") {
            g_interrupted = 0;
            std::cout << "Running (Ctrl-C to stop)...\n";
            bool stopped = false;
            while (!stopped) {
                StepResult res = stepOneInstruction(sys);
                if (res == StepResult::Halted) {
                    std::cout << "CPU halted.\n";
                    stopped = true;
                } else if (res == StepResult::Waiting) {
                    std::cout << "CPU waiting for interrupt (WAI), no progress.\n";
                    stopped = true;
                } else if (res == StepResult::TimedOut) {
                    std::cout << "Stopped: exceeded cycle budget without retiring an instruction.\n";
                    stopped = true;
                } else if (g_interrupted) {
                    std::cout << "Interrupted.\n";
                    stopped = true;
                } else {
                    CPU& cpu = sys.getCPU();
                    uint32_t flat = (static_cast<uint32_t>(cpu.getPB()) << 16) | cpu.getPC();
                    for (uint32_t bp : breakpoints) {
                        if (bp == flat) {
                            std::cout << "Breakpoint hit at "
                                       << formatAddr(cpu.getPB(), cpu.getPC()) << "\n";
                            stopped = true;
                            break;
                        }
                    }
                }
            }
            printCPURegs(sys);
        } else if (cmd == "break" || cmd == "b") {
            if (args.empty()) {
                std::cout << "Usage: break BB:OOOO\n";
                continue;
            }
            uint32_t addr;
            if (!parseAddr(args[0], sys.getCPU().getPB(), addr)) {
                std::cout << "Bad address: " << args[0] << "\n";
                continue;
            }
            breakpoints.push_back(addr);
            std::cout << "Breakpoint " << (breakpoints.size() - 1) << " at "
                       << formatAddr((addr >> 16) & 0xFF, addr & 0xFFFF) << "\n";
        } else if (cmd == "breakpoints" || cmd == "bl") {
            if (breakpoints.empty()) {
                std::cout << "No breakpoints.\n";
            } else {
                for (size_t i = 0; i < breakpoints.size(); i++) {
                    std::cout << "  " << i << ": "
                               << formatAddr((breakpoints[i] >> 16) & 0xFF, breakpoints[i] & 0xFFFF)
                               << "\n";
                }
            }
        } else if (cmd == "delete" || cmd == "bd") {
            uint32_t idx;
            if (args.empty() || !parseU32(args[0], idx) || idx >= breakpoints.size()) {
                std::cout << "Usage: delete N (see 'breakpoints')\n";
                continue;
            }
            breakpoints.erase(breakpoints.begin() + idx);
            std::cout << "Deleted.\n";
        } else if (cmd == "mem" || cmd == "m") {
            if (args.empty()) {
                std::cout << "Usage: mem BB:OOOO [len]\n";
                continue;
            }
            uint32_t addr, len = 64;
            if (!parseAddr(args[0], sys.getCPU().getPB(), addr)) {
                std::cout << "Bad address: " << args[0] << "\n";
                continue;
            }
            if (args.size() > 1) parseU32(args[1], len);
            hexDumpCPU(sys, addr, len);
        } else if (cmd == "write" || cmd == "w") {
            if (args.size() < 2) {
                std::cout << "Usage: write BB:OOOO VV\n";
                continue;
            }
            uint32_t addr, val;
            if (!parseAddr(args[0], sys.getCPU().getPB(), addr) || !parseU32(args[1], val)) {
                std::cout << "Bad address/value.\n";
                continue;
            }
            sys.getCPU().writeByte(addr, static_cast<uint8_t>(val));
            std::cout << "Wrote $" << std::hex << std::uppercase << (val & 0xFF)
                       << std::dec << " to " << formatAddr((addr >> 16) & 0xFF, addr & 0xFFFF) << "\n";
        } else if (cmd == "pmem" || cmd == "pm") {
            uint32_t addr, len = 64;
            if (args.empty() || !parseU32(args[0], addr)) {
                std::cout << "Usage: pmem OOOO [len]\n";
                continue;
            }
            if (args.size() > 1) parseU32(args[1], len);
            hexDumpPPU(sys, static_cast<uint16_t>(addr), len);
        } else if (cmd == "pwrite" || cmd == "pw") {
            uint32_t addr, val;
            if (args.size() < 2 || !parseU32(args[0], addr) || !parseU32(args[1], val)) {
                std::cout << "Usage: pwrite OOOO VV\n";
                continue;
            }
            sys.getPPU().writeMemory(static_cast<uint16_t>(addr), static_cast<uint8_t>(val));
            std::cout << "Wrote.\n";
        } else if (cmd == "amem" || cmd == "am") {
            uint32_t addr, len = 64;
            if (args.empty() || !parseU32(args[0], addr)) {
                std::cout << "Usage: amem OOOO [len]\n";
                continue;
            }
            if (args.size() > 1) parseU32(args[1], len);
            hexDumpAPU(sys, static_cast<uint16_t>(addr), len);
        } else if (cmd == "awrite" || cmd == "aw") {
            uint32_t addr, val;
            if (args.size() < 2 || !parseU32(args[0], addr) || !parseU32(args[1], val)) {
                std::cout << "Usage: awrite OOOO VV\n";
                continue;
            }
            sys.getAPU().writeByte(static_cast<uint16_t>(addr), static_cast<uint8_t>(val));
            std::cout << "Wrote.\n";
        } else {
            std::cout << "Unknown command '" << cmd << "'. Type 'help'.\n";
        }
    }

    return 0;
}
