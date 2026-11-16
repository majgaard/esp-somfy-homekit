from copy import deepcopy
from pathlib import Path
import secrets

from textual.app import ComposeResult
from textual.containers import Horizontal, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Button, Input, Label, Select, Static, Switch, TextArea
from rich.text import Text

from .config import Installation, ShadeSettings


class Confirm(ModalScreen[bool]):
    BINDINGS = [("escape", "cancel", "Cancel")]

    def __init__(self, title, body, accept="Continue"):
        super().__init__()
        self.heading, self.body, self.accept = title, body, accept

    def compose(self) -> ComposeResult:
        with VerticalScroll(classes="dialog"):
            yield Label(Text(self.heading), classes="dialog-title")
            yield Static(Text(self.body))
            with Horizontal(classes="buttons"):
                yield Button("Cancel", id="cancel")
                yield Button(self.accept, id="accept", variant="primary")

    def on_button_pressed(self, event: Button.Pressed):
        self.dismiss(event.button.id == "accept")

    def action_cancel(self):
        self.dismiss(False)


class PairScreen(ModalScreen[bool]):
    BINDINGS = [("escape", "cancel", "Cancel")]

    def __init__(self, shade):
        super().__init__()
        self.shade = shade

    def compose(self) -> ComposeResult:
        with VerticalScroll(classes="dialog"):
            yield Label(Text(f"Pair {self.shade.name}"), classes="dialog-title")
            yield Static("1. Select ONLY this shade on the existing Somfy remote.\n\n"
                         "2. Hold its PROG button for about 2–3 seconds, until the motor jogs.\n\n"
                         "3. Click Send PROG below while the motor is in learning mode.\n\n"
                         "Repeating this for an already-paired address can remove the pairing. "
                         "The ESP cannot confirm a pairing; you must watch for the second jog.")
            with Horizontal(classes="buttons"):
                yield Button("Cancel", id="cancel")
                yield Button("Send PROG once", id="send-prog", variant="primary")

    def on_button_pressed(self, event: Button.Pressed):
        self.dismiss(event.button.id == "send-prog")

    def action_cancel(self):
        self.dismiss(False)


class ConfigScreen(ModalScreen[str | None]):
    BINDINGS = [("escape", "cancel", "Cancel")]

    def __init__(self, project: Path, config: Installation, demo=False):
        super().__init__()
        self.project, self.demo = project, demo
        self.config = deepcopy(config)
        self.original_names, self.original_address = config.names[:], config.address
        self.current_index = 0

    def compose(self) -> ComposeResult:
        config = self.config
        shade = config.shades[0]
        with VerticalScroll(classes="dialog config-dialog"):
            yield Label("Installation configuration", classes="dialog-title")
            yield Static("Saved locally; applied to the ESP only after Build & upload. "
                         "Keep shade order and addresses stable for an existing installation.")
            yield Label("Shade names · one per line, in accessory order")
            yield TextArea("\n".join(config.names), id="names")
            yield Button("Update shade list", id="update-shades")
            yield Label("First virtual remote address · hexadecimal")
            with Horizontal(classes="field-row"):
                yield Input(f"{config.address:06X}", id="address")
                yield Button("Generate for new bridge", id="generate")
            yield Label("Settings for this shade")
            yield Select([(Text(name), i) for i, name in enumerate(config.names)],
                         value=0, allow_blank=False, id="settings-shade")
            yield Label("Opening / closing time · seconds, between this shade's chosen endpoints")
            with Horizontal(classes="field-row"):
                yield Input(f"{shade.open_ms / 1000:g}", id="open-time")
                yield Input(f"{shade.close_ms / 1000:g}", id="close-time")
            with Horizontal(classes="switch-row"):
                yield Switch(shade.favourite, id="favourite-mode")
                yield Label("Close to My (must be programmed on this motor)")
            with Horizontal(classes="switch-row"):
                yield Switch(shade.open_limit, id="open-limit")
                yield Label("This motor stops itself at the fully open position")
            with Horizontal(classes="switch-row"):
                yield Switch(shade.close_limit, id="close-limit")
                yield Label("This motor stops itself at closed (when My close is off)")
            yield Static("", id="config-error")
            with Horizontal(classes="buttons"):
                yield Button("Cancel", id="cancel")
                yield Button("Save configuration", id="save-config", variant="primary", disabled=self.demo)

    def store_current(self):
        shade = ShadeSettings(
            round(float(self.query_one("#open-time", Input).value) * 1000),
            round(float(self.query_one("#close-time", Input).value) * 1000),
            self.query_one("#favourite-mode", Switch).value,
            self.query_one("#open-limit", Switch).value,
            self.query_one("#close-limit", Switch).value,
        )
        shade.validate()
        self.config.shades[self.current_index] = shade

    def show_current(self):
        shade = self.config.shades[self.current_index]
        self.query_one("#open-time", Input).value = f"{shade.open_ms / 1000:g}"
        self.query_one("#close-time", Input).value = f"{shade.close_ms / 1000:g}"
        self.query_one("#favourite-mode", Switch).value = shade.favourite
        self.query_one("#open-limit", Switch).value = shade.open_limit
        self.query_one("#close-limit", Switch).value = shade.close_limit

    def on_select_changed(self, event: Select.Changed):
        if event.select.id != "settings-shade" or not self.is_mounted:
            return
        if event.value is Select.NULL or event.value != event.select.value or event.value == self.current_index:
            return
        try:
            self.store_current()
        except (ValueError, OverflowError) as error:
            event.select.value = self.current_index
            self.query_one("#config-error", Static).update(Text(str(error)))
            return
        self.current_index = event.value
        self.show_current()
        self.query_one("#config-error", Static).update("")

    def update_shades(self):
        self.store_current()
        names = [line.strip() for line in self.query_one("#names", TextArea).text.splitlines() if line.strip()]
        self.config.resize(names)
        self.current_index = min(self.current_index, len(names) - 1)
        select = self.query_one("#settings-shade", Select)
        select.set_options([(Text(name), i) for i, name in enumerate(names)])
        select.value = self.current_index
        self.show_current()

    def on_button_pressed(self, event: Button.Pressed):
        if event.button.id == "cancel":
            self.dismiss(None)
        elif event.button.id == "generate":
            self.app.push_screen(Confirm("New virtual remote identity", "Only generate a new address for a new "
                                 "bridge or a deliberate re-pairing. Existing motors will not recognize it.",
                                 "Generate address"), self.generate)
        elif event.button.id == "update-shades":
            try:
                self.update_shades()
                self.query_one("#config-error", Static).update("")
            except (ValueError, OverflowError) as error:
                self.query_one("#config-error", Static).update(Text(str(error)))
        elif event.button.id == "save-config":
            try:
                self.update_shades()
                self.config.address = int(self.query_one("#address", Input).value, 16)
                self.config.validate()
            except (ValueError, OverflowError) as error:
                self.query_one("#config-error", Static).update(Text(str(error)))
                return
            changed_identity = self.config.names != self.original_names or self.config.address != self.original_address
            if changed_identity:
                self.app.push_screen(Confirm("Change shade identities?", "Changing names/order/count or addresses "
                                     "can affect existing HomeKit accessories and motor pairings. "
                                     "Keep the old values for an already-working bridge.", "Save changes"),
                                     lambda yes: self.save() if yes else None)
            else:
                self.save()

    def generate(self, yes):
        if yes:
            self.query_one("#address", Input).value = f"{secrets.randbelow(0xFFFF00) + 1:06X}"

    def save(self):
        try:
            self.config.save(self.project)
        except (OSError, ValueError) as error:
            self.query_one("#config-error", Static).update(Text(str(error)))
        else:
            self.dismiss("Per-shade settings saved locally. Build & upload when ready to apply them.")

    def action_cancel(self):
        self.dismiss(None)
