# Security

Report vulnerabilities through
[GitHub private vulnerability reporting](https://github.com/majgaard/esp-somfy-homekit/security/advisories/new)
when available. If private reporting is unavailable, contact the repository
owner using the contact options on their GitHub profile before disclosing
details publicly. Do not put credentials or exploit details in a public issue.

Include the affected commit, hardware, reproduction steps and expected impact.
Only the current main branch is maintained; this is a community hardware
project without a guaranteed response time.

Wi-Fi credentials, HomeKit pairing state and RTS rolling codes are stored on
the device. Flash/NVS dumps and verbose serial logs can contain private data.
Remove credentials, setup codes and controller identifiers before sharing logs.
Choose a personal HomeKit setup code during commissioning rather than leaving
HomeSpan's public default in place.

RTS is a one-way radio protocol. This firmware cannot confirm delivery, detect
obstacles or guarantee a physical shade position. See the
[configuration guide](docs/configuration.md) for limits and rolling-code recovery.
