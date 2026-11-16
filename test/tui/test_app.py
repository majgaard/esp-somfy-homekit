import asyncio
from pathlib import Path
import tempfile
import unittest
from unittest.mock import AsyncMock, patch

from textual.widgets import Button, Input, Select, Static, Switch, TabbedContent, TextArea

from somfy_setup.app import SetupApp
from somfy_setup.config import Installation
from somfy_setup.screens import ConfigScreen, Confirm, PairScreen


PROJECT = Path(__file__).resolve().parents[2]


class AppTests(unittest.IsolatedAsyncioTestCase):
    async def test_demo_pairing_requires_explicit_send_and_jog_confirmation(self):
        app = SetupApp(PROJECT, demo=True)
        with patch("somfy_setup.app.ports", side_effect=AssertionError("Demo must not enumerate USB")):
            async with app.run_test(size=(120, 48)) as pilot:
                await pilot.pause()
                self.assertEqual(len(app.client.state.shades), 3)
                await pilot.click("#pair")
                self.assertIsInstance(app.screen, PairScreen)
                await pilot.click("#send-prog")
                await pilot.pause()
                self.assertIsInstance(app.screen, Confirm)
                await pilot.click("#accept")
                await pilot.pause()
                self.assertIn("confirmed by you", str(app.query_one("#notice", Static).content))

    async def test_network_forms_validate_clear_passwords_and_never_show_them(self):
        app = SetupApp(PROJECT, demo=True)
        async with app.run_test(size=(120, 48)) as pilot:
            await pilot.pause()
            app.query_one("#tabs", TabbedContent).active = "network-tab"
            await pilot.pause()
            app.query_one("#ssid", Input).value = "PrivateNetwork"
            app.query_one("#password", Input).value = "secret-password"
            await pilot.click("#wifi")
            await pilot.pause()
            self.assertEqual(app.query_one("#password", Input).value, "")
            self.assertNotIn("secret-password", str(app.query_one("#notice", Static).content))
            app.query_one("#code", Input).value = "12345678"
            app.on_button_pressed(Button.Pressed(app.query_one("#save-code", Button)))
            self.assertIn("simple setup code", str(app.query_one("#notice", Static).content))

    async def test_demo_configuration_cannot_save_or_upload_and_modal_survives_updates(self):
        app = SetupApp(PROJECT, demo=True)
        async with app.run_test(size=(100, 40)) as pilot:
            await pilot.pause()
            app.query_one("#tabs", TabbedContent).active = "config-tab"
            await pilot.pause()
            self.assertTrue(app.query_one("#flash", Button).disabled)
            await pilot.click("#edit-config")
            self.assertIsInstance(app.screen, ConfigScreen)
            self.assertTrue(app.screen.query_one("#save-config", Button).disabled)
            app.render_state()
            await pilot.press("escape")
            self.assertNotIsInstance(app.screen, ConfigScreen)

    async def test_real_config_form_saves_to_local_header(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp)
            (project / "include").mkdir()
            (project / "include/somfy_config.h").write_text((PROJECT / "include/somfy_config.h").read_text())
            app = SetupApp(project)
            with patch("somfy_setup.app.ports", return_value=[]):
                async with app.run_test(size=(120, 48)) as pilot:
                    await pilot.pause()
                    app.query_one("#tabs", TabbedContent).active = "config-tab"
                    await pilot.pause()
                    await pilot.click("#edit-config")
                    app.screen.query_one("#open-time", Input).value = "20.5"
                    app.screen.query_one("#settings-shade", Select).value = 1
                    await pilot.pause()
                    app.screen.query_one("#close-time", Input).value = "22"
                    app.screen.query_one("#favourite-mode", Switch).value = False
                    app.screen.on_button_pressed(Button.Pressed(app.screen.query_one("#save-config", Button)))
                    await pilot.pause()
                    loaded = Installation.load(project)
                    self.assertEqual([s.open_ms for s in loaded.shades], [20500, 17900, 17900])
                    self.assertEqual([s.close_ms for s in loaded.shades], [17100, 22000, 17100])
                    self.assertEqual([s.favourite for s in loaded.shades], [True, False, True])

    async def test_switching_shades_preserves_times_flags_and_rejects_invalid_edits(self):
        app = SetupApp(PROJECT, demo=True)
        async with app.run_test(size=(120, 60)) as pilot:
            await pilot.pause()
            app.push_screen(ConfigScreen(PROJECT, Installation.load(PROJECT), demo=True))
            await pilot.pause()
            screen = app.screen
            screen.query_one("#open-time", Input).value = "22.5"
            screen.query_one("#favourite-mode", Switch).value = False
            screen.query_one("#settings-shade", Select).value = 1
            await pilot.pause()
            self.assertEqual(screen.query_one("#open-time", Input).value, "17.9")
            self.assertTrue(screen.query_one("#favourite-mode", Switch).value)
            screen.query_one("#close-time", Input).value = "25"
            screen.query_one("#settings-shade", Select).value = 0
            await pilot.pause()
            self.assertEqual(screen.query_one("#open-time", Input).value, "22.5")
            self.assertFalse(screen.query_one("#favourite-mode", Switch).value)
            self.assertEqual(screen.config.shades[1].close_ms, 25000)
            screen.query_one("#open-time", Input).value = "invalid"
            screen.query_one("#settings-shade", Select).value = 2
            await pilot.pause()
            self.assertEqual(screen.current_index, 0)
            self.assertEqual(screen.query_one("#open-time", Input).value, "invalid")

    async def test_narrow_terminal_and_disconnected_controls(self):
        app = SetupApp(PROJECT)
        with patch("somfy_setup.app.ports", return_value=[]):
            async with app.run_test(size=(80, 28)) as pilot:
                await pilot.pause()
                self.assertTrue(app.screen.has_class("compact"))
                for key in ["pair", "open", "close", "wifi", "verify"]:
                    self.assertTrue(app.query_one(f"#{key}", Button).disabled)
                await pilot.resize_terminal(120, 48)
                await pilot.pause()
                self.assertFalse(app.screen.has_class("compact"))

    async def test_first_shade_and_zero_percent_are_valid_selections(self):
        app = SetupApp(PROJECT, demo=True)
        async with app.run_test(size=(120, 48)) as pilot:
            await pilot.pause()
            with patch.object(app.client, "move", new_callable=AsyncMock, return_value="Target requested") as move:
                app.on_button_pressed(Button.Pressed(app.query_one("#close", Button)))
                await pilot.pause()
                move.assert_awaited_once_with(0, 0)

    async def test_upload_releases_serial_and_passes_port_without_shell(self):
        app = SetupApp(PROJECT, demo=True)
        process = AsyncMock()
        process.stdout.readline.side_effect = [b"Building project\n", b""]
        process.wait.return_value = 0
        process.returncode = 0
        async with app.run_test(size=(120, 48)) as pilot:
            await pilot.pause()
            with patch("asyncio.create_subprocess_exec", return_value=process) as create:
                result = await app.flash("fake port; not a shell command")
                self.assertIn("Upload complete", result)
                self.assertFalse(app.client.state.connected)
                self.assertIn("fake port; not a shell command", create.call_args.args)
                self.assertNotIn("shell", create.call_args.kwargs)
