#include "somfy_rts.h"

#include <string.h>

/* The checksum is a 4-bit XOR fold over the plain frame with the checksum
 * nibble itself held at zero. */
static uint8_t checksum4(const uint8_t f[SOMFY_RTS_FRAME_BYTES])
{
    uint8_t cs = 0;
    for (int i = 0; i < SOMFY_RTS_FRAME_BYTES; i++) {
        cs ^= (uint8_t)(f[i] ^ (f[i] >> 4));
    }
    return cs & 0x0F;
}

void somfy_rts_encode(const somfy_rts_frame_t *in,
                      uint8_t out[SOMFY_RTS_FRAME_BYTES])
{
    out[0] = in->enc_key;
    out[1] = (uint8_t)(((uint8_t)in->cmd & 0x0F) << 4); /* low nibble = checksum */
    out[2] = (uint8_t)(in->rolling_code >> 8);
    out[3] = (uint8_t)(in->rolling_code);
    out[4] = (uint8_t)(in->address >> 16);
    out[5] = (uint8_t)(in->address >> 8);
    out[6] = (uint8_t)(in->address);

    out[1] |= checksum4(out);

    /* Obfuscation: each byte XORs the already-obfuscated byte before it. */
    for (int i = 1; i < SOMFY_RTS_FRAME_BYTES; i++) {
        out[i] ^= out[i - 1];
    }
}

bool somfy_rts_decode(const uint8_t in[SOMFY_RTS_FRAME_BYTES],
                      somfy_rts_frame_t *out)
{
    uint8_t f[SOMFY_RTS_FRAME_BYTES];

    /* obf[i] = plain[i] ^ obf[i-1], so plain[i] = obf[i] ^ obf[i-1]. */
    f[0] = in[0];
    for (int i = 1; i < SOMFY_RTS_FRAME_BYTES; i++) {
        f[i] = (uint8_t)(in[i] ^ in[i - 1]);
    }

    uint8_t got = f[1] & 0x0F;
    f[1] &= 0xF0;
    if (checksum4(f) != got) {
        return false;
    }

    out->enc_key      = f[0];
    out->cmd          = (somfy_cmd_t)(f[1] >> 4);
    out->rolling_code = (uint16_t)((f[2] << 8) | f[3]);
    out->address      = ((uint32_t)f[4] << 16) | ((uint32_t)f[5] << 8) | f[6];
    return true;
}

/* --- OOK envelope ------------------------------------------------------- */

typedef struct {
    somfy_pulse_t *buf;
    size_t         cap;
    size_t         len;
    bool           overflow;
} pulse_ctx_t;

static void emit(pulse_ctx_t *c, uint8_t level, uint32_t us)
{
    if (c->len > 0 && c->buf[c->len - 1].level == level) {
        c->buf[c->len - 1].us += us; /* merge */
        return;
    }
    if (c->len >= c->cap) {
        c->overflow = true;
        return;
    }
    c->buf[c->len].level = level;
    c->buf[c->len].us    = us;
    c->len++;
}

size_t somfy_rts_pulse_capacity(uint8_t repeats)
{
    /* Per frame, before merging: hw sync pairs, sw sync + start bit,
     * 2 pulses per Manchester bit, and the inter-frame gap. */
    const size_t first  = 2 /* wake-up */
                        + 2 * SOMFY_RTS_HW_SYNC_FIRST
                        + 2 + 2 * SOMFY_RTS_FRAME_BITS + 1;
    const size_t repeat = 2 * SOMFY_RTS_HW_SYNC_REPEAT
                        + 2 + 2 * SOMFY_RTS_FRAME_BITS + 1;
    return first + (size_t)repeats * repeat;
}

size_t somfy_rts_build_pulses(const somfy_rts_frame_t *frame, uint8_t repeats,
                              somfy_pulse_t *out, size_t cap)
{
    uint8_t bytes[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_encode(frame, bytes);

    pulse_ctx_t c = { .buf = out, .cap = cap, .len = 0, .overflow = false };

    for (int f = 0; f <= (int)repeats; f++) {
        /* The wake-up pulse goes on the first frame only. */
        if (f == 0) {
            emit(&c, 1, SOMFY_RTS_WAKEUP_HIGH_US);
            emit(&c, 0, SOMFY_RTS_WAKEUP_LOW_US);
        }

        int hw = (f == 0) ? SOMFY_RTS_HW_SYNC_FIRST : SOMFY_RTS_HW_SYNC_REPEAT;
        for (int i = 0; i < hw; i++) {
            emit(&c, 1, 4 * SOMFY_RTS_SYMBOL_US);
            emit(&c, 0, 4 * SOMFY_RTS_SYMBOL_US);
        }

        /* Software sync, then one half-symbol low that starts the data. */
        emit(&c, 1, SOMFY_RTS_SW_SYNC_US);
        emit(&c, 0, SOMFY_RTS_SYMBOL_US);

        /* Manchester, MSB first. A 1 bit is low then high; a 0 bit is high
         * then low. Note this is inverted against some published references. */
        for (int i = 0; i < SOMFY_RTS_FRAME_BITS; i++) {
            int bit = (bytes[i / 8] >> (7 - (i % 8))) & 1;
            if (bit) {
                emit(&c, 0, SOMFY_RTS_SYMBOL_US);
                emit(&c, 1, SOMFY_RTS_SYMBOL_US);
            } else {
                emit(&c, 1, SOMFY_RTS_SYMBOL_US);
                emit(&c, 0, SOMFY_RTS_SYMBOL_US);
            }
        }

        emit(&c, 0, SOMFY_RTS_IF_GAP_US);
    }

    if (c.overflow) {
        return 0;
    }
    return c.len;
}
