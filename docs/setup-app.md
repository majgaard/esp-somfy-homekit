# Terminal setup app

`somfy-setup` runs on your computer and communicates with the ESP over USB
serial at 115200 baud. It works with the current firmware's commands and
HomeSpan prompts, including firmware installed before the app was introduced.
No extra network service or firmware protocol is required.

## Start

From the repository, with your virtual environment activated:

```sh
python -m pip install -r requirements-dev.txt
somfy-setup
```

Keyboard navigation uses Tab / Shift+Tab, arrows and Enter. Mouse clicks work
in terminals that support them. Ctrl+R refreshes status and Ctrl+Q exits.
Forms and tabs scroll in smaller terminals; a terminal around 100 columns
wide is more comfortable.

Optional launch arguments:

```sh
somfy-setup --demo
somfy-setup --port /dev/cu.usbmodem2101
somfy-setup --project /path/to/esp-somfy-homekit
```

Demo mode never opens serial devices, saves configuration or uploads firmware.
It simulates three shades so you can explore the screens. `--port` explicitly
selects and connects to a device. Without it, choose a port and click Connect.

For just the TUI, `python -m pip install -e .` installs Textual and pySerial.
Build & upload also needs PlatformIO from `requirements-dev.txt`. If installed
outside a source checkout, use `--project` to select the firmware repository.

## Connect a device

Connect the ESP with a USB data cable and click **Find devices**. Select its
port and **Connect**. The app queries status before enabling motor controls;
connecting does not send a motor command. A blank board needs **Build &
upload** before it will answer as a bridge.

The header shows radio state, shade count, the reported Wi-Fi IP and whether
HomeSpan reports a paired controller. The shade table shows estimated
position, target and observed motion. Motion is unknown until the app receives
a position trace; calibration and percentages are not sensor measurements.

The app checks status periodically. Serial operations are exclusive: polling
does not run while a setup operation owns the port. Close PlatformIO Monitor
or any other serial terminal before connecting. If USB disconnects, controls
are disabled; find the port again and reconnect. Port names can change after
restart or upload.

## Wi-Fi and Apple Home

Start with all motors physically stopped; Wi-Fi setup pauses normal firmware
control while HomeSpan waits for credentials. Enter the exact network name
and password in **Wi-Fi & Apple Home**, then choose **Save Wi-Fi & restart
ESP**. The app waits for each firmware prompt before sending its answer. It
uses the scanned network's menu number when available so numeric SSIDs are
not mistaken for an index. Passwords must be 8–63 bytes or a 64-digit hex PSK;
open networks are not supported by HomeSpan's serial setup flow.

Successful setup saves credentials on the ESP and restarts it. Reconnect to
check the resulting Wi-Fi connection. Saving is not proof that the network
accepted the password. If setup is interrupted or times out, the app closes
the serial connection. **Reset the ESP before reconnecting**: it could still
be waiting for a password, and another command could be consumed as one.

Choose a personal HomeKit setup code before adding the bridge in Apple Home.
The form rejects HomeSpan's disallowed simple codes. The code is masked and
cleared after submission. Saving a code does not remove existing HomeKit
pairings. Complete accessory discovery in Apple Home; the TUI cannot pair
your iPhone on your behalf.

## Pair and test motors

Select a shade and choose **Pair shade**. The wizard asks you to select its
channel on your existing remote, hold PROG until the motor jogs, and then
click **Send PROG once**. Watch for the second jog and confirm it in the app.
The firmware confirms transmission, not motor reception. Repeating PROG can
remove a pairing, so the app never retries it automatically.

Use **Open**, **Close** or **Move to %** to test movement. HomeKit's scale is
100% open and 0% closed. **Check My** sends one bare My to a stopped motor and
does not update the position model. Follow the configuration guide when using
the motor's favourite as your chosen closed position.

Keep the physical remote nearby. This firmware has no dedicated serial Stop
command, and the app does not simulate one with a second My or a guessed
percentage. The firmware's existing position model still handles timed stops
for normal percentage moves.

## Configuration and USB upload

**Edit installation** reads the shared defaults plus your local overrides.
It edits shade names/count, the address base, travel times, favourite close
and motor limits. Other local defines, such as pin assignments and transmit
settings, are preserved. The editor accepts literal values; it refuses to
guess the result of custom C expressions.

Saving writes `include/somfy_config.local.h` atomically. This file is ignored
by git and contains no network credentials. Changes take effect only after
firmware upload. Keep addresses and accessory order stable for a paired
installation. Generate an address only for a new bridge or a deliberate
re-pairing; the app does not erase or change rolling codes.

**Build & upload** runs the pinned PlatformIO project in the app's Python
environment, with the selected USB port as the upload target. It releases
serial first and streams compiler/upload output into **Activity**. It uses
the existing partition layout and does not erase NVS. Reconnect after the
ESP restarts. Keep the terminal running and USB power connected during upload.
Errors remain visible in Activity; failed motor commands or uploads are not
automatically retried.

## Privacy and validation

The activity log is bounded and held only in memory. The app hides Wi-Fi setup
output, setup-code output, QR pairing payloads and controller identifiers.
Passwords and setup codes are masked in forms and are not saved to disk.
Serial credentials still pass over the USB connection in plaintext, as in
HomeSpan's original console setup.

Tests cover serial fragmentation, prompt ordering, validation, secret
redaction, single-send commands, disconnects, configuration preservation and
headless form interactions. A POSIX pseudo-terminal test also exercises the
real pySerial transport without hardware. Physical motor reception and Apple
Home commissioning still need testing on your installation.
