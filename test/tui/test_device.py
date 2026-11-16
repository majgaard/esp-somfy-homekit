import asyncio
import queue
import unittest

from somfy_setup.device import BridgeClient, DeviceError, pairing_code, validate_wifi


STATUS = "radio: ready\n  0 Left Shade     addr 100000 code 145 pos 100% calibrated\n"


class FakeSerial:
    def __init__(self, port):
        self.incoming = queue.Queue()
        self.sent = []
        self.closed = False
        self.mode = "normal"
        self.drop = False

    def read(self):
        try:
            return self.incoming.get(timeout=0.01)
        except queue.Empty:
            if self.drop:
                raise OSError("unplugged")
            return b""

    def write(self, data):
        self.sent.append(data)
        command = data.decode().strip()
        if self.mode == "ssid":
            self.mode = "password"
            self.incoming.put(b"Selected SSID\n>>> WiFi ")
            self.incoming.put(b"PASS: ")
        elif self.mode == "password":
            self.mode = "normal"
            self.incoming.put(data + b"WiFi Credentials SAVED! Restarting\n")
        elif command == "@S":
            self.incoming.put(b"Some unrelated boot log\nradio: rea")
            self.incoming.put(STATUS.split("radio: rea")[1].encode())
        elif command == "@R":
            self.incoming.put(b"  PATABLE off 00 on C0 ok\n[radio] 0 mismatches of 34 registers\n")
        elif command == "s":
            self.incoming.put(b"IP Addresses: IPv4 = 192.0.2.20\nPaired Controller: private-id LTPK: private-key\n*** End Status ***\n")
        elif command == "@P0":
            self.incoming.put(b"Sending PROG to shade 0\n[radio] shade 0 cmd 0x8 addr 100000 code 145\n")
        elif command.startswith("@M0 "):
            target = command.split()[1]
            self.incoming.put(f"Shade 0 (Left Shade): 100% -> {target}%\n".encode())
        elif command == "@F0":
            self.incoming.put(b"[radio] shade 0 cmd 0x1 addr 100000 code 146\n")
        elif command == "W":
            self.mode = "ssid"
            self.incoming.put(b"Scanning\n  1) Other network\n  2) 1Home\n>>> WiFi SS")
            self.incoming.put(b"ID: ")
        elif command.startswith("S "):
            self.incoming.put(f"Generating SRP for {command[2:]} ... New Code Saved!\n".encode())
            self.incoming.put(b"Setup Payload: X-HM://PRIVATE\n")

    def close(self):
        self.closed = True


class DeviceTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.events = []
        self.client = BridgeClient(lambda kind, value: self.events.append((kind, value)), FakeSerial)
        await self.client.connect("fake")
        self.wire = self.client.transport

    async def asyncTearDown(self):
        await self.client.disconnect()

    async def test_fragmented_handshake_and_status(self):
        self.assertTrue(self.client.state.identified)
        self.assertTrue(self.client.state.radio)
        self.assertEqual(self.client.state.shades[0].name, "Left Shade")
        self.assertEqual(self.wire.sent, [b"@S\n", b"s\n"])
        self.assertEqual(self.client.state.network, "192.0.2.20")
        self.assertTrue(self.client.state.paired)

    async def test_commands_are_sent_once_and_verify_is_interpreted(self):
        await self.client.pair(0)
        await self.client.move(0, 45)
        await self.client.favourite(0)
        self.assertIn("34", await self.client.verify())
        self.assertEqual(self.wire.sent, [b"@S\n", b"s\n", b"@P0\n", b"@M0 45\n", b"@F0\n", b"@R\n"])

    async def test_wifi_handles_prompts_without_newlines_and_numeric_ssid(self):
        message = await self.client.wifi("1Home", "private-password")
        self.assertIn("restarting", message)
        self.assertEqual(self.wire.sent, [b"@S\n", b"s\n", b"W\n", b"2\n", b"private-password\n"])
        self.assertFalse(self.client.state.connected)
        log = "\n".join(value for kind, value in self.events if kind == "log")
        self.assertNotIn("private-password", log)
        self.assertNotIn("1Home", log)
        self.assertEqual(self.client.response, "")

    async def test_code_echo_and_qr_are_not_logged(self):
        await self.client.set_code("357-24-680")
        await asyncio.sleep(0.1)
        log = "\n".join(value for kind, value in self.events if kind == "log")
        self.assertNotIn("35724680", log)
        self.assertNotIn("X-HM://PRIVATE", log)
        self.assertNotIn("private-id", log)
        self.assertNotIn("private-key", log)
        self.assertEqual(self.wire.sent[-1], b"S 35724680\n")

    async def test_motion_and_snapshot_replace_old_shades(self):
        self.client.feed("[0 Left Shade  ]  72% ->  50%  closing\n")
        self.assertEqual(self.client.state.shades[0].motion, "closing")
        await self.client.refresh()
        self.assertEqual(self.client.state.shades[0].motion, "closing")
        self.client.feed("radio: FAILED\n  1 Office Shade addr ABCDEF code 1 pos 50% not calibrated\n")
        self.assertNotIn(0, self.client.state.shades)
        self.assertFalse(self.client.state.shades[1].calibrated)

    async def test_unplug_wakes_waiters_and_invalidates_connection(self):
        self.wire.drop = True
        await asyncio.sleep(0.1)
        self.assertFalse(self.client.state.connected)
        self.assertFalse(self.client.state.identified)
        with self.assertRaises(DeviceError):
            await self.client.move(0, 50)

    async def test_utf8_names_can_cross_serial_chunk_boundaries(self):
        self.wire.incoming.put(b"  0 Caf\xc3")
        self.wire.incoming.put(b"\xa9 Shade addr 100000 code 145 pos 100% calibrated\n")
        await asyncio.sleep(0.1)
        self.assertEqual(self.client.state.shades[0].name, "Café Shade")

    async def test_radio_failure_and_invalid_actions_send_nothing(self):
        for percent in [-1, 101]:
            with self.assertRaises(DeviceError):
                await self.client.move(0, percent)
        self.client.state.radio = False
        with self.assertRaises(DeviceError):
            await self.client.pair(0)
        self.assertEqual(self.wire.sent, [b"@S\n", b"s\n"])

    async def test_timeout_does_not_retry_pair(self):
        original_wait = self.client._wait

        async def short_wait(pattern, timeout=5):
            return await original_wait("response that will never arrive", 0.03)

        self.client._wait = short_wait
        with self.assertRaises(DeviceError):
            await self.client.pair(0)
        self.assertEqual(self.wire.sent.count(b"@P0\n"), 1)

    async def test_wifi_failure_disconnects_before_any_other_command(self):
        original_wait = self.client._wait

        async def fail_at_password(pattern, timeout=5):
            if "PASS" in pattern:
                raise DeviceError("timeout")
            return await original_wait(pattern, timeout)

        self.client._wait = fail_at_password
        with self.assertRaisesRegex(DeviceError, "Reset the ESP"):
            await self.client.wifi("1Home", "private-password")
        self.assertTrue(self.wire.closed)
        with self.assertRaises(DeviceError):
            await self.client.refresh()
        self.assertEqual(self.wire.sent, [b"@S\n", b"s\n", b"W\n", b"2\n"])

    async def test_sensitive_transactions_cannot_overlap(self):
        async with self.client.lock:
            self.client.sensitive = True
            with self.assertRaises(DeviceError):
                await self.client.set_code("35724680")
            self.assertTrue(self.client.sensitive)


class ValidationTests(unittest.TestCase):
    def test_codes_and_wifi_reject_line_injection_and_invalid_lengths(self):
        self.assertEqual(pairing_code("357-24-680"), "35724680")
        for code in ["12345678", "00000000", "123", "35724680\nW", "３５７２４６８０"]:
            with self.assertRaises(DeviceError):
                pairing_code(code)
        for ssid, password in [("", "abcdefgh"), ("Room\n@P0", "abcdefgh"), ("Room", "short"),
                               ("é" * 17, "abcdefgh"), ("Room", "password\rW")]:
            with self.assertRaises(DeviceError):
                validate_wifi(ssid, password)
