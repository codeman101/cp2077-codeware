"""Lists the virtual calls through given vtable offsets in MSVC assembly listings (/FAs), with the instructions that set up their
arguments, for a side-by-side check of two builds.

Usage: callsites.py <listing dir> <name>[,<name>...] <offset>[,<offset>...] <out dir>
Offsets are hexadecimal (1A8). For every function whose decorated name contains one of the names, writes the full listing of the
function to <out dir>/functions/ and, for every `call QWORD PTR [reg+offset]` with one of the offsets, prints the lines from the
previous call (or the start of the function) to the call, and the six lines after it (how rax is used).
"""
import pathlib
import re
import sys

PROC = re.compile(r"^(\S+)\s+PROC\b")
ENDP = re.compile(r"^(\S+)\s+ENDP\b")
CALL = re.compile(r"^\s*call\s+(?:QWORD PTR\s+)?\[(\w+)\+([0-9A-Fa-f]+H?)\]", re.IGNORECASE)
ANYCALL = re.compile(r"^\s*call\s", re.IGNORECASE)


def parse_offset(text):
    text = text.strip()
    if text.upper().endswith("H"):
        return int(text[:-1], 16)
    return int(text, 10)


def main():
    directory, names, offsets, out = sys.argv[1], sys.argv[2].split(","), sys.argv[3].split(","), pathlib.Path(sys.argv[4])
    wanted = {int(o, 16) for o in offsets}
    (out / "functions").mkdir(parents=True, exist_ok=True)
    seen = set()
    for listing in sorted(pathlib.Path(directory).rglob("*.asm")):
        lines = listing.read_text(errors="replace").splitlines()
        start = None
        name = None
        for i, line in enumerate(lines):
            if name is None:
                m = PROC.match(line)
                if m and any(x in m.group(1) for x in names):
                    name, start = m.group(1), i
                continue
            if not ENDP.match(line):
                continue
            body = lines[start : i + 1]
            if name not in seen:
                seen.add(name)
                safe = re.sub(r"[^A-Za-z0-9_.@-]", "_", name)[:180]
                (out / "functions" / f"{safe}.asm").write_text("\n".join(body) + "\n")
                last_call = 0
                for j, text in enumerate(body):
                    m = CALL.match(text)
                    if m and parse_offset(m.group(2)) in wanted:
                        print(f"=== {name}  [{listing.name}]  call through [{m.group(1)}+{parse_offset(m.group(2)):#x}]")
                        for k in range(last_call, j + 1):
                            print("    " + body[k])
                        print("    ---- after")
                        for k in range(j + 1, min(j + 7, len(body))):
                            print("    " + body[k])
                    if ANYCALL.match(text):
                        last_call = j + 1
            name = None


main()
