/*
 * HomeKit bridge for three Somfy RTS roller shades.
 *
 * One bridge accessory plus one Window Covering accessory per shade, so the
 * Home app shows three tiles behind a single pairing code.
 */
#include <Arduino.h>

#include "HomeSpan.h"

#include "shade_ctl.h"
#include "somfy_config.h"
#include "somfy_radio.h"

static const char *kShadeNames[SHADE_COUNT] = {
    "Left Shade",
    "Middle Shade",
    "Right Shade",
};

static const char *motion_name(uint8_t hk_state)
{
    switch (hk_state) {
    case SHADE_HK_INCREASING: return "opening";
    case SHADE_HK_DECREASING: return "closing";
    default:                  return "stopped";
    }
}

struct SomfyShade : Service::WindowCovering {
    /* HomeKit counts the opposite way to shade_ctl: 100 is fully open here.
     *
     * Only TargetPosition carries nvsStore. HomeSpan's setVal() commits to
     * NVS on every call with no change check, and CurrentPosition changes a
     * hundred times per travel, so backing it would mean a synchronous flash
     * write per step. TargetPosition only moves when someone asks for
     * something, and it is enough to restore state after a reboot. */
    Characteristic::CurrentPosition current{100};
    Characteristic::TargetPosition  target{100, true};
    Characteristic::PositionState   state{SHADE_HK_STOPPED};

    uint8_t  idx;
    shade_t  model;
    /* Last values pushed to HomeKit. Every setVal costs a notification, and
     * an NVS commit if the characteristic is stored, so only push changes. */
    uint8_t  last_pct   = 0;
    uint8_t  last_state = 0;

    explicit SomfyShade(uint8_t i) : idx(i)
    {
        shade_cfg_t cfg = {};
        cfg.travel_open_ms      = SHADE_TRAVEL_OPEN_MS;
        cfg.travel_close_ms     = SHADE_TRAVEL_CLOSE_MS;
        cfg.open_is_hard_limit  = SHADE_OPEN_IS_HARD_LIMIT;
        cfg.close_is_hard_limit = SHADE_CLOSE_IS_HARD_LIMIT;
        cfg.stop_lead_ms          = SHADE_STOP_LEAD_MS;
        cfg.close_uses_favourite  = SHADE_CLOSE_USES_FAVOURITE;
        cfg.favourite_delay_ms    = SHADE_FAVOURITE_DELAY_MS;
        /* Restore from the stored target. We cannot know where the shade
         * really sits at boot, so this is a guess either way, and calibrated
         * stays false until a travel reaches a limit. */
        uint8_t restored = target.getVal();
        shade_init(&model, &cfg, shade_from_hk_position(restored), millis());
        current.setVal(restored);
        last_pct   = restored;
        last_state = SHADE_HK_STOPPED;
    }

    void drive(shade_action_t a)
    {
        static const char *names[] = { "none", "UP", "DOWN", "STOP",
                                       "FAVOURITE" };
        if (a != SHADE_ACT_NONE) {
            Serial.printf("[%u %-12s] send %s%s\n", idx, kShadeNames[idx],
                          names[a], somfy_radio_ok() ? "" : "  (no radio)");
        }

        switch (a) {
        case SHADE_ACT_UP:
            somfy_send(idx, SOMFY_CMD_UP);
            break;
        case SHADE_ACT_DOWN:
            somfy_send(idx, SOMFY_CMD_DOWN);
            break;
        case SHADE_ACT_STOP:
        case SHADE_ACT_FAVOURITE:
            /* Both are the same My command. The motor decides: it stops a
             * shade that is moving, and sends a stopped one to its favourite
             * position. shade_ctl is what guarantees the ordering. */
            somfy_send(idx, SOMFY_CMD_MY);
            break;
        default:
            break;
        }
    }

    boolean update() override
    {
        if (target.updated()) {
            uint8_t want = target.getNewVal();
            Serial.printf("[%u %-12s] HomeKit asks for %u%% (now %u%%)\n", idx,
                          kShadeNames[idx], want,
                          shade_to_hk_position(model.position));
            drive(shade_go_to(&model, shade_from_hk_position(want), millis()));
        }
        return true;
    }

    void loop() override
    {
        shade_action_t a = shade_tick(&model, millis());
        if (a != SHADE_ACT_NONE) {
            drive(a);
        }

        uint8_t pct = shade_to_hk_position(model.position);
        uint8_t st  = shade_hk_position_state(&model);

        /* Nothing changed, so say nothing. This is what keeps the accessory
         * quiet and the flash intact while the shades sit still. */
        if (pct == last_pct && st == last_state) {
            return;
        }

        if (pct != last_pct) {
            current.setVal(pct);
        }
        if (st != last_state) {
            state.setVal(st);
        }

        Serial.printf("[%u %-12s] %3u%% -> %3u%%  %s\n", idx, kShadeNames[idx],
                      pct, shade_to_hk_position(model.target), motion_name(st));

        last_pct   = pct;
        last_state = st;
    }
};

static SomfyShade *g_shades[SHADE_COUNT];

/* Pairing. Hold PROG on an existing remote until the shade jogs, then run
 * this within a few seconds. The shade jogs again to confirm. */
static void cmd_prog(const char *buf)
{
    int n = -1;
    if (sscanf(buf + 1, "%d", &n) != 1 || n < 0 || n >= SHADE_COUNT) {
        Serial.printf("usage: @P<n>, n from 0 to %d\n", SHADE_COUNT - 1);
        return;
    }
    Serial.printf("Sending PROG to shade %d (%s), address %06lX\n", n,
                  kShadeNames[n], (unsigned long)somfy_address((uint8_t)n));
    somfy_send((uint8_t)n, SOMFY_CMD_PROG);
}

/* Drive a shade from the console, in HomeKit percent. Lets bring-up exercise
 * the position model and the radio without going through the Home app. */
static void cmd_move(const char *buf)
{
    int n = -1, pct = -1;
    if (sscanf(buf + 1, "%d %d", &n, &pct) != 2 || n < 0 || n >= SHADE_COUNT ||
        pct < 0 || pct > 100) {
        Serial.printf("usage: @M<n> <pct>, n 0 to %d, pct 0 to 100\n",
                      SHADE_COUNT - 1);
        return;
    }
    SomfyShade *sh = g_shades[n];
    Serial.printf("Shade %d (%s): %u%% -> %d%%\n", n, kShadeNames[n],
                  shade_to_hk_position(sh->model.position), pct);
    /* Keep HomeKit's target in step, or the Home app shows a stale one. */
    sh->target.setVal(pct);
    sh->drive(shade_go_to(&sh->model, shade_from_hk_position((uint8_t)pct),
                          millis()));
}

/* Send a bare My. From a standstill the motor drives to its favourite
 * position, so this shows where that actually is. Use it only when the shade
 * is stopped: My on a moving shade just stops it. */
static void cmd_favourite(const char *buf)
{
    int n = -1;
    if (sscanf(buf + 1, "%d", &n) != 1 || n < 0 || n >= SHADE_COUNT) {
        Serial.printf("usage: @F<n>, n from 0 to %d\n", SHADE_COUNT - 1);
        return;
    }
    SomfyShade *sh = g_shades[n];
    if (sh->model.motion != SHADE_MOTION_IDLE) {
        Serial.printf("Shade %d is moving. My would only stop it. Wait.\n", n);
        return;
    }
    Serial.printf("Shade %d (%s): My from standstill, watch where it parks\n", n,
                  kShadeNames[n]);
    somfy_send((uint8_t)n, SOMFY_CMD_MY);
}

static void cmd_verify(const char *buf)
{
    (void)buf;
    somfy_radio_verify();
}

static void cmd_status(const char *buf)
{
    (void)buf;
    Serial.printf("radio: %s\n", somfy_radio_ok() ? "ready" : "FAILED");
    for (uint8_t i = 0; i < SHADE_COUNT; i++) {
        Serial.printf("  %u %-14s addr %06lX code %5u pos %3u%% %s\n", i,
                      kShadeNames[i], (unsigned long)somfy_address(i),
                      somfy_rolling_code(i),
                      shade_to_hk_position(g_shades[i]->model.position),
                      g_shades[i]->model.calibrated ? "calibrated"
                                                    : "not calibrated");
    }
}

void setup()
{
    Serial.begin(115200);

    somfy_radio_begin(); /* logs and carries on, so the CLI stays reachable */

    homeSpan.setLogLevel(1);
    homeSpan.begin(Category::Bridges, "Somfy RTS Bridge");

    new SpanAccessory();
    new Service::AccessoryInformation();
    new Characteristic::Identify();
    new Characteristic::Name("Somfy RTS Bridge");

    for (uint8_t i = 0; i < SHADE_COUNT; i++) {
        new SpanAccessory();
        new Service::AccessoryInformation();
        new Characteristic::Identify();
        new Characteristic::Name(kShadeNames[i]);
        g_shades[i] = new SomfyShade(i);
    }

    new SpanUserCommand('P', "<n> - pair: send PROG to shade n", cmd_prog);
    new SpanUserCommand('S', " - show radio and shade status", cmd_status);
    new SpanUserCommand('M', "<n> <pct> - move shade n to pct (HomeKit scale)",
                        cmd_move);
    new SpanUserCommand('R', " - read the CC1101 config back and check it",
                        cmd_verify);
    new SpanUserCommand('F', "<n> - send My to shade n, to find its favourite",
                        cmd_favourite);
}

void loop() { homeSpan.poll(); }
