# Third-party notices

Original project code is licensed under [MIT](LICENSE). Upstream work retains
its own license and copyright notices.

| Project | Use here | License |
| --- | --- | --- |
| [HomeSpan 2.1.8](https://github.com/HomeSpan/HomeSpan/tree/2.1.8) | HomeKit library, fetched by PlatformIO | [MIT text](LICENSES/HomeSpan-MIT.txt), copyright Gregg E. Berman |
| [ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS) | Reference for RTS framing and measured pulse timings in `components/somfy_rts` | [Unlicense text](LICENSES/ESPSomfy-RTS-Unlicense.txt) |
| [SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib) | Reference for CC1101 register settings and 433 MHz power table in `components/cc1101` | [MIT text](LICENSES/SmartRC-CC1101-Driver-Lib-MIT.txt), copyright Little Satan (SmartRC) |

HomeSpan's notice is copied from the pinned 2.1.8 package. The other notice
snapshots were checked against ESPSomfy-RTS commit
`eb75868adb92b3e6f5d3ead069f915d84a72efde` and SmartRC commit
`de6757ef633b3e626dae5c7835cdb66ab53f9a1d`. These identify the license
snapshots, not a claim that those exact revisions were used during development.
The component implementations are local; those two reference libraries are
not linked as firmware dependencies.

## Framework and build dependencies

The pinned [pioarduino 55.03.311 platform](https://github.com/pioarduino/platform-espressif32/releases/tag/55.03.311)
downloads Arduino-ESP32 3.3.11, ESP-IDF libraries and the compiler toolchain.
They are not vendored in this repository. Arduino-ESP32 declares
[LGPL-2.1-or-later](https://github.com/espressif/arduino-esp32/blob/3.3.11/LICENSE.md);
ESP-IDF uses [Apache-2.0](https://github.com/espressif/esp-idf/blob/v5.5.5/LICENSE)
with additional third-party components carrying their own notices. The
platform and toolchain packages also contain their respective license texts.

This project's MIT license does not replace those licenses. If distributing
compiled firmware, review the licenses of the actual linked dependencies,
retain their notices and provide the required corresponding source and
relinking materials where applicable. Source releases avoid bundling compiled
framework libraries, but downstream binary distributors still need to review
their own distribution.
