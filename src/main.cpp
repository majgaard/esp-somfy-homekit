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

struct SomfyShade : Service::WindowCovering {
    /* HomeKit counts the opposite way to shade_ctl: 100 is fully open here.
     * The nvsStore flag keeps the last position across a reboot. */
    Characteristic::CurrentPosition current{100, true};
    Characteristic::TargetPosition  target{100, true};
    Characteristic::PositionState   state{SHADE_HK_STOPPED};

    uint8_t  idx;
    shade_t  model;
    uint32_t last_report = 0;

    explicit SomfyShade(uint8_t i) : idx(i)
    {
        shade_cfg_t cfg = {};
        cfg.travel_open_ms      = SHADE_TRAVEL_OPEN_MS;
        cfg.travel_close_ms     = SHADE_TRAVEL_CLOSE_MS;
        cfg.open_is_hard_limit  = SHADE_OPEN_IS_HARD_LIMIT;
        cfg.close_is_hard_limit = SHADE_CLOSE_IS_HARD_LIMIT;
        cfg.stop_lead_ms        = SHADE_STOP_LEAD_MS;
        shade_init(&model, &cfg, shade_from_hk_position(current.getVal()),
                   millis());
    }

    void drive(shade_action_t a)
    {
        switch (a) {
        case SHADE_ACT_UP:
            somfy_send(idx, SOMFY_CMD_UP);
            break;
        case SHADE_ACT_DOWN:
            somfy_send(idx, SOMFY_CMD_DOWN);
            break;
        case SHADE_ACT_STOP:
            /* On an RTS motor the My button stops a shade that is moving. */
            somfy_send(idx, SOMFY_CMD_MY);
            break;
        default:
            break;
        }
    }

    boolean update() override
    {
        if (target.updated()) {
            uint16_t want = shade_from_hk_position(target.getNewVal());
            drive(shade_go_to(&model, want, millis()));
        }
        return true;
    }

    void loop() override
    {
        shade_action_t a = shade_tick(&model, millis());
        if (a != SHADE_ACT_NONE) {
            drive(a);
        }

        /* Report twice a second while moving. The Home app animates between
         * updates, so faster gains nothing. */
        if (millis() - last_report >= 500) {
            last_report = millis();
            current.setVal(shade_to_hk_position(model.position));
            state.setVal(shade_hk_position_state(&model));
        }
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
}

void loop() { homeSpan.poll(); }
