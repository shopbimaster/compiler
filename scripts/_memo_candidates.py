#!/usr/bin/env python3
"""Scan SysY sources for self-recursive functions and report why the
recursiveMemoization pass would (or would not) accept them.

Purely a diagnostic aid: it approximates the IR-level checks in
src/opt/RecursiveMemoization.cpp at the source level so we can see which
restriction is the binding one for each candidate.
"""
import os
import re
import sys

ROOTS = ["test/performance", "test/functional", "test/h_functional"]

# Library routines that perform I/O -- any call to these disqualifies.
IO_FUNCS = {
    "getint", "getch", "getfloat", "getarray", "getfarray",
    "putint", "putch", "putfloat", "putarray", "putfarray", "putf",
    "starttime", "stoptime", "_sysy_starttime", "_sysy_stoptime",
}

DEF_RE = re.compile(
    r"\b(int|float|void)\s+(\w+)\s*\(([^)]*)\)\s*\{", re.S)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def extract_body(text, brace_pos):
    depth = 0
    for i in range(brace_pos, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace_pos + 1:i]
    return text[brace_pos + 1:]


def parse_params(raw):
    raw = raw.strip()
    if not raw or raw == "void":
        return []
    out = []
    for chunk in raw.split(","):
        chunk = chunk.strip()
        if not chunk:
            continue
        is_array = "[" in chunk
        base = "int" if chunk.startswith("int") else (
            "float" if chunk.startswith("float") else "?")
        out.append(("array" if is_array else base))
    return out


def analyze(path):
    text = strip_comments(open(path, encoding="utf-8", errors="ignore").read())

    globals_decl = set()
    for m in re.finditer(r"^\s*(?:const\s+)?(?:int|float)\s+(\w+)\s*(\[|=|;)",
                         text, re.M):
        globals_decl.add(m.group(1))

    results = []
    for m in DEF_RE.finditer(text):
        ret, name, params = m.group(1), m.group(2), m.group(3)
        body = extract_body(text, m.end() - 1)

        self_calls = len(re.findall(r"\b" + re.escape(name) + r"\s*\(", body))
        if self_calls == 0:
            continue  # not recursive

        ptypes = parse_params(params)
        callees = set(re.findall(r"\b(\w+)\s*\(", body)) - {name}
        callees -= {"if", "while", "for", "return", "else"}

        reasons = []
        if ret != "int":
            reasons.append(f"return type is {ret}, pass requires int")
        if len(ptypes) != 2:
            reasons.append(f"{len(ptypes)} params, pass requires exactly 2")
        non_i32 = [p for p in ptypes if p != "int"]
        if non_i32:
            reasons.append(f"param types {ptypes} include non-i32")
        io_used = callees & IO_FUNCS
        if io_used:
            reasons.append(f"calls I/O: {sorted(io_used)}")
        other = callees - IO_FUNCS
        if other:
            reasons.append(f"calls other functions: {sorted(other)}")

        # writes to globals / arrays inside the body
        writes = set()
        for w in re.finditer(r"\b(\w+)\s*\[[^\]]*\]\s*=(?!=)", body):
            writes.add(w.group(1))
        for w in re.finditer(r"^\s*(\w+)\s*=(?!=)", body, re.M):
            if w.group(1) in globals_decl:
                writes.add(w.group(1))
        if writes:
            reasons.append(f"writes to arrays/globals: {sorted(writes)}")

        # external call sites
        ext_sites = 0
        for m2 in DEF_RE.finditer(text):
            if m2.group(2) == name:
                continue
            other_body = extract_body(text, m2.end() - 1)
            ext_sites += len(
                re.findall(r"\b" + re.escape(name) + r"\s*\(", other_body))
        if ext_sites != 1:
            reasons.append(f"{ext_sites} external call sites, pass requires 1")

        results.append({
            "file": path, "name": name, "ret": ret, "params": ptypes,
            "self_calls": self_calls, "ext_sites": ext_sites,
            "reasons": reasons,
        })
    return results


def main():
    accepted, rejected = [], []
    for root in ROOTS:
        if not os.path.isdir(root):
            continue
        for fn in sorted(os.listdir(root)):
            if not fn.endswith(".sy"):
                continue
            for r in analyze(os.path.join(root, fn)):
                (accepted if not r["reasons"] else rejected).append(r)

    print("=== WOULD BE MEMOIZED (all source-level checks pass) ===")
    for r in accepted:
        print(f"  {r['file']}: {r['ret']} {r['name']}({r['params']}) "
              f"self_calls={r['self_calls']}")
    print(f"  total: {len(accepted)}")

    print("\n=== SELF-RECURSIVE BUT REJECTED ===")
    for r in rejected:
        print(f"  {r['file']}: {r['ret']} {r['name']}({r['params']})")
        for reason in r["reasons"]:
            print(f"      - {reason}")
    print(f"  total: {len(rejected)}")

    print("\n=== REJECTION REASON HISTOGRAM ===")
    hist = {}
    for r in rejected:
        for reason in r["reasons"]:
            key = re.sub(r"[:\[].*", "", reason).strip()
            hist[key] = hist.get(key, 0) + 1
    for k, v in sorted(hist.items(), key=lambda kv: -kv[1]):
        print(f"  {v:3d}  {k}")


if __name__ == "__main__":
    sys.exit(main())
