#!/usr/bin/env python3
"""prepare_library.py - turn the extracted HP 2000 contributed library into
programs the 2000E can take, and a manifest for tsb_session's library command.

Author: Thomas Dzubin

Usage: prepare_library.py EXTRACTED_DIR OUTPUT_DIR [ACCOUNT ...]

EXTRACTED_DIR holds one folder per library account (C901, Z905, ...) with one
text file per program. OUTPUT_DIR gets programs/NAME.bas and manifest.txt
("NAME SUBCHANNEL PATH" per line). With account names after the folders only
those accounts are used, in the order given; without, all are used in a default
order that puts the games first.

A program is taken only if the 2000E can be expected to accept it:
  - it starts with a line number (it is BASIC source, not a data file);
  - after the gap behind each line number is shortened to one space, no line is
    longer than 72 characters (the 2000E limit);
  - it has no characters that TSB treats specially or a terminal cannot send: only
    the printable ASCII 0x20 to 0x7E, and no underscore (TSB's character delete).
tsb_session then drops any program TSB still rejects.
"""

import os
import re
import sys

MAX_LINE = 72                          # the 2000E limit; --max-line=N for a system that allows more
SECTOR_BYTES = 256
SECTORS_PER_SUBCHANNEL = 9000          # usable, a little under the 9744 of a platter
SECTOR_RATIO = 0.85                    # saved size is about 0.83 of the text size
DEFAULT_ORDER = ["Z905", "C904", "Z904", "Z902", "Z901", "C906", "Z903", "Z906", "C902", "C901", "C903"]
FIRST_USER_SUBCHANNEL = 1
LAST_SUBCHANNEL = 3
BAD_CHARS = set("_")
RENAME = False                         # --rename-duplicates: keep different programs that share a name


def normalise(text):
    """Return the program's lines with one space after the line number, or None."""
    out = []
    for raw in text.split("\n"):
        if raw.strip() == "":
            continue
        m = re.match(r"^\s*(\d+)\s+(.*)$", raw)
        if m is None:
            m = re.match(r"^\s*(\d+)$", raw)
            if m is None:
                return None
            line = m.group(1)
        else:
            line = m.group(1) + " " + m.group(2).rstrip()
        out.append(line)
    return out


def acceptable(lines):
    if not lines:
        return "empty"
    for line in lines:
        if len(line) > MAX_LINE:
            return "line longer than %d" % MAX_LINE
        for c in line:
            if ord(c) < 0x20 or ord(c) > 0x7E:
                return "control or non-ASCII character"
            if c in BAD_CHARS:
                return "underscore"
    return None


def unused_name(name, seen):
    """A name for a different program that has the same name as an earlier one:
    the first five characters and a digit or letter."""
    stem = name[:5]
    for c in "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ":
        if stem + c not in seen:
            return stem + c
    return None


def main():
    global MAX_LINE, RENAME
    args = []
    for a in sys.argv[1:]:
        if a.startswith("--max-line="):
            MAX_LINE = int(a.split("=", 1)[1])
        elif a == "--rename-duplicates":
            RENAME = True
        else:
            args.append(a)
    if len(args) < 2:
        print(__doc__)
        return 2
    src, dst = args[0], args[1]
    accounts = args[2:] or DEFAULT_ORDER
    os.makedirs(os.path.join(dst, "programs"), exist_ok=True)
    seen = {}
    contents = {}
    renames = []
    chosen = []
    skipped = {}
    for acc in accounts:
        folder = os.path.join(src, acc)
        if not os.path.isdir(folder):
            print("no folder", folder)
            continue
        for name in sorted(os.listdir(folder)):
            data = open(os.path.join(folder, name), "rb").read().decode("latin1")
            lines = normalise(data) if re.match(r"^\s*\d+(\s|$)", data) else None
            why = "not BASIC source" if lines is None else acceptable(lines)
            if why is None and name in seen:
                if RENAME:
                    if lines == contents[name]:
                        why = "same program as " + seen[name]
                    else:
                        new = unused_name(name, seen)
                        if new is None:
                            why = "no free name for a copy of " + name
                        else:
                            renames.append("%s %s/%s %s" % (new, acc, name, seen[name] + "/" + name))
                            name = new
                else:
                    why = "name already used by " + seen[name]
            if why is not None:
                skipped.setdefault(why, []).append(acc + "/" + name)
                continue
            seen[name] = acc
            contents[name] = lines
            chosen.append((acc, name, lines))
    sub = FIRST_USER_SUBCHANNEL
    used = 0
    with open(os.path.join(dst, "manifest.txt"), "w", newline="\n") as manifest:
        for acc, name, lines in chosen:
            body = "\n".join(lines) + "\n"
            path = os.path.join(dst, "programs", name + ".bas")
            with open(path, "w", newline="\n") as f:
                f.write(body)
            sectors = int(len(body) * SECTOR_RATIO / SECTOR_BYTES) + 2
            if used + sectors > SECTORS_PER_SUBCHANNEL and sub < LAST_SUBCHANNEL:
                sub += 1
                used = 0
            used += sectors
            manifest.write("%s %d %s\n" % (name, sub, path.replace("\\", "/")))
    if renames:
        with open(os.path.join(dst, "renamed.txt"), "w", newline="\n") as f:
            f.write("NEWNAME  FROM-ACCOUNT/NAME  FIRST-USE\n" + "\n".join(renames) + "\n")
    total = sum(len("\n".join(l)) for a, n, l in chosen)
    print("programs chosen: %d (%d bytes of text)" % (len(chosen), total))
    print("subchannel 1..%d: about %d sectors in the last one" % (sub, used))
    for why, names in sorted(skipped.items(), key=lambda x: -len(x[1])):
        print("skipped %4d: %s" % (len(names), why))
    return 0


if __name__ == "__main__":
    sys.exit(main())
