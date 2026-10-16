/* tape_image_const.h - constants for the tape image format (tape_image.c). */

#ifndef TAPE_IMAGE_CONST_H
#define TAPE_IMAGE_CONST_H

#define TAPE_MARKER_MARK        0x00000000u     /* tape mark */
#define TAPE_MARKER_EOM         0xFFFFFFFFu     /* end of medium */
#define TAPE_MARKER_GAP         0xFFFFFFFEu     /* a piece of erased tape */
#define TAPE_MARKER_HALF_GAP    0xFFFEFFFFu     /* forward half gap */
#define TAPE_ERROR_FLAG         0x80000000u     /* the record was written in error */

#endif
