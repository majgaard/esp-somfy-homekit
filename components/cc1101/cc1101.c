#include "cc1101.h"

uint32_t cc1101_freq_word(uint32_t freq_hz, uint32_t xosc_hz)
{
    uint64_t n = ((uint64_t)freq_hz << 16) + (xosc_hz / 2); /* round to nearest */
    return (uint32_t)(n / xosc_hz) & 0x00FFFFFFu;
}

uint32_t cc1101_word_to_hz(uint32_t word, uint32_t xosc_hz)
{
    uint64_t n = (uint64_t)word * xosc_hz + (1u << 15);
    return (uint32_t)(n >> 16);
}

uint8_t cc1101_pa_value_433(int tx_dbm)
{
    /* PA_TABLE_433 from SmartRC-CC1101-Driver-Lib, for
     * -30, -20, -15, -10, 0, 5, 7 and 10 dBm. */
    static const uint8_t table[8] = {
        0x12, 0x0E, 0x1D, 0x34, 0x60, 0x84, 0xC8, 0xC0
    };
    if (tx_dbm <= -30) return table[0];
    if (tx_dbm <= -20) return table[1];
    if (tx_dbm <= -15) return table[2];
    if (tx_dbm <= -10) return table[3];
    if (tx_dbm <= 0)   return table[4];
    if (tx_dbm <= 5)   return table[5];
    if (tx_dbm <= 7)   return table[6];
    return table[7];
}

void cc1101_write_reg(const cc1101_hal_t *hal, uint8_t addr, uint8_t value)
{
    hal->cs(hal->ctx, true);
    hal->spi_xfer(hal->ctx, addr);
    hal->spi_xfer(hal->ctx, value);
    hal->cs(hal->ctx, false);
}

static void write_burst(const cc1101_hal_t *hal, uint8_t addr,
                        const uint8_t *data, uint8_t len)
{
    hal->cs(hal->ctx, true);
    hal->spi_xfer(hal->ctx, (uint8_t)(addr | CC1101_WRITE_BURST));
    for (uint8_t i = 0; i < len; i++) {
        hal->spi_xfer(hal->ctx, data[i]);
    }
    hal->cs(hal->ctx, false);
}

uint8_t cc1101_read_reg(const cc1101_hal_t *hal, uint8_t addr)
{
    hal->cs(hal->ctx, true);
    hal->spi_xfer(hal->ctx, (uint8_t)(addr | CC1101_READ_SINGLE));
    uint8_t v = hal->spi_xfer(hal->ctx, 0x00);
    hal->cs(hal->ctx, false);
    return v;
}

uint8_t cc1101_read_status(const cc1101_hal_t *hal, uint8_t addr)
{
    /* Status registers live at the same addresses as the strobes. The burst
     * bit is what tells the chip we want the register and not the strobe. */
    hal->cs(hal->ctx, true);
    hal->spi_xfer(hal->ctx, (uint8_t)(addr | CC1101_READ_BURST));
    uint8_t v = hal->spi_xfer(hal->ctx, 0x00);
    hal->cs(hal->ctx, false);
    return v;
}

void cc1101_strobe(const cc1101_hal_t *hal, uint8_t strobe)
{
    hal->cs(hal->ctx, true);
    hal->spi_xfer(hal->ctx, strobe);
    hal->cs(hal->ctx, false);
}

void cc1101_reset(const cc1101_hal_t *hal)
{
    /* Manual power-on reset from the datasheet section 19.1. */
    hal->cs(hal->ctx, true);
    hal->delay_us(hal->ctx, 10);
    hal->cs(hal->ctx, false);
    hal->delay_us(hal->ctx, 40);
    cc1101_strobe(hal, CC1101_SRES);
    hal->delay_us(hal->ctx, 1000);
}

/* Baseline that does not depend on frequency or power. */
static const uint8_t k_base[][2] = {
    /* GDO2 and GDO0 both carry async serial data. In transmit the chip reads
     * GDO0 as an input, which is the pin our RMT channel drives. */
    { CC1101_IOCFG2,   0x0D },
    { CC1101_IOCFG0,   0x0D },
    /* Async serial mode: PKT_FORMAT = 3, no CRC, no whitening, length
     * config = infinite. All packet handling is off. */
    { CC1101_PKTCTRL0, 0x32 },
    { CC1101_PKTCTRL1, 0x04 },
    { CC1101_ADDR,     0x00 },
    { CC1101_PKTLEN,   0x00 },
    /* ASK/OOK, no sync word, no preamble. The chip's own Manchester encoder
     * stays off: async mode bypasses it, and somfy_rts already emits
     * Manchester in the envelope. */
    { CC1101_MDMCFG2,  0x30 },
    { CC1101_MDMCFG4,  0xF7 },
    { CC1101_MDMCFG3,  0x93 },
    { CC1101_MDMCFG1,  0x02 },
    { CC1101_MDMCFG0,  0xF8 },
    { CC1101_CHANNR,   0x00 },
    { CC1101_DEVIATN,  0x47 },
    { CC1101_FSCTRL1,  0x06 },
    { CC1101_FSCTRL0,  0x00 },
    /* PA_POWER = 1 selects PATABLE[0] for carrier off and PATABLE[1] for
     * carrier on. Without this the OOK envelope does nothing. */
    { CC1101_FREND0,   0x11 },
    { CC1101_FREND1,   0x56 },
    { CC1101_MCSM0,    0x18 },
    { CC1101_FOCCFG,   0x16 },
    { CC1101_BSCFG,    0x1C },
    { CC1101_AGCCTRL2, 0xC7 },
    { CC1101_AGCCTRL1, 0x00 },
    { CC1101_AGCCTRL0, 0xB2 },
    { CC1101_FSCAL3,   0xE9 },
    { CC1101_FSCAL2,   0x2A },
    { CC1101_FSCAL1,   0x00 },
    { CC1101_FSCAL0,   0x1F },
    { CC1101_FSTEST,   0x59 },
    { CC1101_TEST2,    0x81 },
    { CC1101_TEST1,    0x35 },
    /* 433.42 MHz sits above 430.5 MHz, so TEST0 takes 0x09. */
    { CC1101_TEST0,    0x09 },
};

bool cc1101_configure_ook_tx(const cc1101_hal_t *hal, const cc1101_cfg_t *cfg)
{
    uint32_t xosc = cfg->xosc_hz ? cfg->xosc_hz : CC1101_XOSC_HZ_DEFAULT;

    cc1101_reset(hal);

    for (unsigned i = 0; i < sizeof(k_base) / sizeof(k_base[0]); i++) {
        cc1101_write_reg(hal, k_base[i][0], k_base[i][1]);
    }

    uint32_t word = cc1101_freq_word(cfg->freq_hz, xosc);
    cc1101_write_reg(hal, CC1101_FREQ2, (uint8_t)(word >> 16));
    cc1101_write_reg(hal, CC1101_FREQ1, (uint8_t)(word >> 8));
    cc1101_write_reg(hal, CC1101_FREQ0, (uint8_t)(word));

    /* Index 0 is carrier off and must stay at zero. Index 1 is carrier on. */
    uint8_t pa[8] = { 0 };
    pa[1] = cc1101_pa_value_433(cfg->tx_dbm);
    write_burst(hal, CC1101_PATABLE, pa, sizeof(pa));

    uint8_t part = cc1101_read_status(hal, CC1101_PARTNUM);
    uint8_t ver  = cc1101_read_status(hal, CC1101_VERSION);
    /* A silent or floating bus reads all zeroes or all ones. Real parts report
     * PARTNUM 0 and VERSION 0x14 or 0x04. */
    if (part != 0x00 || ver == 0x00 || ver == 0xFF) {
        return false;
    }
    return true;
}

/* The registers worth naming when something is wrong. */
static const char *reg_name(uint8_t addr)
{
    switch (addr) {
    case CC1101_IOCFG2:   return "IOCFG2";
    case CC1101_IOCFG0:   return "IOCFG0";
    case CC1101_PKTCTRL0: return "PKTCTRL0";
    case CC1101_MDMCFG2:  return "MDMCFG2";
    case CC1101_FREND0:   return "FREND0";
    case CC1101_FREQ2:    return "FREQ2";
    case CC1101_FREQ1:    return "FREQ1";
    case CC1101_FREQ0:    return "FREQ0";
    case CC1101_TEST0:    return "TEST0";
    default:              return "";
    }
}

size_t cc1101_verify(const cc1101_hal_t *hal, const cc1101_cfg_t *cfg,
                     cc1101_regcheck_t *out, size_t cap)
{
    size_t n = 0;

    for (unsigned i = 0; i < sizeof(k_base) / sizeof(k_base[0]) && n < cap; i++) {
        out[n].addr = k_base[i][0];
        out[n].want = k_base[i][1];
        out[n].got  = cc1101_read_reg(hal, k_base[i][0]);
        out[n].name = reg_name(k_base[i][0]);
        n++;
    }

    uint32_t xosc = cfg->xosc_hz ? cfg->xosc_hz : CC1101_XOSC_HZ_DEFAULT;
    uint32_t word = cc1101_freq_word(cfg->freq_hz, xosc);
    const uint8_t freq[3][2] = {
        { CC1101_FREQ2, (uint8_t)(word >> 16) },
        { CC1101_FREQ1, (uint8_t)(word >> 8) },
        { CC1101_FREQ0, (uint8_t)(word) },
    };
    for (unsigned i = 0; i < 3 && n < cap; i++) {
        out[n].addr = freq[i][0];
        out[n].want = freq[i][1];
        out[n].got  = cc1101_read_reg(hal, freq[i][0]);
        out[n].name = reg_name(freq[i][0]);
        n++;
    }

    return n;
}

void cc1101_read_patable(const cc1101_hal_t *hal, uint8_t out[8])
{
    hal->cs(hal->ctx, true);
    hal->spi_xfer(hal->ctx, (uint8_t)(CC1101_PATABLE | CC1101_READ_BURST));
    for (int i = 0; i < 8; i++) {
        out[i] = hal->spi_xfer(hal->ctx, 0x00);
    }
    hal->cs(hal->ctx, false);
}

void cc1101_tx_start(const cc1101_hal_t *hal)
{
    cc1101_strobe(hal, CC1101_SIDLE);
    cc1101_strobe(hal, CC1101_SFTX);
    cc1101_strobe(hal, CC1101_STX);
    /* The frequency synthesiser needs to settle before the envelope starts. */
    hal->delay_us(hal->ctx, 1000);
}

void cc1101_tx_end(const cc1101_hal_t *hal)
{
    cc1101_strobe(hal, CC1101_SIDLE);
}
