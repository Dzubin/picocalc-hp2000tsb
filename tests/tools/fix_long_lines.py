#!/usr/bin/env python3
"""fix_long_lines.py - make library programs fit what a 2000 Access terminal line
accepts, with changes that keep the program doing the same thing.

Author: Thomas Dzubin

Usage: fix_long_lines.py PROGRAM_DIR OUTPUT_DIR [--max=119]

PROGRAM_DIR holds NAME.bas files (the output of prepare_library.py). The files
are copied to OUTPUT_DIR with these repairs; every change is listed in
OUTPUT_DIR/fixes.txt, and what could not be repaired in OUTPUT_DIR/unfixed.txt.

 1. Adjacent quoted strings, as in PRINT "A""B", were allowed by the older TSB
    systems (the two strings are joined) but Access rejects them. The two
    quotes are removed so the string is joined: "AB".
 2. A statement longer than the terminal accepts (119 characters here) is split.
    Only PRINT statements without a file number or USING are split, and only
    where the output cannot change: after a top level semicolon, or between two
    string pieces (quoted strings and apostrophe codes), where a semicolon is
    added. The first piece keeps the line number, so jumps into the line still
    land on the start. The other pieces take the free line numbers after it. If
    there are not enough, the whole program is renumbered in steps of 10 and every
    literal line number after GOTO, GOSUB, THEN, OF, ON and USING is rewritten; a
    program is left alone if a reference cannot be found for its target.
 3. A REM line is cut to the limit.
"""

import os
import re
import sys

MAX = 119
PIECE_MAX = 108                  # leave room for the line number and PRINT


def merge_adjacent_strings(body):
    """Join "A""B" into "AB". An empty string ("") on its own is left alone."""
    out = []
    i = 0
    n = len(body)
    while i < n:
        c = body[i]
        if c != '"':
            out.append(c)
            i += 1
            continue
        j = body.find('"', i + 1)
        if j < 0:
            out.append(body[i:])
            break
        text = body[i + 1:j]
        i = j + 1
        while i < n and body[i] == '"':          # an adjacent quoted string follows
            k = body.find('"', i + 1)
            if k < 0:
                break
            text += body[i + 1:k]
            i = k + 1
        out.append('"' + text + '"')
    return "".join(out)


def pieces_of_print(args):
    """Split the argument text of a PRINT into pieces: ('lit', text), ('sep', ';'),
    ('other', text). Returns None if something unusual is found."""
    pieces = []
    i = 0
    n = len(args)
    other = []

    def flush():
        if other:
            pieces.append(("other", "".join(other)))
            del other[:]

    while i < n:
        c = args[i]
        if c == '"':
            j = args.find('"', i + 1)
            if j < 0:
                return None
            flush()
            pieces.append(("lit", args[i:j + 1]))
            i = j + 1
        elif c == "'" and i + 1 < n and args[i + 1].isdigit():
            j = i + 1
            while j < n and args[j].isdigit():
                j += 1
            flush()
            pieces.append(("lit", args[i:j]))
            i = j
        elif c == ";":
            flush()
            pieces.append(("sep", ";"))
            i += 1
        elif c == "(":
            depth = 0
            j = i
            while j < n:
                if args[j] == '"':
                    k = args.find('"', j + 1)
                    if k < 0:
                        return None
                    j = k
                elif args[j] == "(":
                    depth += 1
                elif args[j] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            if j >= n:
                return None
            other.append(args[i:j + 1])
            i = j + 1
        else:
            other.append(c)
            i += 1
    flush()
    return pieces


def break_long_literals(pieces, size=80):
    """A quoted string longer than size is cut into several quoted strings (at a
    blank where there is one); split_print then puts a semicolon between them."""
    out = []
    for kind, text in pieces:
        if kind == "lit" and text.startswith('"') and len(text) > size + 2:
            body = text[1:-1]
            while len(body) > size:
                cut = body.rfind(" ", 0, size)
                cut = cut + 1 if cut > 0 else size
                out.append(("lit", '"' + body[:cut] + '"'))
                body = body[cut:]
            out.append(("lit", '"' + body + '"'))
        else:
            out.append((kind, text))
    return out


def split_print(number, body):
    """Return the list of statement bodies replacing a long PRINT, or None."""
    if not body.startswith("PRINT"):
        return None
    args = body[5:].lstrip()
    if args.startswith("#") or args.startswith("USING") or "USING" in args.split(";")[0][:8]:
        return None
    pieces = pieces_of_print(args)
    if pieces is None:
        return None
    pieces = break_long_literals(pieces)
    prefix = len(str(number)) + 1 + 6
    limit = PIECE_MAX - prefix + 6
    chunks = []
    cur = []                           # pieces of the current chunk
    cur_len = 0
    best = None                        # (index in cur after which we may cut, adds_semicolon)
    for kind, text in pieces:
        add = len(text)
        cur.append((kind, text))
        cur_len += add
        # a cut after a separator, or between two literal pieces
        if kind == "sep":
            best = (len(cur), False)
        elif kind == "lit":
            best = (len(cur), True)    # a cut here adds a semicolon, but only if a literal follows
        if cur_len > limit:
            # cut at the last allowed place before the overflow
            cut = None
            for idx in range(len(cur) - 1, 0, -1):
                if cur[idx - 1][0] == "sep":
                    cut = (idx, False)
                    break
                if cur[idx - 1][0] == "lit" and cur[idx][0] == "lit":
                    cut = (idx, True)
                    break
            if cut is None:
                return None
            idx, add_sc = cut
            head = "".join(t for _, t in cur[:idx]) + (";" if add_sc else "")
            chunks.append(head)
            cur = cur[idx:]
            cur_len = sum(len(t) for _, t in cur)
    chunks.append("".join(t for _, t in cur))
    if any(len(c) + prefix > MAX for c in chunks):
        return None
    return ["PRINT " + c for c in chunks]


REF = re.compile(r"""
    (?P<kw>\b(?:GOTO|GOSUB|THEN|USING)\b\s*)(?P<n>\d+)(?!\d)(?!\s*(?:[-+*/^<>=(]|OF\b))
  | (?P<ofkw>\bOF\b\s*)(?P<list>\d+(?:\s*,\s*\d+)*)
""", re.X)


def renumber(lines):
    """lines: list of (number, body). Return new list renumbered by 10, or None."""
    old = [n for n, _ in lines]
    step = 10
    if len(old) * step > 9990:
        step = 5
    if len(old) * step > 9990:
        return None
    mapping = {n: (i + 1) * step for i, n in enumerate(old)}
    out = []

    def fix_code(code):
        bad = []

        def sub(m):
            if m.group("n") is not None:
                t = int(m.group("n"))
                if t not in mapping:
                    bad.append(t)
                    return m.group(0)
                return m.group("kw") + str(mapping[t])
            nums = [int(x) for x in re.split(r"\s*,\s*", m.group("list"))]
            if any(t not in mapping for t in nums):
                bad.extend(t for t in nums if t not in mapping)
                return m.group(0)
            return m.group("ofkw") + ",".join(str(mapping[t]) for t in nums)
        return REF.sub(sub, code), bad

    for n, body in lines:
        # keep text inside quotes unchanged
        parts = re.split(r'("[^"]*")', body)
        new_parts = []
        for k, part in enumerate(parts):
            if k % 2 == 1 or body.startswith("REM"):
                new_parts.append(part)
            else:
                fixed, bad = fix_code(part)
                if bad:
                    return None
                new_parts.append(fixed)
        out.append((mapping[n], "".join(new_parts)))
    return out


def process(name, text, fixes, unfixed):
    lines = []
    for raw in text.split("\n"):
        if not raw.strip():
            continue
        m = re.match(r"^(\d+) (.*)$", raw)
        if m is None:
            return None
        lines.append((int(m.group(1)), m.group(2)))
    changed = False
    # 1. adjacent strings
    for i, (n, body) in enumerate(lines):
        if "REM" == body[:3]:
            continue
        new = merge_adjacent_strings(body)
        if new != body:
            fixes.append("%s %d: joined adjacent strings" % (name, n))
            lines[i] = (n, new)
            changed = True
    # 2/3. long lines
    result = []
    nums = [n for n, _ in lines]
    need_renumber = False
    plan = {}
    for i, (n, body) in enumerate(lines):
        if len("%d %s" % (n, body)) <= MAX:
            continue
        if body.startswith("REM"):
            lines[i] = (n, body[:MAX - len(str(n)) - 1])
            fixes.append("%s %d: REM shortened" % (name, n))
            changed = True
            continue
        parts = split_print(n, body)
        if parts is None:
            unfixed.append("%s %d: %s" % (name, n, body[:60]))
            continue
        plan[i] = parts
        nxt = nums[i + 1] if i + 1 < len(nums) else 99999
        if nxt - n - 1 < len(parts) - 1:
            need_renumber = True
    if plan:
        if need_renumber:
            re_lines = renumber(lines)
            if re_lines is None:
                for i in plan:
                    unfixed.append("%s %d: no room and cannot renumber safely" % (name, lines[i][0]))
                plan = {}
            else:
                lines = re_lines
                fixes.append("%s: renumbered by 10" % name)
                changed = True
                # the plan's line positions are unchanged; numbers come from the new list
        nums = [n for n, _ in lines]
        for i, parts in plan.items():
            n = lines[i][0]
            nxt = nums[i + 1] if i + 1 < len(nums) else 99999
            if nxt - n - 1 < len(parts) - 1:
                unfixed.append("%s %d: no room" % (name, n))
                continue
            extra = [(n + k + 1, parts[k + 1]) for k in range(len(parts) - 1)]
            plan[i] = [(n, parts[0])] + extra
            fixes.append("%s %d: split into %d lines" % (name, n, len(parts)))
            changed = True
        for i, (n, body) in enumerate(lines):
            if i in plan and isinstance(plan[i], list) and plan[i] and isinstance(plan[i][0], tuple):
                result.extend(plan[i])
            else:
                result.append((n, body))
    else:
        result = lines
    return "\n".join("%d %s" % (n, b) for n, b in result) + "\n", changed


def main():
    global MAX
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    for a in sys.argv[1:]:
        if a.startswith("--max="):
            MAX = int(a.split("=", 1)[1])
    if len(args) != 2:
        print(__doc__)
        return 2
    src, dst = args
    os.makedirs(dst, exist_ok=True)
    fixes, unfixed = [], []
    count = changed_n = 0
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".bas"):
            continue
        text = open(os.path.join(src, fn), newline="").read()
        res = process(fn[:-4], text, fixes, unfixed)
        if res is None:
            res = (text, False)
        out, changed = res
        open(os.path.join(dst, fn), "w", newline="\n").write(out)
        count += 1
        changed_n += changed
    open(os.path.join(dst, "fixes.txt"), "w").write("\n".join(fixes) + "\n")
    open(os.path.join(dst, "unfixed.txt"), "w").write("\n".join(unfixed) + "\n")
    print("%d programs, %d changed, %d changes, %d lines left unfixed"
          % (count, changed_n, len(fixes), len(unfixed)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
