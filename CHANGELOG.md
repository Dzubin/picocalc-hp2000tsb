# Changelog

All notable changes to HP 2000 Time-Shared BASIC (the 2000F and Access systems).
Format loosely follows [Keep a Changelog](https://keepachangelog.com/);
the version here matches the `VERSION` define in `hp2000access_const.h` and
`hp2000f_const.h`.

## [Unreleased]

## [V1.00A] - 2026-10-16

First public release. The project works. The history of its development was cleared on 2026-10-11; from
here on, add a bullet under [Unreleased] whenever behaviour changes. Since the build of 2026-10-11 the source has:

- the first-start spare save tells the operator in the middle of the screen to wait for it to finish;
- the vendored starter's `fat32.c` `fat32_write()` no longer leaks a cluster each time a write starts on a
  cluster boundary (the program itself is unchanged).

The release carries the programs (PicoCalc `.uf2` and Windows `.exe` for Access and the 2000F) and two zip files with
the system each one runs (disc and tapes, Hewlett-Packard's software; see the README).
