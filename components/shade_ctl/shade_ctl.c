#include "shade_ctl.h"

void shade_init(shade_t *s, const shade_cfg_t *cfg, uint16_t assumed_position)
{
    s->cfg        = *cfg;
    s->position   = assumed_position;
    s->target     = assumed_position;
    s->motion     = SHADE_MOTION_IDLE;
    s->last_ms    = 0;
    s->calibrated = false;
}

/* Move the estimate forward by the time since the last call. */
static void integrate(shade_t *s, uint32_t now_ms)
{
    uint32_t elapsed = now_ms - s->last_ms; /* wraps correctly on uint32 */
    s->last_ms = now_ms;

    if (s->motion == SHADE_MOTION_IDLE || elapsed == 0) {
        return;
    }

    uint32_t travel = (s->motion == SHADE_MOTION_OPENING)
                          ? s->cfg.travel_open_ms
                          : s->cfg.travel_close_ms;
    if (travel == 0) {
        return;
    }

    uint32_t delta = (uint32_t)((uint64_t)elapsed * SHADE_CLOSED / travel);

    if (s->motion == SHADE_MOTION_OPENING) {
        s->position = (delta >= s->position) ? SHADE_OPEN
                                             : (uint16_t)(s->position - delta);
    } else {
        uint32_t p = s->position + delta;
        s->position = (p >= SHADE_CLOSED) ? SHADE_CLOSED : (uint16_t)p;
    }
}

shade_action_t shade_tick(shade_t *s, uint32_t now_ms)
{
    integrate(s, now_ms);

    if (s->motion == SHADE_MOTION_IDLE) {
        return SHADE_ACT_NONE;
    }

    bool at_limit = (s->motion == SHADE_MOTION_OPENING && s->position == SHADE_OPEN) ||
                    (s->motion == SHADE_MOTION_CLOSING && s->position == SHADE_CLOSED);

    if (at_limit) {
        /* The motor stops itself at its own hard limit, so we send nothing.
         * The estimate now matches the real position. */
        s->motion     = SHADE_MOTION_IDLE;
        s->calibrated = true;
        return SHADE_ACT_NONE;
    }

    bool reached = (s->motion == SHADE_MOTION_OPENING && s->position <= s->target) ||
                   (s->motion == SHADE_MOTION_CLOSING && s->position >= s->target);

    if (reached) {
        s->motion = SHADE_MOTION_IDLE;
        return SHADE_ACT_STOP;
    }

    return SHADE_ACT_NONE;
}

shade_action_t shade_stop(shade_t *s, uint32_t now_ms)
{
    integrate(s, now_ms);
    s->motion = SHADE_MOTION_IDLE;
    s->target = s->position;
    return SHADE_ACT_STOP;
}

shade_action_t shade_open(shade_t *s, uint32_t now_ms)
{
    integrate(s, now_ms);
    s->target = SHADE_OPEN;
    s->motion = SHADE_MOTION_OPENING;
    return SHADE_ACT_UP;
}

shade_action_t shade_close(shade_t *s, uint32_t now_ms)
{
    integrate(s, now_ms);
    s->target = SHADE_CLOSED;
    s->motion = SHADE_MOTION_CLOSING;
    return SHADE_ACT_DOWN;
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
        s->motion = SHADE_MOTION_OPENING;
        return SHADE_ACT_UP;
    }
    s->motion = SHADE_MOTION_CLOSING;
    return SHADE_ACT_DOWN;
}
