/* dev_mux.h - 12920A asynchronous terminal multiplexer (three cards).
 *
 * Author: Thomas Dzubin.
 *
 * The user terminals of Time-Shared BASIC. The lower data card (select code 14
 * in the 2000E) carries the characters, the upper data card (next code up, 15)
 * holds the channel number and status, and the control card (16) carries the
 * modem signals. Channel n is terminal CONSOLE_USER + n in console.h.
 *
 * A channel is "connected" (a terminal is on the line) after dev_mux_connect().
 * If the system hangs the line up by dropping DTR, the multiplexer redials by
 * itself after about a second, so the terminal is always available.
 */

#ifndef DEV_MUX_H
#define DEV_MUX_H

#include "hp21xx.h"
#include "dev_mux_const.h"

typedef struct {
    hp_card_t  lower, upper, control;
    hp_event_t poll_event;
    hp_event_t send_event[MUX_TERMINALS];

    uint32_t   lower_in;                /* lower card input word */
    uint32_t   lower_out;
    uint32_t   upper_in;
    uint32_t   upper_out;
    uint32_t   control_chan;
    uint32_t   control_scan;

    uint16_t   status[MUX_RECV_CHANNELS];
    uint16_t   recv_param[MUX_RECV_CHANNELS];
    uint16_t   send_param[MUX_TERMINALS];
    uint8_t    recv_pending[MUX_RECV_CHANNELS];
    uint8_t    defer_break[MUX_RECV_CHANNELS];
    uint8_t    greeting;                /* the first CR on a new line is followed by LF */
    uint8_t    greeted[MUX_TERMINALS];
    uint8_t    lf_due[MUX_TERMINALS];
    uint8_t    send_done[MUX_TERMINALS];
    uint8_t    ctl_out[MUX_TERMINALS];  /* control card: Cn, ESn, SSn */
    uint8_t    ctl_in[MUX_TERMINALS];   /* control card: Sn */
    uint16_t   recv_buf[MUX_RECV_CHANNELS];
    uint16_t   send_buf[MUX_TERMINALS];

    uint8_t    connected[MUX_TERMINALS];
    uint8_t    dataset[MUX_TERMINALS];  /* line behaves like a modem (DCD too) */
    uint16_t   redial[MUX_TERMINALS];   /* polls left before reconnecting */
} dev_mux_t;

/* lower_sc is the lower data card (upper is the next code); control_sc the
 * control card. */
void dev_mux_init(dev_mux_t *m, unsigned lower_sc, unsigned control_sc);

/* Put a terminal on a channel (raises DSR; DCD as well for a dataset line). */
void dev_mux_connect(dev_mux_t *m, unsigned channel, int dataset);

/* The 2000 Access log-in handshake wants a line feed after the first carriage
 * return on a newly connected line. With this on, the multiplexer supplies it. */
void dev_mux_set_greeting(dev_mux_t *m, int on);

#endif
