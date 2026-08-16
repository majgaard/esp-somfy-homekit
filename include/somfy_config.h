/* Board and installation settings. */
#ifndef SOMFY_CONFIG_H
#define SOMFY_CONFIG_H

/* ESP32-C3 SuperMini. Avoid GPIO2, 8 and 9 (strapping and the onboard LED),
 * 18 and 19 (USB), and 20 and 21 (UART). */
#define PIN_SCK  4
#define PIN_MISO 5
#define PIN_MOSI 6
#define PIN_CSN  7
#define PIN_GDO0 10 /* RMT drives the OOK envelope on this pin */
#define PIN_GDO2 3  /* receive, later */

#define SHADE_COUNT 3

/* One virtual remote address per shade. Any 24-bit value works as long as it
 * stays stable, because the motor stores it when you pair. */
#define SOMFY_ADDR_BASE 0x100000u

#define SOMFY_TX_DBM  10
/* One frame plus this many repeats. Real remotes send 2 to 6. */
#define SOMFY_REPEATS 4

/* Full travel, in ms. Measure this per shade and correct it later. */
#define SHADE_TRAVEL_OPEN_MS  7000
#define SHADE_TRAVEL_CLOSE_MS 7000

#endif /* SOMFY_CONFIG_H */
