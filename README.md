# esp-somfy-matter

Smart-home bridge for three Somfy RTS roller shades, on an ESP32-C3 with a
CC1101 transceiver.

Protocol work is based on [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS)
(Unlicense). See [docs/protocol.md](docs/protocol.md).

Apple Home only, over native HAP. We dropped Matter: a 4 MB ESP32-C3 leaves no
room for OTA alongside it, and test-certificate attestation against Apple Home
is unreliable. Native HAP needs no certificates and fits with room to spare.

## Status

| Part | State |
| --- | --- |
| `components/somfy_rts` — frame encode/decode, OOK envelope | done, tested on host |
| `components/shade_ctl` — position model, HomeKit conversion | done, tested on host |
| `components/cc1101` — register map, config, OOK setup | done, tested on host with a mock SPI |
| CC1101 platform glue — SPI and RMT wiring | not started, needs the board |
| HomeSpan accessory layer | not started |

No hardware is needed for anything that exists today.

## Test

```
make -C test/host run
```

27 tests, no ESP-IDF and no toolchain beyond a C compiler. `make -C test/host asan`
adds the sanitizers; run that one from a normal terminal, because ASan binaries
hang under some sandboxed shells.

## HomeKit library

[HomeSpan](https://github.com/HomeSpan/HomeSpan) (MIT). Espressif's
`esp-homekit-sdk` is the obvious alternative and is native ESP-IDF C, but it
carries **no LICENSE file anywhere in the repository**, has no releases, and
last saw a commit in February 2026. HomeSpan is MIT, actively maintained, and
supports the ESP32-C3.

The cost is the Arduino layer. Our three components are framework-neutral C
with no Arduino and no ESP-IDF headers, so they compile either way.

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

`shade_ctl` holds position in hundredths of a percent, 0 = fully open, 10000 =
fully closed.

**HomeKit counts the opposite way**: whole percent, 100 = fully open, 0 = fully
closed. Every value that crosses that boundary goes through
`shade_to_hk_position` and `shade_from_hk_position`. Both directions are tested.

A move to an intermediate position ends with a `Stop`. A move to fully open or
fully closed does not: the motor stops at its own hard limit, which re-syncs
the estimate against reality. Only a full travel sets `calibrated`.
