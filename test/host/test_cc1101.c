#include "cc1101.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

/* --- mock SPI ----------------------------------------------------------- */

#define MAX_OPS 256

typedef struct {
    uint8_t addr;
    uint8_t value;
} regwrite_t;

typedef struct {
    uint8_t    bytes[MAX_OPS];
    size_t     nbytes;
    regwrite_t writes[MAX_OPS];
    size_t     nwrites;
    /* Bytes the chip clocks back, consumed in order. */
    uint8_t    reads[16];
    size_t     nreads;
    size_t     read_pos;
    bool       selected;
    size_t     frame_len; /* bytes since CS went low */
    uint8_t    frame_hdr;
    int        cs_errors;
} mock_t;

static void mock_cs(void *ctx, bool select)
{
    mock_t *m = ctx;
    if (select == m->selected) {
        m->cs_errors++; /* double assert or double release */
    }
    if (select) {
        m->frame_len = 0;
    }
    m->selected = select;
}

static uint8_t mock_xfer(void *ctx, uint8_t out)
{
    mock_t *m = ctx;
    if (!m->selected) {
        m->cs_errors++; /* traffic with CS high never reaches the chip */
    }
    if (m->nbytes < MAX_OPS) {
        m->bytes[m->nbytes++] = out;
    }

    size_t pos = m->frame_len++;

    if (pos == 0) {
        m->frame_hdr = out;
        /* The chip returns its status byte while it clocks in the header. It
         * never returns register data here. */
        return 0x0F;
    }

    if (pos == 1 && !(m->frame_hdr & 0x80) &&
        !(m->frame_hdr & CC1101_WRITE_BURST)) {
        /* single register write */
        if (m->nwrites < MAX_OPS) {
            m->writes[m->nwrites].addr  = m->frame_hdr;
            m->writes[m->nwrites].value = out;
            m->nwrites++;
        }
    }

    if ((m->frame_hdr & 0x80) && m->read_pos < m->nreads) {
        return m->reads[m->read_pos++];
    }
    return 0x00;
}

static void mock_delay(void *ctx, uint32_t us) { (void)ctx; (void)us; }

/* Last value written to addr, or -1 if it was never written. */
static int reg_of(const mock_t *m, uint8_t addr)
{
    int v = -1;
    for (size_t i = 0; i < m->nwrites; i++) {
        if (m->writes[i].addr == addr) {
            v = m->writes[i].value;
        }
    }
    return v;
}

static void mock_init(mock_t *m, cc1101_hal_t *hal)
{
    memset(m, 0, sizeof(*m));
    hal->spi_xfer = mock_xfer;
    hal->cs       = mock_cs;
    hal->delay_us = mock_delay;
    hal->ctx      = m;
}

/* --- tests -------------------------------------------------------------- */

/* Hand-computed: 433.42e6 * 65536 / 26e6 = 1092485.12 -> 1092485 = 0x10AB85. */
static void test_freq_word_for_somfy(void)
{
    uint32_t w = cc1101_freq_word(CC1101_SOMFY_HZ, CC1101_XOSC_HZ_DEFAULT);
    ASSERT_EQ(0x10AB85, w);
    ASSERT_EQ(0x10, (w >> 16) & 0xFF);
    ASSERT_EQ(0xAB, (w >> 8) & 0xFF);
    ASSERT_EQ(0x85, w & 0xFF);

    /* The synthesiser step is about 397 Hz, so we land very close. */
    uint32_t back = cc1101_word_to_hz(w, CC1101_XOSC_HZ_DEFAULT);
    ASSERT_NEAR(CC1101_SOMFY_HZ, back, 400);
}

/* 433.92 is the band everyone assumes. Prove the two differ, so a wrong
 * constant cannot pass unnoticed. */
static void test_somfy_differs_from_433_92(void)
{
    uint32_t somfy = cc1101_freq_word(CC1101_SOMFY_HZ, CC1101_XOSC_HZ_DEFAULT);
    uint32_t common = cc1101_freq_word(433920000u, CC1101_XOSC_HZ_DEFAULT);
    ASSERT_TRUE(somfy != common);
    ASSERT_TRUE(common - somfy > 1000); /* about 500 kHz apart */
}

static void test_freq_word_round_trip(void)
{
    for (uint32_t hz = 387000000u; hz <= 464000000u; hz += 1000000u) {
        uint32_t w = cc1101_freq_word(hz, CC1101_XOSC_HZ_DEFAULT);
        ASSERT_TRUE(w <= 0x00FFFFFFu);
        uint32_t back = cc1101_word_to_hz(w, CC1101_XOSC_HZ_DEFAULT);
        ASSERT_NEAR(hz, back, 400);
    }
}

static void test_pa_table_steps(void)
{
    ASSERT_EQ(0xC0, cc1101_pa_value_433(10));  /* max for this band */
    ASSERT_EQ(0xC8, cc1101_pa_value_433(7));
    ASSERT_EQ(0x60, cc1101_pa_value_433(0));
    ASSERT_EQ(0x12, cc1101_pa_value_433(-30));
    ASSERT_EQ(0x12, cc1101_pa_value_433(-100)); /* clamps low */
    ASSERT_EQ(0xC0, cc1101_pa_value_433(30));   /* clamps high */
    /* 8 is above the 7 dBm step, so it must pick the 10 dBm entry. */
    ASSERT_EQ(0xC0, cc1101_pa_value_433(8));
}

static void test_configure_writes_the_right_registers(void)
{
    mock_t m;
    cc1101_hal_t hal;
    mock_init(&m, &hal);
    m.reads[0] = 0x00; /* PARTNUM */
    m.reads[1] = 0x14; /* VERSION */
    m.nreads   = 2;

    cc1101_cfg_t cfg = {
        .freq_hz = CC1101_SOMFY_HZ,
        .xosc_hz = CC1101_XOSC_HZ_DEFAULT,
        .tx_dbm  = 10,
    };
    ASSERT_TRUE(cc1101_configure_ook_tx(&hal, &cfg));
    ASSERT_EQ(0, m.cs_errors);

    /* Carrier frequency */
    ASSERT_EQ(0x10, reg_of(&m, CC1101_FREQ2));
    ASSERT_EQ(0xAB, reg_of(&m, CC1101_FREQ1));
    ASSERT_EQ(0x85, reg_of(&m, CC1101_FREQ0));

    /* ASK/OOK with no sync word and no Manchester in the chip. */
    ASSERT_EQ(0x30, reg_of(&m, CC1101_MDMCFG2));
    /* PA_POWER = 1 so the envelope swings between PATABLE[0] and [1]. */
    ASSERT_EQ(0x11, reg_of(&m, CC1101_FREND0));
    /* Async serial: PKT_FORMAT = 3 in bits 5:4, CRC off in bit 2. */
    int pk = reg_of(&m, CC1101_PKTCTRL0);
    ASSERT_EQ(0x32, pk);
    ASSERT_EQ(3, (pk >> 4) & 0x03);
    ASSERT_EQ(0, (pk >> 2) & 0x01);
    /* GDO0 carries async serial data. */
    ASSERT_EQ(0x0D, reg_of(&m, CC1101_IOCFG0));
    /* Above 430.5 MHz TEST0 takes 0x09. */
    ASSERT_EQ(0x09, reg_of(&m, CC1101_TEST0));
}

/* The single most important OOK detail: PATABLE[0] must be zero, or the
 * carrier never switches off and the motor hears nothing. */
static void test_patable_index_zero_is_off(void)
{
    mock_t m;
    cc1101_hal_t hal;
    mock_init(&m, &hal);
    m.reads[0] = 0x00;
    m.reads[1] = 0x14;
    m.nreads   = 2;

    cc1101_cfg_t cfg = {
        .freq_hz = CC1101_SOMFY_HZ,
        .xosc_hz = CC1101_XOSC_HZ_DEFAULT,
        .tx_dbm  = 10,
    };
    ASSERT_TRUE(cc1101_configure_ook_tx(&hal, &cfg));

    /* Find the PATABLE burst header and read the 8 bytes after it. */
    int at = -1;
    for (size_t i = 0; i < m.nbytes; i++) {
        if (m.bytes[i] == (CC1101_PATABLE | CC1101_WRITE_BURST)) {
            at = (int)i;
        }
    }
    ASSERT_TRUE(at >= 0);
    ASSERT_TRUE(m.nbytes >= (size_t)at + 9);
    ASSERT_EQ(0x00, m.bytes[at + 1]); /* carrier off */
    ASSERT_EQ(0xC0, m.bytes[at + 2]); /* carrier on, 10 dBm */
    for (int i = 3; i <= 8; i++) {
        ASSERT_EQ(0x00, m.bytes[at + i]);
    }
}

/* A floating SPI bus reads as all ones or all zeroes. Catch that at init
 * rather than after a silent transmit. */
static void test_configure_detects_a_dead_bus(void)
{
    cc1101_cfg_t cfg = {
        .freq_hz = CC1101_SOMFY_HZ,
        .xosc_hz = CC1101_XOSC_HZ_DEFAULT,
        .tx_dbm  = 10,
    };

    mock_t m;
    cc1101_hal_t hal;

    mock_init(&m, &hal); /* reads return 0x00, so VERSION is 0 */
    ASSERT_TRUE(!cc1101_configure_ook_tx(&hal, &cfg));

    mock_init(&m, &hal);
    m.reads[0] = 0xFF;
    m.reads[1] = 0xFF;
    m.nreads   = 2;
    ASSERT_TRUE(!cc1101_configure_ook_tx(&hal, &cfg));
}

static void test_status_read_uses_the_burst_bit(void)
{
    mock_t m;
    cc1101_hal_t hal;
    mock_init(&m, &hal);
    m.reads[0] = 0x14;
    m.nreads   = 1;

    ASSERT_EQ(0x14, cc1101_read_status(&hal, CC1101_VERSION));
    /* Without the burst bit, 0x31 is the SFSTXON strobe and not a register. */
    ASSERT_EQ(CC1101_VERSION | CC1101_READ_BURST, m.bytes[0]);
    ASSERT_EQ(0, m.cs_errors);
}

static void test_tx_start_strobes_in_order(void)
{
    mock_t m;
    cc1101_hal_t hal;
    mock_init(&m, &hal);

    cc1101_tx_start(&hal);
    ASSERT_EQ(3, m.nbytes);
    ASSERT_EQ(CC1101_SIDLE, m.bytes[0]);
    ASSERT_EQ(CC1101_SFTX, m.bytes[1]);
    ASSERT_EQ(CC1101_STX, m.bytes[2]);

    cc1101_tx_end(&hal);
    ASSERT_EQ(CC1101_SIDLE, m.bytes[3]);
    ASSERT_EQ(0, m.cs_errors);
}

void run_cc1101_tests(void)
{
    RUN(test_freq_word_for_somfy);
    RUN(test_somfy_differs_from_433_92);
    RUN(test_freq_word_round_trip);
    RUN(test_pa_table_steps);
    RUN(test_configure_writes_the_right_registers);
    RUN(test_patable_index_zero_is_off);
    RUN(test_configure_detects_a_dead_bus);
    RUN(test_status_read_uses_the_burst_bit);
    RUN(test_tx_start_strobes_in_order);
}
