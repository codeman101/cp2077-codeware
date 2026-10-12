"""Compares MSVC assembly listings (/FAs) of two builds, function by function.

Usage: compare.py <base listing dir> <change listing dir> <name>[,<name>...]
Prints, for every function whose decorated name contains one of the names, whether the normalised body is identical in both builds,
and the differences when it is not. Normalising drops comments, source lines, label numbers and string literal names, which change
with unrelated edits.
"""
import difflib
import pathlib
import re
import sys

PROC = re.compile(r"^(\S+)\s+PROC\b")
ENDP = re.compile(r"^(\S+)\s+ENDP\b")


def functions(directory):
    result = {}
    for listing in pathlib.Path(directory).rglob("*.asm"):
        name = None
        body = []
        for line in listing.read_text(errors="replace").splitlines():
            if name is None:
                m = PROC.match(line)
                if m:
                    name, body = m.group(1), []
                continue
            if ENDP.match(line):
                result.setdefault(name, body)
                name = None
                continue
            text = line.split(";", 1)[0].rstrip()
            if not text.strip():
                continue
            text = re.sub(r"\$LN\d+@\S+|\$LN\d+|\$SG\d+|\$T\d+|\$ip2state\$\S+", "<L>", text)
            body.append(text.strip())
    return result


def main():
    base, change, names = sys.argv[1], sys.argv[2], sys.argv[3].split(",")
    a, b = functions(base), functions(change)
    picked = sorted(n for n in set(a) | set(b) if any(x in n for x in names))
    identical = 0
    for n in picked:
        if n not in a or n not in b:
            print(f"ONLY IN {'BASE' if n in a else 'CHANGE'}: {n}")
            continue
        if a[n] == b[n]:
            identical += 1
            print(f"IDENTICAL: {n}")
            continue
        print(f"DIFFERENT: {n}")
        for d in difflib.unified_diff(a[n], b[n], "base", "change", lineterm="", n=2):
            print("    " + d)
    print(f"{identical} of {len(picked)} matching functions identical")


main()
