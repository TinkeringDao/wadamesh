# Contributing to wadamesh

Thanks for your interest — wadamesh is open and collaborative by design, and
contributions are very welcome.

## License of contributions

wadamesh is licensed under **GPL-3.0-or-later** (see [LICENSE](LICENSE)). By
submitting a contribution you agree that it is licensed under the same terms
(inbound = outbound). This keeps every build and fork open.

Sign off your commits to certify you wrote (or have the right to submit) the
change, per the [Developer Certificate of Origin](https://developercertificate.org/):

```
git commit -s -m "..."     # adds a Signed-off-by line
```

New source files should carry an SPDX header:

```c
// SPDX-License-Identifier: GPL-3.0-or-later
```

Files derived from MeshCore keep their original **MIT** header — don't relicense
upstream code; only your own additions are GPL.

## Legal, Clean-Room & RF Regulatory Compliance

All contributors must read and comply with [**LEGAL.md**](LEGAL.md) and our
[Code of Conduct](CODE_OF_CONDUCT.md):

- **Clean-Room & Open-Source Provenance (`Directive 2009/24/EC (Articles 1(2), 5(3), 6, 8)` / `Article 3(1)(b) of Directive (EU) 2016/943` / `17 U.S.C. §§ 102(b), 107, 1201(f)` / `18 U.S.C. § 1839(6)(B) (DTSA)`):**
  Only submit original code, publicly documented hardware register sequences
  (from public datasheets or compatible open-source BSPs), or clean-room
  interoperability implementations. Always document third-party attributions and
  licenses in [`NOTICE`](NOTICE). Never submit proprietary vendor SDK code,
  leaked NDA materials, or cryptographic/TPM bypasses.
- **Trademark Nominative Fair Use (`Article 14(1)(c) of Regulation (EU) 2017/1001 (EUTMR)` / `15 U.S.C. §§ 1115(b)(4), 1125`):**
  Reference third-party hardware and chip names (`LilyGo`, `T-Deck`, `Heltec V4`,
  `Elecrow`, `ThinkNode M9`, `Semtech SX1262`, `ESP32-S3`, `nRF52840`) strictly
  as descriptive indications of intended purpose and nominative fair use. Do not
  add vendor logos or imply official manufacturer endorsement.
- **RF & Battery Safety Boundaries (`Directive 2014/53/EU (RED)` / `ETSI EN 300 220-1/-2` / `FCC 47 C.F.R. Part 15.247 / Part 97`):**
  Changes touching LoRa radio configuration (`SX1262` / `LR1110` / `LR1121` /
  `LR2021`) or PMIC/battery charger drivers (`AXP2101` / `BQ25896` / `BQ27220`)
  must respect regional frequency/power/duty-cycle limits and safe lithium-ion
  charging defaults (`<= 4.20–4.288 V`, `0–45 °C` NTC temperature guard).
- **Zero PII / Location Metadata:** Never commit personal names, private email
  addresses, home addresses, postal codes, GPS coordinates, or baked Wi-Fi
  credentials in source code, comments, test fixtures, or commit messages.

### Pre-Merge Verification & RF Safety Gate Checklist

| Gate | Requirement | Verification Standard |
|---|---|---|
| **1. OPSEC & Zero-PII** | Zero personal identifiers, local paths, Wi-Fi credentials, or tokens in diff | Automated Gate 1 in `.github/workflows/autonomous-pr-review.yml` |
| **2. Signed Commits & DCO** | All commits authored by `TinkeringDao <203040288+TinkeringDao@users.noreply.github.com>` with ED25519 SSH signature (`-S -s`) | Automated Gate 2 (`gpgsig` verification) |
| **3. RF ERP & Duty Cycle** | `SX1262` / `LR1110` / `LR1121` / `LR2021` regional presets respect `<= 1%` (`g1`) / `<= 10%` (`g3`) duty cycle & ERP limits (`ETSI EN 300 220-1/-2`, `Directive 2014/53/EU`, `FCC Part 15.247`) | Variant & region preset audit (`REGIONS.md`, `LEGAL.md`) |
| **4. PMIC & Battery Safety** | `AXP2101` / `BQ25896` charge termination `<= 4.20–4.288 V` and `0–45 °C` NTC thermal protection preserved | Driver & board variant review (`variants/`) |
| **5. Clean-Room & Legal** | `GPL-3.0-or-later` / `MIT` headers intact; `LEGAL.md` retains `Addendum: German Statutory References` | Automated Gate 3 & Stage 4B Agentic Review |

## How we work

A few principles (borrowed from how this project is built):

- **One topic per pull request.** Small, focused changes that do one thing are
  easy to review and easy to revert. Open a separate PR per concern.
- **Discuss next steps, keep the actual change scoped.** It's great to note what
  could come next — just don't bundle it into the same diff.
- **Prefer pulling in a library over hand-rolling.** If a maintained library
  solves it, depend on it (and add it to [NOTICE](NOTICE)) rather than reinventing.
- **Don't regress what ships.** wadamesh runs on real devices; changes should keep
  both boards (LilyGo T-Deck and Heltec V4 TFT) building and behaving.

## Building

The firmware is a PlatformIO project that depends on a MeshCore fork via
`lib_deps`. Build/flash instructions land in the README as the split from
meshcomod completes. In the meantime, open an issue if you'd like to help.

## Reporting issues

Bug reports and feature ideas are welcome in the issue tracker — please include
your board, firmware version, and steps to reproduce.
