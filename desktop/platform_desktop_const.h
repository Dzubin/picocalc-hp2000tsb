/* platform_desktop_const.h - constants for platform_desktop.c. */

#ifndef PLATFORM_DESKTOP_CONST_H
#define PLATFORM_DESKTOP_CONST_H

/* The folder, next to the program, that holds the disc, tapes and loaders. The
 * build sets it for the system being built (the 2000F uses hp2000f/). */
#ifndef PLAT_DATA_DIR
#define PLAT_DATA_DIR   "hp2000access/"
#endif

#define PLAT_DATA_PATH_MAX  512     /* longest path to the data folder */

#endif
