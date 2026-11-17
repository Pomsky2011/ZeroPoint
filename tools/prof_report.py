#!/usr/bin/env python3
"""Flat profile from tools/prof_sampler.c output: prof_report.py <program> <samples.txt> [top]
Resolves each sampled address with addr2line -i, so time in inlined code is credited to the function it was inlined
into ("self") and, separately, to the inlined function itself ("incl. inlines"). Needs the program built with -g."""
import collections, subprocess, sys

prog, samples = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
rows = [l.split() for l in open(samples) if l.strip()]
addrs = [r[0] for r in rows]
callers = [hex(max(int(r[1], 16) - 1, 0)) for r in rows]   # return address - 1 lands inside the call instruction
uniq = sorted(set(addrs) | set(callers))
# addr2line -i prints (function, file:line) pairs, innermost first, per address; -a marks where each address starts.
out = subprocess.run(["addr2line", "-a", "-i", "-f", "-C", "-e", prog] + uniq, capture_output=True, text=True).stdout.splitlines()
frames, cur = {}, None
i = 0
while i < len(out):
    if out[i].startswith("0x"):
        cur = int(out[i], 16); frames[cur] = []; i += 1
    else:
        frames[cur].append((out[i], out[i + 1])); i += 2

outer = collections.Counter(); inner = collections.Counter(); lines = collections.Counter()
for a in addrs:
    f = frames.get(int(a, 16))
    if not f: continue
    inner[f[0][0]] += 1
    outer[f[-1][0]] += 1
    lines[f[0][1].split("/")[-1]] += 1
n = len(addrs)
# who was running when the hottest innermost functions were sampled: the caller's source line, outermost-first
caller_lines = collections.defaultdict(collections.Counter)
for a, c in zip(addrs, callers):
    f, g = frames.get(int(a, 16)), frames.get(int(c, 16))
    if f and g: caller_lines[f[0][0]][g[0][1].split("/")[-1]] += 1
def show(title, c):
    print(f"\n{title} ({n} samples)")
    for name, k in c.most_common(top):
        print(f"{100.0 * k / n:5.1f}%  {name[:110]}")
show("By outermost function (inlines folded in)", outer)
show("By innermost function (where the instruction was written)", inner)
show("By source line", lines)

for name, _ in inner.most_common(6):
    print(f"\ncalled from, for {name[:80]}")
    for line, k in caller_lines[name].most_common(4):
        print(f"{100.0 * k / n:5.1f}%  {line}")
