/* Radio glue: CC1101 over SPI, envelope over RMT, rolling codes in NVS. */
#ifndef SOMFY_RADIO_H
#define SOMFY_RADIO_H

#include <stdbool.h>
#include <stdint.h>

#include "somfy_rts.h"

/* Brings up SPI, the CC1101 and the RMT channel. Returns false if the CC1101
 * does not answer, which almost always means the wiring is wrong. */
bool somfy_radio_begin(void);
bool somfy_radio_ok(void);

/* Steps the rolling code, persists it, then transmits. */
bool somfy_send(uint8_t shade, somfy_cmd_t cmd);

uint16_t somfy_rolling_code(uint8_t shade);
uint32_t somfy_address(uint8_t shade);

/* Read the CC1101 config back and print it. Returns the mismatch count. */
int somfy_radio_verify(void);

#endif /* SOMFY_RADIO_H */
