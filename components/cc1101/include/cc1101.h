/*
 * CC1101 configuration for 433.42 MHz OOK transmit, async transparent mode.
 *
 * The chip does no framing in this mode. It only keys the carrier on and off
 * to follow the GDO0 pin, and somfy_rts generates that envelope.
 *
 * All SPI and GPIO access goes through cc1101_hal_t, so this file pulls in no
 * ESP-IDF and no Arduino headers. The host tests drive it with a mock HAL and
 * check the exact register map we put on the wire.
 *
 * Register values follow LSatan/SmartRC-CC1101-Driver-Lib (MIT), which is the
 * driver ESPSomfy-RTS uses against real Somfy motors. See docs/protocol.md.
 */
#ifndef CC1101_H
#define CC1101_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Config registers */
#define CC1101_IOCFG2   0x00
#define CC1101_IOCFG0   0x02
#define CC1101_FIFOTHR  0x03
#define CC1101_PKTLEN   0x06
#define CC1101_PKTCTRL1 0x07
#define CC1101_PKTCTRL0 0x08
#define CC1101_ADDR     0x09
#define CC1101_CHANNR   0x0A
#define CC1101_FSCTRL1  0x0B
#define CC1101_FSCTRL0  0x0C
#define CC1101_FREQ2    0x0D
#define CC1101_FREQ1    0x0E
#define CC1101_FREQ0    0x0F
#define CC1101_MDMCFG4  0x10
#define CC1101_MDMCFG3  0x11
#define CC1101_MDMCFG2  0x12
#define CC1101_MDMCFG1  0x13
#define CC1101_MDMCFG0  0x14
#define CC1101_DEVIATN  0x15
#define CC1101_MCSM0    0x18
#define CC1101_FOCCFG   0x19
#define CC1101_BSCFG    0x1A
#define CC1101_AGCCTRL2 0x1B
#define CC1101_AGCCTRL1 0x1C
#define CC1101_AGCCTRL0 0x1D
#define CC1101_FREND1   0x21
#define CC1101_FREND0   0x22
#define CC1101_FSCAL3   0x23
#define CC1101_FSCAL2   0x24
#define CC1101_FSCAL1   0x25
#define CC1101_FSCAL0   0x26
#define CC1101_FSTEST   0x29
#define CC1101_TEST2    0x2C
#define CC1101_TEST1    0x2D
#define CC1101_TEST0    0x2E
#define CC1101_PATABLE  0x3E

/* Status registers. Reads of these need the burst bit set. */
#define CC1101_PARTNUM  0x30
#define CC1101_VERSION  0x31
#define CC1101_MARCSTATE 0x35

/* Command strobes */
#define CC1101_SRES     0x30
#define CC1101_STX      0x35
#define CC1101_SIDLE    0x36
#define CC1101_SFTX     0x3B

/* SPI header bits */
#define CC1101_WRITE_BURST 0x40
#define CC1101_READ_SINGLE 0x80
#define CC1101_READ_BURST  0xC0

#define CC1101_XOSC_HZ_DEFAULT 26000000u
/* Somfy RTS. Not 433.92 MHz. */
#define CC1101_SOMFY_HZ        433420000u

typedef struct {
    /* Full-duplex byte swap. Returns the byte the chip clocked back. */
    uint8_t (*spi_xfer)(void *ctx, uint8_t out);
    /* select = true asserts CSn, which means drive it low. */
    void (*cs)(void *ctx, bool select);
    void (*delay_us)(void *ctx, uint32_t us);
    void *ctx;
} cc1101_hal_t;

typedef struct {
    uint32_t freq_hz;
    uint32_t xosc_hz;
    int8_t   tx_dbm;
} cc1101_cfg_t;

/* FREQ = f_carrier * 2^16 / f_xosc, rounded to nearest. */
uint32_t cc1101_freq_word(uint32_t freq_hz, uint32_t xosc_hz);
/* Inverse, so a test can report the real carrier and its error. */
uint32_t cc1101_word_to_hz(uint32_t word, uint32_t xosc_hz);
/* PATABLE value for the 387-464 MHz band. Rounds down to a supported step. */
uint8_t cc1101_pa_value_433(int tx_dbm);

void cc1101_write_reg(const cc1101_hal_t *hal, uint8_t addr, uint8_t value);
uint8_t cc1101_read_reg(const cc1101_hal_t *hal, uint8_t addr);
uint8_t cc1101_read_status(const cc1101_hal_t *hal, uint8_t addr);
void cc1101_strobe(const cc1101_hal_t *hal, uint8_t strobe);

void cc1101_reset(const cc1101_hal_t *hal);

/* Full reset and register load. Returns false if the chip does not answer with
 * a plausible PARTNUM/VERSION, which usually means the wiring is wrong. */
bool cc1101_configure_ook_tx(const cc1101_hal_t *hal, const cc1101_cfg_t *cfg);

/* Key the PA on. Call this, play the envelope on GDO0, then call idle. */
void cc1101_tx_start(const cc1101_hal_t *hal);
void cc1101_tx_end(const cc1101_hal_t *hal);

typedef struct {
    uint8_t addr;
    uint8_t want;
    uint8_t got;
    const char *name;
} cc1101_regcheck_t;

/* Read every register we configured back off the chip and compare. Writing a
 * register and having it stick are different things, and a half-working SPI
 * bus reads plausible rubbish. Returns the number of entries written to out;
 * the caller counts the mismatches. */
size_t cc1101_verify(const cc1101_hal_t *hal, const cc1101_cfg_t *cfg,
                     cc1101_regcheck_t *out, size_t cap);

/* PATABLE needs a burst read, so it is not part of cc1101_verify. */
void cc1101_read_patable(const cc1101_hal_t *hal, uint8_t out[8]);

#ifdef __cplusplus
}
#endif

#endif /* CC1101_H */
