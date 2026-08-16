#include "shade_ctl.h"
#include "test_util.h"

#define TRAVEL_MS 7000

static shade_cfg_t cfg = {
    .travel_open_ms  = TRAVEL_MS,
    .travel_close_ms = TRAVEL_MS,
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
    shade_init(&s, &cfg, SHADE_OPEN);
    s.last_ms = now;

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
    shade_init(&s, &cfg, SHADE_CLOSED);
    s.last_ms = now;

    ASSERT_EQ(SHADE_ACT_UP, shade_open(&s, now));
    run_for(&s, &now, TRAVEL_MS + 500);

    ASSERT_EQ(SHADE_OPEN, s.position);
    ASSERT_TRUE(s.calibrated);
}

static void test_go_to_half_stops(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN);
    s.last_ms = now;

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
    shade_init(&s, &cfg, SHADE_CLOSED);
    s.last_ms = now;

    ASSERT_EQ(SHADE_ACT_UP, shade_go_to(&s, 2500, now));
    ASSERT_EQ(SHADE_ACT_STOP, run_for(&s, &now, 6000));
    ASSERT_NEAR(2500, s.position, 100);
}

static void test_stop_freezes_position(void)
{
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &cfg, SHADE_OPEN);
    s.last_ms = now;

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
    shade_init(&s, &cfg, SHADE_OPEN);
    s.last_ms = now;

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
    shade_init(&s, &cfg, 5000);
    s.last_ms = now;

    /* 0.5% away is inside the 1% deadband. */
    ASSERT_EQ(SHADE_ACT_NONE, shade_go_to(&s, 5050, now));
    ASSERT_EQ(SHADE_MOTION_IDLE, s.motion);

    /* 2% away is outside it. */
    ASSERT_EQ(SHADE_ACT_DOWN, shade_go_to(&s, 5200, now));
}

static void test_asymmetric_travel_times(void)
{
    shade_cfg_t asym = { .travel_open_ms = 9000, .travel_close_ms = 6000 };
    shade_t s;
    uint32_t now = 1000;
    shade_init(&s, &asym, SHADE_OPEN);
    s.last_ms = now;

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
    shade_init(&s, &cfg, 5000);
    s.last_ms = now;

    shade_go_to(&s, 7000, now);
    run_for(&s, &now, 2000);
    ASSERT_TRUE(!s.calibrated);

    shade_close(&s, now);
    run_for(&s, &now, TRAVEL_MS);
    ASSERT_TRUE(s.calibrated);
}

void run_shade_ctl_tests(void)
{
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
