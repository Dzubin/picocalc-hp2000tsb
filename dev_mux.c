/* dev_mux.c - 12920A asynchronous terminal multiplexer.
 *
 * Author: Thomas Dzubin.
 *
 * A reduction of hp2100_mux.c of the SIMH HP 2100 simulator (Robert M. Supnik
 * 2002-2008, J. David Bryan 2008-2022, MIT licence) to what the 2000E needs: the
 * three-card interface as the system sees it, with characters passing to and
 * from the program's terminals (console.h) instead of Telnet connections, and no
 * diagnostic loopback mode. The behaviour of the interrupt scan, the stored
 * status of the control card and the deferred BREAK follows SIMH.
 */

#include "dev_mux.h"
#include "console.h"

static hp_word lower_input(hp_card_t *card);
static void    lower_output(hp_card_t *card, hp_word v);
static void    lower_set_control(hp_card_t *card);
static void    lower_reset(hp_card_t *card);
static void    lower_flag_clear(hp_card_t *card);
static hp_word upper_input(hp_card_t *card);
static void    upper_output(hp_card_t *card, hp_word v);
static hp_word control_input(hp_card_t *card);
static void    control_output(hp_card_t *card, hp_word v);
static void    control_flag_clear(hp_card_t *card);

static void    poll_service(void *arg);
static void    send_service(void *arg);
static void    receive_char(dev_mux_t *m, unsigned ln, int c);
static void    update_status(dev_mux_t *m, unsigned channel);
static void    data_interrupt(dev_mux_t *m);
static void    control_interrupt(dev_mux_t *m);
static uint32_t status_changes(const dev_mux_t *m, unsigned chan);
static unsigned odd_ones(unsigned c);

/* Each send event carries the line it belongs to. */
typedef struct {
    dev_mux_t *mux;
    unsigned   line;
} send_ref_t;

static send_ref_t send_ref[MUX_TERMINALS];

/* Bits per character for each value of the character size parameter (the
 * start bit is counted). */
static const unsigned bits_per_char[8] = { 9, 10, 11, 12, 5, 6, 7, 8 };

void dev_mux_init(dev_mux_t *m, unsigned lower_sc, unsigned control_sc)
{
    unsigned i;

    m->lower.ctx = m;
    m->lower.input = lower_input;
    m->lower.output = lower_output;
    m->lower.set_control = lower_set_control;
    m->lower.clear_control = NULL;
    m->lower.reset = lower_reset;
    m->lower.flag_set = NULL;
    m->lower.flag_clear = lower_flag_clear;
    m->lower.edt = NULL;
    m->lower.flag = 0;
    m->lower.flag_buffer = 1;
    m->lower.control = 0;

    m->upper.ctx = m;
    m->upper.input = upper_input;
    m->upper.output = upper_output;
    m->upper.set_control = NULL;
    m->upper.clear_control = NULL;
    m->upper.reset = NULL;
    m->upper.flag_set = NULL;
    m->upper.flag_clear = NULL;
    m->upper.edt = NULL;
    m->upper.flag = m->upper.flag_buffer = m->upper.control = 0;

    m->control.ctx = m;
    m->control.input = control_input;
    m->control.output = control_output;
    m->control.set_control = NULL;
    m->control.clear_control = NULL;
    m->control.reset = NULL;
    m->control.flag_set = NULL;
    m->control.flag_clear = control_flag_clear;
    m->control.edt = NULL;
    m->control.flag = 0;
    m->control.flag_buffer = 1;
    m->control.control = 0;

    m->lower_in = m->lower_out = m->upper_in = m->upper_out = 0;
    m->control_chan = m->control_scan = 0;
    for (i = 0; i < MUX_RECV_CHANNELS; i++) {
        m->status[i] = m->recv_param[i] = 0;
        m->recv_pending[i] = m->defer_break[i] = 0;
        m->recv_buf[i] = 0;
    }
    for (i = 0; i < MUX_TERMINALS; i++) {
        m->send_param[i] = m->send_buf[i] = 0;
        m->send_done[i] = 0;
        m->ctl_out[i] = m->ctl_in[i] = 0;
        m->connected[i] = m->dataset[i] = 0;
        m->redial[i] = 0;
        m->greeted[i] = m->lf_due[i] = 0;
        m->send_event[i].active = 0;
        send_ref[i].mux = m;
        send_ref[i].line = i;
    }

    hp_card_install(&m->lower, lower_sc);
    hp_card_install(&m->upper, lower_sc + 1);
    hp_card_install(&m->control, control_sc);
    hp_event_after(&m->poll_event, MUX_POLL_INSTRUCTIONS, poll_service, m);
}

void dev_mux_connect(dev_mux_t *m, unsigned channel, int dataset)
{
    if (channel >= MUX_TERMINALS)
        return;
    m->connected[channel] = 1;
    m->dataset[channel] = (uint8_t)(dataset != 0);
    m->redial[channel] = 0;
    m->greeted[channel] = m->lf_due[channel] = 0;
}

void dev_mux_set_greeting(dev_mux_t *m, int on)
{
    m->greeting = (uint8_t)(on != 0);
}

static unsigned odd_ones(unsigned c)
{
    c &= 0377;
    c ^= c >> 4;
    c ^= c >> 2;
    c ^= c >> 1;
    return c & 1;
}

/* ---- Lower data card ------------------------------------------------------ */

static hp_word lower_input(hp_card_t *card)
{
    dev_mux_t *m = card->ctx;

    return (hp_word)m->lower_in;
}

static void lower_output(hp_card_t *card, hp_word v)
{
    dev_mux_t *m = card->ctx;

    m->lower_out = v;
}

/* STC acts on the channel chosen through the upper card: a send or receive
 * parameter, or a character to send. */
static void lower_set_control(hp_card_t *card)
{
    dev_mux_t *m = card->ctx;
    unsigned ln = (m->upper_out >> MUX_CHAN_SHIFT) & MUX_CHAN_MASK;

    if (m->lower_out & MUXL_TRANSMIT) {
        if (ln >= MUX_TERMINALS)
            return;
        if (m->lower_out & MUXL_PARAMETER) {
            m->send_param[ln] = (uint16_t)m->lower_out;
        } else {
            if (m->send_param[ln] & MUXL_TX_PARITY)
                m->lower_out = (m->lower_out & ~MUXL_PARITY_BIT) |
                               (odd_ones(m->lower_out) ? MUXL_PARITY_BIT : 0);
            m->send_buf[ln] = (uint16_t)m->lower_out;
            if (hp_event_active(&m->send_event[ln]))
                m->status[ln] |= MUXU_LOST;             /* character lost */
            else
                hp_event_after(&m->send_event[ln], MUX_SEND_INSTRUCTIONS,
                               send_service, &send_ref[ln]);
        }
    } else {
        if (ln >= MUX_RECV_CHANNELS)
            return;
        if (m->lower_out & MUXL_PARAMETER)
            m->recv_param[ln] = (uint16_t)m->lower_out;
        /* data written to a receive channel is invalid and ignored */
    }
}

/* A control reset clears every channel. */
static void lower_reset(hp_card_t *card)
{
    dev_mux_t *m = card->ctx;
    unsigned i;

    card->control = 0;
    for (i = 0; i < MUX_TERMINALS; i++) {
        m->send_buf[i] = m->send_param[i] = 0;
        m->ctl_out[i] = m->ctl_in[i] = 0;
        m->send_done[i] = 0;
    }
    for (i = 0; i < MUX_RECV_CHANNELS; i++) {
        m->recv_buf[i] = m->recv_param[i] = 0;
        m->status[i] = 0;
        m->recv_pending[i] = 0;
    }
}

/* Clearing the flag lets the next completed character interrupt. */
static void lower_flag_clear(hp_card_t *card)
{
    data_interrupt(card->ctx);
}

/* ---- Upper data card ------------------------------------------------------ */

static hp_word upper_input(hp_card_t *card)
{
    return (hp_word)((dev_mux_t *)card->ctx)->upper_in;
}

static void upper_output(hp_card_t *card, hp_word v)
{
    ((dev_mux_t *)card->ctx)->upper_out = v;
}

/* ---- Control card --------------------------------------------------------- */

/* The interrupt bits: the stored expected state differs from the line state in
 * an enabled position. */
static uint32_t status_changes(const dev_mux_t *m, unsigned chan)
{
    return ((uint32_t)((m->ctl_in[chan] ^ m->ctl_out[chan]) &
                       ((m->ctl_out[chan] & MUXC_ESX) >> MUXC_ES_SHIFT)))
           << MUXC_I_SHIFT;
}

static hp_word control_input(hp_card_t *card)
{
    dev_mux_t *m = card->ctx;
    hp_word v;

    update_status(m, m->control_chan);
    v = (hp_word)(MUXC_IN_MBO |
                  ((m->control_chan & MUXC_CHAN_MASK) << MUXC_CHAN_SHIFT) |
                  status_changes(m, m->control_chan) |
                  (m->ctl_out[m->control_chan] & MUXC_ESX) |
                  (m->ctl_in[m->control_chan] & MUXC_IN_SX));
    m->control_chan = (m->control_chan + 1) & MUXC_CHAN_MASK;
    return v;
}

static void control_output(hp_card_t *card, hp_word v)
{
    dev_mux_t *m = card->ctx;
    unsigned ln = (v >> MUXC_CHAN_SHIFT) & MUXC_CHAN_MASK;

    m->control_chan = ln;
    m->control_scan = (v & MUXC_SCAN) != 0;
    if (v & MUXC_UPDATE) {
        m->ctl_out[ln] = (uint8_t)((m->ctl_out[ln] & ~MUXC_RW) | (v & MUXC_RW));
        if (v & MUXC_EC2)
            m->ctl_out[ln] = (uint8_t)((m->ctl_out[ln] & ~MUXC_C2) | (v & MUXC_C2));
        if (v & MUXC_EC1)
            m->ctl_out[ln] = (uint8_t)((m->ctl_out[ln] & ~MUXC_C1) | (v & MUXC_C1));
        if ((v & MUXC_ECX) && !(m->ctl_out[ln] & MUXC_DTR) && m->connected[ln]) {
            /* dropping DTR hangs the line up; it redials after a while */
            m->connected[ln] = 0;
            m->redial[ln] = MUX_REDIAL_POLLS;
        }
    }
}

static void control_flag_clear(hp_card_t *card)
{
    control_interrupt(card->ctx);
}

/* ---- Interrupt scans ------------------------------------------------------ */

/* If the data flag is clear, look for a finished character and raise the flag. */
static void data_interrupt(dev_mux_t *m)
{
    unsigned i;

    for (i = 0; i < MUX_TERMINALS; i++) {           /* received characters */
        if ((m->recv_param[i] & MUXL_ENABLE) && m->recv_pending[i]) {
            m->lower_in = ((uint32_t)i << MUX_CHAN_SHIFT) |
                          (m->recv_buf[i] & MUXL_IN_CHAR) |
                          (odd_ones(m->recv_buf[i]) ? MUXL_IN_PARITY : 0);
            m->upper_in = ((uint32_t)i << MUX_CHAN_SHIFT) | m->status[i];
            m->recv_pending[i] = 0;
            m->status[i] = 0;
            hp_card_set_flag(&m->lower);
            return;
        }
    }
    for (i = 0; i < MUX_TERMINALS; i++) {           /* characters sent */
        if ((m->send_param[i] & MUXL_ENABLE) && m->send_done[i]) {
            uint32_t remnant = (m->send_buf[i] & MUXL_CHAR_MASK) >>
                (bits_per_char[(m->send_param[i] >> MUXL_LENGTH_SHIFT) &
                               MUXL_LENGTH_MASK] - 1);

            m->lower_in = ((uint32_t)i << MUX_CHAN_SHIFT) | remnant |
                          (odd_ones(remnant) ? MUXL_IN_PARITY : 0);
            m->upper_in = ((uint32_t)i << MUX_CHAN_SHIFT) | m->status[i] | MUXU_SEND;
            m->send_done[i] = 0;
            m->status[i] = 0;
            hp_card_set_flag(&m->lower);
            return;
        }
    }
}

/* If the control flag is clear, look for a modem status change on one line or,
 * when scanning, on all of them. */
static void control_interrupt(dev_mux_t *m)
{
    unsigned i, count = m->control_scan ? MUX_TERMINALS : 1;

    for (i = 0; i < count; i++) {
        if (m->control_scan)
            m->control_chan = (m->control_chan + 1) & MUXC_CHAN_MASK;
        if (status_changes(m, m->control_chan) != 0) {
            hp_card_set_flag(&m->control);
            break;
        }
    }
}

/* ---- Terminals ------------------------------------------------------------ */

/* The terminal presents DSR when connected, and DCD too for a dataset line. */
static void update_status(dev_mux_t *m, unsigned channel)
{
    if (m->connected[channel] && m->dataset[channel])
        m->ctl_in[channel] |= MUXC_DCD;
    else
        m->ctl_in[channel] &= (uint8_t)~MUXC_DCD;
    if (m->connected[channel])
        m->ctl_in[channel] |= MUXC_DSR;
    else
        m->ctl_in[channel] &= (uint8_t)~MUXC_DSR;
}

static void receive_char(dev_mux_t *m, unsigned ln, int c)
{
    if (c & CONSOLE_BREAK) {
        if (m->defer_break[ln]) {                   /* second look: process it */
            m->defer_break[ln] = 0;
            m->recv_buf[ln] = 0;
            m->status[ln] |= MUXU_BREAK;
        } else {                                    /* defer until the next output */
            m->defer_break[ln] = 1;
            return;
        }
    } else {
        if (m->recv_pending[ln])
            m->status[ln] |= MUXU_LOST;
        c &= 0177;
        if (MUX_FOLD_CASE && c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        if (m->recv_param[ln] & MUXL_RX_ECHO)
            console_write(CONSOLE_USER + ln, (uint8_t)c);
        m->recv_buf[ln] = (uint16_t)c;
    }
    m->recv_pending[ln] = 1;
}

/* Poll the terminals for keys, update the line status, and look for interrupts. */
static void poll_service(void *arg)
{
    dev_mux_t *m = arg;
    unsigned ln, chan;
    int c;

    hp_event_after(&m->poll_event, MUX_POLL_INSTRUCTIONS, poll_service, m);

    for (ln = 0; ln < MUX_TERMINALS; ln++) {        /* a dropped line redials */
        if (!m->connected[ln] && m->redial[ln] != 0 && --m->redial[ln] == 0) {
            m->connected[ln] = 1;
            m->greeted[ln] = m->lf_due[ln] = 0;
        }
    }
    for (chan = 0; chan < MUX_TERMINALS; chan++)
        update_status(m, chan);

    for (ln = 0; ln < MUX_TERMINALS; ln++) {
        if (!m->connected[ln])
            continue;
        if (m->defer_break[ln]) {
            c = CONSOLE_BREAK;
        } else if (m->lf_due[ln]) {                 /* the handshake's line feed */
            m->lf_due[ln] = 0;
            c = '\n';
        } else {
            c = console_read(CONSOLE_USER + ln);
            if (c == '\r' && m->greeting && !m->greeted[ln]) {
                m->greeted[ln] = 1;
                m->lf_due[ln] = 1;
            }
        }
        if (c >= 0)
            receive_char(m, ln, c);
    }
    if (!m->lower.flag)
        data_interrupt(m);
    if (!m->control.flag)
        control_interrupt(m);
}

/* A character has been sent: show it on the terminal and report completion. */
static void send_service(void *arg)
{
    send_ref_t *ref = arg;
    dev_mux_t *m = ref->mux;
    unsigned ln = ref->line;
    unsigned c = m->send_buf[ln] & MUXL_CHAR_MASK;

    if (!m->connected[ln]) {                        /* no terminal: wait */
        hp_event_after(&m->send_event[ln], MUX_SEND_INSTRUCTIONS, send_service, ref);
        return;
    }
    if (m->defer_break[ln])                         /* a deferred BREAK goes first */
        receive_char(m, ln, CONSOLE_BREAK);
    if (!(m->send_buf[ln] & MUXL_SYNC)) {           /* start bit 0: a real character */
        c &= 0177;
        if (MUX_FOLD_CASE && c >= 'a' && c <= 'z')
            c -= 'a' - 'A';
        console_write(CONSOLE_USER + ln, (uint8_t)c);
    }
    m->send_done[ln] = 1;
    if (!m->lower.flag)
        data_interrupt(m);
}
