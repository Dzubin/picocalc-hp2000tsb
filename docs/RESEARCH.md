# Research notes

For the history of the HP 2100 and the HP 2000 systems see `HISTORY.md`.

Working notes for the HP 2000 Time-Shared BASIC (TSB) emulator (written when the 2000E was the first target; see the README for the current state: Access and 2000F). Everything under
`docs/` other than this file was downloaded as reference data and is never run. Those downloads (the manuals, kits and SIMH files in the table below) are not in
this repository; the table says where each one came from.

## Sources downloaded (2026-10-08)

All from the SIMH HP 2100 page, http://simh.trailing-edge.com/hp/

| Local file | Source | Notes |
|---|---|---|
| `simh-docs/hp2100_guide.pdf` | `/docs/hp2100_guide.pdf` | HP 2100 Simulator User's Guide (Apr 2022) |
| `simh-docs/running_hp_2000_tsb.pdf` | `/docs/running_hp_2000_tsb.pdf` | Running HP 2000 TSB on SIMH (Sep 2020) |
| `simh-docs/hp_1000_io_simulation.pdf` | `/docs/hp_1000_io_simulation.pdf` | How the I/O is simulated (Mar 2022) |
| `simh-source/HP2100-source-release-32.zip` (+ unpacked `SCP/`) | `/releases/HP2100-source-release-32.zip` | SIMH HP 2100 release 32 source, Apr 2022 (J. David Bryan, from Robert Supnik's original) |
| `kits/2000E-kit-release-4.zip` (+ unpacked `2000E/`) | `/kits/2000E-kit-release-4.zip` | 2000E TSB rev 1534 bootable disc image, SLEEP tape, scripts |

Other kits listed there (not downloaded): 2000A, 2000F rev 1410, 2000 Access rev
1812 (with source kits), DOS-III, RTE-II. The kits are issued under the "HP 1000
and HP21XX Software Agreement"; check its terms before redistributing any of it
(the project should tell the user to supply the kit, not ship it).

Manuals named by the kit readme, on Bitsavers (not yet fetched):
`http://www.bitsavers.org/pdf/hp/2000TSB/`, 02000-90048 (A Guide to Time-Shared
BASIC) and 02000-90049 (System Operator's Guide), both Aug 1972.

## What the 2000E kit needs (from `spconfig.inc`)

- CPU: HP 2100, 16K words of memory, floating-point firmware (`SET CPU 2100,16K,FP`).
  16K words is 32 KB, so memory is no problem for the Pico.
- Devices by select code (the printed operator's guide is wrong; these come from
  the 2000E sources, rev 1534):

| SC | Device | Interface |
|---|---|---|
| 10 | TTY | 12531C buffered teleprinter (the system console) |
| 11, 12 | DPD, DPC | 13210A disc interface, data and command (HP 7900 disc) |
| 13 | PTR | 12597A-002 paper tape reader (bootstrap only) |
| 14, 15, 16 | MUXL, MUX, MUXC | 12920A asynchronous terminal multiplexer (the user terminals) |
| 17 | TBG | 12539C time base generator (system clock) |
| 20, 21 | MSD, MSC | 13183B magnetic tape (7970), used for SLEEP, load and dump |

- `SYSTEM-7900.disc`: 4,988,928 bytes, 16-bit words little-endian. Needs random
  access on the SD card.
- Boot: the disc bootstrap `24286-60001_Rev-1438.abin` is loaded from (simulated)
  paper tape, S = 0, run, then the console answers "IS SYSTEM ON SUBCHANNEL 0?",
  "DATE?" (day-of-year/yy) and "TIME?" (hhmm).
- TSB must be shut down with SLEEP (to a mag tape image) or the next start fails
  with "SYS NOT SLEPT". An emulator on a calculator that can lose power needs a
  plan for this.
- Idling is not supported by TSB: the real machine runs flat out, so the emulator
  will use 100 percent of the CPU unless throttled.
- The default accounts: A000 OZZIE, A001 HARIET, B000 DAVID, B001 RICK.
  Character delete is `_`, line delete is CTRL+X.

## SIMH source sizes (hp2100 directory, bytes)

CPU core is `hp2100_cpu.c` (303 KB) plus `hp2100_cpu0.c` to `hp2100_cpu7.c`
(instruction groups), `hp2100_cpu_fp.c` (firmware floating point), `hp2100_mux.c`
(96 KB), `hp2100_dp.c` (7900 disc, 90 KB), `hp2100_ms.c` (7970 tape), `hp2100_tty.c`,
`hp2100_pt.c`, `hp2100_tbg.c`, `hp2100_disclib.c`. The SIMH code is large because it
covers the whole 2100 and 1000 families; we need a small subset.

## Single or dual CPU (from running_hp_2000_tsb)

- 2000A and 2000E: one CPU. 2000B, C, F and Access: two CPUs (a System Processor
  running BASIC for up to 32 users and an I/O Processor for the terminal
  multiplexers) joined by an HP 12875A Processor Interconnect, which SIMH models as
  two simulator instances sharing memory (the IPL device). Startup timing between
  the two is fragile even in SIMH. Conclusion: the 2000E is the right first target;
  F and Access would need two emulated CPUs in one program.
- SIMH runs about ten times faster than a real 2100 on a PC. TSB has no idle loop.

## Licence

The SIMH HP 2100 source (Robert M. Supnik 1993-2016, J. David Bryan 2017-2022) is
under the MIT licence, so ideas and code may be reused provided the copyright and
permission notice stay with any copied portion. The 2000E kit files are separate
(HP software agreement, see above).

## Extracted text

`simh-docs/*.txt` are `pdftotext -layout` conversions of the three PDFs, for
searching.

## Decisions (Thomas, 2026-10-08)

1. First target: the 2000E rev 1534 kit only (single CPU, 2100 with 16K and FP).
2. One user line on the multiplexer (the user console replaces Telnet), plus the
   operator console on the TTY interface; two consoles, one on screen at a time,
   switched with a key.
3. Quitting the emulator runs the SLEEP sequence to a tape image on the SD card
   automatically; a "reload" command recovers from a missed SLEEP ("SYS NOT SLEPT").
4. Speed: full speed first, then measure.
5. Order of work: plain-C CPU core (tested against the SIMH diagnostics and the
   instruction set), then console and disc, then the boot.

## Open questions

- Terminal grid size (font) for the two consoles.
- Whether to build the downloaded SIMH source on the PC as a reference to compare
  instruction traces against.
