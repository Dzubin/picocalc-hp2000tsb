/* hp21xx.h - HP 2100 CPU emulation, public interface.
 *
 * Author: Thomas Dzubin.
 *
 * Portable ISO C with no hardware in it. Devices plug in as "cards" at a
 * select code (see hp_card_t); the CPU keeps the standard flag, flag buffer
 * and control flip-flops of each card, so a device only supplies its data and
 * its own behaviour. Time is counted in instructions; devices schedule events
 * with hp_event_after().
 *
 * Instruction behaviour follows the HP 2100A and the SIMH HP 2100 simulator.
 */

#ifndef HP21XX_H
#define HP21XX_H

#include <stdint.h>
#include <stddef.h>
#include "hp21xx_const.h"

typedef uint16_t hp_word;

typedef struct hp_card hp_card_t;

/* An I/O interface card. flag, flag_buffer and control are the three
 * flip-flops every card has; the CPU operates them for STF, CLF, STC, CLC,
 * SFS and SFC. The hooks are all optional. */
struct hp_card {
    uint8_t  flag;                      /* the flag flip-flop                */
    uint8_t  flag_buffer;               /* the flag buffer flip-flop         */
    uint8_t  control;                   /* the control flip-flop             */
    uint8_t  select_code;               /* set by hp_card_install            */
    void    *ctx;                       /* the device's own data             */
    hp_word (*input)(hp_card_t *card);              /* LIA, LIB, MIA, MIB     */
    void    (*output)(hp_card_t *card, hp_word v);  /* OTA, OTB               */
    void    (*set_control)(hp_card_t *card);        /* after STC              */
    void    (*clear_control)(hp_card_t *card);      /* after CLC              */
    void    (*reset)(hp_card_t *card);              /* CLC 0, power on        */
    void    (*flag_set)(hp_card_t *card);           /* after the flag is set  */
    void    (*flag_clear)(hp_card_t *card);         /* after the flag is cleared */
    void    (*edt)(hp_card_t *card);                /* end of a DMA data transfer */
};

/* A scheduled event: fn(arg) is called when the instruction count reaches it. */
typedef struct {
    uint32_t due;
    void   (*fn)(void *arg);
    void    *arg;
    uint8_t  active;
} hp_event_t;

typedef struct {
    hp_word  a, b;                      /* A and B accumulators              */
    hp_word  p;                         /* program counter                   */
    hp_word  m;                         /* memory address register           */
    hp_word  t;                         /* memory data register              */
    hp_word  ir;                        /* instruction register              */
    hp_word  s;                         /* switch register                   */
    hp_word  display;                   /* display register (OTA/OTB 1)      */
    uint8_t  e;                         /* extend                            */
    uint8_t  o;                         /* overflow                          */
    uint8_t  int_system;                /* interrupt system on (STF 0)       */
    uint8_t  int_ok;                    /* interrupts allowed this instr.    */
    uint8_t  have_fp;                   /* floating point firmware installed */
    uint8_t  have_eau;                  /* extended arithmetic installed     */
    uint8_t  have_iop;                  /* I/O Processor firmware installed (not with FP) */
    hp_word  spr;                       /* IOP stack pointer (the 2100's memory protect fence) */
    uint8_t  halted;
    uint16_t irq_sc;                    /* select code now interrupting      */
    uint16_t cir;                       /* central interrupt register: last interrupting select code */
    uint16_t last_irq;                  /* select code of the interrupt just taken, else 0 */
    uint8_t  dma_request;               /* DMA channels asking for a cycle   */
    uint8_t  iog_clf;                   /* a CLF follows the I/O hook running */
    void   (*dma_eval)(void);           /* set by the DMA module             */
    void   (*dma_cycle)(void);          /* ... and its cycle routine         */
    void    *dma_ctx;                   /* the DMA module's data for this CPU */
    void   (*io_trace)(unsigned sc, hp_word ir, hp_word reg);  /* debugging aid */
    uint32_t mem_size;                  /* words of memory installed         */
    uint32_t ticks;                     /* instructions executed (wraps)     */
    uint32_t next_due;                  /* ticks value of the next event     */
    hp_word  mem[HP_MEM_WORDS];
    hp_card_t *card[HP_CARD_SLOTS];
    uint8_t  irq_slot[HP_CARD_SLOTS];   /* select codes (4 and up) that have a card, in order */
    uint8_t  irq_slots;                 /* ... and how many; the interrupt scan visits only these */
    hp_event_t *event[HP_EVENT_SLOTS];
} hp_cpu_t;

/* A system has one CPU (the 2000E) or two (the 2000F: a System Processor and an
 * I/O Processor). The whole emulator names the CPU it is working on as "hp";
 * hp_select() chooses which of the CPUs that is. Anything that makes devices or
 * touches the other CPU must select it first and select the previous one back. */
extern hp_cpu_t hp_cpus[HP_CPU_COUNT];
extern hp_cpu_t *hp_cur;
#define hp (*hp_cur)

void hp_select(unsigned cpu);
unsigned hp_selected(void);

/* ---- CPU ---------------------------------------------------------------- */

void hp_init(uint32_t mem_words, int with_fp);  /* once, before anything else */
void hp_enable_iop(void);               /* the 2000 I/O Processor instruction set, instead of FP */
void hp_preset(void);                   /* RESET: registers, cards, interrupts */
int  hp_run(uint32_t max_instructions); /* returns an HP_STOP_ or HP_RUN code  */

hp_word hp_read(uint32_t address);      /* memory as the CPU sees it (0 = A, 1 = B) */
void    hp_write(uint32_t address, hp_word value);
hp_word hp_read_core(uint32_t address); /* the core memory itself (for DMA)    */
void    hp_write_core(uint32_t address, hp_word value);

/* ---- Cards -------------------------------------------------------------- */

int  hp_card_install(hp_card_t *card, unsigned select_code);
void hp_card_remove(unsigned select_code);

/* Device events. Set the flag buffer and flag, as an interface does when its
 * device is ready (STF, ENF), and re-evaluate interrupts. */
void hp_card_set_flag(hp_card_t *card);
void hp_card_clear_flag(hp_card_t *card);
void hp_irq_eval(void);                 /* after changing any card flip-flop   */

/* ---- Events (time counted in instructions) ------------------------------ */

void hp_event_after(hp_event_t *ev, uint32_t instructions,
                    void (*fn)(void *arg), void *arg);
void hp_event_cancel(hp_event_t *ev);
int  hp_event_active(const hp_event_t *ev);
uint32_t hp_event_remaining(const hp_event_t *ev);   /* 0 if not active */

/* ---- Floating point firmware (hp21xx_fp.c) ------------------------------ */

/* Execute the instruction in hp.ir (105000 to 105xxx), returns a HP_STOP_
 * code or 0. */
int  hp_fp_execute(void);

/* ---- I/O Processor firmware (hp21xx_iop.c) ----------------------------- */

int  hp_iop_execute(void);
int  hp_iog_run(hp_word instruction);   /* run one I/O instruction, as PRFIO does */

#endif
