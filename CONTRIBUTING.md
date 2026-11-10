# Contributing to wadamesh

Contributions that improve reliability, test coverage, documentation, or hardware support are welcome.

## License of Contributions

`wadamesh` is licensed under **GPL-3.0-or-later** (see [`LICENSE`](LICENSE)). By submitting a contribution you agree that it is licensed under the same terms (inbound = outbound).

Sign your commits cryptographically (`git config commit.gpgsign true`) and include a [Developer Certificate of Origin](https://developercertificate.org/) sign-off where appropriate:

```bash
git commit -S -s -m "fix(scope): concise technical description"
```

New source files should carry an SPDX header:

```c
// SPDX-License-Identifier: GPL-3.0-or-later
```

Files derived from MeshCore keep their original **MIT** header — do not relicense upstream code; only project additions are GPL-3.0-or-later.

## Ground Rules & Regulatory Boundaries

All contributors must read and comply with [`CODE_OF_CONDUCT.md`](CODE_OF_CONDUCT.md), [`LEGAL.md`](LEGAL.md), and [`SECURITY.md`](SECURITY.md):

- **Clean-Room & Open-Source Provenance (`Directive 2009/24/EC (Articles 1(2), 5(3), 6, 8)` / `Article 3(1)(b) of Directive (EU) 2016/943` / `17 U.S.C. §§ 102(b), 107, 1201(f)` / `18 U.S.C. § 1839(6)(B) (DTSA)`):**
  Only submit original code, publicly documented hardware register sequences (from public datasheets or compatible open-source BSPs), or clean-room interoperability implementations. Document third-party attributions and licenses in [`NOTICE`](NOTICE). Never submit proprietary vendor SDK code, leaked NDA materials, or cryptographic/TPM bypasses.
- **Trademark Nominative Fair Use (`Article 14(1)(c) of Regulation (EU) 2017/1001 (EUTMR)` / `15 U.S.C. §§ 1115(b)(4), 1125`):**
  Reference third-party hardware and chip names (`LilyGo`, `T-Deck`, `Heltec V4`, `Elecrow`, `ThinkNode M9`, `Semtech SX1262`, `ESP32-S3`, `nRF52840`) strictly as descriptive indications of intended purpose and nominative fair use. Do not add vendor logos or imply official manufacturer endorsement.
- **RF & Battery Safety Boundaries (`Directive 2014/53/EU (RED)` / `ETSI EN 300 220-1/-2` / `FCC 47 C.F.R. Part 15.247 / Part 97`):**
  Changes touching LoRa radio configuration (`SX1262` / `LR1110` / `LR1121` / `LR2021`) or PMIC/battery charger drivers (`AXP2101` / `BQ25896` / `BQ27220`) must respect regional frequency/power/duty-cycle limits (`<= 1%` in `g1`, `<= 10%` in `g3`), prohibit encryption on Amateur Radio bands, and preserve safe lithium-ion charging defaults (`<= 4.20–4.288 V`, `0–45 °C` NTC temperature guard).
- **Zero PII / Location Metadata:** Never commit personal names, private email addresses, physical locations, postal codes, GPS coordinates, MAC addresses, device serial numbers, or Wi-Fi credentials in source code, comments, test fixtures, or commit messages.
- **Security Reporting:** Report security vulnerabilities privately via [`SECURITY.md`](SECURITY.md) rather than public issues.

## Local Build & Test Verification

Run the C++17 host unit-test suite and target firmware builds locally before opening a pull request:

```bash
./scripts/run-host-tests.sh
pio run -e LilyGo_TDeck_companion_radio_touch
pio run -e ThinkNode_M9_companion_radio_touch
```

## Pull Request Workflow

1. Create a focused topic branch from `main` (`feat/...`, `fix/...`, `docs/...`, `chore/...`, `ci/...`) — one concern per pull request.
2. Keep commits atomic, use Conventional Commit headers (`feat:`, `fix:`, `docs:`, `ci:`, `refactor:`, `test:`), and sign all commits (`commit.gpgsign=true`).
3. Verify host unit tests and PlatformIO builds pass without regressions.
4. Open a pull request against `main` summarizing the problem, root cause or design choice, and verification output. All pull requests squash-merge onto `main` once `ci` and `security` checks pass.
