# Bring-up and troubleshooting

Follow this guide for a new installation. An already-paired, working bridge
does not need to be paired again after a normal firmware upload.

For guided setup without typing serial commands, use the
[local terminal app](setup-app.md). The steps below remain available for
manual setup and troubleshooting.

## 1. Configure the installation

Follow the [README](../README.md#quick-start) to install PlatformIO and copy
`include/somfy_config.local.example.h` to `include/somfy_config.local.h`.
Review the [configuration guide](configuration.md) before operating a motor.

Choose an unused remote address range before first pairing. Measure travel
in each direction between the positions you call open and closed. The sample
17.9 s opening and 17.1 s closing values will not suit every installation.
Set each shade's individual times and endpoint behavior in the TUI's
configuration editor, then save and upload the updated firmware.

The default close mode uses My. With your existing remote, verify that every
motor's My position is where you want HomeKit's **0% open** to be. Consult the
motor's instructions to program it; many RTS motors store it when you hold MY
for about five seconds at the desired position. Do not enable this mode with
an unset or differently placed favourite.

## 2. Wire, build and flash

Disconnect USB power, then follow the wiring table in the README. Feed the
radio from **3.3 V only**, connect ground, fit an antenna and keep SPI leads
short. GDO2 is unused and can be left disconnected.

```sh
pio run -e supermini
pio device list
pio run -e supermini -t upload
pio device monitor --echo
```

If several serial devices are connected, supply `--upload-port <port>` for
upload and `--port <port>` for the monitor. Typical ports are
`/dev/cu.usbmodem*` on macOS, `/dev/ttyACM*` on Linux and `COM3` on Windows.
The monitor uses 115200 baud from `platformio.ini`. Send commands with a newline.
Close the serial monitor before uploading again.

Expected startup messages:

```text
[radio] RMT ready on GPIO10, 1 us resolution.
[radio] CC1101 ready at 433.42 MHz.
```

Run `@S`, then `@R`. Expect `radio: ready`, zero register mismatches,
`PATABLE off 00 / on C0` at the default 10 dBm, and a 433.42 MHz carrier.
This verifies SPI configuration; it does not prove that a motor received RF.

If the chip is absent, the console and HomeKit still work, and the position
model still animates. A changing percentage is not proof of motor movement.

## 3. Pair each motor

Pairing adds a new virtual remote; it does not clone your existing remote.
Do one motor at a time. For a multi-channel remote, first select only the
channel for that motor.

1. Hold PROG on the existing paired remote for about 2–3 seconds, until the
   shade jogs. Use the motor's instructions if its procedure differs.
2. Within the learning window, enter `@P0` in the serial console for shade 0.
3. A second jog confirms the virtual remote was learned.
4. Try `@M0 100` to open, then `@M0 0` to close to My, while watching the motor.
   An open request may send nothing if the model already thinks it is open;
   use the existing remote to verify the starting position before testing.
5. Repeat with `@P1`, `@P2`, etc. for the remaining shades.

PROG can also remove an existing remote on many motors. Do not repeat pairing
as a routine connectivity check. If several motors learn the same address,
they will move together until you remove that pairing.

`@F0` sends a bare My to motor 0 to check its programmed favourite. It refuses
when the model says the shade is moving, but cannot detect movement started
by another remote. This diagnostic does **not** update the position model.
Let it finish, then re-establish a known position before percentage moves.

## 4. Configure Wi-Fi and Apple Home

1. Enter `W` and follow the serial prompts for your 2.4 GHz Wi-Fi network.
2. Set a personal HomeKit setup code with `S <8-digit-code>`. HomeSpan rejects
   certain trivial codes. Do this before pairing with Apple Home.
3. In Apple Home, choose **Add Accessory**, then **More options** if needed,
   select **Somfy RTS Bridge** and enter your code. Follow any prompt for an
   uncertified accessory.
4. Assign the bridge and shades to rooms and give the tiles useful names.

HomeSpan's default setup code is `466-37-726`. It is a public library default;
choose your own for a new installation. HomeSpan does not support operating
without an Apple Home Hub (HomePod or Apple TV).

If discovery stalls, check Wi-Fi and the Home Hub, and ensure the phone, hub
and bridge can communicate over the LAN, including mDNS. Guest network
isolation can prevent discovery.

## Serial reference

Indices start at zero. HomeKit percentages: **100 = open, 0 = closed**.

| Command | Action |
| --- | --- |
| `?` | HomeSpan help, including this firmware's commands |
| `W` | Configure Wi-Fi interactively |
| `S <8-digit-code>` | Set the HomeKit setup code |
| `s` | HomeSpan network/controller status |
| `@S` | Radio state, addresses, next rolling codes and estimated positions |
| `@R` | Read back and verify CC1101 configuration |
| `@P<n>` | Send PROG to pair/unpair virtual remote n |
| `@M<n> <pct>` | Move shade n to a percentage; e.g. `@M0 50` |
| `@F<n>` | Send bare My to check a favourite; does not update the model |

Commands are case sensitive. See the
[HomeSpan CLI reference](https://github.com/HomeSpan/HomeSpan/blob/2.1.8/docs/CLI.md)
for built-in commands. `S` and `s` do different things.

During a move, logs report the target, transmitted action and each percentage
change. At rest the estimated current position converges on the target so
Apple Home can finish showing the move.

## Troubleshooting

| Symptom | Checks |
| --- | --- |
| No USB port | Use a data cable. Try holding BOOT while connecting, then release. |
| Upload cannot open the port | Close other programs using it, including the serial monitor. |
| `CC1101 did not answer` | Check 3.3 V/GND, CSN and MISO wiring; shorten SPI leads. |
| `@R` mismatches | Check power and SPI before attempting motor pairing. |
| Radio ready, motor ignores commands | Check antenna, motor pairing, frequency and rolling-code history. |
| Close stops at the wrong place | Check the motor's My setting and favourite-close configuration. |
| Intermediate positions drift | Remeasure travel, tune stop lead and account for other remote use. |
| HomeKit says moving but motor is still | Read serial logs; the model has no RF acknowledgement or position feedback. |

At 433.42 MHz with a 26 MHz crystal, `FREQ2/1/0` should be `10 AB 85`.
`PATABLE[0]` must be zero for OOK to switch the carrier off. `@R` checks both.
If NVS was erased or restored from an old backup, the rolling code may have
gone backwards. See [recovery notes](configuration.md#rolling-codes-and-updates)
before pairing or erasing anything else.

On the development Mac, Python dependency subprocesses have crashed during
pioarduino's installer checks under both Python 3.13 and 3.14. If all packages
are already installed, `PLATFORMIO_OFFLINE=1 pio run -e supermini` skips those
checks and builds with cached dependencies. This cannot install missing
packages or replace a complete first-time setup. Use the pinned environment
on another machine if a clean setup is blocked by this macOS issue.

A logic analyser on GPIO10 verifies the envelope before it reaches the radio.
An SDR tuned to 433.42 MHz can check the RF envelope. At default settings, a
command has one initial frame and four repeat frames, separated by about 27 ms.

## Hardware validation

The reference ESP32-C3 SuperMini/CC1101 installation has run reliably for
several months. Initial bring-up verified USB upload, RMT output, the HomeKit
database and Apple Home pairing. CC1101 register readback verified 34 registers
with zero mismatches. Host tests cover framing, position logic and mock SPI.
Other boards, all RTS motor variants, reception and obstacle detection have
not been validated by this project.
