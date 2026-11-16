/* Board and installation settings. */
#ifndef SOMFY_CONFIG_H
#define SOMFY_CONFIG_H

/* Optional installation overrides, kept out of git. Copy the example next to
 * this file to somfy_config.local.h. Defaults below preserve the original
 * three-shade installation. */
#if __has_include("somfy_config.local.h")
#include "somfy_config.local.h"
#endif

/* ESP32-C3 SuperMini. Avoid GPIO2, 8 and 9 (strapping and the onboard LED),
 * 18 and 19 (USB), and 20 and 21 (UART). */
#ifndef PIN_SCK
#define PIN_SCK  4
#endif
#ifndef PIN_MISO
#define PIN_MISO 5
#endif
#ifndef PIN_MOSI
#define PIN_MOSI 6
#endif
#ifndef PIN_CSN
#define PIN_CSN  7
#endif
#ifndef PIN_GDO0
#define PIN_GDO0 10 /* RMT drives the OOK envelope on this pin */
#endif
#ifndef PIN_GDO2
#define PIN_GDO2 3  /* receive, later */
#endif

#ifndef SHADE_COUNT
#define SHADE_COUNT 3
#endif
#ifndef SHADE_NAMES
#define SHADE_NAMES { "Left Shade", "Middle Shade", "Right Shade" }
#endif

/* One virtual remote address per shade. Pick an unused 24-bit range for a new
 * bridge. Keep it stable after pairing, and never share it between bridges. */
#ifndef SOMFY_ADDR_BASE
#define SOMFY_ADDR_BASE 0x100000u
#endif

#ifndef SOMFY_TX_DBM
#define SOMFY_TX_DBM  10
#endif
/* One frame plus this many repeats. Real remotes send 2 to 6. */
#ifndef SOMFY_REPEATS
#define SOMFY_REPEATS 4
#endif

/* Shared defaults: 17.9 s from closed to the top dock, 17.1 s from the top
 * to the chosen closed position. Optional *_PER_SHADE lists override timing,
 * favourite mode and limits by accessory index; see shade_settings.h and the
 * local example. Measure both directions for each motor. */
#ifndef SHADE_TRAVEL_OPEN_MS
#define SHADE_TRAVEL_OPEN_MS  17900
#endif
#ifndef SHADE_TRAVEL_CLOSE_MS
#define SHADE_TRAVEL_CLOSE_MS 17100
#endif

/* The top is a dock the motor knows, so we let it stop itself there.
 * The chosen bottom is not a physical motor limit. My close mode below lets
 * the motor stop at its favourite; without that mode, false selects a timed
 * Stop. Only set a hard-limit flag when the motor really stops at that end.
 * A My/Stop sent after the motor has halted can move it to its favourite. */
#ifndef SHADE_OPEN_IS_HARD_LIMIT
#define SHADE_OPEN_IS_HARD_LIMIT  true
#endif
#ifndef SHADE_CLOSE_IS_HARD_LIMIT
#define SHADE_CLOSE_IS_HARD_LIMIT false
#endif

/* Send Stop this early, to cover the time before the motor acts on the first
 * frame. About 130 ms of that is the wake-up pulse and the first frame. Tune
 * for the actual motor and radio. */
#ifndef SHADE_STOP_LEAD_MS
#define SHADE_STOP_LEAD_MS 150
#endif

/* Set true once the My position is programmed on every shade, at the position
 * we call fully closed.
 *
 * Closing then becomes a single My from a standstill. The motor drives itself
 * to that position and parks. Position reporting is still an estimate; RTS
 * gives no acknowledgement that the motor received the command.
 *
 * To program it: drive the shade to the wanted position with the existing
 * remote, then hold MY for about 5 s until the shade jogs. Do all three, then
 * flip this to true. */
#ifndef SHADE_CLOSE_USES_FAVOURITE
#define SHADE_CLOSE_USES_FAVOURITE true
#endif

/* Gap between the Stop and the follow-up My when a close interrupts a move.
 * Must outlast a transmission, which is about 700 ms. */
#ifndef SHADE_FAVOURITE_DELAY_MS
#define SHADE_FAVOURITE_DELAY_MS 1200
#endif

#if SHADE_COUNT < 1 || SHADE_COUNT > 255
#error "SHADE_COUNT must be between 1 and 255"
#endif
#if SOMFY_ADDR_BASE < 1 || SOMFY_ADDR_BASE > (0xFFFFFFu - (SHADE_COUNT - 1))
#error "Virtual remote addresses must fit in the nonzero 24-bit range"
#endif
#if SHADE_TRAVEL_OPEN_MS < 1 || SHADE_TRAVEL_CLOSE_MS < 1
#error "Shade travel times must be positive"
#endif
/* The radio pulse buffer fits at most five repeats plus the initial frame. */
#if SOMFY_REPEATS < 0 || SOMFY_REPEATS > 5
#error "SOMFY_REPEATS must be between 0 and 5"
#endif

#endif /* SOMFY_CONFIG_H */
