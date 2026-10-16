# Lessons learned

Notes for anyone who wants to build a small HP 2100 emulator that boots HP 2000E
Time-Shared BASIC (TSB). Written while the project was under way, so it is a
working diary of what mattered, not a finished guide. Newest lessons are added at
the end of each section.

## 1. Pick the right target first

- The HP 2000 family comes in two kinds. The 2000A and 2000E run on **one CPU**.
  The 2000B, C, F and Access need **two CPUs** (a System Processor and an I/O
  Processor joined by a Processor Interconnect), and even SIMH has trouble
  starting them reliably. Start with the 2000E.
- The SIMH project (http://simh.trailing-edge.com/hp/) ships a ready-to-boot 2000E
  disc image (`SYSTEM-7900.disc`, 4,988,928 bytes) plus the paper tape bootstrap
  and the startup scripts. That kit is the whole test bed: no need to build a TSB
  system from sources.
- The machine the kit configures is small: an HP 2100 with 16K words (32 KB), the
  floating point firmware, and ten interface cards. Memory is no problem even for a
  microcontroller. Speed is not either: the real machine ran about 1.6 million
  instructions per second, and a straightforward C core does over 100 million on a
  PC.

## 2. Use SIMH as the oracle, and build it

Reading the SIMH HP 2100 source (`hp2100_cpu*.c`, `hp2100_dp.c`, `hp2100_mux.c`
and the other device files) gave the behaviour. Running it gave the proof. The
single most useful thing in the whole project was a **reference build of SIMH on
the development PC** and a way to compare traces:

- Build: `gcc -O2 -std=gnu99 -DUSE_VM_INIT -DHAVE_INT64 ... HP2100/*.c scp.c sim_*.c`
  (on Windows add a `setenv` shim and `-lws2_32 -lwinmm`). It runs only in a real
  console window, so start it hidden from PowerShell and send output to a log file
  with `set console log=...`.
- `set debug trace.dbg` and `set cpu debug=instr;reg` write one instruction line and
  one register line per instruction. Turn them into "P IR A B E O" lines (state
  before each instruction) and diff against your own emulator's trace.
- The traces will differ wherever device timing differs (a polling loop that
  spins five times in one emulator and eight in the other). Filter those out:
  find the instruction pairs that repeat with unchanged registers and delete every
  execution of those addresses from both traces before diffing.
- SIMH starts with the E register set. Mimic that in the test, or the first line
  differs.
- SIMH logs an interrupt as two lines (the interrupt, then the trap cell
  instruction). Log yours the same way.
- For anything the trace cannot show, break at the same address in both
  (`break nnnn`, `go`, `examine 2-37677`) and compare whole memory dumps. A dump of
  the paper tape load matched except for the 64 loader words (SIMH hides the
  protected loader after a halt).
- SIMH has a `go until "text"` / `reply "text\r"` mechanism for answering the
  start-up questions. A tiny Python Telnet client can then log in on the
  multiplexer port to capture what a real session does (disc operations, console
  text).

## 3. Things that are easy to get wrong in the CPU

Each of these cost real debugging time. All were found by diffing against SIMH.

- **The instruction group masks.** The I/O group is bit 15 set, bits 14 to 12
  clear, bit 10 set (mask `0172000`, value `0102000`). Getting the alter-skip /
  shift-rotate split wrong (it is bit 10 as well) made the paper tape loader
  misbehave.
- **Locations 0 and 1 are the A and B registers** for CPU memory references, but
  **not for DMA**: a DMA transfer to address 0 writes real core. This showed up
  when TSB deliberately points a DMA channel at address 0.
- **`STF` with the C bit is `CLF`**, and `CLC` is the `STC` pattern with the A/B bit
  set. `HLT,C` clears the flag too.
- **Interrupt deferral on the 2100**: after JMP, JSB,I and the I/O instructions
  STF, CLF, STC, CLC and HLT the next instruction cannot be interrupted, unless it
  is a memory reference other than JMP or JSB,I.
- **`LIA 4` reads the central interrupt register** (the select code of the card
  that interrupted). TSB's interrupt handler uses it to find the cause. Reading
  zero there makes TSB ignore the interrupt, and the card that raised it stays
  "in service" and blocks every card below it in priority.
- **The power-on preset sets both the flag buffer and the flag** of every card
  (POPIO then ENF), not only the buffer. A card that starts with flag clear
  changes which interrupts TSB sees during start-up.
- Extended arithmetic and firmware floating point instructions take their operand
  from the **word after the instruction** (an address, possibly indirect), and the
  instruction skips that word.
- The firmware floating point deliberately reproduces the 2100 microcode quirks
  (the multiply loses a bit, the divide can be one bit out). Port it as it is; TSB
  prints those digits.

## 4. The I/O model that worked

- Give every card the three standard flip-flops (control, flag, flag buffer) in the
  CPU, and let a device supply only hooks: input, output, STC, CLC, reset, "flag was
  set", "flag was cleared". The standard behaviour of STF, CLF, SFC, SFS, STC, CLC
  and the ,C forms then lives in one place.
- Priority: scan select codes from low to high. A card with control and flag both
  set holds off all lower-priority cards; if its flag buffer is set too, it is the
  one interrupting. Acknowledging clears the flag buffer only; the flag stays until
  the handler clears it.
- Count time in **instructions**, not microseconds, and let devices schedule events
  by instruction count. The real-time clock card (time base generator) is the one
  place that needs wall-clock time: calibrate "instructions per millisecond"
  against the host clock while running.
- A **DMA cycle takes no instruction time** in SIMH. Copy that if you want the
  traces to line up.
- Some cards break the standard model: the disc data card has no control
  flip-flop and never interrupts; `OTA` to the disc command card also clears its
  control flip-flop. Handle these in the hooks.

## 5. Devices that TSB needs

For the 2000E kit configuration (select codes): teleprinter 10, disc 11 and 12,
paper tape reader 13, multiplexer 14, 15 and 16, time base generator 17, magnetic
tape 20 and 21, plus the DMA channels (6, 7, 2, 3) that the disc uses even though
the kit's configuration file does not list them.

- **Paper tape reader and loader**: the 64-word basic binary loader (listed in the
  SIMH source) is copied to the top of memory, with its I/O select codes patched
  to the reader's. Running it reads the disc bootstrap tape and halts with `HLT 77`.
- **Disc**: a 13210A controller with 7900 drives is a real state machine (seek,
  address record, read, write, check, status, attention polling). Porting SIMH's
  logic closely was worth it; the instruction traces matched through the whole
  disc boot. The image is little-endian 16-bit words, 203 cylinders by 4 surfaces
  by 24 sectors of 128 words.
- **Multiplexer** (three cards): the interesting part is the control card's modem
  signal logic. TSB programs it per channel, then waits for a "data set ready"
  change to start a line. **A terminal that is already connected before TSB has
  initialised the multiplexer is never noticed**; connect the line after the
  system is up.
- The multiplexer is also **reset by a long run of `CLC 0`** during TSB start-up.
  Characters that arrive before that is finished are wiped, so a login script
  must wait (a human does).
- **TSB has no idle loop.** It spins at full speed, so an emulator will use all the
  CPU it is given unless it is throttled.

## 6. Testing habits that paid off

- A **ring buffer of the last N instructions**, printed when the run ends or stalls,
  shows a hang immediately.
- Break at an address (n-th hit), print the registers and chosen memory words, and
  optionally dump all of memory to a file. Compare dumps with a small script.
- A hang with a card showing `control 1, flag 1, flag buffer 0` means an interrupt
  was acknowledged and the handler did not finish (or never ran). Print every
  card's three flip-flops at the end of a run.
- Fresh copy of the disc image for every run: TSB writes to it, and a system that
  was not shut down with `SLEEP` refuses to start ("SYS NOT SLEPT").
- Feed scripted keystrokes slowly (one key every several polls). The multiplexer
  reports a lost character when keys arrive faster than TSB reads them.

## 7. Practical notes

- TSB is licensed material from HP. The SIMH kits come under the "HP 1000 and
  HP21XX Software Agreement". Do not redistribute the disc image; have the user
  supply it. The SIMH source itself is MIT licensed, so credit its authors in any
  file that adapts it.
- The 2000E account passwords in the kit: A000 OZZIE, A001 HARIET, B000 DAVID,
  B001 RICK. The character delete key is `_` and line delete is CTRL+X.
- Windows: PowerShell is needed to start SIMH hidden; a Git Bash shell has no
  console for it.

## 8. Milestones reached

- Paper tape bootstrap load matches SIMH instruction for instruction.
- Disc boot of the 2000E kit matches SIMH instruction for instruction through the
  start-up (after polling loops are filtered out); TSB reaches `READY`.
- A scripted user logs on over the multiplexer (`HELLO-B001,RICK`), loads the kit's
  example program (`GET-TEN`), runs it and logs off. The square roots it prints
  (1.41421, 1.73205, ... 3.16228) are identical to SIMH's, so the extended
  arithmetic and the firmware floating point work.

## 9. Test script pitfalls (mine, not the emulator's)

Two apparent emulator bugs were bugs in the test script:

- The script answered the next prompt while it was still typing the previous line,
  overwriting the pending carriage return. TSB then saw `GET-TENRUN` and looked for
  a file with that name, which looked like a disc or CPU fault. Always finish a line
  before starting the next, and compare the disc commands with the reference's
  before suspecting the CPU.
- Typing before TSB has finished initialising the multiplexer loses the keys (see
  section 5). Wait until the system has settled, and type slowly.

## 10. The tape card and SLEEP

- TSB shuts down with `SLEEP`: it asks `MAG TAPE SLEEP?` (answer YES) and `SELECT CODE?`
  (the tape interface, 20), writes the whole system to tape, and ends at the loader
  utility's `*` prompt. Always do this before stopping the emulator, or the next start
  says `SYS NOT SLEPT`.
- To recover: load the 2000E loader utility from paper tape (`24285-60001`), start it at
  octal 2000, type `LOAD,20`, answer the ports (16), date and time. It reads the SLEEP
  tape back onto the disc and starts TSB.
- The loader finishes by rewinding the tape **and taking the drive offline**. Until the
  operator presses ON LINE (SIMH: `set MSC0 ONLINE`) a following `SLEEP` halts with
  `CHANGE MAG TAPE TO AUTO`. The emulator needs an ON LINE action for the operator.
- The SIMH tape image format is simple (length, data padded to even, length; zero length
  is a tape mark) and writing does not truncate. A tape SLEEPed by this emulator differs
  from SIMH's own in about 40 bytes (clock and state words).
- Controller clear (`STC` with the clear command) completes at once and must not have
  its flag cleared by the `,C` of the same instruction. The CPU hook lets a card cancel
  that CLF.

## 11. Using the vendored FAT32 driver for big images

The PicoCalc starter's FAT32 driver (picocalc-text-starter, Blair Leduc) is used for the SD card.
Its `get_next_free_cluster()` never advanced its search hint, so every new cluster rescanned the
FAT from the start and a sequential write of a few hundred KB looked hung. The one-line fix (set
`fsinfo.next_free = i + 1` after handing out a cluster) must be in the vendored copy before writing
the SLEEP tape or growing a disc image. `fat32_seek()` only sets a position: it does not grow the
file, so `storage_pico.c` fills any gap with zero bytes before writing past the end.

Also: the driver follows the file's cluster chain from the first cluster on every `fat32_read()` and
`fat32_write()` (write does it three times), one SD sector read per link and no FAT cache. For a 4.9 MB
disc image that is hundreds of card reads for each 256 byte disc sector. Booting and a short session were
bearable, but the loader's reload (thousands of disc writes) looked frozen on the PicoCalc while it ran
quickly on the PC. `storage_pico.c` now reads each file's chain once at open and reads and writes the
sectors directly (it includes `fat32.c` to reach the sector functions, so the vendored file is not edited).
Moral: measure the storage layer early, not only the emulation.

## 11a. Two-CPU systems (2000F, Access)

- The F and Access systems are two machines: a System Processor (BASIC interpreter, discs, tape, operator console)
  and an I/O Processor (terminal multiplexers) joined by two pairs of 16-bit interfaces (12875A). SIMH runs them as
  two processes and needs "execution interlocking"; one thread running the two CPUs in 16-instruction alternating
  slices gives the same tight coupling with no host synchronisation, and it is repeatable.
- The whole emulator names its CPU `hp`; turning that into a macro for a selected element of an array let all the
  existing CPU and device code serve both CPUs unchanged. Devices must be created with the right CPU selected, and
  anything that pokes the other CPU's cards (the interconnect delivering a word) has to select it and select back.
- Start-up: the SP loads the cross-loader from paper tape (24343, first program), the IOP is parked in a special binary
  loader at the top of its 8K memory reading the interconnect, the SP sends the IOP program across, both halt at
  HLT 77, the IOP is started at 2, the SP boots the disc with the BMDL disc bootstrap and, after its HLT 77, is run
  again. The loader tables (IPL BBL, BMDL) are copied from SIMH and relocated: current-page addressing needs no
  change, but the DEF words, the minus-start word and the select code words do.
- Counting instructions in `unsigned long` fails on Windows (32 bits): use unsigned long long for run totals.
- The shell tool turns backslash-n in heredocs into real newlines; write C source with the file Write tool.

## 12. Lessons from getting Access onto the PicoCalc (2026-10-09)

- **The SD card refuses a write now and then.** The PicoCalc driver returns a write failure on the first bad
  transfer (random sectors, about once per long write). HIBERNATE's 100 KB tape write ended in TAPE BAD on
  the real machine only. Retry every sector transfer (5 tries, a pause, restart the card on the 3rd) in the
  project's storage layer, and count the failed tries where the user can see them.
- **Test the storage layer on the PC with a simulated card.** A stub `sd_read_block`/`sd_write_block` over a
  FAT32 image file runs the real `storage_pico.c` and the real driver. It proved the logic was right, so the
  fault had to be the card.
- **A terminal handshake can depend on a character you never send.** The Access log-in needs CR then LF; two
  CRs or an LF alone get no answer, whatever the timing. Compare a working trace with a failing one before
  theorising about time-outs (I lost hours on a timing theory).
- **Library text can be damaged by the extraction.** Every DEF FN in the tape's listing had lost its dummy
  variable. A consistent, systematic rejection is a data defect: find the pattern and repair it by rule
  (`tests/tools/repair_library.py`), logging every change.
- **Check the hardware limits before offering options.** A 7900 holds two subchannels, the generator refused
  more logical discs, and the 2883 (24 MB a drive, 12565A, nearly the same controller as the 7900) was the
  simpler and bigger answer for both Access and the 2000F.
- **Run independent test work on all cores.** Split the program list over 8 processes, each with its own disc
  copy; a three hour run became twenty minutes.
- **Shell heredocs and Python corrupt backslash escapes in C source.** Use the editor tool for any C text
  that holds a carriage return or newline escape, and check the bytes with `od -An -tx1`.
- **Rehearse the ending of a long job first.** The first library import ran 55 minutes and was lost: HIBERNATE
  waited on a tape drive that was off line, and the programs were saved PRIVATE. Do a tiny end-to-end run of
  the whole pipeline (import, shut down, reload, log in as an ordinary user), keep staged checkpoints, and
  verify as the end user before calling it done.
- **Any transform in the input path silently changes the data.** The multiplexer folded input to capitals, so
  51 library programs lost their lower case. SIMH makes upshifting an option (UC and CAPSLOCK); the real
  12920A does not convert case, and TSB upcases commands itself. Compare what was loaded with the source.
- **Size a desktop screen from the driver's own constants.** The shim takes its window size from WIDTH in the
  vendored lcd.h; an override header placed first in the include path (include_next, then redefine WIDTH to
  640) gives 80 columns of the real 8x10 font without touching vendored or managed files.
- **Windows will not rename a folder that anything has open.** An Explorer window, or a Command Prompt whose
  directory (or running program) is inside a subfolder, blocks the rename of every parent. Test each
  subfolder to find the culprit. After a rename, delete the build folders: CMake caches absolute paths.

- **Wait for the end of a prompt, not its start.** The loader prints `ENTER NAME LIST, ONE PER LINE; TERMINATE WITH
  'END'` one character at a time; a script that waited for the first words typed its answer into the middle of the
  prompt and got ILLEGAL INPUT. Match the last words. Also: TSB program names are at most six characters
  (`ZZTEST2` gave ILLEGAL FORMAT), and a login RETURN sent before the system is ready is lost for good.
- **Use a watchdog and a step log in host tests.** Each failed run cost 10 to 25 minutes until the test printed its
  step and time and gave up after two minutes with a screen dump.

- **Retry at the lowest level you control, not only in your own code.** The retries covered our own sector transfers but not the
  vendored FAT32 driver's, which does the writes when a file grows. A newly created SLEEP tape silently stopped growing at 4,106
  bytes on the real card while every test on the PC passed. A refused write must be injected into the simulated card for growth
  too (`tests/sdsim/growtest.c`). Also keep the last screen of the console after a halt: the menu used to hide the clue.

- **Test the state a feature leaves behind, not just the feature.** SLEEP recovery worked, but it left the read-only SLEEP tape on the
  tape drive, so the next HIBERNATE failed. The review found it by reading, and a test that does the next operation proved it. A
  short script that chains the operations (recover, then shut down) is cheap and catches this kind of fault.

## 13. Open items

(Updated as the work goes on.)

- Magnetic tape card (select codes 20 and 21) for the `SLEEP` shutdown and the
  reload.
- The SD-card storage layer and the screen terminals for the PicoCalc.
