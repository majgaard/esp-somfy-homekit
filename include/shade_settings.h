/* Resolve legacy shared defaults and optional settings for each accessory. */
#ifndef SOMFY_SHADE_SETTINGS_H
#define SOMFY_SHADE_SETTINGS_H

#include <stddef.h>
#include "shade_ctl.h"
#include "somfy_config.h"

namespace somfy_settings {

template <typename T, size_t N>
constexpr bool valid_travel(const T (&values)[N])
{
    for (size_t i = 0; i < N; i++) {
        if (values[i] == 0 || values[i] > 600000) return false;
    }
    return true;
}

#ifdef SHADE_TRAVEL_OPEN_MS_PER_SHADE
constexpr uint32_t open_ms[] = SHADE_TRAVEL_OPEN_MS_PER_SHADE;
static_assert(sizeof(open_ms) / sizeof(open_ms[0]) == SHADE_COUNT,
              "Opening times must contain one entry per shade");
static_assert(valid_travel(open_ms), "Opening times must be 1..600000 ms");
#endif
#ifdef SHADE_TRAVEL_CLOSE_MS_PER_SHADE
constexpr uint32_t close_ms[] = SHADE_TRAVEL_CLOSE_MS_PER_SHADE;
static_assert(sizeof(close_ms) / sizeof(close_ms[0]) == SHADE_COUNT,
              "Closing times must contain one entry per shade");
static_assert(valid_travel(close_ms), "Closing times must be 1..600000 ms");
#endif
#ifdef SHADE_CLOSE_USES_FAVOURITE_PER_SHADE
constexpr bool favourite[] = SHADE_CLOSE_USES_FAVOURITE_PER_SHADE;
static_assert(sizeof(favourite) / sizeof(favourite[0]) == SHADE_COUNT,
              "Favourite flags must contain one entry per shade");
#endif
#ifdef SHADE_OPEN_IS_HARD_LIMIT_PER_SHADE
constexpr bool open_limit[] = SHADE_OPEN_IS_HARD_LIMIT_PER_SHADE;
static_assert(sizeof(open_limit) / sizeof(open_limit[0]) == SHADE_COUNT,
              "Open limit flags must contain one entry per shade");
#endif
#ifdef SHADE_CLOSE_IS_HARD_LIMIT_PER_SHADE
constexpr bool close_limit[] = SHADE_CLOSE_IS_HARD_LIMIT_PER_SHADE;
static_assert(sizeof(close_limit) / sizeof(close_limit[0]) == SHADE_COUNT,
              "Close limit flags must contain one entry per shade");
#endif

constexpr shade_cfg_t for_shade(size_t index)
{
    shade_cfg_t cfg = {};
    cfg.travel_open_ms = SHADE_TRAVEL_OPEN_MS;
    cfg.travel_close_ms = SHADE_TRAVEL_CLOSE_MS;
    cfg.open_is_hard_limit = SHADE_OPEN_IS_HARD_LIMIT;
    cfg.close_is_hard_limit = SHADE_CLOSE_IS_HARD_LIMIT;
    cfg.close_uses_favourite = SHADE_CLOSE_USES_FAVOURITE;
    cfg.stop_lead_ms = SHADE_STOP_LEAD_MS;
    cfg.favourite_delay_ms = SHADE_FAVOURITE_DELAY_MS;
#ifdef SHADE_TRAVEL_OPEN_MS_PER_SHADE
    cfg.travel_open_ms = open_ms[index];
#endif
#ifdef SHADE_TRAVEL_CLOSE_MS_PER_SHADE
    cfg.travel_close_ms = close_ms[index];
#endif
#ifdef SHADE_OPEN_IS_HARD_LIMIT_PER_SHADE
    cfg.open_is_hard_limit = open_limit[index];
#endif
#ifdef SHADE_CLOSE_IS_HARD_LIMIT_PER_SHADE
    cfg.close_is_hard_limit = close_limit[index];
#endif
#ifdef SHADE_CLOSE_USES_FAVOURITE_PER_SHADE
    cfg.close_uses_favourite = favourite[index];
#endif
    (void)index;
    return cfg;
}

} // namespace somfy_settings
#endif /* SOMFY_SHADE_SETTINGS_H */
