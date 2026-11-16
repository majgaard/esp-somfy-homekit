/* Copy to somfy_config.local.h, which is ignored by git. */
#ifndef SOMFY_CONFIG_LOCAL_H
#define SOMFY_CONFIG_LOCAL_H

#define SHADE_COUNT 3
#define SHADE_NAMES { "Left Shade", "Middle Shade", "Right Shade" }

/* Replace with an unused 24-bit base address BEFORE pairing a new bridge.
 * Shades use base + index. Never change this on an already-paired bridge. */
#define SOMFY_ADDR_BASE 0x100000u

/* Measure travel between your fully open and chosen fully closed positions.
 * These shared defaults apply when a per-shade list below is omitted. */
#define SHADE_TRAVEL_OPEN_MS  17900
#define SHADE_TRAVEL_CLOSE_MS 17100

/* true requires My to be programmed at the desired closed position on EVERY
 * motor. See docs/bringup.md before operating the shades. */
#define SHADE_CLOSE_USES_FAVOURITE true
#define SHADE_OPEN_IS_HARD_LIMIT  true
#define SHADE_CLOSE_IS_HARD_LIMIT false

/* Optional settings per shade, in the same order as SHADE_NAMES. Every list
 * must contain exactly SHADE_COUNT entries. The TUI manages these lists. */
#define SHADE_TRAVEL_OPEN_MS_PER_SHADE { 17900, 17900, 17900 }
#define SHADE_TRAVEL_CLOSE_MS_PER_SHADE { 17100, 17100, 17100 }
#define SHADE_CLOSE_USES_FAVOURITE_PER_SHADE { true, true, true }
#define SHADE_OPEN_IS_HARD_LIMIT_PER_SHADE { true, true, true }
#define SHADE_CLOSE_IS_HARD_LIMIT_PER_SHADE { false, false, false }

/* Other settings (pins, transmit power, repeats, stop lead and My delay)
 * can also be overridden here. See somfy_config.h for the defaults. */

#endif /* SOMFY_CONFIG_LOCAL_H */
