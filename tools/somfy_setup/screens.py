from pathlib import Path
import secrets

from textual.app import ComposeResult
from textual.containers import Horizontal, VerticalScroll
from textual.screen import ModalScreen
from textual.widgets import Button, Input, Label, Static, Switch, TextArea
from rich.text import Text

from .config import Installation


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
        self.project, self.config, self.demo = project, config, demo

    def compose(self) -> ComposeResult:
        config = self.config
        with VerticalScroll(classes="dialog config-dialog"):
            yield Label("Installation configuration", classes="dialog-title")
            yield Static("Saved locally; applied to the ESP only after Build & upload. "
                         "Keep shade order and addresses stable for an existing installation.")
            yield Label("Shade names · one per line, in accessory order")
            yield TextArea("\n".join(config.names), id="names")
            yield Label("First virtual remote address · hexadecimal")
            with Horizontal(classes="field-row"):
                yield Input(f"{config.address:06X}", id="address")
                yield Button("Generate for new bridge", id="generate")
            yield Label("Opening / closing time · seconds, between your chosen endpoints")
            with Horizontal(classes="field-row"):
                yield Input(f"{config.open_ms / 1000:g}", id="open-time")
                yield Input(f"{config.close_ms / 1000:g}", id="close-time")
            with Horizontal(classes="switch-row"):
                yield Switch(config.favourite, id="favourite-mode")
                yield Label("Close to My (must be programmed on every motor)")
            with Horizontal(classes="switch-row"):
                yield Switch(config.open_limit, id="open-limit")
                yield Label("Motor stops itself at the fully open position")
            with Horizontal(classes="switch-row"):
                yield Switch(config.close_limit, id="close-limit")
                yield Label("Motor stops itself at closed (used when My close is off)")
            yield Static("", id="config-error")
            with Horizontal(classes="buttons"):
                yield Button("Cancel", id="cancel")
                yield Button("Save configuration", id="save-config", variant="primary", disabled=self.demo)

    def on_button_pressed(self, event: Button.Pressed):
        if event.button.id == "cancel":
            self.dismiss(None)
        elif event.button.id == "generate":
            self.app.push_screen(Confirm("New virtual remote identity", "Only generate a new address for a new "
                                 "bridge or a deliberate re-pairing. Existing motors will not recognize it.",
                                 "Generate address"), self.generate)
        elif event.button.id == "save-config":
            try:
                config = Installation(
                    [line.strip() for line in self.query_one("#names", TextArea).text.splitlines() if line.strip()],
                    int(self.query_one("#address", Input).value, 16),
                    round(float(self.query_one("#open-time", Input).value) * 1000),
                    round(float(self.query_one("#close-time", Input).value) * 1000),
                    self.query_one("#favourite-mode", Switch).value,
                    self.query_one("#open-limit", Switch).value,
                    self.query_one("#close-limit", Switch).value,
                )
                config.validate()
            except (ValueError, OverflowError) as error:
                self.query_one("#config-error", Static).update(Text(str(error)))
                return
            changed_identity = config.names != self.config.names or config.address != self.config.address
            if changed_identity:
                self.app.push_screen(Confirm("Change shade identities?", "Changing names/order/count or addresses "
                                     "can affect existing HomeKit accessories and motor pairings. "
                                     "Keep the old values for an already-working bridge.", "Save changes"),
                                     lambda yes: self.save(config) if yes else None)
            else:
                self.save(config)

    def generate(self, yes):
        if yes:
            self.query_one("#address", Input).value = f"{secrets.randbelow(0xFFFF00) + 1:06X}"

    def save(self, config):
        try:
            config.save(self.project)
        except (OSError, ValueError) as error:
            self.query_one("#config-error", Static).update(Text(str(error)))
        else:
            self.dismiss("Configuration saved locally. Build & upload when ready to apply it.")

    def action_cancel(self):
        self.dismiss(None)
