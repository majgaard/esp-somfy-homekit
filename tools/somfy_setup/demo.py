"""Interactive demo: no serial devices are opened and no files are changed."""
import asyncio

from .device import BridgeClient, DeviceError, Shade, State, pairing_code, validate_wifi


class DemoClient(BridgeClient):
    async def connect(self, port="demo"):
        self.state = State(True, True, True, {
            0: Shade(0, "Left Shade", "100000", 145, 100, True, 100, "stopped"),
            1: Shade(1, "Middle Shade", "100001", 172, 65, True, 65, "stopped"),
            2: Shade(2, "Right Shade", "100002", 158, 0, True, 0, "stopped"),
        }, "demo", "192.0.2.10 (demo)")
        self.state.paired = True
        self.emit("state", self.state)
        self.emit("log", "Demo bridge connected. No hardware is accessed.")

    async def refresh(self):
        self.emit("state", self.state)

    async def network_status(self):
        self.state.paired = True
        self.emit("state", self.state)

    async def move(self, index, percent):
        self._motor(index)
        shade = self.state.shades[index]
        shade.target = percent
        shade.code += 1
        while shade.position != percent:
            shade.motion = "opening" if percent > shade.position else "closing"
            shade.position += max(-5, min(5, percent - shade.position))
            self.emit("state", self.state)
            await asyncio.sleep(0.05)
        shade.motion = "stopped"
        self.emit("state", self.state)
        return "Demo move complete. No motor command was sent."

    async def pair(self, index):
        self._motor(index)
        return "Demo: PROG transmitted once. Did the selected shade jog a second time?"

    async def favourite(self, index):
        self._motor(index)
        return "Demo: My transmitted. Watch where it stops."

    async def verify(self):
        return "Demo: all 34 radio registers match."

    async def set_code(self, value):
        pairing_code(value)
        return "Demo: setup code accepted. Nothing was stored."

    async def wifi(self, ssid, password):
        validate_wifi(ssid, password)
        return "Demo: Wi-Fi details accepted. Nothing was sent or stored."
