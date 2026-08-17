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

## The bottom end is a timed stop, and that carries a risk

Confirmed on 2026-08-15: send Down and leave it, and the shade runs about a
foot past the wanted position and knocks into furniture. There is no motor
limit there, so `SHADE_CLOSE_IS_HARD_LIMIT` is false and we time the stop
ourselves.

RTS is one way. Nothing tells us the motor heard the Stop. **If a Stop is
lost, the shade runs on into the furniture.** The repeats inside one
transmission are the only defence: one command, one rolling code, five frames,
so the motor only needs to decode any one of them.

Do not paper over this by sending a second, separate Stop. A Stop that arrives
after the motor already halted is read as "go to the favourite position", so
the retry itself moves the shade.

### The better fix: program the My position

Somfy motors hold one favourite position per shade. If you set it to your
wanted bottom position, two good things follow.

1. We can drive the shade there with a single My command from a standstill.
   The motor runs to the position and stops itself, exactly, with no timing on
   our side and nothing to lose in the air. That end then becomes a hard limit
   and re-calibrates the estimate.
2. A stray or duplicated Stop becomes harmless, because "go to the favourite
   position" is where we wanted the shade anyway.

To set it: drive the shade to the wanted position with the existing remote,
then hold MY for about 5 s until the shade jogs.

## Driving a shade from the console

`@M<n> <pct>` moves shade `n` to a HomeKit percent, so bring-up can exercise
the position model and later the radio without the Home app.

```
@M0 50      move the left shade to 50% open
@S          show where it thinks it is
```

With no radio fitted the model still runs, which is how the position estimate
was verified. Sampling during a move gave 8.8% per 1.5 s, matching the measured
17.1 s full travel.

Expect the shade to rest about 1% short of the target. `SHADE_STOP_LEAD_MS` is
150 ms, which is 0.88% of a 17.1 s travel, and the model assumes the shade
coasts that last bit while the radio transmits. With no motor attached nothing
coasts. Once the radio works, check the real resting position and tune the lead.
