/*
 * Time-based position model for a Somfy RTS roller shade.
 *
 * Somfy RTS is one-way. The motor sends no position back. This module
 * estimates the position from the elapsed travel time, and emits the radio
 * action the caller must transmit. It holds no radio and no clock of its own,
 * so it runs unchanged on the host under test.
 *
 * Position uses the Matter WindowCovering convention:
 * hundredths of a percent, 0 = fully open, 10000 = fully closed.
 */
#ifndef SHADE_CTL_H
#define SHADE_CTL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHADE_OPEN   0
#define SHADE_CLOSED 10000

/* Targets closer than this to the current position are ignored. One full
 * travel is about 7 s, so 1% is about 70 ms of motor run. */
#define SHADE_DEADBAND 100

typedef enum {
    SHADE_MOTION_IDLE = 0,
    SHADE_MOTION_OPENING,
    SHADE_MOTION_CLOSING,
} shade_motion_t;

/* What the caller must send on the radio. */
typedef enum {
    SHADE_ACT_NONE = 0,
    SHADE_ACT_UP,
    SHADE_ACT_DOWN,
    SHADE_ACT_STOP,
} shade_action_t;

typedef struct {
    uint32_t travel_open_ms;  /* full closed -> full open */
    uint32_t travel_close_ms; /* full open  -> full closed */
} shade_cfg_t;

typedef struct {
    shade_cfg_t    cfg;
    uint16_t       position;
    uint16_t       target;
    shade_motion_t motion;
    uint32_t       last_ms;
    /* False until the shade drives into a hard limit. Before that the position
     * is a guess, because we cannot know where the shade sat at boot. */
    bool           calibrated;
} shade_t;

void shade_init(shade_t *s, const shade_cfg_t *cfg, uint16_t assumed_position);

/* Drive fully open. Runs into the hard limit, which re-calibrates. */
shade_action_t shade_open(shade_t *s, uint32_t now_ms);
/* Drive fully closed. Runs into the hard limit, which re-calibrates. */
shade_action_t shade_close(shade_t *s, uint32_t now_ms);
/* Stop where it is. */
shade_action_t shade_stop(shade_t *s, uint32_t now_ms);
/* Drive to a position in hundredths of a percent. */
shade_action_t shade_go_to(shade_t *s, uint16_t target, uint32_t now_ms);

/* Advance the estimate. Call this often, at least every 100 ms while the
 * shade moves. Returns SHADE_ACT_STOP when an intermediate target arrives. */
shade_action_t shade_tick(shade_t *s, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* SHADE_CTL_H */
