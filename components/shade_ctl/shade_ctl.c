#include "shade_ctl.h"

void shade_init(shade_t *s, const shade_cfg_t *cfg, uint16_t assumed_position,
                uint32_t now_ms)
{
    if (assumed_position > SHADE_CLOSED) {
        assumed_position = SHADE_CLOSED;
    }
    s->cfg            = *cfg;
    s->position       = assumed_position;
    s->target         = assumed_position;
    s->motion         = SHADE_MOTION_IDLE;
    s->move_start_ms  = now_ms;
    s->move_start_pos = assumed_position;
    s->calibrated     = false;
}

/* Derive the position from the time the move began, never from a running sum.
 * Summing per-tick increments throws away the integer-division remainder on
 * every tick. The caller ticks as fast as its main loop runs, so at a 1 ms
 * tick over a 17 s travel every increment truncates to zero and the shade
 * never moves at all. */
static void integrate(shade_t *s, uint32_t now_ms)
{
    if (s->motion == SHADE_MOTION_IDLE) {
        return;
    }

    uint32_t travel = (s->motion == SHADE_MOTION_OPENING)
                          ? s->cfg.travel_open_ms
                          : s->cfg.travel_close_ms;
    if (travel == 0) {
        return;
    }

    uint32_t elapsed = now_ms - s->move_start_ms; /* wraps correctly */
    uint32_t delta   = (uint32_t)((uint64_t)elapsed * SHADE_CLOSED / travel);

    if (s->motion == SHADE_MOTION_OPENING) {
        s->position = (delta >= s->move_start_pos)
                          ? SHADE_OPEN
                          : (uint16_t)(s->move_start_pos - delta);
    } else {
        uint32_t p  = (uint32_t)s->move_start_pos + delta;
        s->position = (p >= SHADE_CLOSED) ? SHADE_CLOSED : (uint16_t)p;
    }
}

/* Rebase the move so the next integrate() measures from here. */
static void begin_move(shade_t *s, shade_motion_t motion, uint32_t now_ms)
{
    s->motion         = motion;
    s->move_start_ms  = now_ms;
    s->move_start_pos = s->position;
}

/* Distance the shade covers while the radio gets the first frame out. */
static uint32_t lead_distance(const shade_t *s)
{
    uint32_t travel = (s->motion == SHADE_MOTION_OPENING)
                          ? s->cfg.travel_open_ms
                          : s->cfg.travel_close_ms;
    if (travel == 0 || s->cfg.stop_lead_ms == 0) {
        return 0;
    }
    return (uint32_t)((uint64_t)s->cfg.stop_lead_ms * SHADE_CLOSED / travel);
}

shade_action_t shade_tick(shade_t *s, uint32_t now_ms)
{
    integrate(s, now_ms);

    if (s->motion == SHADE_MOTION_IDLE) {
        return SHADE_ACT_NONE;
    }

    bool opening = (s->motion == SHADE_MOTION_OPENING);
    uint16_t end = opening ? SHADE_OPEN : SHADE_CLOSED;

    /* Heading for an end the motor knows about? Let it stop itself. Sending
     * Stop here would arrive after the motor halted and be read as "go to the
     * favourite position". */
    bool into_hard_stop =
        (s->target == end) &&
        (opening ? s->cfg.open_is_hard_limit : s->cfg.close_is_hard_limit);

    if (into_hard_stop) {
        if (s->position == end) {
            s->motion     = SHADE_MOTION_IDLE;
            s->calibrated = true;
        }
        return SHADE_ACT_NONE;
    }

    int32_t lead = (int32_t)lead_distance(s);
    int32_t pos  = (int32_t)s->position;
    int32_t want = (int32_t)s->target;

    bool reached = opening ? (pos - lead <= want) : (pos + lead >= want);

    if (reached) {
        s->motion = SHADE_MOTION_IDLE;
        return SHADE_ACT_STOP;
    }

    return SHADE_ACT_NONE;
}

shade_action_t shade_stop(shade_t *s, uint32_t now_ms)
{
    integrate(s, now_ms);
    begin_move(s, SHADE_MOTION_IDLE, now_ms);
    s->target = s->position;
    return SHADE_ACT_STOP;
}

shade_action_t shade_open(shade_t *s, uint32_t now_ms)
{
    integrate(s, now_ms);
    s->target = SHADE_OPEN;
    begin_move(s, SHADE_MOTION_OPENING, now_ms);
    return SHADE_ACT_UP;
}

shade_action_t shade_close(shade_t *s, uint32_t now_ms)
{
    integrate(s, now_ms);
    s->target = SHADE_CLOSED;
    begin_move(s, SHADE_MOTION_CLOSING, now_ms);
    return SHADE_ACT_DOWN;
}

uint8_t shade_to_hk_position(uint16_t position)
{
    if (position > SHADE_CLOSED) {
        position = SHADE_CLOSED;
    }
    /* Round to the nearest whole percent, then flip the sense. */
    return (uint8_t)(100u - ((position + 50u) / 100u));
}

uint16_t shade_from_hk_position(uint8_t hk_percent)
{
    if (hk_percent > 100) {
        hk_percent = 100;
    }
    return (uint16_t)((100u - hk_percent) * 100u);
}

uint8_t shade_hk_position_state(const shade_t *s)
{
    switch (s->motion) {
    /* We open, so the HomeKit number grows. */
    case SHADE_MOTION_OPENING: return SHADE_HK_INCREASING;
    case SHADE_MOTION_CLOSING: return SHADE_HK_DECREASING;
    default:                   return SHADE_HK_STOPPED;
    }
}

shade_action_t shade_go_to(shade_t *s, uint16_t target, uint32_t now_ms)
{
    if (target > SHADE_CLOSED) {
        target = SHADE_CLOSED;
    }
    if (target == SHADE_OPEN) {
        return shade_open(s, now_ms);
    }
    if (target == SHADE_CLOSED) {
        return shade_close(s, now_ms);
    }

    integrate(s, now_ms);

    int32_t diff = (int32_t)target - (int32_t)s->position;
    if (diff > -SHADE_DEADBAND && diff < SHADE_DEADBAND) {
        /* Already close enough. Stop only if the shade still moves. */
        if (s->motion != SHADE_MOTION_IDLE) {
            return shade_stop(s, now_ms);
        }
        return SHADE_ACT_NONE;
    }

    s->target = target;
    if (diff < 0) {
        begin_move(s, SHADE_MOTION_OPENING, now_ms);
        return SHADE_ACT_UP;
    }
    begin_move(s, SHADE_MOTION_CLOSING, now_ms);
    return SHADE_ACT_DOWN;
}
