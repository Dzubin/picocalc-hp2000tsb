#!/usr/bin/env python3
"""repair_library.py - restore the dummy variable of DEF FN statements in the
extracted HP 2000 contributed library.

Author: Thomas Dzubin

Usage: repair_library.py EXTRACTED_DIR OUTPUT_DIR

The text of the library as extracted from the Access DUMP tape has lost the
dummy variable of every user-defined function: the header reads DEF FNM() and
the body has a hole wherever the dummy variable was used, for example

    DEF FNM()=(=0)*7+(>0 AND <8)*+(=8)*1     was     DEF FNM(X)=(X=0)*7+...
    DEF FNL()=LOG()/LOG(10)                  was     DEF FNL(X)=LOG(X)/LOG(10)
    DEF FNN()=P[]-INT(P[]/512)*512           was     DEF FNN(X)=P[X]-INT(...)

The calls (FNM(M[I,1])) are intact, so every function took one argument.

The repair: tokenise the body and walk it expecting an operand or an operator.
Where an operand is expected and the next token cannot start one (a binary
operator, a closing bracket, a comma, the end of the line) a dummy variable is
inserted. A leading "+" counts as a hole as well (nobody writes a unary plus), a
leading "-" is taken as a real unary minus. The dummy is the first letter of
X Y Z W V U T S R Q ... that the body does not use as a variable itself. If no
hole is found the header still gets a dummy, because the calls pass an argument.

Every changed line is written to OUTPUT_DIR/repairs.txt for checking; files
with a function that could not be repaired are listed in OUTPUT_DIR/unsure.txt.
"""

import os
import re
import sys

TOKEN = re.compile(r"""
    \s+ |
    (?P<str>"[^"]*") |
    (?P<num>(\d+\.?\d*|\.\d+)(E[+-]?\d+)?) |
    (?P<fn>FN[A-Z][0-9]?\$?) |
    (?P<word>AND|OR|NOT|MIN|MAX|MOD) |
    (?P<func>[A-Z]{2,}\$?) |
    (?P<var>[A-Z][0-9]?\$?) |
    (?P<op><>|<=|>=|=<|=>|[-+*/^=<>#,()\[\]])
    """, re.X)

BINARY = {"*", "/", "^", "=", "<", ">", "<>", "<=", ">=", "=<", "=>", "#",
          "AND", "OR", "MIN", "MAX", "MOD"}
DUMMY_ORDER = "XYZWVUTSRQPONMLKJIHGFEDCBA"


def tokenise(text):
    out = []
    pos = 0
    while pos < len(text):
        m = TOKEN.match(text, pos)
        if m is None:
            return None
        if m.lastgroup is not None:
            out.append((m.lastgroup, m.group()))
        pos = m.end()
    return out


def fill_holes(tokens, dummy, minus_mode):
    """Return the token texts with the dummy inserted at the holes, and the count.

    A "-" where an operand is due may be a real unary minus or the dummy dropped in
    front of a subtraction. minus_mode 1: it is a hole only when a number follows
    ((-1)/12 was (X-1)/12); minus_mode 2: it is always a hole."""
    out = []
    holes = 0
    stack = []
    expect = True                       # an operand is expected next
    prev_name = False                   # the previous token was a name that may take ( or [
    for index, (kind, text) in enumerate(tokens):
        if expect:
            if kind in ("num", "str"):
                out.append(text)
                expect = False
                prev_name = False
            elif kind in ("var", "fn", "func"):
                out.append(text)
                expect = False
                prev_name = True
            elif text == "(":
                out.append(text)
                stack.append(")")
                prev_name = False
            elif text == "-" and (minus_mode == 2 or (
                    minus_mode == 1 and index + 1 < len(tokens) and tokens[index + 1][0] == "num")):
                out.append(dummy)
                holes += 1
                out.append(text)
                prev_name = False
            elif text == "-" or text == "NOT":
                out.append(text)
                prev_name = False
            elif text == "+" or text in BINARY:
                out.append(dummy)
                holes += 1
                out.append(text)
                expect = True
                prev_name = False
            elif text in (")", "]", ","):
                out.append(dummy)
                holes += 1
                out.append(text)
                if text == ",":
                    expect = True
                else:
                    if stack:
                        stack.pop()
                    expect = False
                prev_name = False
            else:
                out.append(text)
        else:
            if text in ("(", "[") and prev_name:
                out.append(text)
                stack.append(")" if text == "(" else "]")
                expect = True
            elif text in (")", "]"):
                out.append(text)
                if stack:
                    stack.pop()
            elif text == "," or text in BINARY or text in ("+", "-"):
                out.append(text)
                expect = True
            else:
                out.append(text)
            prev_name = False
    if expect:                          # the line ended where an operand was due
        out.append(dummy)
        holes += 1
    return out, holes


def repair_line(line, program_vars):
    m = re.match(r"^(\s*\d+\s+)DEF\s+(FN[A-Z][0-9]?\$?)\(\)\s*=(.*)$", line)
    if m is None:
        return line, None
    head, name, body = m.groups()
    tokens = tokenise(body)
    if tokens is None:
        return line, "cannot tokenise"
    used = {t for k, t in tokens if k == "var"}
    dummy = next((c for c in DUMMY_ORDER if c not in used), None)
    if dummy is None:
        return line, "no free dummy letter"
    fixed, holes = fill_holes(tokens, dummy, 1)
    if holes == 0:
        fixed, holes = fill_holes(tokens, dummy, 2)
    new_body = join_tokens(fixed)
    return "%sDEF %s(%s)=%s" % (head, name, dummy, new_body), holes


def join_tokens(parts):
    """Put the tokens back together: a space only where two words would run together."""
    out = []
    for p in parts:
        if out and re.match(r"[A-Z0-9.$]", p[0]) and re.match(r"[A-Z0-9.$]", out[-1][-1]):
            out.append(" ")
        out.append(p)
    return "".join(out)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    changed = unsure = files = total = 0
    repairs = []
    doubts = []
    for root, _dirs, names in os.walk(src):
        for name in sorted(names):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, src)
            data = open(path, "rb").read().decode("latin1")
            out_lines = []
            file_changed = False
            for line in data.split("\n"):
                new, holes = repair_line(line, None)
                if holes is None:
                    out_lines.append(line)
                    continue
                total += 1
                if isinstance(holes, str) or holes == 0:
                    unsure += 1
                    doubts.append("%s: %s  [%s]" % (rel, line.strip(), holes))
                if new != line:
                    changed += 1
                    file_changed = True
                    repairs.append("%s\n   was: %s\n   now: %s" % (rel, line.strip(), new.strip()))
                out_lines.append(new)
            target = os.path.join(dst, rel)
            os.makedirs(os.path.dirname(target), exist_ok=True)
            with open(target, "wb") as f:
                f.write("\n".join(out_lines).encode("latin1"))
            files += 1
    with open(os.path.join(dst, "repairs.txt"), "w") as f:
        f.write("\n".join(repairs) + "\n")
    with open(os.path.join(dst, "unsure.txt"), "w") as f:
        f.write("\n".join(doubts) + "\n")
    print("files %d, DEF FN lines %d, repaired %d, with no hole found or other doubt %d"
          % (files, total, changed, unsure))
    return 0


if __name__ == "__main__":
    sys.exit(main())
