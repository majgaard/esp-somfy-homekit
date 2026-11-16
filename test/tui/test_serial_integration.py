"""Exercise pySerial against a pseudo-terminal; never opens physical hardware."""
import asyncio
import os
import select
import threading
import unittest

from somfy_setup.device import BridgeClient


@unittest.skipUnless(os.name == "posix", "Pseudo-terminals require POSIX")
class SerialIntegrationTests(unittest.IsolatedAsyncioTestCase):
    async def test_real_serial_transport_with_fragmented_firmware_output(self):
        master, slave = os.openpty()
        stop = threading.Event()
        commands = []

        def firmware():
            pending = b""
            while not stop.is_set():
                if not select.select([master], [], [], 0.05)[0]:
                    continue
                try:
                    pending += os.read(master, 4096)
                    while b"\n" in pending:
                        line, pending = pending.split(b"\n", 1)
                        commands.append(line)
                        if line == b"@S":
                            os.write(master, b"radio: rea")
                            os.write(master, b"dy\n  0 Test Shade addr AABBCC code 10 pos 100% calibrated\n")
                        elif line == b"s":
                            os.write(master, b"IP Addresses: IPv4 = 192.0.2.25\nNo Paired Controllers\n*** End Status ***\n")
                        elif line == b"@M0 40":
                            os.write(master, b"Shade 0 (Test Shade): 100% -> 40%\n")
                            os.write(master, b"[0 Test Shade ] 40% -> 40% stopped\n")
                except OSError:
                    return

        thread = threading.Thread(target=firmware, daemon=True)
        thread.start()
        client = BridgeClient()
        try:
            await client.connect(os.ttyname(slave))
            self.assertEqual(client.state.shades[0].address, "AABBCC")
            self.assertFalse(client.state.paired)
            await client.move(0, 40)
            await asyncio.sleep(0.1)
            self.assertEqual(client.state.shades[0].position, 40)
            self.assertEqual(commands, [b"@S", b"s", b"@M0 40"])
        finally:
            await client.disconnect()
            stop.set()
            await asyncio.to_thread(thread.join, 1)
            os.close(master)
            os.close(slave)
