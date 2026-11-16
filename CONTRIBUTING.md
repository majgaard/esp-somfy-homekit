# Contributing

Bug reports, documentation fixes and focused pull requests are welcome.
For a substantial behavior change or support for another protocol, open an
issue first so the scope can be discussed.

## Development setup

Follow the [README](README.md#quick-start) for Python 3.13, the pinned
PlatformIO environment and the ignored installation configuration header.
Keep the existing dependency versions unless your change requires an upgrade.

Before submitting firmware changes, run:

```sh
make -C test/host run
make -C test/host asan
pio run -e supermini
```

The host tests require only Make and a C11 compiler. The sanitizer target
requires a compiler with AddressSanitizer and UBSan support. Some restricted
environments may not support sanitizers; report that explicitly if it happens.

Keep protocol, radio-register and position logic in the portable C components
where possible. Add meaningful regression tests for behavior changes. Host
tests cannot establish real RF delivery or motor behavior: state what hardware
you tested, and distinguish it from compilation and mock-driver tests.

Keep `SHADE_NAMES` and `SHADE_COUNT` consistent. Keep personal installation
settings in `include/somfy_config.local.h`. Do not commit credentials, setup
codes, NVS/flash dumps, local logs or generated firmware. Avoid changing
existing address assignments, accessory order or partition layouts incidentally.

Pull requests should explain the problem, resulting behavior and validation.
Contributions to this repository are provided under its [MIT license](LICENSE).
Preserve upstream copyright and license notices when reusing third-party work.
