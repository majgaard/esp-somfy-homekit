import asyncio
from pathlib import Path
import sys

from rich.text import Text
from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.message import Message
from textual.widgets import (Button, DataTable, Footer, Header, Input, Label,
                             RichLog, Select, Static, TabbedContent, TabPane)

from .config import Installation
from .demo import DemoClient
from .device import BridgeClient, DeviceError, ports, pairing_code, validate_wifi
from .screens import ConfigScreen, Confirm, PairScreen


class DeviceEvent(Message):
    def __init__(self, kind, value):
        super().__init__()
        self.kind, self.value = kind, value


class SetupApp(App):
    TITLE = "Somfy · HomeKit setup"
    SUB_TITLE = "USB companion"
    CSS_PATH = Path(__file__).with_name("setup.tcss")
    BINDINGS = [("ctrl+q", "quit", "Quit"), ("ctrl+r", "refresh", "Refresh status")]

    def __init__(self, project: Path, port=None, demo=False, client=None):
        super().__init__()
        self.project, self.initial_port, self.demo = project, port, demo
        self.busy = False
        self.process = None
        self.shade_options = []
        self.client = client or (DemoClient if demo else BridgeClient)()
        self.client.emit = lambda kind, value: self.post_message(DeviceEvent(kind, value))

    def compose(self) -> ComposeResult:
        yield Header()
        with Vertical(id="workspace"):
            with Horizontal(id="connection"):
                yield Select([], prompt="Select a USB serial device", id="port")
                yield Button("Find devices", id="find-ports")
                yield Button("Connect", id="connect", variant="primary")
            yield Static("", id="overview")
            yield Static("Choose a USB device and Connect to begin. Configuration can be edited before connecting.",
                         id="notice", markup=False)
            with TabbedContent(initial="shades-tab", id="tabs"):
                with TabPane("Shades", id="shades-tab"):
                    with VerticalScroll():
                        yield Static("PAIR & TEST", classes="eyebrow")
                        yield Static("One bridge. One tile per shade.\n"
                                     "Pair each motor with your existing remote, then try a target position.",
                                     classes="intro")
                        yield DataTable(id="shades", cursor_type="row", zebra_stripes=True)
                        yield Select([], prompt="Select a shade", id="shade")
                        with Horizontal(classes="buttons"):
                            yield Button("Pair shade…", id="pair", variant="primary")
                            yield Button("Check My…", id="my")
                            yield Button("Radio check", id="verify")
                            yield Button("Refresh status", id="refresh")
                        with Horizontal(classes="buttons"):
                            yield Button("Open · 100%", id="open")
                            yield Button("Close · 0%", id="close")
                            yield Input("50", id="target", type="integer", placeholder="% open")
                            yield Button("Move to %", id="move")
                        yield Static("Positions are estimates. RTS provides no feedback. Keep your physical "
                                     "remote nearby; this firmware has no dedicated serial Stop command.", classes="hint")
                with TabPane("Wi-Fi & Apple Home", id="network-tab"):
                    with VerticalScroll():
                        yield Static("CONNECT YOUR HOME", classes="eyebrow")
                        yield Static("1 · Wi-Fi", classes="section-title")
                        yield Static("Use your 2.4 GHz network. Start with all shades stopped: "
                                     "the ESP pauses normal control while Wi-Fi setup is running.", classes="hint")
                        yield Label("Network name")
                        yield Input(placeholder="Your Wi-Fi network", id="ssid")
                        yield Label("Wi-Fi password")
                        yield Input(password=True, placeholder="Kept in memory only; never logged", id="password")
                        yield Button("Save Wi-Fi & restart ESP", id="wifi", variant="primary")
                        yield Static("2 · Apple Home", classes="section-title")
                        yield Static("Choose a personal eight-digit setup code before adding the bridge. "
                                     "Changing this code does not remove existing HomeKit pairings.", classes="hint")
                        yield Label("HomeKit setup code")
                        yield Input(password=True, placeholder="123-45-679", id="code")
                        yield Button("Save HomeKit setup code", id="save-code")
                        yield Static("Then open Apple Home → Add Accessory → More options → Somfy RTS Bridge. "
                                     "Enter your code and follow the prompt for an uncertified accessory. "
                                     "A HomePod or Apple TV Home Hub is required.", classes="hint")
                with TabPane("Configuration & firmware", id="config-tab"):
                    with VerticalScroll():
                        yield Static("MAKE IT YOURS", classes="eyebrow")
                        yield Static("Shade names, travel times & close behavior", classes="section-title")
                        yield Static("Edit the installation in a form. Settings are saved to the ignored local "
                                     "configuration and applied when you upload firmware. "
                                     "All shades currently share the same travel times and limit settings.", classes="intro")
                        yield Button("Edit installation…", id="edit-config", variant="primary")
                        yield Static("USB firmware update", classes="section-title")
                        yield Static("Build the current project and upload to the selected USB device. "
                                     "The TUI releases its serial connection before upload. "
                                     "The existing partition layout preserves rolling codes and pairing data.", classes="intro")
                        yield Button("Build & upload…", id="flash", disabled=self.demo)
                        yield Static(Text(f"Firmware project: {self.project}"), classes="hint")
                with TabPane("Activity", id="activity-tab"):
                    yield RichLog(id="activity", max_lines=1000, markup=False, highlight=False, wrap=True)
        yield Footer()

    def on_mount(self):
        self.screen.set_class(self.size.width < 100, "compact")
        self.query_one("#shades", DataTable).add_columns("Shade", "Estimated", "Target", "Motion", "Address", "Next code")
        self.find_ports()
        self.render_state()
        self.set_interval(3, self.poll_status)
        if self.demo:
            self.message("DEMO · No USB devices are opened, no motors are moved, and configuration saving/upload is disabled.")
            self.run_operation(lambda: self.client.connect("demo"))
        elif self.initial_port:
            self.query_one("#port", Select).value = self.initial_port
            self.run_operation(lambda: self.client.connect(self.initial_port))

    def on_resize(self, event):
        self.screen.set_class(event.size.width < 100, "compact")

    def find_ports(self):
        available = [("demo", "Demo bridge · no hardware")] if self.demo else ports()
        if self.initial_port and not any(p == self.initial_port for p, _ in available):
            available.append((self.initial_port, self.initial_port))
        select = self.query_one("#port", Select)
        previous = select.value
        select.set_options([(Text(label), port) for port, label in available])
        if previous is not Select.NULL and any(p == previous for p, _ in available):
            select.value = previous
        elif len(available) == 1:
            select.value = available[0][0]
        if not available:
            self.message("No serial devices found. Connect the ESP with a USB data cable, then Find devices.")

    def message(self, text):
        self.query_one("#notice", Static).update(Text(text))
        self.query_one("#activity", RichLog).write(Text(text))

    def on_device_event(self, event: DeviceEvent):
        if event.kind == "log":
            self.query_one("#activity", RichLog).write(Text(event.value))
        elif event.kind == "error":
            self.message(event.value)
        self.render_state()

    def render_state(self):
        state = self.client.state
        radio = "ready" if state.radio is True else "not ready" if state.radio is False else "not checked"
        homekit = "paired" if state.paired is True else "unpaired" if state.paired is False else "not checked"
        self.query_one("#overview", Static).update(Text(
            f"{'● CONNECTED' if state.connected else '○ DISCONNECTED'}   ·   Radio {radio}   ·   "
            f"{len(state.shades)} shades   ·   Wi-Fi {state.network}   ·   HomeKit {homekit}"))
        self.query_one("#connect", Button).label = "Disconnect" if state.connected else "Connect"
        self.query_one("#connect", Button).disabled = self.busy
        self.query_one("#port", Select).disabled = state.connected or self.busy
        self.query_one("#find-ports", Button).disabled = self.busy or self.demo
        table = self.query_one("#shades", DataTable)
        selected_row = table.cursor_row
        table.clear()
        shades = sorted(state.shades.values(), key=lambda s: s.index)
        for shade in shades:
            table.add_row(Text(shade.name), f"{shade.position}%", "—" if shade.target is None else f"{shade.target}%",
                          shade.motion, shade.address, str(shade.code), key=str(shade.index))
        if shades:
            table.move_cursor(row=min(selected_row, len(shades) - 1), animate=False)
        options = [(s.name, s.index) for s in shades]
        if options != self.shade_options:
            self.shade_options = options
            select = self.query_one("#shade", Select)
            previous = select.value
            select.set_options([(Text(name), index) for name, index in options])
            select.value = previous if previous in state.shades else shades[0].index if shades else Select.NULL
        ready = state.connected and state.identified and not self.busy
        for key in ["pair", "my", "open", "close", "move"]:
            self.query_one(f"#{key}", Button).disabled = not (ready and state.radio is True)
        for key in ["wifi", "save-code", "verify", "refresh"]:
            self.query_one(f"#{key}", Button).disabled = not ready
        self.query_one("#edit-config", Button).disabled = self.busy
        self.query_one("#flash", Button).disabled = self.demo or self.busy

    def run_operation(self, function, done=None):
        if not self.busy:
            self.run_worker(self.perform(function, done))

    async def perform(self, function, done=None):
        if self.busy:
            return
        self.busy = True
        self.render_state()
        try:
            result = await function()
            if result:
                self.message(result)
            if done:
                done()
        except (DeviceError, OSError, ValueError) as error:
            self.message(str(error))
        finally:
            self.busy = False
            self.render_state()

    async def poll_status(self):
        if (len(self.screen_stack) == 1 and self.client.state.connected
                and self.client.state.identified and not self.busy and not self.client.busy):
            self.run_operation(self.client.refresh)

    def action_refresh(self):
        if self.client.state.connected:
            async def refresh_all():
                await self.client.refresh()
                await self.client.network_status()
            self.run_operation(refresh_all)
        else:
            self.find_ports()

    def selected_shade(self):
        value = self.query_one("#shade", Select).value
        if value is Select.NULL or value not in self.client.state.shades:
            raise DeviceError("Connect and select a shade first.")
        return self.client.state.shades[value]

    def on_data_table_row_selected(self, event: DataTable.RowSelected):
        self.query_one("#shade", Select).value = int(event.row_key.value)

    def on_button_pressed(self, event: Button.Pressed):
        key = event.button.id
        if self.busy:
            return
        try:
            if key == "find-ports":
                self.find_ports()
            elif key == "connect":
                if self.client.state.connected:
                    self.run_operation(self.client.disconnect)
                else:
                    port = self.query_one("#port", Select).value
                    if port is Select.NULL:
                        raise DeviceError("Select a USB serial device first.")
                    self.run_operation(lambda: self.client.connect(port))
            elif key == "refresh":
                self.action_refresh()
            elif key == "verify":
                self.run_operation(self.client.verify)
            elif key in {"open", "close", "move"}:
                shade = self.selected_shade()
                try:
                    percent = 100 if key == "open" else 0 if key == "close" else int(self.query_one("#target", Input).value)
                except ValueError:
                    raise DeviceError("Enter a whole percentage between 0 and 100.") from None
                if not 0 <= percent <= 100:
                    raise DeviceError("Enter a whole percentage between 0 and 100.")
                self.run_operation(lambda: self.client.move(shade.index, percent))
            elif key == "pair":
                shade = self.selected_shade()
                self.push_screen(PairScreen(shade), lambda yes: self.run_operation(
                    lambda: self.client.pair(shade.index), self.confirm_jog) if yes else None)
            elif key == "my":
                shade = self.selected_shade()
                self.push_screen(Confirm("Check My favourite", "Make sure the actual motor is stopped, including "
                                 "movement from another remote. This sends My once and may move the shade. "
                                 "It does not update the position model.", "Send My once"),
                                 lambda yes: self.run_operation(lambda: self.client.favourite(shade.index)) if yes else None)
            elif key == "wifi":
                ssid = self.query_one("#ssid", Input).value
                password = self.query_one("#password", Input).value
                validate_wifi(ssid, password)
                self.query_one("#password", Input).value = ""
                self.message("Configuring Wi-Fi. Keep USB connected; this can take 30 seconds. Private output is hidden.")
                self.run_operation(lambda: self.client.wifi(ssid, password))
            elif key == "save-code":
                code = pairing_code(self.query_one("#code", Input).value)
                self.query_one("#code", Input).value = ""
                self.message("Saving your HomeKit setup code. Private output is hidden.")
                self.run_operation(lambda: self.client.set_code(code))
            elif key == "edit-config":
                config = Installation.load(self.project)
                self.push_screen(ConfigScreen(self.project, config, self.demo),
                                 lambda result: self.message(result) if result else None)
            elif key == "flash":
                port = self.query_one("#port", Select).value
                if port is Select.NULL:
                    raise DeviceError("Select the USB device to upload to first.")
                self.push_screen(Confirm("Build & upload firmware", "This builds the current local configuration "
                                 "and uploads to the selected device. Keep power connected. "
                                 "Existing address assignments and shade order must match a paired installation.",
                                 "Build & upload"), lambda yes: self.run_operation(lambda: self.flash(port)) if yes else None)
        except (DeviceError, ValueError, OSError) as error:
            self.message(str(error))

    def confirm_jog(self):
        self.push_screen(Confirm("Watch the motor", "Did ONLY the intended shade jog a second time? "
                         "The ESP confirms transmission, not pairing. If it did not jog, check the "
                         "remote channel, learning window and antenna before another attempt.", "Yes, it jogged"),
                         lambda yes: self.message("Pairing confirmed by you. Try Open or Close while watching the shade."
                                                  if yes else "Pairing was not confirmed. No retry was sent."))

    async def flash(self, port):
        if not (self.project / "platformio.ini").exists():
            raise DeviceError("Choose the firmware repository with --project before uploading.")
        await self.client.disconnect()
        self.query_one("#tabs", TabbedContent).active = "activity-tab"
        self.message("Building and uploading. The serial connection has been released.")
        self.process = await asyncio.create_subprocess_exec(
            sys.executable, "-m", "platformio", "run", "-e", "supermini", "-t", "upload",
            "--upload-port", port, cwd=self.project,
            stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT)
        try:
            while line := await self.process.stdout.readline():
                self.query_one("#activity", RichLog).write(Text(self.client.redact(line.decode(errors="replace").rstrip())))
            code = await self.process.wait()
        finally:
            if self.process.returncode is None:
                self.process.terminate()
                await self.process.wait()
            self.process = None
        if code:
            raise DeviceError("Build/upload failed. See Activity. Install requirements-dev.txt if PlatformIO is missing.")
        self.find_ports()
        return "Upload complete. The ESP is restarting. Reconnect when its USB port returns."

    async def on_unmount(self):
        await self.client.disconnect()
        if self.process and self.process.returncode is None:
            self.process.terminate()
            await self.process.wait()
