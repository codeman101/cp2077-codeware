#!/usr/bin/env python3
"""INVALID_TEMP-style lint for redscript sources (R34, a warning only, never fails a build).

The script VM reads a value straight off a temporary in place: a field of a struct a call returns (`player.GetWorldPosition().Z`) or an
array a call returns, handed to an array intrinsic (`ArraySize(des.GetTaggedIDs(tag))`) or indexed (`GetList()[0]`). On the Mac (R33
struct-return probe, runs 20261010-220521 and -220851) every such read gave 0 or stale memory, for native and script functions alike;
through a local (`let p = player.GetWorldPosition(); p.Z`) it is right. Upstream redscript reports the struct case as INVALID_TEMP
(an error it allows at runtime, commit 69bfad671, 2025-01-16); the older scc the Mac game ships compiles it silently.

The lint is lexical (no types), so it sorts its findings:
  certain   `).X` `.Y` `.Z` `.W` (Vector2/3/4), `.i` `.j` `.k` `.r` (Quaternion), `.Roll` `.Pitch` `.Yaw` (EulerAngles), `.hash`
            (EntityID) read off a call or parenthesized expression; an array intrinsic whose array argument is a call; `)[`.
  possible  any other field read off a call result. Fine when the call returns a ref<> (a handle); a bug when it returns a struct.

Usage: reds_invalid_temp.py <dir or file> [...]   prints one line per finding, then a summary; exit 0 always.
As a module: scan_tree(root) -> list of findings (dicts with path, line, kind, text).
"""
import os
import re
import sys

STRUCT_FIELDS = {'X', 'Y', 'Z', 'W', 'i', 'j', 'k', 'r', 'Roll', 'Pitch', 'Yaw', 'hash'}
ARRAY_INTRINSICS = ('ArraySize', 'ArrayContains', 'ArrayCount', 'ArrayFindFirst', 'ArrayFindLast', 'ArrayLast', 'ArrayFindFirstFast',
                    'ArrayFindLastFast', 'ArrayContainsFast')


def strip_code(text):
    """Blank out comments and string/name literals, keeping offsets and line breaks, so the patterns only see code."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j]))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            j = min(j + 1, n)
            out.append('"' + ' ' * max(0, j - i - 2) + ('"' if j - i >= 2 else ''))
            i = j
        else:
            out.append(c)
            i += 1
    return ''.join(out)


FIELD_AFTER_CLOSE = re.compile(r'\)\s*\.\s*([A-Za-z_][A-Za-z0-9_]*)\b(?!\s*\()')
INDEX_AFTER_CLOSE = re.compile(r'\)\s*\[')


def matching_open(code, close_idx):
    depth = 0
    for k in range(close_idx, -1, -1):
        if code[k] == ')':
            depth += 1
        elif code[k] == '(':
            depth -= 1
            if depth == 0:
                return k
    return -1


def first_argument(code, open_idx):
    """The text of the first argument of the call whose '(' is at open_idx."""
    depth = 0
    for k in range(open_idx, len(code)):
        ch = code[k]
        if ch in '([':
            depth += 1
        elif ch in ')]':
            depth -= 1
            if depth == 0:
                return code[open_idx + 1:k]
        elif ch == ',' and depth == 1:
            return code[open_idx + 1:k]
    return ''


# value types (structs) whose fields read off a temporary are always wrong; ref-returning natives a field read off is fine
KNOWN_STRUCTS = {'Vector2', 'Vector3', 'Vector4', 'Quaternion', 'EulerAngles', 'EntityID', 'WorldPosition', 'WorldTransform', 'TweakDBID',
                 'ItemID', 'Color', 'HDRColor', 'Transform', 'GameInstance', 'Box', 'Matrix'}
KNOWN_REF_CALLS = {'GetAllBlackboardDefs', 'GetPlayer', 'Deref'}
FUNC_DECL = re.compile(r'\bfunc\s+([A-Za-z_][A-Za-z0-9_]*)\s*\([^)]*\)\s*->\s*([A-Za-z_][A-Za-z0-9_:<>]*)')
STRUCT_DECL = re.compile(r'\bstruct\s+([A-Za-z_][A-Za-z0-9_]*)')


class Index:
    """Return types of the script functions and the script structs of the scanned tree (natives of the base game are not in it)."""

    def __init__(self):
        self.returns = {}
        self.structs = set(KNOWN_STRUCTS)

    def add(self, code):
        for m in FUNC_DECL.finditer(code):
            self.returns.setdefault(m.group(1), set()).add(m.group(2))
        for m in STRUCT_DECL.finditer(code):
            self.structs.add(m.group(1))

    def kind_of(self, name):
        """'ref' when every script declaration of name returns ref<>/wref<>, 'struct' when one returns a struct, None when unknown."""
        if name in KNOWN_REF_CALLS:
            return 'ref'
        rets = self.returns.get(name)
        if not rets:
            return None
        if any(r.split('<')[0] in self.structs for r in rets):
            return 'struct'
        if all(r.startswith(('ref<', 'wref<')) for r in rets):
            return 'ref'
        return None


def callee_name(code, opened):
    m = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:<[^<>()]*>)?\s*$', code[:opened])
    return m.group(1) if m else ''


def scan_text(path, text, index=None):
    code = strip_code(text)
    found = []
    index = index or Index()

    def add(pos, kind, what):
        line = code.count('\n', 0, pos) + 1
        src = text.splitlines()[line - 1].strip() if line - 1 < len(text.splitlines()) else ''
        found.append({'path': path, 'line': line, 'kind': kind, 'what': what, 'text': src[:200]})

    for m in FIELD_AFTER_CLOSE.finditer(code):
        close = m.start()
        opened = matching_open(code, close)
        before = code[:opened].rstrip() if opened >= 0 else ''
        is_call = bool(re.search(r'[A-Za-z0-9_>]\s*$', before))  # name( ... ) or generic>( ... )
        field = m.group(1)
        name = callee_name(code, opened) if is_call else ''
        kind = index.kind_of(name) if name else None
        if kind == 'ref':
            continue
        if field in STRUCT_FIELDS or kind == 'struct':
            add(close, 'certain', ('field .%s read off the result of %s()' % (field, name)) if is_call else
                ('field .%s read off a parenthesized expression' % field))
        elif is_call:
            add(close, 'possible', 'field .%s read off the result of %s() (fine if it returns a ref<>)' % (field, name))
    for m in INDEX_AFTER_CLOSE.finditer(code):
        opened = matching_open(code, m.start())
        before = code[:opened].rstrip() if opened >= 0 else ''
        if re.search(r'[A-Za-z0-9_>]\s*$', before) and callee_name(code, opened) not in KNOWN_REF_CALLS:
            add(m.start(), 'certain', 'an element read off the result of %s() ([...])' % callee_name(code, opened))
    for name in ARRAY_INTRINSICS:
        for m in re.finditer(r'\b%s\s*\(' % name, code):
            arg = first_argument(code, m.end() - 1).strip()
            if arg.endswith(')') and re.search(r'[A-Za-z_][A-Za-z0-9_]*\s*\(', arg) and not arg.startswith('('):
                inner = callee_name(arg, matching_open(arg, len(arg) - 1))
                if inner in KNOWN_REF_CALLS:
                    continue
                add(m.start(), 'certain', '%s over the result of %s()' % (name, inner))
    return found


def scan_tree(root):
    findings = []
    paths = [root] if os.path.isfile(root) else [os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs if f.endswith('.reds')]
    texts = {}
    index = Index()
    for p in sorted(paths):
        try:
            texts[p] = open(p, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        index.add(strip_code(texts[p]))
    for p in sorted(texts):
        findings.extend(scan_text(p, texts[p], index))
    return findings


def format_findings(findings, base=None):
    lines = []
    for f in findings:
        p = os.path.relpath(f['path'], base) if base else f['path']
        lines.append('[INVALID_TEMP %s] %s:%d: %s | %s' % (f['kind'], p, f['line'], f['what'], f['text']))
    return lines


def main(argv):
    if not argv:
        print(__doc__)
        return 0
    allf = []
    for a in argv:
        allf.extend(scan_tree(a))
    for line in format_findings(allf):
        print(line)
    certain = sum(1 for f in allf if f['kind'] == 'certain')
    print('reds_invalid_temp: %d certain, %d possible' % (certain, len(allf) - certain))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
