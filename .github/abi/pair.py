"""Compares MSVC assembly listings (/FAs) of two builds for functions whose decorated names differ because the change altered their
signatures (compare.py can only compare functions that keep their decorated name).

Usage: pair.py <base listing dir> <change listing dir> <specs> <out dir>

<specs> is a ';' separated list of  label::base pattern[::change pattern]. A pattern is a '+' separated list of substrings that the
decorated name must contain; a substring that starts with '!' must not be contained. Without a change pattern the base pattern is
used for both builds. For every spec the script prints the matching functions of both builds and, when each build has exactly one,
the difference of their normalised bodies. The full listings of all matches are written to <out dir>/<base|change>/<label>/.
Normalising drops comments, source lines, label numbers and string literal names, which change with unrelated edits.
"""
import difflib
import pathlib
import re
import sys

PROC = re.compile(r"^(\S+)\s+PROC\b")
ENDP = re.compile(r"^(\S+)\s+ENDP\b")


def functions(directory):
    """Returns {decorated name: (raw body lines, normalised body lines)} of every function in the listings."""
    result = {}
    for listing in sorted(pathlib.Path(directory).rglob("*.asm")):
        name = None
        raw = []
        body = []
        for line in listing.read_text(errors="replace").splitlines():
            if name is None:
                m = PROC.match(line)
                if m:
                    name, raw, body = m.group(1), [line], []
                continue
            raw.append(line)
            if ENDP.match(line):
                result.setdefault(name, (raw, body))
                name = None
                continue
            text = line.split(";", 1)[0].rstrip()
            if not text.strip():
                continue
            text = re.sub(r"\$LN\d+@\S+|\$LN\d+|\$SG\d+|\$T\d+|\$ip2state\$\S+", "<L>", text)
            body.append(text.strip())
    return result


def matches(funcs, pattern):
    wanted = [p for p in pattern.split("+") if p and not p.startswith("!")]
    unwanted = [p[1:] for p in pattern.split("+") if p.startswith("!")]
    return sorted(n for n in funcs if all(p in n for p in wanted) and not any(p in n for p in unwanted))


def short(name, limit=200):
    return name if len(name) <= limit else name[:limit] + "..."


def main():
    base_dir, change_dir, specs, out = sys.argv[1], sys.argv[2], sys.argv[3], pathlib.Path(sys.argv[4])
    base, change = functions(base_dir), functions(change_dir)

    for spec in [s for s in specs.split(";") if s.strip()]:
        fields = spec.split("::")
        label = fields[0].strip()
        base_pattern = fields[1].strip()
        change_pattern = fields[2].strip() if len(fields) > 2 else base_pattern

        a, b = matches(base, base_pattern), matches(change, change_pattern)
        print(f"==== {label}: {len(a)} in base, {len(b)} in change")

        for side, funcs, names in (("base", base, a), ("change", change, b)):
            for i, n in enumerate(names):
                safe = re.sub(r"[^A-Za-z0-9_.@-]", "_", n)[:150]
                target = out / side / label
                target.mkdir(parents=True, exist_ok=True)
                (target / f"{i}-{safe}.asm").write_text("\n".join(funcs[n][0]) + "\n")
                print(f"  {side}[{i}] {short(n)}  ({len(funcs[n][1])} instructions)")

        for side, funcs, names, pattern in (("base", base, a, base_pattern), ("change", change, b, change_pattern)):
            if not names:
                # Help to fix a pattern: the names that contain its first two substrings.
                first = [p for p in pattern.split("+") if p and not p.startswith("!")][:2]
                near = sorted(n for n in funcs if all(p in n for p in first))[:10]
                print(f"  no match in {side}; names with {first}: {len(near)} shown")
                for n in near:
                    print(f"    {short(n)}")

        if len(a) == 1 and len(b) == 1:
            x, y = base[a[0]][1], change[b[0]][1]
            if x == y:
                print("  IDENTICAL bodies")
            else:
                print("  DIFFERENT bodies:")
                for d in difflib.unified_diff(x, y, "base", "change", lineterm="", n=3):
                    print("    " + d)
        else:
            print("  (not exactly one function on each side: compare the listings by hand)")


main()
