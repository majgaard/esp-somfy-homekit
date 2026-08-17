#include "shade_ctl.h"
#include "test_util.h"

#define TRAVEL_MS 7000

/* Both ends are motor limits, and no lead compensation. */
static shade_cfg_t cfg = {
    .travel_open_ms      = TRAVEL_MS,
    .travel_close_ms     = TRAVEL_MS,
    .open_is_hard_limit  = true,
    .close_is_hard_limit = true,
    .stop_lead_ms        = 0,
};

/* Run the tick loop forward, and report the action the model asked for. */
static shade_action_t run_for(shade_t *s, uint32_t *now, uint32_t ms)
{
    shade_action_t last = SHADE_ACT_NONE;
    for (uint32_t t = 0; t < ms; t += 50) {
        *now += 50;
        shade_action_t a = shade_tick(s, *now);
        if (a != SHADE_ACT_NONE) {
            last = a;
        }
    }
    return last;
}

static void test_close_runs_to_limit_and_calibrates(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN, now);

    ASSERT_EQ(SHADE_ACT_DOWN, shade_close(&s, now));
    shade_action_t a = run_for(&s, &now, TRAVEL_MS + 500);

    /* No stop is sent. The motor stops itself at the hard limit. */
    ASSERT_EQ(SHADE_ACT_NONE, a);
    ASSERT_EQ(SHADE_CLOSED, s.position);
    ASSERT_EQ(SHADE_MOTION_IDLE, s.motion);
    ASSERT_TRUE(s.calibrated);
}

static void test_open_runs_to_limit(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_CLOSED, now);

    ASSERT_EQ(SHADE_ACT_UP, shade_open(&s, now));
    run_for(&s, &now, TRAVEL_MS + 500);

    ASSERT_EQ(SHADE_OPEN, s.position);
    ASSERT_TRUE(s.calibrated);
}

static void test_go_to_half_stops(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN, now);

    ASSERT_EQ(SHADE_ACT_DOWN, shade_go_to(&s, 5000, now));

    /* Halfway is 3.5 s of travel. Nothing should fire before that. */
    ASSERT_EQ(SHADE_ACT_NONE, run_for(&s, &now, 3000));
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 1000));

    /* One 50 ms tick of overshoot is 0.71%. */
    ASSERT_NEAR(5000, s.position, 100);
    ASSERT_EQ(SHADE_MOTION_IDLE, s.motion);
}

static void test_go_to_upward_sends_up(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_CLOSED, now);

    ASSERT_EQ(SHADE_ACT_UP, shade_go_to(&s, 2500, now));
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 6000));
    ASSERT_NEAR(2500, s.position, 100);
}

static void test_stop_freezes_position(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN, now);

    shade_close(&s, now);
    run_for(&s, &now, 3500);
    ASSERT_EQ(SHADE_ACT_STOP, shade_stop(&s, now));

    uint16_t frozen = s.position;
    ASSERT_NEAR(5000, frozen, 100);

    run_for(&s, &now, 5000);
    ASSERT_EQ(frozen, s.position);
    ASSERT_EQ(SHADE_MOTION_IDLE, s.motion);
}

static void test_reverse_mid_travel(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN, now);

    shade_close(&s, now);
    run_for(&s, &now, 3500); /* about 50% */

    ASSERT_EQ(SHADE_ACT_UP, shade_go_to(&s, 2000, now));
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 3000));
    ASSERT_NEAR(2000, s.position, 100);
}

static void test_deadband_ignores_tiny_move(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, 5000, now);

    /* 0.5% away is inside the 1% deadband. */
    ASSERT_EQ(SHADE_ACT_NONE, shade_go_to(&s, 5050, now));
    ASSERT_EQ(SHADE_MOTION_IDLE, s.motion);

    /* 2% away is outside it. */
    ASSERT_EQ(SHADE_ACT_DOWN, shade_go_to(&s, 5200, now));
}

static void test_asymmetric_travel_times(void)
{
    shade_cfg_t asym = {
        .travel_open_ms      = 9000,
        .travel_close_ms     = 6000,
        .open_is_hard_limit  = true,
        .close_is_hard_limit = true,
    };
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &asym, SHADE_OPEN, now);

    shade_go_to(&s, 5000, now);
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 3100)); /* 3.0 s of 6.0 s */
    ASSERT_NEAR(5000, s.position, 150);

    shade_go_to(&s, 2500, now);
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 2350)); /* 2.25 s of 9.0 s */
    ASSERT_NEAR(2500, s.position, 150);
}

static void test_not_calibrated_until_a_limit_is_hit(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, 5000, now);

    shade_go_to(&s, 7000, now);
    run_for(&s, &now, 2000);
    ASSERT_TRUE(!s.calibrated);

    shade_close(&s, now);
    run_for(&s, &now, TRAVEL_MS);
    ASSERT_TRUE(s.calibrated);
}

/* HomeKit counts the opposite way to this module. Pin both ends. */
static void test_homekit_position_is_inverted(void)
{
    ASSERT_EQ(100, shade_to_hk_position(SHADE_OPEN));   /* open  -> 100 */
    ASSERT_EQ(0,   shade_to_hk_position(SHADE_CLOSED)); /* closed -> 0  */
    ASSERT_EQ(50,  shade_to_hk_position(5000));
    ASSERT_EQ(75,  shade_to_hk_position(2500));
    ASSERT_EQ(25,  shade_to_hk_position(7500));

    ASSERT_EQ(SHADE_OPEN,   shade_from_hk_position(100));
    ASSERT_EQ(SHADE_CLOSED, shade_from_hk_position(0));
    ASSERT_EQ(5000,         shade_from_hk_position(50));
    ASSERT_EQ(2500,         shade_from_hk_position(75));

    /* Out-of-range input must clamp, not wrap. */
    ASSERT_EQ(SHADE_OPEN, shade_from_hk_position(200));
    ASSERT_EQ(0, shade_to_hk_position(60000));
}

static void test_homekit_position_round_trip(void)
{
    for (int pct = 0; pct <= 100; pct++) {
        uint16_t internal = shade_from_hk_position((uint8_t)pct);
        ASSERT_EQ(pct, shade_to_hk_position(internal));
    }
    /* Rounding must go to the nearest percent, never truncate toward closed. */
    ASSERT_EQ(50, shade_to_hk_position(4951));
    ASSERT_EQ(51, shade_to_hk_position(4949));
}

static void test_homekit_position_state(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN, now);

    ASSERT_EQ(SHADE_HK_STOPPED, shade_hk_position_state(&s));

    shade_close(&s, now);
    /* We close, so the HomeKit number falls. */
    ASSERT_EQ(SHADE_HK_DECREASING, shade_hk_position_state(&s));

    shade_stop(&s, now);
    ASSERT_EQ(SHADE_HK_STOPPED, shade_hk_position_state(&s));

    shade_open(&s, now);
    ASSERT_EQ(SHADE_HK_INCREASING, shade_hk_position_state(&s));
}

/* End to end in HomeKit units: ask for 30% open and check we drive the right
 * way and land on the right number. */
static void test_homekit_target_drives_correct_direction(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN, now); /* HomeKit reads 100 */
    ASSERT_EQ(100, shade_to_hk_position(s.position));

    /* 30% open is mostly closed, so the shade must go down. */
    ASSERT_EQ(SHADE_ACT_DOWN,
              shade_go_to(&s, shade_from_hk_position(30), now));
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 6000));
    ASSERT_NEAR(30, shade_to_hk_position(s.position), 1);

    /* 80% open is more open, so the shade must go up. */
    ASSERT_EQ(SHADE_ACT_UP,
              shade_go_to(&s, shade_from_hk_position(80), now));
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 6000));
    ASSERT_NEAR(80, shade_to_hk_position(s.position), 1);
}

/* The real living-room setup: the top is a dock the motor knows, the bottom is
 * a point we chose. Closing must end with our own Stop. */
static shade_cfg_t soft_bottom = {
    .travel_open_ms      = 17900,
    .travel_close_ms     = 17100,
    .open_is_hard_limit  = true,
    .close_is_hard_limit = false,
    .stop_lead_ms        = 0,
};

static void test_soft_close_end_sends_stop(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &soft_bottom, SHADE_OPEN, now);

    ASSERT_EQ(SHADE_ACT_DOWN, shade_close(&s, now));
    ASSERT_EQ(SHADE_ACT_NONE, run_for(&s, &now, 16000));
    /* We must stop it ourselves, because the motor would keep going. */
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 2000));
    ASSERT_EQ(SHADE_CLOSED, s.position);
    ASSERT_EQ(SHADE_MOTION_IDLE, s.motion);
    /* Nothing about a timed stop tells us where the shade really is. */
    ASSERT_TRUE(!s.calibrated);
}

static void test_hard_open_end_still_sends_nothing(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &soft_bottom, SHADE_CLOSED, now);

    ASSERT_EQ(SHADE_ACT_UP, shade_open(&s, now));
    /* The top is a dock, so the motor stops itself and we stay quiet. */
    ASSERT_EQ(SHADE_ACT_NONE, run_for(&s, &now, 18500));
    ASSERT_EQ(SHADE_OPEN, s.position);
    ASSERT_TRUE(s.calibrated);
}

/* Tick until Stop, and report how long that took. */
static uint32_t ms_until_stop(shade_t *s, uint32_t *now, uint32_t limit_ms)
{
    for (uint32_t t = 0; t < limit_ms; t += 10) {
        *now += 10;
        if (shade_tick(s, *now) == SHADE_ACT_STOP) {
            return t + 10;
        }
    }
    return limit_ms;
}

/* Stop must go out early enough that the shade coasts onto the target rather
 * than past it. The lead shows up in the timing, not in the final position:
 * the whole point is that the shade ends up on the target. */
static void test_stop_lead_fires_early(void)
{
    shade_cfg_t lead = {
        .travel_open_ms      = TRAVEL_MS,
        .travel_close_ms     = TRAVEL_MS,
        .open_is_hard_limit  = true,
        .close_is_hard_limit = true,
        .stop_lead_ms        = 700, /* 10% of a 7 s travel */
    };
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &lead, SHADE_OPEN, now);

    shade_go_to(&s, 5000, now);
    uint32_t took = ms_until_stop(&s, &now, 6000);

    /* 50% of a 7 s travel is 3500 ms. The 700 ms lead brings it to 2800 ms. */
    ASSERT_NEAR(2800, took, 60);
    /* And the estimate settles on the target, not short of it. */
    ASSERT_EQ(5000, s.position);
}

/* Without a lead the Stop goes out at the target, and the estimate still
 * settles exactly on it. */
static void test_no_lead_stops_on_time(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN, now);

    shade_go_to(&s, 5000, now);
    uint32_t took = ms_until_stop(&s, &now, 6000);

    ASSERT_NEAR(3500, took, 60);
    ASSERT_EQ(5000, s.position);
}

/* HomeKit shows "Closing..." for as long as CurrentPosition differs from
 * TargetPosition, so every path that ends a move must land the two together. */
static void test_position_always_converges_on_target(void)
{
    const uint16_t targets[] = { 500, 2500, 5000, 7500, 9500 };
    shade_cfg_t lead = {
        .travel_open_ms      = 17900,
        .travel_close_ms     = 17100,
        .open_is_hard_limit  = true,
        .close_is_hard_limit = false,
        .stop_lead_ms        = 150,
    };

    for (size_t i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        shade_t s;
        uint32_t now = 1000;
        shade_init(&s, &lead, SHADE_OPEN, now);

        shade_go_to(&s, targets[i], now);
        run_for(&s, &now, 20000);

        ASSERT_EQ(targets[i], s.position);
        ASSERT_EQ(shade_to_hk_position(targets[i]),
                  shade_to_hk_position(s.position));
        ASSERT_EQ(SHADE_HK_STOPPED, shade_hk_position_state(&s));
    }
}

/* A target inside the deadband must still converge, or the tile never
 * settles even though nothing needs to move. */
static void test_deadband_target_still_converges(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, 5000, now);

    ASSERT_EQ(SHADE_ACT_NONE, shade_go_to(&s, 5050, now));
    ASSERT_EQ(5050, s.position);
    ASSERT_EQ(5050, s.target);
    ASSERT_EQ(SHADE_HK_STOPPED, shade_hk_position_state(&s));
}

/* With the motor's favourite programmed at our fully-closed position, closing
 * becomes motor-managed: exact, and with nothing to lose in the air. */
static shade_cfg_t favourite = {
    .travel_open_ms       = 17900,
    .travel_close_ms      = 17100,
    .open_is_hard_limit   = true,
    .close_is_hard_limit  = false, /* shade_init must force this true */
    .stop_lead_ms         = 0,
    .close_uses_favourite = true,
    .favourite_delay_ms   = 1000,
};

static void test_favourite_implies_a_hard_close_end(void)
{
    shade_t s;
    shade_init(&s, &favourite, SHADE_OPEN, 1000);
    ASSERT_TRUE(s.cfg.close_is_hard_limit);
}

static void test_favourite_close_from_standstill(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &favourite, SHADE_OPEN, now);

    /* One My, and the motor does the rest. No Down, no timed Stop. */
    ASSERT_EQ(SHADE_ACT_FAVOURITE, shade_close(&s, now));
    ASSERT_EQ(SHADE_ACT_NONE, run_for(&s, &now, 18000));
    ASSERT_EQ(SHADE_CLOSED, s.position);
    ASSERT_EQ(SHADE_MOTION_IDLE, s.motion);
    /* The motor parked itself, so we know exactly where the shade is. */
    ASSERT_TRUE(s.calibrated);
}

static void test_favourite_close_while_moving_stops_first(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &favourite, SHADE_CLOSED, now);

    shade_open(&s, now);
    run_for(&s, &now, 4000);

    /* My on a moving shade only stops it, so that is all this can be. */
    ASSERT_EQ(SHADE_ACT_STOP, shade_close(&s, now));
    ASSERT_EQ(SHADE_MOTION_IDLE, s.motion);
    ASSERT_TRUE(s.favourite_pending);

    /* Nothing more until the shade has actually halted. */
    ASSERT_EQ(SHADE_ACT_NONE, run_for(&s, &now, 900));
    /* Then the real My follows. */
    ASSERT_EQ(SHADE_ACT_FAVOURITE, run_for(&s, &now, 300));
    ASSERT_TRUE(!s.favourite_pending);

    ASSERT_EQ(SHADE_ACT_NONE, run_for(&s, &now, 18000));
    ASSERT_EQ(SHADE_CLOSED, s.position);
    ASSERT_TRUE(s.calibrated);
}

static void test_new_command_cancels_a_pending_favourite(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &favourite, SHADE_CLOSED, now);

    shade_open(&s, now);
    run_for(&s, &now, 4000);
    ASSERT_EQ(SHADE_ACT_STOP, shade_close(&s, now));
    ASSERT_TRUE(s.favourite_pending);

    /* Change your mind before the follow-up goes out. */
    ASSERT_EQ(SHADE_ACT_UP, shade_open(&s, now));
    ASSERT_TRUE(!s.favourite_pending);
    /* The My must never arrive late and drag the shade to the bottom. */
    ASSERT_EQ(SHADE_ACT_NONE, run_for(&s, &now, 3000));
    ASSERT_EQ(SHADE_MOTION_OPENING, s.motion);
}

static void test_favourite_reached_via_go_to_zero_percent(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &favourite, SHADE_OPEN, now);

    /* "Close the blinds" in HomeKit is TargetPosition 0. */
    ASSERT_EQ(SHADE_ACT_FAVOURITE,
              shade_go_to(&s, shade_from_hk_position(0), now));
    run_for(&s, &now, 18000);
    ASSERT_EQ(0, shade_to_hk_position(s.position));
    ASSERT_TRUE(s.calibrated);
}

void run_shade_ctl_tests(void)
{
    RUN(test_favourite_implies_a_hard_close_end);
    RUN(test_favourite_close_from_standstill);
    RUN(test_favourite_close_while_moving_stops_first);
    RUN(test_new_command_cancels_a_pending_favourite);
    RUN(test_favourite_reached_via_go_to_zero_percent);
    RUN(test_soft_close_end_sends_stop);
    RUN(test_hard_open_end_still_sends_nothing);
    RUN(test_stop_lead_fires_early);
    RUN(test_no_lead_stops_on_time);
    RUN(test_position_always_converges_on_target);
    RUN(test_deadband_target_still_converges);
    RUN(test_homekit_position_is_inverted);
    RUN(test_homekit_position_round_trip);
    RUN(test_homekit_position_state);
    RUN(test_homekit_target_drives_correct_direction);
    RUN(test_close_runs_to_limit_and_calibrates);
    RUN(test_open_runs_to_limit);
    RUN(test_go_to_half_stops);
    RUN(test_go_to_upward_sends_up);
    RUN(test_stop_freezes_position);
    RUN(test_reverse_mid_travel);
    RUN(test_deadband_ignores_tiny_move);
    RUN(test_asymmetric_travel_times);
    RUN(test_not_calibrated_until_a_limit_is_hit);
}
