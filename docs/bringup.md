# Bring-up, once the CC1101 arrives

## 1. Wire it

Power the module from **3V3, never 5 V**. See the table in the README.

Keep the SPI leads short. Long flying leads at 4 MHz cause read errors that
look exactly like a dead chip.

## 2. Check the radio answers

Flash, open the monitor, and type `@S`.

```
pio run -t upload
pio device monitor
```

`radio: ready` means the CC1101 answered with a sane PARTNUM and VERSION.
`radio: FAILED` means the SPI bus read all zeroes or all ones. That is almost
always CSN, MISO or power, in that order.

## 3. Pair each shade

Somfy RTS motors store the remote address, so each shade must learn ours. This
needs the transmitter; there is no software-only path.

For each shade, `n` from 0 to 2:

1. Hold PROG on the existing remote for about 2-3 s. The shade jogs.
2. Within a few seconds, type `@P<n>` in the monitor.
3. The shade jogs again. It is paired.

`@S` shows the address and rolling code for each shade.

Pair one shade at a time and check it before you move on. If you pair two
shades to the same address they will always move together.

## 4. Measure the travel

Time a full close and a full open with a stopwatch. Put the numbers into
`SHADE_TRAVEL_OPEN_MS` and `SHADE_TRAVEL_CLOSE_MS` in `include/somfy_config.h`
and re-flash. They are often different by a second or more, because the motor
fights gravity in one direction only.

## 5. Add to Home

Pair in the Home app with the HomeSpan setup code. The bridge shows up once
and brings three tiles with it.

**You need an Apple Home Hub** — a HomePod or an Apple TV. HomeSpan states that
use without one is not supported. This is an Apple requirement for any HomeKit
accessory, not a HomeSpan limit.

## If the radio answers but the shade ignores it

Work down this list.

1. **Frequency.** Somfy is 433.42 MHz, not 433.92. `FREQ2/1/0` must read
   `10 AB 85`. A module sold as "433 MHz" is fine; the chip is tuned in
   software.
2. **Rolling code.** A motor rejects a code it has already seen. If you flashed
   over a paired device and lost NVS, the counter went backwards. Re-pair.
3. **PATABLE.** Index 0 must be `0x00`. If it is not, the carrier never keys
   off and the envelope carries no information. `test_patable_index_zero_is_off`
   guards this, but check the live chip if you changed the config.
4. **Antenna.** A CC1101 with no antenna transmits a few centimetres. Fit a
   17.3 cm wire for a quarter wave at 433 MHz.
5. **GDO0 contention.** We set `IOCFG0 = 0x0D`, which is what ESPSomfy-RTS uses
   and is proven against real motors. In transmit the chip switches GDO0 to an
   input, so RMT drives it. When the chip is idle it may drive that pin as an
   output while RMT also drives it. If transmit behaves oddly, try
   `IOCFG0 = 0x2E`, which puts the pin in high impedance. We kept the proven
   value rather than deviate on hardware nobody has tested yet.

## Checking the waveform without a receiver

A cheap RTL-SDR at 433.42 MHz shows the envelope. You should see one wake-up
pulse, then five frames, each with its hardware sync burst. The gap between
frames is about 27 ms.

A logic analyser on GPIO10 works just as well and needs no radio at all. That
verifies everything except the CC1101 itself.

## Verified so far

On 2026-08-15, with no CC1101 fitted:

- Flashed over USB-C. The board enumerates as `/dev/cu.usbmodem*` with no
  BOOT button needed.
- RMT channel comes up on GPIO10 at 1 us resolution.
- CC1101 correctly reports absent.
- HAP database validates: 0 warnings, 0 errors. One bridge, three Window
  Covering accessories.
- Paired to Apple Home. Two controllers registered, one admin and one regular.
- WiFi RSSI about -55 dBm, so the SuperMini antenna is adequate at this range.
- `@S` and the HomeSpan `s` command both work over the serial console.

Still unproven: everything that needs the radio.

## Confirm the bottom end before you trust it

`SHADE_CLOSE_IS_HARD_LIMIT` is set false, on the assumption that your chosen
bottom position is **not** where the motor's own down limit sits. We therefore
send Stop ourselves after 17.1 s.

Check this once the radio works. Send Down and let it run without our Stop. If
the shade keeps going past your point, the assumption is right. If it halts
exactly there, the motor has a limit and the flag should be true.

Getting it wrong the harmful way — flag true when there is no limit — makes the
shade run to the motor's real limit. Getting it wrong the other way sends a
Stop after the motor already halted, and an RTS motor reads that as "go to the
favourite position".
