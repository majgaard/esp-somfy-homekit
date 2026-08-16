/* Board and installation settings. */
#ifndef SOMFY_CONFIG_H
#define SOMFY_CONFIG_H

/* ESP32-C3 SuperMini. Avoid GPIO2, 8 and 9 (strapping and the onboard LED),
 * 18 and 19 (USB), and 20 and 21 (UART). */
#define PIN_SCK  4
#define PIN_MISO 5
#define PIN_MOSI 6
#define PIN_CSN  7
#define PIN_GDO0 10 /* RMT drives the OOK envelope on this pin */
#define PIN_GDO2 3  /* receive, later */

#define SHADE_COUNT 3

/* One virtual remote address per shade. Any 24-bit value works as long as it
 * stays stable, because the motor stores it when you pair. */
#define SOMFY_ADDR_BASE 0x100000u

#define SOMFY_TX_DBM  10
/* One frame plus this many repeats. Real remotes send 2 to 6. */
#define SOMFY_REPEATS 4

/* Measured on the living-room shades, 2026-08-15.
 * Down: 17.1 s from the top dock to the chosen bottom position.
 * Up:   17.9 s from that bottom position back to the dock. */
#define SHADE_TRAVEL_OPEN_MS  17900
#define SHADE_TRAVEL_CLOSE_MS 17100

/* The top is a dock the motor knows, so we let it stop itself there and the
 * position estimate re-calibrates.
 *
 * The bottom is a position we chose, not a motor limit, so we must send Stop
 * ourselves. Never set this true unless the motor really does stop there: a
 * Stop that lands after the motor already halted is read as "go to the
 * favourite position", and the shade wanders off. */
#define SHADE_OPEN_IS_HARD_LIMIT  true
#define SHADE_CLOSE_IS_HARD_LIMIT false

/* Send Stop this early, to cover the time before the motor acts on the first
 * frame. About 130 ms of that is the wake-up pulse and the first frame. Tune
 * once the radio is fitted. */
#define SHADE_STOP_LEAD_MS 150

/* Set true once the My position is programmed on every shade, at the position
 * we call fully closed.
 *
 * Closing then becomes a single My from a standstill. The motor drives itself
 * to that position and parks, so there is no timing on our side and no Stop
 * that can be lost. "Close the blinds" cannot overshoot into the furniture.
 *
 * To program it: drive the shade to the wanted position with the existing
 * remote, then hold MY for about 5 s until the shade jogs. Do all three, then
 * flip this to true. */
#define SHADE_CLOSE_USES_FAVOURITE false

/* Gap between the Stop and the follow-up My when a close interrupts a move.
 * Must outlast a transmission, which is about 700 ms. */
#define SHADE_FAVOURITE_DELAY_MS 1200

#endif /* SOMFY_CONFIG_H */
