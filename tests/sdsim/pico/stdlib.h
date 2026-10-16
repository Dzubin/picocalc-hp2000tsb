#ifndef SIM_STDLIB_H
#define SIM_STDLIB_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { int dummy; } repeating_timer_t;
typedef bool (*repeating_timer_callback_t)(repeating_timer_t *);
static inline bool add_repeating_timer_ms(int ms, repeating_timer_callback_t cb, void *u, repeating_timer_t *t){(void)ms;(void)cb;(void)u;(void)t;return true;}
void sleep_ms(uint32_t ms);
static inline uint64_t time_us_64(void) { return 0; }
#endif
