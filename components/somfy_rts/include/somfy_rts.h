/*
 * Somfy RTS 56-bit frame encoder and OOK envelope generator.
 *
 * This file has no dependency on ESP-IDF or on any radio driver. It builds a
 * list of level/duration pairs. A transport (CC1101 in async transparent mode,
 * or a test harness) plays that list.
 *
 * Protocol constants come from rstrouse/ESPSomfy-RTS (Unlicense / public
 * domain), file Somfy.cpp, functions somfy_frame_t::encodeFrame and
 * Transceiver::sendFrame. See docs/protocol.md.
 */
#ifndef SOMFY_RTS_H
#define SOMFY_RTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOMFY_RTS_FRAME_BYTES 7
#define SOMFY_RTS_FRAME_BITS  56

/* Half-symbol period. One Manchester bit is two half-symbols. */
#define SOMFY_RTS_SYMBOL_US       640
/* Wake-up pulse. These are the measured values from ESPSomfy-RTS, not the
 * 9415/89565 pair that most online references give. */
#define SOMFY_RTS_WAKEUP_HIGH_US  10920
#define SOMFY_RTS_WAKEUP_LOW_US   7357
#define SOMFY_RTS_SW_SYNC_US      4850
#define SOMFY_RTS_IF_GAP_US       27434
/* Hardware sync pulse pairs: 2 on the first frame, 7 on each repeat. */
#define SOMFY_RTS_HW_SYNC_FIRST   2
#define SOMFY_RTS_HW_SYNC_REPEAT  7

typedef enum {
    SOMFY_CMD_MY         = 0x1,
    SOMFY_CMD_UP         = 0x2,
    SOMFY_CMD_MY_UP      = 0x3,
    SOMFY_CMD_DOWN       = 0x4,
    SOMFY_CMD_MY_DOWN    = 0x5,
    SOMFY_CMD_UP_DOWN    = 0x6,
    SOMFY_CMD_MY_UP_DOWN = 0x7,
    SOMFY_CMD_PROG       = 0x8,
    SOMFY_CMD_SUN_FLAG   = 0x9,
    SOMFY_CMD_FLAG       = 0xA,
} somfy_cmd_t;

typedef struct {
    /* Real remotes use 0xA0..0xAF and step the low nibble on each press. The
     * motor ignores the low nibble. */
    uint8_t     enc_key;
    somfy_cmd_t cmd;
    uint16_t    rolling_code;
    uint32_t    address; /* 24-bit remote address */
} somfy_rts_frame_t;

/* One segment of the OOK envelope. level 1 = carrier on. */
typedef struct {
    uint32_t us;
    uint8_t  level;
} somfy_pulse_t;

/* Build the 7-byte on-air frame: checksum, then the XOR obfuscation chain. */
void somfy_rts_encode(const somfy_rts_frame_t *in,
                      uint8_t out[SOMFY_RTS_FRAME_BYTES]);

/* Inverse of somfy_rts_encode. Returns false if the checksum does not match.
 * Used by the unit tests today and by the CC1101 receive path later. */
bool somfy_rts_decode(const uint8_t in[SOMFY_RTS_FRAME_BYTES],
                      somfy_rts_frame_t *out);

/* Worst-case pulse count for one transmission of 1 + repeats frames. */
size_t somfy_rts_pulse_capacity(uint8_t repeats);

/* Render the full transmission into out[]. Returns the number of pulses
 * written, or 0 if cap is too small. Adjacent equal levels are merged, so the
 * result always alternates. */
size_t somfy_rts_build_pulses(const somfy_rts_frame_t *frame, uint8_t repeats,
                              somfy_pulse_t *out, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* SOMFY_RTS_H */
