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

/* What the caller must send on the radio.
 *
 * STOP and FAVOURITE are the same My command on the air. The motor decides
 * which one it is: My stops a shade that is moving, and sends a shade that is
 * already stopped to its favourite position. The two names keep our intent
 * clear, and remind the caller that ordering matters. */
typedef enum {
    SHADE_ACT_NONE = 0,
    SHADE_ACT_UP,
    SHADE_ACT_DOWN,
    SHADE_ACT_STOP,
    SHADE_ACT_FAVOURITE,
} shade_action_t;

typedef struct {
    uint32_t travel_open_ms;  /* full closed -> full open */
    uint32_t travel_close_ms; /* full open  -> full closed */

    /* True when the motor has its own end stop at that end. We then let it
     * run into the stop and send nothing, and the estimate re-calibrates
     * against the real position.
     *
     * False when the end is a position we chose rather than one the motor
     * knows. We must then send Stop ourselves, and nothing calibrates.
     *
     * Getting this backwards is worse than it sounds. On an RTS motor, My
     * stops a shade that is moving, but a shade that has already stopped
     * treats My as "go to the favourite position". A Stop sent after the
     * motor reached its own limit therefore sends the shade somewhere else. */
    bool open_is_hard_limit;
    bool close_is_hard_limit;

    /* How early to send Stop, to cover the time the radio needs before the
     * motor acts on the first frame. Zero disables the compensation. */
    uint32_t stop_lead_ms;

    /* Set true once the motor's favourite (My) position is programmed at the
     * position we call fully closed. Closing then becomes a single My from a
     * standstill: the motor drives itself there and stops itself, so there is
     * no timing on our side and no Stop to lose in the air.
     *
     * RTS has no acknowledgements. Ordering assumes the motor's motion agrees
     * with the estimate; a lost command or an external remote can break that
     * assumption.
     *
     * Implies close_is_hard_limit, and shade_init enforces that. */
    bool close_uses_favourite;

    /* Gap between the Stop and the follow-up My when a close interrupts a
     * move. Must outlast the transmission, or the motor is still moving when
     * the second My lands and simply stops again. */
    uint32_t favourite_delay_ms;
} shade_cfg_t;

typedef struct {
    shade_cfg_t    cfg;
    uint16_t       position;
    uint16_t       target;
    shade_motion_t motion;
    /* Where and when the current move began. Position is derived from these,
     * never accumulated tick by tick. */
    uint32_t       move_start_ms;
    uint16_t       move_start_pos;
    /* A close interrupted a move, so a Stop went out and the My that means
     * "go to the favourite" still has to follow. */
    bool           favourite_pending;
    uint32_t       favourite_at_ms;
    /* False until the shade drives into a hard limit. Before that the position
     * is a guess, because we cannot know where the shade sat at boot. */
    bool           calibrated;
} shade_t;

void shade_init(shade_t *s, const shade_cfg_t *cfg, uint16_t assumed_position,
                uint32_t now_ms);

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

/* --- HomeKit conversion -------------------------------------------------
 *
 * Careful: HomeKit WindowCovering counts the opposite way to this module.
 * HomeKit uses whole percent with 100 = fully open and 0 = fully closed. We
 * use hundredths with 0 = fully open and 10000 = fully closed. Every value
 * that crosses the HomeKit boundary must go through these two functions.
 */

/* HomeKit PositionState values. */
#define SHADE_HK_DECREASING 0 /* moves toward fully closed */
#define SHADE_HK_INCREASING 1 /* moves toward fully open */
#define SHADE_HK_STOPPED    2

uint8_t  shade_to_hk_position(uint16_t position);
uint16_t shade_from_hk_position(uint8_t hk_percent);
uint8_t  shade_hk_position_state(const shade_t *s);

#ifdef __cplusplus
}
#endif

#endif /* SHADE_CTL_H */
