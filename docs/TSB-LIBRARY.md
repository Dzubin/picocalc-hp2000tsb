# The HP 2000 contributed program library: what exists and where

Research notes, 2026-10-08. Nothing listed here has been downloaded yet.

**Where it came from:** HP kept a Contributed Program Library of customer-written programs, ordered
on paper tape for a small fee. Mike Mayfield's HP BASIC *Star Trek* entered it as `STTR1` in
February 1973 and spread from there to the whole BASIC world (see `HISTORY.md`).

**Current status (2026-10-09):** the research below is the story of how the library was fitted to the systems and is
kept as it was written. The outcome: the 1054 programs that Access accepts (after `tests/tools/repair_library.py` and
`fix_long_lines.py`) are installed in the Access system library on a 2883 disc, all UNRESTRICTED. DECSN, GTAPID and the
two data files STRNG1 and STRNG2 are not imported. See the README, `HOW-TO-RUN.txt` and `LESSONS-LEARNED.md`.

HP sold its Time-Shared BASIC systems with a "Contributed Library": programs
written by users and collected by HP, supplied on paper tape and, in the late
1970s, as one magnetic tape. It is now treated as abandonware, and Bitsavers holds
both the tape images and the printed documentation.

## Tape images (Bitsavers, `bits/HP/tapes/2000tsb/`)

Base address: <http://www.bitsavers.org/bits/HP/tapes/2000tsb/>

| File | Size | Notes |
|---|---|---|
| `36600-10001_2000contrib.tap.gz` | 3.5 MB | HP product 36600-10001, the Contributed Library tape. DUMP format, tape system level 6000 "HP 2000 Operating System, Release B (ACCESS)", feature level 1500, dated May 2, 1980. About 1,266 files. |
| `36600-10001_2000contrib.dir.txt` | 68 KB | Directory listing of that tape. |
| `36600-10001_2000contribLog.txt` | 14 KB | Log of reading the tape, showing 274 medium errors (mostly in the last few files, 1483 to 1486). |
| `36600-10001_2000contrib.tar.gz` | 2.0 MB | The same tape's files extracted. |
| `HP_2000_Contributed_Library_Rev_1638.tap.gz` | 2.2 MB | A second copy of the library, "Rev 1638" (a smaller image, likely a better read). Directory: `HP_2000_Contributed_Library_Rev_1638.dir.txt` (68 KB); files extracted in `HP_2000_Contributed_Library_Rev-1638.tar.gz` (2.0 MB). |
| `2000dmp5-5-84iclContrib.tap.gz` | 3.4 MB | A contributed-programs dump dated 1984 (named for ICL), system level 12549, DUMP format. Holds more accounts (A000, A001, A002, A100, A900, C001, C901 to C906, G000, L100, W500, Z901 to Z906). Directory: `2000dmp5-5-84iclContrib.dir.txt` (101 KB); also a `.tar.gz` (3.0 MB). |
| `TSB_E_F_srcs.tar` | 2.4 MB | Source listings of TSB E and F (not the library). |

### What is on the main library tape (36600-10001)

Accounts and the number of files in each: C901 15, C902 91, C903 12, C904 121,
C906 267, Z901 87, Z902 110, Z903 213, Z904 196, Z905 63, Z906 91. By name:
Z905 is mostly games (KENO, WUMPUS, LUNAR and others), Z902 statistics routines,
Z903 business and accounting, Z904 science and mathematics, C904 tutorials
(TUT01 to TUT25), C902 analysis (the IDA series). Some entries appear to be in
other languages (COBOL, assembler, FORTRAN, ALGOL); the listing does not say
which files are BASIC.

## Documentation (Bitsavers, `pdf/hp/2000TSB/`)

Base address: <http://www.bitsavers.org/pdf/hp/2000TSB/>

| File | Size | What |
|---|---|---|
| `5952-4369_Index_To_HP_2000_Series_Contributed_Library_Jul74.pdf` | 4.0 MB | July 1974 index of the library, with the BASIC and the non-BASIC programs. |
| `36000-91001_HP2000_contribLibrDoc_Vol1_Aug76.pdf` | 7.3 MB | Library documentation, volume 1 (August 1976). |
| `36000-91002_HP2000_contribLibrDoc_Vol2_Aug76.pdf` | 8.7 MB | volume 2 |
| `36000-91003_HP2000_contribLibrDoc_Vol3_Aug76.pdf` | 9.1 MB | volume 3 |
| `36000-91004_HP2000_contribLibrDoc_Vol4_Aug76.pdf` | 19 MB | volume 4 |
| `36000-9100x_HP2000_contribLibrDoc_addendum_Aug76.pdf` | 1.7 MB | addendum (new programs in accounts Z906 and C906; the whole library sold as one tape, HP 36600A) |
| `2000E_UsersMan.pdf`, `2000E_OperGuide.pdf` | 4.7 MB, 1.3 MB | The 2000E manuals (user's manual, operator's guide). |

## What this means for our system

- The images are **magnetic tape** images. Our emulation already reads the SIMH
  tape format (the same one the kit's tapes use), so the tape can be mounted on
  the emulated tape drive (select code 20).
- Loading is done with the 2000E **loader utility**, which has SLOAD (selectively
  load files or accounts from tape to disc) and SDUMP. The kit already uses this:
  `examples.SDUMP.tape` is a selective dump of subchannel 1 of the 7900 disc.
- **Compatibility is not certain.** The library tapes were made on a 2000 Access
  system ("Release B", feature level 1500). A 2020 mailing-list post reports that
  loading the Bitsavers library into a SIMH 2000F system produced no files, and
  the SIMH author thought selective-dump tapes might depend on the TSB version.
  Our system is a 2000E at revision 1534, so the first job is to try it on the PC
  with the test program.
- **Space:** the library holds 1,266 files; the disc is 4.9 MB shared with the
  system. Loading selected accounts (for example only the games, Z905) may be
  needed. The directory listings give the length of every file, so the space can
  be worked out before loading.
- The tape medium errors in the log apply to the 36600-10001 image only; the Rev
  1638 image is the one to try first.
- Not found so far: a ready-made tape of games only, or a 2000E-specific
  library tape. The SIMH `2000E-kit` and the HP part number list (`2000TSB_partNumbers.txt`)
  do not mention a library tape either.

## Suggested next steps

1. Download the Rev 1638 tape image and its directory listing (about 2.3 MB).
2. Mount it in the host test program and try SLOAD of one small account, to
   find out if the 2000E loader utility accepts the tape.
3. If it does, work out which accounts fit on the disc and load them; if not,
   look at the tar extracts (the files are in the DUMP's own text form) and
   consider how to bring the programs in by typing them to the BASIC system.
4. Download the 1974 index and the volume 1 documentation so the programs can be
   described in the README or on a help screen.

## Result of the first SLOAD test (2026-10-08, on the PC)

Downloaded (from the ftp.mirrorservice.org mirror of Bitsavers; bitsavers.org
itself answered 403 to `curl`) into `library-downloads/`:
`HP_2000_Contributed_Library_Rev_1638.tap` (unpacked, 6,039,384 bytes, 3,547
records, 1,270 tape marks), its `.dir.txt`, and the 1974 index PDF.

Test: `SLOAD=1` in `tests/test_boot.c` boots the kit system, takes the user disc
down (`DISC-DN,1`), puts the system to sleep, mounts the library tape instead of
the SLEEP tape, and runs the loader utility's `SLOAD,1,20,0`, the same command
the kit's system generation uses for the example programs.

Answer: `UNEXPECTED END-OF-FILE/END-OF-TAPE  LOAD/DUMP ABORTED`, then the loader
halts. So the 2000E loader utility does not accept this tape. The tapes differ:
the kit's `examples.SDUMP.tape` (made by the 2000E SDUMP) starts with a 12 byte
header record, an 8 byte record and then records of 806, 408 and 1026 bytes; the
library tape (made by a 2000 Access system) starts with a 20 byte header record
and then one tape mark per file, with 2048 and 2072 byte data records.

Ways forward (not yet decided):
1. Look at the extracted files in the Bitsavers `.tar.gz` of the same tape. If
   they are program text, they can be sent to TSB through a terminal (the USB
   line, or a send-file feature) and SAVEd, one program or one account at a time.
2. Work out the Access DUMP record layout from the tape and the directory
   listing, and write a converter to the 2000E SDUMP layout (more work, and only
   worth it if option 1 does not give usable programs).
3. Check whether the SIMH 2000F kit's loader can read the tape and whether a
   2000F system is a better target (it is a different system than ours).

## The extracted files (2026-10-08)

Downloaded `HP_2000_Contributed_Library_Rev-1638.tar.gz` (2.1 MB) into
`library-downloads/` and unpacked it to `library-downloads/extracted/`.

- 1,267 files in 11 account folders, 6.5 MB, all plain ASCII text with LF line
  ends (no tape format at all).
- 1,180 are BASIC source programs (5.77 MB): they start with a line number. The
  other 87 are data files (numbers), empty files or strings.
- The listing uses two spaces after the line number (`1100  PRINT ...`), which
  TSB does not need.
- Sizes by account (BASIC programs / bytes): C901 14 / 89 KB, C902 72 / 340 KB,
  C903 8 / 73 KB, C904 114 / 527 KB, C906 236 / 995 KB, Z901 80 / 459 KB, Z902
  110 / 322 KB, Z903 212 / 1.24 MB, Z904 187 / 799 KB, Z905 (games) 62 / 338 KB,
  Z906 85 / 582 KB. The whole set is bigger than the 4.9 MB disc, so only a part
  can be on the disc at a time.
- **Line length.** TSB-E takes at most 72 characters per line. A test on the PC
  typed a 108 character line: the terminal wrapped it at column 72 with a
  backslash and TSB answered `???`. After shortening the gap after the line
  number to one space, 637 of the 1,180 programs still have at least one line
  longer than 72 characters (mostly long PRINT strings), so they cannot be typed
  in as they are. The other 543 can. For the games (Z905), 25 of 62 are clean.
- Some files hold control characters (22 files, for example plotting and form
  programs); they need care when sent through a terminal.

So the library cannot be loaded by tape, but about half of it can be sent to TSB
as typed text, and many of the rest could be fixed by splitting long PRINT strings
(a mechanical change that has to be done carefully).

## Is there a tape or disc the 2000E can load directly? (searched 2026-10-08)

No. Bitsavers holds three contributed-library tape images (36600-10001,
HP_2000_Contributed_Library_Rev_1638 and the 1984 ICL dump). All three are DUMP
tapes from a 2000 Access system, feature level 1500. The 2020 mailing-list thread
quotes HP's rule that a system with feature level 1000 or more cannot read tapes
from one below 1000 and the other way round, which fits the SLOAD failure on our
2000E. HP's own way across was a conversion program on the Access system
(Appendix F of its manual, 22687-90001) that dumps programs in 2000F format;
nobody in the thread reported that working, and there is no Access system kit.
No ready-made disc of the library was found.

Line length: the 2000E user's manual limits a statement to 72 characters (one
teleprinter line). I did not find the limit for the 2000F or Access in the pages
searched; the 2000F reference manual (2000F-rm-300.pdf) and the Access manual
(22687-90001) would say. We emulate a 2000E, so 72 applies to us.

Plan that avoids typing programs by hand: run the system on the PC (the host
emulator runs about 100 times faster than the PicoCalc), feed the chosen programs
to TSB through a user line from a script, SAVE them, and keep the resulting disc
image. That prepared disc image (and, if wanted, a 2000E-format SDUMP tape made
with the loader utility) is what goes on the SD card.

## Bigger disc, line limits, and the first import test (2026-10-08, later)

**More disc space on the 2000E (works on the PC).** A 7900 pack has two
subchannels: 0 is the fixed platter (system plus the public library) and 1 the
removable one (user files); each is 203 cylinders x 2 surfaces x 24 sectors = 9,744
sectors of 128 words (about 2.4 MB). TSB-E allows subchannels 0 to 3, which means a
second drive. Tested with `tests/tsb_session.c`: attach a blank 4,988,928 byte
image as drive 1, run the loader utility's `FORMAT,2` and `FORMAT,3` (both
answered `# OF BAD TRACKS = 0`), boot, and `DISC-UP,2` and `DISC-UP,3` are
accepted. So the 2000E can have four subchannels, about 9.8 MB in all, and the
emulator already supports four drives; the archived `hp2000e.c` only has to attach a second
image file. The programs that pass the 72 character test (479 files, 1.65 MB of
text, about 6,200 sectors) fit on subchannel 1 alone.

**Line length in other TSB levels.** 2000E manual: a statement has at most 72
characters. 2000F reference manual (20854A): no statement limit is stated; 72 is
mentioned for strings, for DATA strings and for XPUNCH tapes. Access manual
(22687-90001): a statement is always accepted up to 80 characters, and a terminal
may allow as many as 256. The library tape came from an Access system, which is why
many programs have lines over 72.

**The 2000F is a two-CPU system** (as are the B, C and Access systems): a System
Processor runs the BASIC interpreter and owns the discs and tapes, and an I/O
Processor runs the terminal multiplexers, linked by the 12875A Processor
Interconnect (SIMH runs the two as two processes joined by a socket). The SIMH page
lists the kits 2000F rev 1410 (release 7), 2000 Access rev 1812 (release 6) and the
Access source kit; none is downloaded yet.

**First import test.** `tests/tools/prepare_library.py` selects and normalises the
programs and writes a manifest; `tsb_session`'s `library` command logs on as A000,
and for each program does SCR, NAME-name, the lines, SAVE (public library, read with
LIB and GET-$name) and an operator MOVE to the target subchannel. On the 25 games
that pass the length test only 6 were accepted by TSB-E; 18 were rejected (syntax
from later TSB levels: character codes with an apostrophe such as `'7`, MAX and MIN
operators, empty parentheses in `DEF FNA()`, `A$[1,1]` substrings, and so on) and 1
saved program could not be moved. TSB reports a bad line as `ERROR` followed by a
message, shown with the next line's echo. A trial of all 479 selected programs was
started to measure the acceptance rate; see `library_result.txt` of that run.

## What the 2000F and Access kits need (2026-10-08)

Kits downloaded into `docs/kits/2000F/` (release 7, rev 1410, 322 KB zip) and
`docs/kits/2000-Access/` (release 6, rev 1812, 840 KB zip) and unpacked. Their
readmes and `spconfig.inc` / `iopconfig.inc` give the hardware.

| | 2000E (built) | 2000F rev 1410 | 2000 Access rev 1812 |
|---|---|---|---|
| CPUs | one 2100, 16K | System Processor 2100 (or 21MX) 32K + FP; I/O Processor 2100 8K | SP 2100/21MX 32K + FP; IOP 21MX 32K with the IOP instruction set |
| Link between CPUs | none | 12875A Processor Interconnect (new device) | 12875A (new) |
| Disc | 13210A + 7900 | 13210A + 7900 (same as ours), or 12565A + 2883 | 13210A + 7900, or 13175D controller + 7905/7920 (new, 15 MB image supplied) |
| Tape | 13183B | 13183B (same) | 13183B (same) |
| Multiplexer | 12920A | 12920A on the IOP (select codes differ) | 12920A on the IOP |
| Others | paper tape, teleprinter, TBG | teleprinter, paper tape, TBG on each CPU; drum 12610B and line printer 12653A optional | TBG on each CPU; paper tape and punch, line printer on the IOP |
| Supplied disc image | 4.99 MB | 4.99 MB (SYSTEM-7900.disc) | 15.2 MB (SYSTEM-7905.disc) |

Work that is already done for the E and carries over: the 2100 CPU with floating
point, DMA, teleprinter, paper tape reader, time base generator, 13210A disc, 12920A
multiplexer, 13183B tape, the SD card layer, the screen and the USB line.

New work for the 2000F: (1) let the program hold two CPUs and run them in strict
alternation (a macro that turns `hp` into a pointer lets the existing code be
used unchanged for each); (2) the 12875A interconnect between them (SIMH needs
"execution interlocking" to keep the two processors tightly coupled; running both in
one thread in small alternating slices gives that for free); (3) memory of 32K words
for the SP and 8K for the IOP; (4) the start-up procedure of the kit's scripts, which
loads the IOP program through the interconnect with a cross-loader; and (5) the
HIBERNATE shutdown and reload that replace SLEEP. The optional drum and line printer
can be left out.

Extra for Access: the IOP instruction set (extra microcode instructions on the
21MX), 32K for the IOP, the 13175D disc controller with the 7905/7920 drive if the
15 MB disc is kept (a 7900 system could be generated instead), and the larger disc
image on the SD card.

Library impact: the Access system accepts statements up to 80 characters or more
and is the system the library tapes came from, so it should take almost all the
programs and probably the library tape itself (feature level 1500). The F is a
cheaper first step because it shares the two-CPU work; whether it accepts the
longer lines and the Access syntax is not known without running it.

First library trial on the 2000E (partial): 27 programs processed, 8 saved, 18
rejected by TSB-E, 1 could not be moved; then the host session ended with a CPU halt
(`T 103010`, P 31464) right after an operator MOVE, which has not been
investigated.

## 2000F running on the PC (2026-10-08, later)

The two-CPU 2000F (kit rev 1410) boots under tests/tsb_session.c (`system f`, `boot f`). On the same 25 games the
F saved 10 programs (23MTCH, ABAGEL, BATNUM, CHOMP, DRAG, HANGFL, KENO, POLUT, SUNSGN, TRADER) against 6 on the E;
the rest are rejected for Access-only syntax (empty-parenthesis DEF FN, A$[1,1] style substrings, apostrophe
character codes, MAT W=ZER ...). A 108-character line holding an 89-character string was answered, on LIST, with
`72 CHARACTERS MAX FOR STRING`, which is a limit on strings, not on the statement, so the F may take statements
longer than 72 characters (to be checked with a long statement of short strings). So the F helps a little; the
Access system is the one that should accept most of the library.

Update: a 134-character statement made of short strings was accepted by the F, listed as one line and only refused at
RUN for lack of an END statement. So the 2000F takes statements longer than 72 characters and the long-line library
programs are candidates for it (the 72 character limit is the 2000E's).

## 2000F trial of all 62 games, no line-length filter (2026-10-08)

Result: 17 saved, 45 rejected (27 percent accepted; the run may have been disturbed at one point because its disc
image was replaced while it ran, but it finished). Rejections: 34 "UNDECIPHERABLE OPERAND" and 11 "MISSING OR BAD SIMPLE
VARIABLE". TSB reports a bad statement together with the echo of the NEXT line, so the line shown is the one after the
culprit. Typical causes: apostrophe character codes in PRINT ('13'10"..."), undeclared or long string variables,
empty-parenthesis DEF FN, A$[1,1] substrings, MAT forms. So neither the E (about 24 percent of the games that fit
72 characters) nor the F (27 percent of all games) takes much of the library; the Access system, which the library
was written for, is the system that should.
