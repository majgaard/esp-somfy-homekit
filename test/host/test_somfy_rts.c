#include "somfy_rts.h"
#include "test_util.h"

#include <stdlib.h>
#include <string.h>

/* Golden vector, computed by hand from Somfy.cpp:333-437 and pinned here.
 *
 *   enc_key 0xA7, cmd UP (0x2), rolling 0x0001, address 0x123456
 *   plain before checksum: A7 20 00 01 12 34 56
 *   XOR fold b^(b>>4):     AD 22 00 01 13 37 53  -> F9 -> & 0x0F = 9
 *   plain with checksum:   A7 29 00 01 12 34 56
 *   XOR chain:             A7 8E 8E 8F 9D A9 FF
 */
static void test_golden_vector(void)
{
    somfy_rts_frame_t f = {
        .enc_key      = 0xA7,
        .cmd          = SOMFY_CMD_UP,
        .rolling_code = 0x0001,
        .address      = 0x123456,
    };
    const uint8_t want[SOMFY_RTS_FRAME_BYTES] = {
        0xA7, 0x8E, 0x8E, 0x8F, 0x9D, 0xA9, 0xFF
    };
    uint8_t got[SOMFY_RTS_FRAME_BYTES];

    somfy_rts_encode(&f, got);
    ASSERT_MEM_EQ(want, got, SOMFY_RTS_FRAME_BYTES);
}

static void test_round_trip(void)
{
    const somfy_cmd_t cmds[] = {
        SOMFY_CMD_MY, SOMFY_CMD_UP, SOMFY_CMD_DOWN, SOMFY_CMD_PROG,
        SOMFY_CMD_MY_UP, SOMFY_CMD_SUN_FLAG, SOMFY_CMD_FLAG,
    };

    /* Strides are coprime with the byte boundaries, so this walks every
     * nibble pattern that matters without the sweep becoming slow under the
     * sanitizers. */
    for (uint32_t addr = 1; addr < 0xFFFFFF; addr += 0x1D4C11) {
        for (uint32_t rc = 0; rc <= 0xFFFF; rc += 0x7A3) {
            for (size_t ci = 0; ci < sizeof(cmds) / sizeof(cmds[0]); ci++) {
                somfy_rts_frame_t in = {
                    .enc_key      = (uint8_t)(0xA0 | (rc & 0x0F)),
                    .cmd          = cmds[ci],
                    .rolling_code = (uint16_t)rc,
                    .address      = addr,
                };
                uint8_t wire[SOMFY_RTS_FRAME_BYTES];
                somfy_rts_frame_t out;

                somfy_rts_encode(&in, wire);
                ASSERT_TRUE(somfy_rts_decode(wire, &out));
                ASSERT_EQ(in.enc_key, out.enc_key);
                ASSERT_EQ((int)in.cmd, (int)out.cmd);
                ASSERT_EQ(in.rolling_code, out.rolling_code);
                ASSERT_EQ(in.address, out.address);
            }
        }
    }
}

/* The 4-bit checksum is far weaker than it looks, and this test pins exactly
 * how weak. Obfuscation makes plain[i] = wire[i] ^ wire[i-1], so one flipped
 * wire bit flips the same bit position in two neighbouring plain bytes. Both
 * bytes feed the same checksum bit, so the two changes cancel. Only a flip in
 * the last byte has no neighbour to cancel against.
 *
 * Result: 48 of the 56 single-bit errors pass the checksum. The receive path
 * must therefore never trust one frame. It has to require that two of the
 * repeated frames agree. */
static void test_decode_rejects_corruption(void)
{
    somfy_rts_frame_t f = {
        .enc_key = 0xA1, .cmd = SOMFY_CMD_DOWN,
        .rolling_code = 0x1234, .address = 0xABCDEF,
    };
    uint8_t wire[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_encode(&f, wire);

    int rejected = 0, total = 0;
    for (int byte = 0; byte < SOMFY_RTS_FRAME_BYTES; byte++) {
        for (int bit = 0; bit < 8; bit++) {
            uint8_t bad[SOMFY_RTS_FRAME_BYTES];
            somfy_rts_frame_t out;
            memcpy(bad, wire, sizeof(bad));
            bad[byte] ^= (uint8_t)(1 << bit);
            total++;
            if (!somfy_rts_decode(bad, &out)) {
                rejected++;
            }
        }
    }
    ASSERT_EQ(56, total);
    ASSERT_EQ(8, rejected); /* the 8 bits of the last byte, and nothing else */
}

static void test_pulses_alternate_and_start_high(void)
{
    somfy_rts_frame_t f = {
        .enc_key = 0xA0, .cmd = SOMFY_CMD_UP,
        .rolling_code = 42, .address = 0x010203,
    };
    const uint8_t repeats = 4;
    size_t cap = somfy_rts_pulse_capacity(repeats);
    somfy_pulse_t *p = calloc(cap, sizeof(*p));

    size_t n = somfy_rts_build_pulses(&f, repeats, p, cap);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(n <= cap);

    ASSERT_EQ(1, p[0].level);
    ASSERT_EQ(SOMFY_RTS_WAKEUP_HIGH_US, p[0].us);

    for (size_t i = 0; i < n; i++) {
        ASSERT_TRUE(p[i].us > 0);
        if (i > 0) {
            ASSERT_TRUE(p[i].level != p[i - 1].level);
        }
    }
    /* The transmission must end with the carrier off. */
    ASSERT_EQ(0, p[n - 1].level);
    free(p);
}

/* Play the envelope back through a Manchester decoder and check we recover the
 * frame. This is the closest thing to an on-air test we can run with no radio. */
static void test_pulses_demodulate_back_to_frame(void)
{
    somfy_rts_frame_t f = {
        .enc_key = 0xA5, .cmd = SOMFY_CMD_MY,
        .rolling_code = 0x0BAD, .address = 0x0F1E2D,
    };
    uint8_t wire[SOMFY_RTS_FRAME_BYTES];
    somfy_rts_encode(&f, wire);

    const uint8_t repeats = 2;
    size_t cap = somfy_rts_pulse_capacity(repeats);
    somfy_pulse_t *p = calloc(cap, sizeof(*p));
    size_t n = somfy_rts_build_pulses(&f, repeats, p, cap);
    ASSERT_TRUE(n > 0);

    /* Expand to half-symbol slots, skipping the lead-in of each frame. */
    int frames_checked = 0;
    size_t i = 0;
    while (i < n) {
        /* Find the software sync: a high of 4850 us. */
        while (i < n && !(p[i].level == 1 && p[i].us == SOMFY_RTS_SW_SYNC_US)) {
            i++;
        }
        if (i >= n) {
            break;
        }
        i++; /* step past the sw sync high */

        /* Flatten the rest into half-symbol levels. The low that starts the
         * data is part of the first bit when that bit is a 1. */
        uint8_t slots[2 * SOMFY_RTS_FRAME_BITS + 4];
        size_t  ns = 0;
        for (size_t j = i; j < n && ns < sizeof(slots); j++) {
            uint32_t halves = p[j].us / SOMFY_RTS_SYMBOL_US;
            if (p[j].us % SOMFY_RTS_SYMBOL_US != 0) {
                halves++; /* the trailing gap is not a whole number of symbols */
            }
            for (uint32_t k = 0; k < halves && ns < sizeof(slots); k++) {
                slots[ns++] = p[j].level;
            }
        }

        /* Slot 0 is the start-low that precedes the data, so bit b occupies
         * slots 1 + 2b and 2 + 2b. When bit 0 is a 1 its first half is also
         * low and merges with the start-low, which is why the expansion above
         * must work in half-symbols rather than in pulses. */
        ASSERT_TRUE(ns >= 1 + 2 * SOMFY_RTS_FRAME_BITS);
        uint8_t got[SOMFY_RTS_FRAME_BYTES] = { 0 };
        for (int b = 0; b < SOMFY_RTS_FRAME_BITS; b++) {
            uint8_t first = slots[1 + 2 * b];
            uint8_t second = slots[2 + 2 * b];
            ASSERT_TRUE(first != second);
            int bit = (first == 0 && second == 1) ? 1 : 0;
            got[b / 8] |= (uint8_t)(bit << (7 - (b % 8)));
        }
        ASSERT_MEM_EQ(wire, got, SOMFY_RTS_FRAME_BYTES);
        frames_checked++;
    }

    ASSERT_EQ(repeats + 1, frames_checked);
    free(p);
}

void run_somfy_rts_tests(void)
{
    RUN(test_golden_vector);
    RUN(test_round_trip);
    RUN(test_decode_rejects_corruption);
    RUN(test_pulses_alternate_and_start_high);
    RUN(test_pulses_demodulate_back_to_frame);
}
