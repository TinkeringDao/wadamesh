# Security & RF/Hardware Safety Policy

## Supported Versions

Security fixes are applied to the `main` branch and the latest `beta_*` release tag.

| Version | Supported |
| ------- | --------- |
| `main` / latest `beta_*` | Yes |
| Older tags | No |

## Reporting a Vulnerability

**Do not open public GitHub issues for exploitable security vulnerabilities.**

Please report security vulnerabilities privately via **GitHub Security Advisories**:
- **Report URL:** https://github.com/TinkeringDao/wadamesh/security/advisories/new

Include:
1. Affected target board(s) (`LilyGo T-Deck`, `Heltec V4`, `ThinkNode M9`, `T-Lora Pager`, `Tanmatsu`, etc.) and firmware tag/commit SHA.
2. Transport or subsystem involved (LoRa mesh framing, BLE companion protocol, Wi-Fi Transfer/OTA server, Lua sandbox, or PMIC/charger control).
3. Minimal reproduction steps or proof-of-concept.

## Scope & Hardware / RF Safety Boundaries

1. **Non-Commercial Open-Source Software (`EU CRA Regulation (EU) 2024/2847 Art. 2(5)`):**
   - `wadamesh` is developed and supplied strictly outside the course of any commercial activity without remuneration or paid support. Vulnerability triage is performed on a voluntary, best-effort community basis (see [`LEGAL.md`](LEGAL.md)).
2. **In-Scope Security & Safety Issues:**
   - Memory corruption (buffer overflows, out-of-bounds reads/writes) triggered by untrusted over-the-air LoRa mesh packets, BLE companion frames, or Wi-Fi HTTP requests.
   - Authentication bypasses in the local Wi-Fi **Transfer** or **AsyncElegantOTA** interfaces.
   - Escapes from the on-device Lua application sandbox (`wada.*` host APIs).
   - Unintended PMIC (`AXP2101`) or charger (`BQ25896`) register writes that could exceed safe battery charge voltage/current limits or disable NTC thermal protection without explicit user confirmation.
   - Software bugs that cause unintended continuous RF transmission (`TX` stuck high) or bypass user-configured frequency/power limits.
3. **Operator Responsibility for RF Compliance (`Directive 2014/53/EU (RED)` / `ETSI EN 300 220-1/-2` / `FCC 47 C.F.R. Part 15.247 / Part 97`):**
   - Selecting lawful frequency bands (`868 MHz` EU SRD / `902–928 MHz` US ISM), transmit power (`dBm`), antenna gain (`dBi`), and duty cycle (`<= 1%` / `<= 10%`) is the sole legal responsibility of the hardware operator (see [`LEGAL.md`](LEGAL.md)).
