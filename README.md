# esp-somfy-matter

Smart-home bridge for three Somfy RTS roller shades, on an ESP32-C3 with a
CC1101 transceiver.

Protocol work is based on [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS)
(Unlicense). See [docs/protocol.md](docs/protocol.md).

## Status

| Part | State |
| --- | --- |
| `components/somfy_rts` — frame encode/decode, OOK envelope | done, tested on host |
| `components/shade_ctl` — time-based position model | done, tested on host |
| `components/cc1101` — SPI driver, RMT transmit | not started, needs the module |
| Smart-home layer — Matter or HomeKit | not started, decision open |

No hardware is needed for anything that exists today.

## Test

```
make -C test/host run
```

14 tests, no ESP-IDF and no toolchain beyond a C compiler. `make -C test/host asan`
adds the sanitizers; run that one from a normal terminal, because ASan binaries
hang under some sandboxed shells.

## Hardware

- ESP32-C3 SuperMini (board marking HW-466AB), 4 MB flash, Wi-Fi only.
- CC1101 module, 387-464 MHz variant, tuned to 433.42 MHz. **3.3 V only.**

Planned wiring. The ESP32-C3 has a GPIO matrix, so these are all free choices
and become Kconfig options.

| CC1101 | ESP32-C3 | Note |
| --- | --- | --- |
| VCC | 3V3 | never 5 V |
| GND | GND | |
| SCK | GPIO4 | |
| MISO (SO) | GPIO5 | |
| MOSI (SI) | GPIO6 | |
| CSN | GPIO7 | |
| GDO0 | GPIO10 | RMT transmit, the OOK envelope |
| GDO2 | GPIO3 | receive, later |

Avoid GPIO2, 8 and 9 (strapping and the onboard LED), 18 and 19 (USB), and
20 and 21 (UART).

## Design

`somfy_rts` and `shade_ctl` hold no radio, no clock and no ESP-IDF headers.
They are pure logic, so the host tests cover the parts that are hard to debug
over the air. The radio driver and the smart-home layer sit above them.

`shade_ctl` reports position the way Matter WindowCovering does: hundredths of
a percent, 0 = fully open, 10000 = fully closed.

A move to an intermediate position ends with a `Stop`. A move to fully open or
fully closed does not: the motor stops at its own hard limit, which re-syncs
the estimate against reality. Only a full travel sets `calibrated`.
