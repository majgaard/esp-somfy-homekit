#include "somfy_radio.h"

#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>

#include "driver/rmt_tx.h"

#include "cc1101.h"
#include "somfy_config.h"

/* One frame plus SOMFY_REPEATS repeats, before merging. */
#define MAX_PULSES  768
#define MAX_SYMBOLS (MAX_PULSES / 2 + 1)

static somfy_pulse_t     s_pulses[MAX_PULSES];
static rmt_symbol_word_t s_symbols[MAX_SYMBOLS];

static rmt_channel_handle_t s_chan    = nullptr;
static rmt_encoder_handle_t s_encoder = nullptr;
static Preferences          s_nvs;
static bool                 s_ok = false;

/* --- CC1101 HAL over Arduino SPI ---------------------------------------- */

static uint8_t hal_xfer(void *ctx, uint8_t out)
{
    (void)ctx;
    return SPI.transfer(out);
}

/* The CC1101 takes 6.5 MHz on a burst access. 4 MHz keeps margin. */
static const SPISettings kSpi(4000000, MSBFIRST, SPI_MODE0);

static void hal_cs(void *ctx, bool select)
{
    (void)ctx;
    if (!select) {
        digitalWrite(PIN_CSN, HIGH);
        SPI.endTransaction();
        return;
    }
    SPI.beginTransaction(kSpi);
    digitalWrite(PIN_CSN, LOW);
    /* The CC1101 holds MISO high until its regulator and crystal settle.
     * Talking to it before that returns rubbish. */
    uint32_t start = micros();
    while (digitalRead(PIN_MISO) && (micros() - start) < 10000) {
    }
}

static void hal_delay(void *ctx, uint32_t us)
{
    (void)ctx;
    delayMicroseconds(us);
}

static const cc1101_hal_t s_hal = { hal_xfer, hal_cs, hal_delay, nullptr };

/* --- envelope over RMT --------------------------------------------------- */

/* Each RMT symbol holds two level/duration pairs, and each duration field is
 * 15 bits. At 1 tick per microsecond that caps one pulse at 32767 us. Our
 * longest is the inter-frame gap merged with a trailing half-symbol, 28074 us,
 * so it fits with margin. */
#define RMT_MAX_TICKS 32767u

static size_t pack_symbols(const somfy_pulse_t *p, size_t n)
{
    size_t k = 0;
    for (size_t i = 0; i < n; i += 2) {
        if (p[i].us > RMT_MAX_TICKS) {
            return 0;
        }
        rmt_symbol_word_t s = {};
        s.duration0 = p[i].us;
        s.level0    = p[i].level;
        if (i + 1 < n) {
            if (p[i + 1].us > RMT_MAX_TICKS) {
                return 0;
            }
            s.duration1 = p[i + 1].us;
            s.level1    = p[i + 1].level;
        } else {
            /* Odd count. Pad with the shortest possible low. */
            s.duration1 = 1;
            s.level1    = 0;
        }
        s_symbols[k++] = s;
    }
    return k;
}

/* --- public -------------------------------------------------------------- */

uint32_t somfy_address(uint8_t shade) { return SOMFY_ADDR_BASE + shade; }

uint16_t somfy_rolling_code(uint8_t shade)
{
    char key[8];
    snprintf(key, sizeof(key), "rc%u", shade);
    return s_nvs.getUShort(key, 1);
}

bool somfy_radio_ok(void) { return s_ok; }

bool somfy_radio_begin(void)
{
    s_nvs.begin("somfy", false);

    pinMode(PIN_CSN, OUTPUT);
    digitalWrite(PIN_CSN, HIGH);
    /* Pass -1 for the slave-select pin. We drive CSN ourselves, because a
     * CC1101 access is header plus data in one frame. */
    SPI.begin(PIN_SCK, PIN_MISO, PIN_MOSI, -1);

    /* Bring the RMT channel up first. It does not depend on the CC1101, so it
     * still gets verified on a board with no module fitted. */
    rmt_tx_channel_config_t cc = {};
    cc.gpio_num      = (gpio_num_t)PIN_GDO0;
    cc.clk_src       = RMT_CLK_SRC_DEFAULT;
    cc.resolution_hz = 1000000; /* 1 tick = 1 us */
    /* Must be a multiple of SOC_RMT_MEM_WORDS_PER_CHANNEL, which is 48 on the
     * ESP32-C3. One block buffers 96 pulses, far more than the ISR needs to
     * keep ahead of a 640 us half-symbol. */
    cc.mem_block_symbols = 48;
    cc.trans_queue_depth = 4;

    esp_err_t err = rmt_new_tx_channel(&cc, &s_chan);
    if (err != ESP_OK) {
        Serial.printf("[radio] RMT channel failed: %s\n", esp_err_to_name(err));
        return false;
    }

    rmt_copy_encoder_config_t ec = {};
    err = rmt_new_copy_encoder(&ec, &s_encoder);
    if (err != ESP_OK) {
        Serial.printf("[radio] RMT encoder failed: %s\n", esp_err_to_name(err));
        return false;
    }
    rmt_enable(s_chan);
    Serial.printf("[radio] RMT ready on GPIO%d, 1 us resolution.\n", PIN_GDO0);

    cc1101_cfg_t cfg = {};
    cfg.freq_hz = CC1101_SOMFY_HZ;
    cfg.xosc_hz = CC1101_XOSC_HZ_DEFAULT;
    cfg.tx_dbm  = SOMFY_TX_DBM;

    if (!cc1101_configure_ook_tx(&s_hal, &cfg)) {
        Serial.println("[radio] CC1101 did not answer. Check the wiring.");
        s_ok = false;
        return false;
    }

    s_ok = true;
    Serial.println("[radio] CC1101 ready at 433.42 MHz.");
    return true;
}

int somfy_radio_verify(void)
{
    if (!s_ok) {
        Serial.println("[radio] not ready, nothing to verify");
        return -1;
    }

    cc1101_cfg_t cfg = {};
    cfg.freq_hz = CC1101_SOMFY_HZ;
    cfg.xosc_hz = CC1101_XOSC_HZ_DEFAULT;
    cfg.tx_dbm  = SOMFY_TX_DBM;

    cc1101_regcheck_t chk[48];
    size_t n = cc1101_verify(&s_hal, &cfg, chk, 48);

    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        bool ok = chk[i].want == chk[i].got;
        if (!ok) {
            bad++;
        }
        /* Print the named registers always, the rest only when wrong. */
        if (!ok || chk[i].name[0]) {
            Serial.printf("  %-9s 0x%02X  want %02X  got %02X  %s\n",
                          chk[i].name, chk[i].addr, chk[i].want, chk[i].got,
                          ok ? "ok" : "MISMATCH");
        }
    }

    uint8_t pa[8];
    cc1101_read_patable(&s_hal, pa);
    Serial.printf("  PATABLE        off %02X  on %02X  %s\n", pa[0], pa[1],
                  (pa[0] == 0x00 && pa[1] != 0x00) ? "ok"
                                                   : "MISMATCH (OOK needs 0)");
    if (pa[0] != 0x00 || pa[1] == 0x00) {
        bad++;
    }

    uint32_t hz = cc1101_word_to_hz(
        ((uint32_t)cc1101_read_reg(&s_hal, CC1101_FREQ2) << 16) |
            ((uint32_t)cc1101_read_reg(&s_hal, CC1101_FREQ1) << 8) |
            cc1101_read_reg(&s_hal, CC1101_FREQ0),
        CC1101_XOSC_HZ_DEFAULT);
    Serial.printf("  carrier        %lu.%02lu MHz\n", (unsigned long)(hz / 1000000),
                  (unsigned long)((hz % 1000000 + 5000) / 10000));

    Serial.printf("[radio] %d mismatches of %u registers\n", bad, (unsigned)n);
    return bad;
}

bool somfy_send(uint8_t shade, somfy_cmd_t cmd)
{
    if (!s_ok || shade >= SHADE_COUNT) {
        return false;
    }

    char key[8];
    snprintf(key, sizeof(key), "rc%u", shade);
    uint16_t rc = s_nvs.getUShort(key, 1);
    /* Persist before the air, never after. A reset between the two would
     * otherwise replay a code the motor has already seen and rejects. */
    s_nvs.putUShort(key, (uint16_t)(rc + 1));

    somfy_rts_frame_t f;
    f.enc_key      = (uint8_t)(0xA0 | (rc & 0x0F));
    f.cmd          = cmd;
    f.rolling_code = rc;
    f.address      = somfy_address(shade);

    size_t n = somfy_rts_build_pulses(&f, SOMFY_REPEATS, s_pulses, MAX_PULSES);
    if (n == 0) {
        Serial.println("[radio] pulse buffer too small");
        return false;
    }
    size_t k = pack_symbols(s_pulses, n);
    if (k == 0) {
        Serial.println("[radio] a pulse exceeds the RMT duration field");
        return false;
    }

    rmt_transmit_config_t tc = {};
    tc.loop_count      = 0;
    tc.flags.eot_level = 0; /* leave the carrier off when the frame ends */

    cc1101_tx_start(&s_hal);
    esp_err_t err = rmt_transmit(s_chan, s_encoder, s_symbols,
                                 k * sizeof(rmt_symbol_word_t), &tc);
    if (err == ESP_OK) {
        rmt_tx_wait_all_done(s_chan, 2000);
    }
    cc1101_tx_end(&s_hal);

    Serial.printf("[radio] shade %u cmd 0x%X addr %06lX code %u\n", shade,
                  (unsigned)cmd, (unsigned long)f.address, rc);
    return err == ESP_OK;
}
