# Somfy RTS 56-bit protocol

Source of truth: [rstrouse/ESPSomfy-RTS](https://github.com/rstrouse/ESPSomfy-RTS),
`Somfy.cpp`, functions `somfy_frame_t::encodeFrame` and
`Transceiver::sendFrame`. That project is under the Unlicense; see the
[third-party notices](../THIRD_PARTY_NOTICES.md).

## Radio

- 433.42 MHz. This is **not** 433.92 MHz, which is the more common band.
- ASK/OOK modulation.
- CC1101 runs in async transparent mode (`PKTCTRL0.PKT_FORMAT = 3`). The
  CC1101 does no framing at all. GDO0 is a plain data input, and the ESP32
  drives the on/off envelope itself.

## Frame

Seven bytes, sent MSB first.

| Byte | Content |
| --- | --- |
| 0 | Encryption key. Real remotes use `0xA0..0xAF` and step the low nibble on each press. The motor ignores the low nibble. |
| 1 | Command in the high nibble, checksum in the low nibble. |
| 2-3 | Rolling code, big endian. |
| 4-6 | 24-bit remote address. |

Commands: `My=0x1 Up=0x2 MyUp=0x3 Down=0x4 MyDown=0x5 UpDown=0x6
MyUpDown=0x7 Prog=0x8 SunFlag=0x9 Flag=0xA`.

Checksum: XOR-fold every byte with itself shifted right 4, across all seven
bytes, with the checksum nibble held at zero. Keep the low 4 bits.

Obfuscation, applied last: `wire[i] ^= wire[i-1]` for `i = 1..6`.

### The checksum is weak

Obfuscation means `plain[i] = wire[i] ^ wire[i-1]`, so one flipped bit on the
air flips the same bit position in two neighbouring plain bytes. Both feed the
same checksum bit, and the two changes cancel.

48 of the 56 possible single-bit errors pass the checksum. Only the last byte
has no neighbour to cancel against. `test_decode_rejects_corruption` pins this.

**Consequence for the receive path:** never trust one frame. Require that two
of the repeated frames decode to the same content.

## On-air timing

All values in microseconds. `SYMBOL` is 640, which is one half-symbol.

| Element | High | Low |
| --- | --- | --- |
| Wake-up (first frame only) | 10920 | 7357 |
| Hardware sync, one pair | 2560 | 2560 |
| Software sync | 4850 | 640 (start bit) |
| Manchester `1` | second half | first half |
| Manchester `0` | first half | second half |
| Inter-frame gap | - | 27434 |

Hardware sync pairs: **2** on the first frame, **7** on every repeat.

Manchester polarity is inverted against several published references: a `1` is
low-then-high and a `0` is high-then-low.

The wake-up numbers also differ from the widely copied 9415 / 89565 pair.
ESPSomfy-RTS measured a real Telis remote and found no long silence after the
wake-up pulse. We follow the measured values.

Because a `1` bit starts low, the first data bit merges with the 640 us start
bit when that bit is a `1`. The envelope generator merges adjacent equal levels,
so `somfy_rts_build_pulses` always returns a strictly alternating list. That is
what the RMT peripheral wants.

## Rolling code

Every transmission must increment the rolling code and persist it before the
radio sends. A motor rejects a code it has already seen. If the counter ever
goes backwards the shade stops responding until you re-pair it.

## Pairing

Pairing is a transmit operation, so it needs the CC1101. There is no way to do
it in software alone.

1. Hold PROG on an existing remote for about 2-3 s. The shade jogs.
2. Within a few seconds, send a `Prog` command from the new virtual remote
   address.
3. The shade jogs again to confirm.

Use one virtual remote address per shade. Cloning an existing remote instead
would need a receive path to sniff its address and current rolling code, and
would fight the real remote over the counter.

## CC1101 setup for transmit

Register values follow
[LSatan/SmartRC-CC1101-Driver-Lib](https://github.com/LSatan/SmartRC-CC1101-Driver-Lib)
(MIT), which is the driver ESPSomfy-RTS drives real Somfy motors with.

| Register | Value | Why |
| --- | --- | --- |
| `FREQ2/1/0` | `10 AB 85` | 433.42 MHz at a 26 MHz crystal. Error is about 48 Hz. |
| `MDMCFG2` | `0x30` | ASK/OOK, no sync word, no preamble. |
| `FREND0` | `0x11` | `PA_POWER = 1`, so the PA swings between `PATABLE[0]` and `PATABLE[1]`. |
| `PKTCTRL0` | `0x32` | Async serial, no CRC, no whitening, infinite length. |
| `IOCFG0` | `0x0D` | Async serial data. In transmit the chip reads GDO0 as an input. |
| `TEST0` | `0x09` | Correct above 430.5 MHz. |
| `PATABLE` | `00 C0 00 ...` | Index 0 is carrier off, index 1 is carrier on at 10 dBm. |

`PATABLE[0]` **must** stay at zero. If it does not, the carrier never switches
off, the envelope carries no information, and the motor hears nothing.
`test_patable_index_zero_is_off` pins this.

The chip's own Manchester encoder stays off. Async transparent mode bypasses it,
and `somfy_rts_build_pulses` already emits Manchester in the envelope. ESPSomfy
does call `setManchester(1)`, but that setting is inert in this mode.
