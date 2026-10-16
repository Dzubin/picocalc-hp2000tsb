/* dev_mux_const.h - constants for the 12920A terminal multiplexer (dev_mux.c). */

#ifndef DEV_MUX_CONST_H
#define DEV_MUX_CONST_H

#define MUX_TERMINALS           16u         /* terminal channels */
#define MUX_AUX_CHANNELS        5u          /* diagnostic channels (unused here) */
#define MUX_RECV_CHANNELS       (MUX_TERMINALS + MUX_AUX_CHANNELS)

#define MUX_SEND_INSTRUCTIONS   500u        /* time to send one character */
#define MUX_POLL_INSTRUCTIONS   5000u       /* keyboard poll period (about 10 ms) */
#define MUX_REDIAL_POLLS        100u        /* polls before a dropped line redials */
#define MUX_FOLD_CASE           0u          /* 1: user terminals send and show capitals only; TSB itself
                                             * converts lower case outside strings, so 0 passes it through */

/* Channel number in the upper card word and in the lower card word */
#define MUX_CHAN_SHIFT          10
#define MUX_CHAN_MASK           037u

/* Lower data card output word */
#define MUXL_PARAMETER          0100000u    /* a parameter, not data */
#define MUXL_TRANSMIT           0040000u    /* send, not receive */
#define MUXL_ENABLE             0020000u    /* enable interrupt */
#define MUXL_TX_PARITY          0010000u
#define MUXL_RX_ECHO            0010000u
#define MUXL_DIAGNOSE           0004000u
#define MUXL_SYNC               0004000u
#define MUXL_LENGTH_SHIFT       8
#define MUXL_LENGTH_MASK        07u
#define MUXL_CHAR_MASK          03777u
#define MUXL_PARITY_BIT         0200u

/* Lower data card input word */
#define MUXL_IN_PARITY          0100000u
#define MUXL_IN_CHAR            01777u

/* Upper data card status word */
#define MUXU_SEEKING            0100000u
#define MUXU_DIAGNOSE           0000010u
#define MUXU_BREAK              0000004u
#define MUXU_LOST               0000002u
#define MUXU_SEND               0000001u

/* Control card output word */
#define MUXC_SCAN               0100000u
#define MUXC_UPDATE             0040000u
#define MUXC_CHAN_SHIFT         10
#define MUXC_CHAN_MASK          017u
#define MUXC_EC2                0000200u
#define MUXC_EC1                0000100u
#define MUXC_C2                 0000040u    /* RTS */
#define MUXC_C1                 0000020u    /* DTR */
#define MUXC_ES2                0000010u
#define MUXC_ES1                0000004u
#define MUXC_SS2                0000002u
#define MUXC_SS1                0000001u
#define MUXC_RW \
    (MUXC_ES2 | MUXC_ES1 | MUXC_SS2 | MUXC_SS1)
#define MUXC_ESX                (MUXC_ES2 | MUXC_ES1)
#define MUXC_ECX                (MUXC_EC2 | MUXC_EC1)
#define MUXC_CX                 (MUXC_C2 | MUXC_C1)
#define MUXC_C_SHIFT            4           /* C1 down to S1 */
#define MUXC_ES_SHIFT           2

/* Control card input word */
#define MUXC_IN_MBO             0140000u
#define MUXC_IN_I2              0001000u
#define MUXC_IN_I1              0000400u
#define MUXC_IN_S2              0000002u    /* DCD */
#define MUXC_IN_S1              0000001u    /* DSR */
#define MUXC_IN_SX              (MUXC_IN_S2 | MUXC_IN_S1)
#define MUXC_I_SHIFT            8
#define MUXC_DTR                MUXC_C1
#define MUXC_DCD                MUXC_IN_S2
#define MUXC_DSR                MUXC_IN_S1

#endif
