#ifndef SIM_SPI_H
#define SIM_SPI_H
#include <stdint.h>
#include <stddef.h>
#define spi0 0
static inline unsigned spi_set_baudrate(int spi, unsigned baud) { (void)spi; return baud; }
/* the simulated card is never busy: every byte reads back as 0xFF */
static inline int spi_write_read_blocking(int spi, const uint8_t *src, uint8_t *dst, size_t len)
{
    size_t i;

    (void)spi; (void)src;
    for (i = 0; i < len; i++)
        dst[i] = 0xFF;
    return (int)len;
}
static inline int spi_write_blocking(int spi, const uint8_t *src, size_t len) { (void)spi; (void)src; return (int)len; }
static inline void gpio_put(unsigned pin, int value) { (void)pin; (void)value; }
#endif
