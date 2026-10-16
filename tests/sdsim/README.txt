Simulated SD card for testing storage_pico.c on the PC (Thomas Dzubin).

mkimg.py   makes a blank FAT32 image sd.img (300 MB, 8 sectors per cluster)
sdsim.c    sd_read_block / sd_write_block over sd.img; env SD_FAIL_EVERY=N makes every Nth write fail
fatput.c   copies files into hp2000access/ on the image using the real FAT32 driver
growtest.c a file that GROWS (storage_create, then tape-style small writes with read-backs), then close, reopen and compare;
           run with SD_FAIL_EVERY=7 to prove the retries cover the driver's own writes (new clusters, FAT, directory entry)
ph2.c      host test program (a copy of tests/hostprog/prog_host.c with a reload, login and F4 script)
pico/, hardware/   stub headers that stand in for the Pico SDK

Build (MSYS2 UCRT64, from the project folder, with this folder first on the include path):
  gcc -O1 -std=gnu99 -Itests/sdsim -Ipicocalc-text-starter-main/drivers tests/sdsim/fatput.c tests/sdsim/sdsim.c -o fatput.exe
  gcc -O2 -std=gnu99 -Itests/sdsim -Itests/hostprog -I. -Ipicocalc-text-starter-main/drivers hp2000access.c tests/sdsim/ph2.c \
      tests/sdsim/sdsim.c storage_pico.c dev_ipl.c hp_pair.c hp_loaders.c hp21xx_iop.c hp21xx.c hp21xx_fp.c hp_dma.c dev_tty.c \
      dev_tbg.c dev_disc.c dev_dq.c dev_mux.c dev_tape.c tape_image.c terminal.c termdraw.c autotype.c -o psd.exe
Run: python tests/sdsim/mkimg.py; ./fatput.exe SYSTEM-2883.disc HIBERNATE-2883.tape IOP-2100.tape 22687-11002_Rev-1812.tape;
     SD_FAIL_EVERY=500 ./psd.exe      (prints the screen at the end; the run takes many minutes)

More programs here (build each the same way, with -funsigned-char to behave like the Pico; sd.img from mkimg.py):
copyprobe.c   copies a file and prints the SD sector reads and writes per 10 percent of it (env RESERVE=1 reserves the size first)
sparesim.c    runs the spare copy code (spare.c): SSSDRO = save, save, save, damage a spare, restore, restore older
fatls.c       lists /hp2000access with the FAT32 driver and opens every file
trunctest.c   storage_truncate() and storage_list(): cut a file to its exact length, give the clusters back, list .TXT and .BAS files
The FAT can be checked from outside with a script that walks it (clusters in use that no file owns show as lost); see the notes in CLAUDE.md.
