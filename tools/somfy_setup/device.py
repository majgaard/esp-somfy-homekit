"""Serial transport and the existing HomeSpan command/prompt protocol.

Only one command transaction owns the port at a time. Mutating commands are
never retried: a second PROG can unpair a motor, and a second My can move it.
"""
import asyncio
import codecs
from dataclasses import dataclass, field
import os
import re
import time
from typing import Callable

import serial
from serial.tools import list_ports


class DeviceError(Exception):
    pass


@dataclass
class Shade:
    index: int
    name: str
    address: str
    code: int
    position: int
    calibrated: bool
    target: int | None = None
    motion: str = "unknown"


@dataclass
class State:
    connected: bool = False
    identified: bool = False
    radio: bool | None = None
    shades: dict[int, Shade] = field(default_factory=dict)
    port: str = ""
    network: str = "Not checked"
    paired: bool | None = None


def ports():
    return [(p.device, f"{p.device} · {p.description}") for p in list_ports.comports()]


def valid_line(value: str, label: str, maximum: int):
    if not value or len(value.encode()) > maximum or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise DeviceError(f"{label} must be 1–{maximum} bytes with no control characters.")


def pairing_code(value: str):
    value = value.replace("-", "").replace(" ", "")
    if not re.fullmatch(r"[0-9]{8}", value):
        raise DeviceError("Enter eight digits, optionally formatted as 123-45-678.")
    if len(set(value)) == 1 or value in {"12345678", "87654321"}:
        raise DeviceError("HomeSpan does not accept this simple setup code. Choose another.")
    return value


def validate_wifi(ssid: str, password: str):
    valid_line(ssid, "Network name", 32)
    valid_line(password, "Wi-Fi password", 64)
    if not 8 <= len(password.encode()) <= 63 and not re.fullmatch(r"[0-9a-fA-F]{64}", password):
        raise DeviceError("Use an 8–63 byte password or a 64-digit hexadecimal PSK.")


class SerialTransport:
    def __init__(self, port):
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.1,
                                    write_timeout=2, exclusive=True if os.name == "posix" else None)
        self.serial.port = port
        self.serial.dtr = True
        self.serial.rts = False
        self.serial.open()

    def read(self):
        return self.serial.read(max(1, min(self.serial.in_waiting, 4096)))

    def write(self, data):
        self.serial.write(data)

    def close(self):
        self.serial.close()


class BridgeClient:
    def __init__(self, emit: Callable = lambda *_: None, transport_factory=SerialTransport):
        self.emit = emit
        self.factory = transport_factory
        self.state = State()
        self.lock = asyncio.Lock()
        self.transport = None
        self.reader = None
        self.response = ""
        self.partial = ""
        self.wake = asyncio.Event()
        self.sensitive = False
        self.secrets: list[str] = []
        self.previous_shades = {}
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")

    @property
    def busy(self):
        return self.lock.locked()

    def redact(self, text):
        for secret in self.secrets:
            text = text.replace(secret, "[hidden]")
        text = re.sub(r"\b[0-9]{3}-[0-9]{2}-[0-9]{3}\b", "[setup code hidden]", text)
        text = re.sub(r"X-HM://\S+", "[pairing payload hidden]", text)
        if any(marker in text for marker in ["Paired Controller:", "Accessory ID:", "ID="]):
            return "[HomeKit controller identifiers hidden]"
        return text

    async def connect(self, port):
        await self.disconnect()
        self.state = State(port=port)
        self.previous_shades = {}
        self.decoder.reset()
        try:
            self.transport = await asyncio.to_thread(self.factory, port)
        except (OSError, serial.SerialException) as error:
            raise DeviceError(f"Could not open {port}. Close any serial monitor and check the cable. {error}") from error
        self.state.connected = True
        self.reader = asyncio.create_task(self._read())
        try:
            await self.refresh()
            await self.network_status()
        except DeviceError:
            await self.disconnect()
            raise DeviceError("No compatible bridge response. Check the firmware, reset the ESP if a previous "
                              "Wi-Fi setup was interrupted, then reconnect. No motor commands were sent.")
        self.emit("state", self.state)

    async def disconnect(self):
        self.state.connected = False
        self.state.identified = False
        self.wake.set()
        if self.reader:
            self.reader.cancel()
            try:
                await self.reader
            except asyncio.CancelledError:
                pass
            self.reader = None
        if self.transport:
            transport, self.transport = self.transport, None
            await asyncio.to_thread(transport.close)
        self.response = self.partial = ""
        self.emit("state", self.state)

    async def _read(self):
        try:
            while self.state.connected:
                data = await asyncio.to_thread(self.transport.read)
                if data:
                    self.feed(self.decoder.decode(data))
                else:
                    await asyncio.sleep(0.02)
        except (OSError, serial.SerialException) as error:
            self.state.connected = False
            self.state.identified = False
            self.wake.set()
            self.emit("error", f"USB connection lost. Refresh ports and reconnect. {error}")
            self.emit("state", self.state)

    def feed(self, text):
        self.response = (self.response + text)[-65536:]
        self.partial += text.replace("\r", "")
        while "\n" in self.partial:
            line, self.partial = self.partial.split("\n", 1)
            self._parse(line)
            if not self.sensitive and line.strip():
                self.emit("log", self.redact(line))
        self.partial = self.partial[-4096:]
        self.wake.set()

    def _parse(self, line):
        match = re.fullmatch(r"radio: (ready|FAILED)", line.strip())
        if match:
            # A status response is a fresh snapshot, not an accumulation across devices.
            self.previous_shades = dict(self.state.shades)
            self.state.shades.clear()
            self.state.radio = match[1] == "ready"
        match = re.fullmatch(r"\s*(\d+)\s+(.+?)\s+addr\s+([0-9A-Fa-f]{6})\s+code\s+(\d+)"
                             r"\s+pos\s+(\d+)%\s+(not calibrated|calibrated)\s*", line)
        if match:
            index, name, address, code, position, calibration = match.groups()
            self.state.shades[int(index)] = Shade(int(index), name, address.upper(), int(code),
                                                   int(position), calibration == "calibrated")
            if previous := self.previous_shades.get(int(index)):
                self.state.shades[int(index)].motion = previous.motion
                self.state.shades[int(index)].target = previous.target
            self.state.identified = True
        match = re.fullmatch(r"\[(\d+)\s+.+?\]\s+(\d+)%\s*->\s*(\d+)%\s+(opening|closing|stopped)", line)
        if match and int(match[1]) in self.state.shades:
            shade = self.state.shades[int(match[1])]
            shade.position, shade.target, shade.motion = int(match[2]), int(match[3]), match[4]
        if "IPv4" in line and (ip := re.search(r"\b(?:\d{1,3}\.){3}\d{1,3}\b", line)):
            self.state.network = ip[0] if ip[0] != "0.0.0.0" else "Not connected"
        if "No Paired Controllers" in line:
            self.state.paired = False
        elif "Paired Controller:" in line:
            self.state.paired = True
        self.emit("state", self.state)

    async def _send(self, line):
        if not self.state.connected or not self.transport:
            raise DeviceError("Connect to a bridge first.")
        try:
            await asyncio.to_thread(self.transport.write, (line + "\n").encode())
        except (OSError, serial.SerialException) as error:
            raise DeviceError("USB write failed. Check the connection; the command was not retried.") from error

    async def _wait(self, pattern, timeout=5):
        deadline = time.monotonic() + timeout
        while True:
            self.wake.clear()
            match = re.search(pattern, self.response, re.M)
            if match:
                return match
            if not self.state.connected:
                raise DeviceError("USB disconnected before a response was received. The command was not retried.")
            try:
                await asyncio.wait_for(self.wake.wait(), max(0, deadline - time.monotonic()))
            except asyncio.TimeoutError:
                raise DeviceError("The ESP did not confirm the command in time. Check the device; "
                                  "it may have executed. The command was not retried.") from None

    async def _command(self, command, pattern, timeout=5):
        if self.lock.locked():
            raise DeviceError("Wait for the current device operation to finish.")
        async with self.lock:
            self.response = ""
            await self._send(command)
            return await self._wait(pattern, timeout)

    async def refresh(self):
        await self._command("@S", r"^\s*\d+\s+.+?\s+addr [0-9A-Fa-f]{6}\s+code\s+\d+\s+pos\s+\d+%", 6)
        # The status command has no terminator; let the remainder of its short burst arrive.
        await asyncio.sleep(0.15)

    async def network_status(self):
        await self._command("s", r"\*\*\* End Status \*\*\*")

    def _motor(self, index):
        if not self.state.connected or not self.state.identified:
            raise DeviceError("Connect to a recognized Somfy bridge first.")
        if self.state.radio is not True:
            raise DeviceError("The radio is not ready. Check the wiring and run Radio check first.")
        if index not in self.state.shades:
            raise DeviceError("Select a shade reported by this device.")

    async def move(self, index, percent):
        self._motor(index)
        if not isinstance(percent, int) or not 0 <= percent <= 100:
            raise DeviceError("Choose a percentage between 0 and 100.")
        await self._command(f"@M{index} {percent}", rf"Shade {index} \(.+?\): \d+% -> {percent}%")
        return "Target requested. Position is estimated; watch the motor to confirm movement."

    async def pair(self, index):
        self._motor(index)
        await self._command(f"@P{index}", rf"\[radio\] shade {index} cmd 0x8 addr [0-9A-Fa-f]{{6}} code \d+")
        return "PROG transmitted once. Did the selected shade jog a second time?"

    async def favourite(self, index):
        self._motor(index)
        result = await self._command(f"@F{index}", rf"(?:\[radio\] shade {index} cmd 0x1 addr .+|Shade {index} is moving\..+)")
        if "is moving" in result[0]:
            raise DeviceError("The shade is moving. Wait until it stops before checking My.")
        return "My transmitted. Watch where it stops; this diagnostic does not update the position model."

    async def verify(self):
        if not self.state.identified:
            raise DeviceError("Connect to a recognized Somfy bridge first.")
        result = await self._command("@R", r"\[radio\] (?:(\d+) mismatches of (\d+) registers|not ready, nothing to verify)")
        if result[1] is None:
            raise DeviceError("The CC1101 is not ready. Check power and SPI wiring.")
        if int(result[1]):
            raise DeviceError(f"Radio check found {result[1]} mismatches. See the diagnostic log.")
        return f"All {result[2]} radio registers match. This checks SPI, not RF reception by a motor."

    async def set_code(self, value):
        if self.lock.locked():
            raise DeviceError("Wait for the current device operation to finish.")
        if not self.state.identified:
            raise DeviceError("Connect to a recognized Somfy bridge first.")
        code = pairing_code(value)
        self.secrets.extend([code, f"{code[:3]}-{code[3:5]}-{code[5:]}"])
        self.sensitive = True
        try:
            result = await self._command(f"S {code}", r"New Code Saved!|Invalid request to change Setup Code", 15)
            if "Invalid" in result[0]:
                raise DeviceError("The ESP rejected this setup code.")
            return "Setup code saved. In Apple Home, add Somfy RTS Bridge using the code you entered."
        finally:
            # Discard incomplete credential output; never forward it to the log.
            self.response = self.partial = ""
            self.sensitive = False

    async def wifi(self, ssid, password):
        validate_wifi(ssid, password)
        if self.lock.locked():
            raise DeviceError("Wait for the current device operation to finish.")
        if not self.state.identified:
            raise DeviceError("Connect to a recognized Somfy bridge first.")
        if any(s.motion in {"opening", "closing"} for s in self.state.shades.values()):
            raise DeviceError("Wait until all shades stop before configuring Wi-Fi.")
        self.secrets.extend([ssid, password])
        self.sensitive = True
        try:
            async with self.lock:
                self.response = ""
                await self._send("W")
                await self._wait(r">>> WiFi SSID:", 30)
                networks = re.findall(r"^\s*(\d+)\) (.+?)\r?$", self.response, re.M)
                chosen = next((number for number, name in networks if name == ssid), None)
                prefix = re.match(r"\s*\+?(\d+)", ssid)
                if chosen is None and prefix and 0 < int(prefix[1]) <= len(networks):
                    raise DeviceError("This numeric network name was not found in the scan. HomeSpan would "
                                      "interpret it as a menu number. Move closer to the access point and retry.")
                await self._send(chosen or ssid)
                await self._wait(r">>> WiFi PASS:", 10)
                await self._send(password)
                await self._wait(r"WiFi Credentials SAVED!", 10)
        except (DeviceError, asyncio.CancelledError):
            # The firmware may still be waiting for a password. Never let status
            # polling or another command be consumed as that password.
            await self.disconnect()
            raise DeviceError("Wi-Fi setup was interrupted. Reset the ESP before reconnecting; "
                              "it may still be waiting for credentials. No commands were retried.") from None
        finally:
            self.response = self.partial = ""
            self.sensitive = False
        await self.disconnect()
        return "Wi-Fi credentials saved. The ESP is restarting. Refresh ports, reconnect, then check Apple Home."
