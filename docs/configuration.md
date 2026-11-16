# Configuration

The [setup app](setup-app.md) provides a form under **Configuration & firmware
→ Edit installation** for names, addresses, timings and close behavior. Choose
a shade under **Settings for this shade** to change its individual settings.
It preserves other local overrides. The manual equivalent follows.

Copy `include/somfy_config.local.example.h` to `include/somfy_config.local.h`
and edit the copy. It is ignored by git. The shared defaults remain in
`include/somfy_config.h`; the local header overrides only settings you define.
Rebuild and upload after changes.

The defaults preserve the original installation: three shades, addresses
`0x100000`–`0x100002`, 17.9 seconds opening, 17.1 seconds closing, and My used
as fully closed. These are reference settings, not automatic calibration.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `SHADE_COUNT` | `3` | Number of Window Covering accessories |
| `SHADE_NAMES` | Left/Middle/Right Shade | Initializer with exactly one name per shade |
| `SOMFY_ADDR_BASE` | `0x100000u` | First remote's 24-bit address; each shade adds its index |
| `SHADE_TRAVEL_OPEN_MS` | `17900` | Time from chosen closed position to fully open |
| `SHADE_TRAVEL_CLOSE_MS` | `17100` | Time from fully open to chosen closed position |
| `SHADE_CLOSE_USES_FAVOURITE` | `true` | Use My to reach the closed position |
| `SHADE_OPEN_IS_HARD_LIMIT` | `true` | Motor stops itself at the open position |
| `SHADE_CLOSE_IS_HARD_LIMIT` | `false` | Motor stops itself at closed; overridden internally by My close mode |
| `SHADE_STOP_LEAD_MS` | `150` | Send a timed Stop this early to compensate for RF latency |
| `SHADE_FAVOURITE_DELAY_MS` | `1200` | Delay between Stop and follow-up My when closing interrupts movement |
| `SOMFY_TX_DBM` | `10` | Requested transmit power, using the CC1101 power table |
| `SOMFY_REPEATS` | `4` | Repeated frames after the first; buffer allows 0–5 |
| `PIN_SCK/MISO/MOSI/CSN/GDO0` | `4/5/6/7/10` | SPI and envelope GPIOs |
| `PIN_GDO2` | `3` | Reserved; unused by this firmware |

The scalar timing, limit and favourite settings above are shared defaults.
Optional per-shade lists override them independently, in accessory order:

```c
#define SHADE_TRAVEL_OPEN_MS_PER_SHADE { 17900, 22000, 30000 }
#define SHADE_TRAVEL_CLOSE_MS_PER_SHADE { 17100, 21000, 28500 }
#define SHADE_CLOSE_USES_FAVOURITE_PER_SHADE { true, false, true }
#define SHADE_OPEN_IS_HARD_LIMIT_PER_SHADE { true, true, true }
#define SHADE_CLOSE_IS_HARD_LIMIT_PER_SHADE { false, true, false }
```

Every list supplied must have exactly `SHADE_COUNT` entries. Omit any list
to keep using its shared default for all shades. In this example the middle
shade closes to its physical motor limit; the others close to their My
positions. Stop lead and favourite delay remain shared settings.

The TUI imports an older configuration by copying the shared values to every
shade, then saves explicit lists when edited. It does not change motor
addresses, rolling codes or HomeKit identities when changing timings.
Per-shade settings require firmware containing `shade_settings.h`; saving a
header alone cannot change an older firmware running on the ESP. Use **Build
& upload** after saving.

The validated physical setup has three shades; other counts need hardware
testing. Keep accessory order stable after HomeKit pairing: order determines
accessory identities, remote addresses and which settings apply to each motor.

For a single shade, for example:

```c
#define SHADE_COUNT 1
#define SHADE_NAMES { "Office Shade" }
/* Choose your own unused address before motor pairing. */
#define SOMFY_ADDR_BASE 0x2A4100u
```

Choose a different unused range for each bridge. Do not clone a physical
remote's address or run two bridges with the same addresses; their rolling
counters will conflict. Keep addresses unchanged after pairing.

## Choosing the closed position

With a shade's favourite-close flag enabled, HomeKit **0% open** means its motor's
My position, which can be above its physical bottom limit. The motor must
already have that favourite programmed. Closing from a standstill sends My;
interrupting an estimated move sends Stop, waits, then sends My. A new target
cancels a pending follow-up My.

Intermediate moves still use a direction command followed by a timed Stop.
Percentages are estimates even when the motor manages its final stop at My.
There is no sensor or acknowledgement confirming arrival.

If My should not be a shade's closed position, turn off its favourite-close
flag and choose its closed hard-limit setting to match the motor:

- `true`: the motor has an end stop exactly at your desired closed position.
- `false`: the firmware sends a timed Stop. A lost Stop can let the motor run
  to its actual end stop. This cannot protect furniture or detect obstacles.

My means Stop while moving and Favourite while stopped. Another Stop after
the motor has halted can start it moving again. External remote use or lost
commands can make the model disagree with the motor. Test while watching it.

## Rolling codes and updates

Each remote's next rolling code is stored in the `somfy` NVS namespace under
`rc0`, `rc1`, etc. The firmware attempts to persist the next code before
transmitting the current one. Motors reject codes they have already accepted.

A normal `pio run -t upload` with the existing partition layout preserves NVS,
Wi-Fi credentials and HomeKit pairing. Keep that layout for a working device.
Do not erase flash, reset all NVS or restore an old NVS image as part of a
routine update. HomeSpan's full NVS erase also removes these rolling codes.

If codes are lost, stop transmitting blindly with the old address. Use the
motor's remote-management procedure to remove the old virtual remote if
necessary, select a fresh unused address range, and pair that new remote.
PROG can toggle pairings; follow the motor's manual. The 16-bit counter is
finite, and this firmware has no automatic rollover recovery; an exhausted
counter also needs a fresh remote identity and pairing.

HomeKit restores the last target after restart; it cannot recover the actual
position. The `calibrated` label means the model completed a move to an assumed
known endpoint, not that it measured position. Re-establish a known physical
position before relying on percentages after power loss or external remote use.

## Build and flash layout

The `supermini` environment uses the ESP32-C3 DevKitM-1 board definition and
native USB CDC on GPIO18/19. Avoid those pins for the radio. GPIO2/8/9 are
strapping/onboard-LED pins; GPIO20/21 are UART pins.

`min_spiffs.csv` provides two 1,966,080-byte app slots in 4 MB flash. The
firmware keeps this existing layout but does not enable an OTA service.
Upload over USB. Partition changes can invalidate stored state and are not
needed for publishing or ordinary updates.
