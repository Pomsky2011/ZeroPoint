#!/usr/bin/env python3
"""Determinism audit of the emulator core: fails if anything outside (ROM, config, host event schedule) can reach the
machine's state. This is the checkable half of docs/determinism-proof.md (premises P3, P6, P7).

  tools/determinism_audit.py [path/to/compile_commands.json]    (default: ./build/compile_commands.json, or any build dir)

1. AST pass (clang): every variable with static/thread storage or namespace scope that is not const/constexpr, in the
   core's files, is hidden state shared by all Systems and surviving reset(). None are allowed.
2. Token pass: banned constructs in the core (wall clock, RNG, environment, pointer values, hash-ordered containers,
   floating point, threads). Each exception is listed below with the reason it cannot reach emulated state.

Exit status 1 and a list on any violation. Exceptions are the only thing to edit, and each needs a reason.
"""
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The machine itself (frontends, tools and tests are outside the boundary: they only drive it).
CORE = [
    "src/cpu.cpp", "src/ppu.cpp", "src/apu.cpp", "src/dma.cpp", "src/display.cpp", "src/system.cpp",
    "src/apu_hle.cpp", "src/rom.cpp", "src/cpu_instructions.cpp", "src/ppu_jit.cpp", "src/boot_hle/boot_hle.cpp",
]
CORE_HEADERS = [
    "include/cpu.h", "include/ppu.h", "include/apu.h", "include/dma.h", "include/display.h", "include/system.h",
    "include/rom.h", "include/interrupt_controller.h", "include/boot_hle.h", "include/ppu_jit.h",
]

# (file, regex) -> why it cannot reach emulated state.
ALLOW = {
    ("src/system.cpp", r"getenv"):
        "System::videoThreadDefault()/verifySignaturesDefault(): read by frontends only, never by the machine; the video "
        "thread is bit-identical to the single-threaded run (test_determinism, fuzz_determinism)",
    ("src/system.cpp", r"std::thread"):
        "the video worker: sole owner protocol in system.h, checked under TSan",
    ("include/system.h", r"std::thread"):
        "the video worker (see src/system.cpp)",
    ("src/boot_hle/boot_hle.cpp", r"\bdouble\b|std::sqrt"):
        "isqrt64(): a double sqrt that is then corrected to the exact integer floor, so the result is host-independent",
    ("include/ppu_jit.h", r"unordered_"):
        "the PPU 'JIT' block cache is keyed by address and only ever looked up or cleared, never iterated into state",
    ("src/ppu_jit.cpp", r"__builtin"):
        "icache flush of generated code; no effect on results",
}

BANNED = [
    (r"\b(float|double)\b", "floating point (host-dependent rounding/contraction)"),
    (r"\b(rand|srand|rand_r|random|drand48)\s*\(", "RNG"),
    (r"std::random_device|std::mt19937|std::uniform_", "RNG"),
    (r"\b(time|clock|gettimeofday|clock_gettime)\s*\(", "wall clock"),
    (r"std::chrono|steady_clock|system_clock|high_resolution_clock", "wall clock"),
    (r"\bgetenv\b", "environment"),
    (r"std::thread|std::async|std::jthread|pthread_create", "threads"),
    (r"unordered_(map|set|multimap|multiset)", "hash-ordered container (iteration order is unspecified)"),
    (r"std::hash\b", "std::hash is implementation-defined"),
    (r"\buintptr_t\b|\bintptr_t\b|%p\b", "pointer value"),
    (r"\b__rdtsc\b|\brdtsc\b|__builtin_readcyclecounter", "cycle counter"),
    (r"std::this_thread|hardware_concurrency", "host scheduling/topology"),
    (r"\bthread_local\b", "thread-local state"),
]


def strip_comments(src):
    src = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def token_pass():
    bad = []
    for rel in CORE + CORE_HEADERS:
        path = os.path.join(ROOT, rel)
        if not os.path.exists(path):
            continue
        text = strip_comments(open(path, errors="replace").read())
        for ln, line in enumerate(text.split("\n"), 1):
            for rx, why in BANNED:
                if re.search(rx, line):
                    allowed = any(f == rel and re.search(a, line) for (f, a) in ALLOW)
                    if not allowed:
                        bad.append("%s:%d: %s: %s" % (rel, ln, why, line.strip()[:100]))
    return bad


def ast_pass(cc_path):
    cc = json.load(open(cc_path))
    by_file = {os.path.relpath(e["file"], ROOT): e for e in cc}
    bad = []
    core_abs = tuple(os.path.join(ROOT, p) for p in CORE + CORE_HEADERS)
    for rel in CORE:
        e = by_file.get(rel)
        if not e:
            print("note: %s not in %s (skipped)" % (rel, cc_path))
            continue
        cmd = e.get("command") or " ".join(e["arguments"])
        args = [a for a in cmd.split() if a != "-c"]
        # drop output args, keep the compiler + flags; run a pure syntax pass dumping the AST
        out_flags, skip = [], False
        for a in args[1:]:
            if skip:
                skip = False
                continue
            if a == "-o":
                skip = True
                continue
            if a == e["file"] or a.endswith(".o"):
                continue
            out_flags.append(a)
        res = subprocess.run(["clang++"] + out_flags + ["-fsyntax-only", "-Xclang", "-ast-dump", e["file"]],
                             cwd=e["directory"], capture_output=True, text=True)
        if res.returncode != 0 or "TranslationUnitDecl" not in res.stdout:
            bad.append("%s: clang could not parse it (%s); an unchecked file is not a clean file" %
                       (rel, (res.stderr.strip().split("\n") or ["?"])[0][:100]))
            continue
        # Track the enclosing file via the "<file:line:col" locations the dump prints; only keep decls from core files.
        cur_file = None
        for line in res.stdout.split("\n"):
            m = re.search(r"<(/[^:>]+):\d+:\d+", line)
            if m:
                cur_file = m.group(1)
            m2 = re.search(r"line:\d+:\d+|col:\d+", line)
            if "VarDecl" not in line or cur_file is None or not cur_file.startswith(tuple(core_abs)):
                continue
            # VarDecl 0x... <loc, loc> loc used|referenced name 'type' [static|cinit|...]
            m = re.search(r"VarDecl 0x[0-9a-f]+ (?:prev 0x[0-9a-f]+ )?<[^>]*> \S+ (?:used |referenced )?(\w+) '([^']*)'", line)
            if not m:
                continue
            name, ty = m.group(1), m.group(2)
            is_static = " static" in line or "thread_local" in line
            # Namespace/TU-scope vars show with no indentation depth > 2 in the dump; function locals are deeper and
            # only matter when static. Treat depth <= 3 ("|-VarDecl") as namespace scope.
            top = bool(re.match(r"^[|`]-VarDecl", line))
            const = "const" in ty or "constexpr" in line
            if (is_static or top) and not const:
                bad.append("%s: non-const %s variable '%s' of type '%s'" % (os.path.relpath(cur_file, ROOT),
                           "static" if is_static else "namespace-scope", name, ty))
    # static data members declared in headers and defined in .cpp show up as top-level VarDecls too.
    return sorted(set(bad))


def find_cc():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for d in ("build", "build-tidy", "."):
        p = os.path.join(ROOT, d, "compile_commands.json")
        if os.path.exists(p):
            return p
    sys.exit("no compile_commands.json: configure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON")


def main():
    bad = token_pass()
    bad += ast_pass(find_cc())
    for b in bad:
        print("VIOLATION", b)
    print("determinism audit: %s" % ("FAILED (%d)" % len(bad) if bad else "clean"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
