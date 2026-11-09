# AGENTS.md — Autonomous Agent Engineering Guide (`wadamesh`)

This file is the canonical engineering and operational playbook for autonomous AI coding agents working in **`TinkeringDao/wadamesh`**. Always read this document alongside [`docs/agents/autonomous-pipeline.md`](docs/agents/autonomous-pipeline.md), [`docs/agents/domain.md`](docs/agents/domain.md), [`LEGAL.md`](LEGAL.md), and [`SECURITY.md`](SECURITY.md) before modifying any file.

---

## Fully Autonomous Development Pipeline

All changes in `TinkeringDao/wadamesh` follow the **6-Stage Autonomous Development Pipeline** specified in [`docs/agents/autonomous-pipeline.md`](docs/agents/autonomous-pipeline.md) and enforced by [`.github/workflows/autonomous-pr-review.yml`](.github/workflows/autonomous-pr-review.yml):

1. **Stage 1 — Context & Hindsight Recall**: Query `POST http://192.168.10.238:9100/v1/default/banks/satori/memories/recall` and review `AGENTS.md`, `LEGAL.md`, `SECURITY.md`, and `docs/agents/domain.md`.
2. **Stage 2 — Branch Isolation & Signed TDD Implementation**: Branch from `origin/main` (`feat/*`, `fix/*`, `chore/*`, `docs/*`). Author all commits as `TinkeringDao <203040288+TinkeringDao@users.noreply.github.com>` with ED25519 SSH commit signing enabled (`git config commit.gpgsign true`).
3. **Stage 3 — Pull Request Creation**: Open a PR against `main` containing `## Summary`, `## Evidence`, and `## Merge Danger` (`Door: two-way | one-way`, `Blast Radius: local | module | system`).
4. **Stage 4 — Dual GitHub PR Review**:
   - **Automated CI Gate** ([`.github/workflows/autonomous-pr-review.yml`](.github/workflows/autonomous-pr-review.yml)): Verifies OPSEC/Zero-PII on changed files, SSH commit signatures on all PR commits, 8-pillar `LEGAL.md` + German Statutory Addendum & governance file integrity, and `platformio.ini` configuration integrity.
   - **Independent Agentic Spec/Code/Security Review**: Audits `gh pr diff` for architectural, RF/PMIC hardware safety, and legal compliance and posts a structured review via `gh pr review --comment`.
5. **Stage 5 — Autonomous Remediation Loop**: Remediate any review finding or CI failure in a signed commit on the PR branch and post a remediation summary comment.
6. **Stage 6 — Automated Approval, Squash Merge & Hindsight Retention**: `github-actions[bot]` approves the PR (`gh pr review --approve`), satisfying `Protect main branch` (`required_approving_review_count: 1`, `require_last_push_approval: true`), followed by `gh pr merge --squash --delete-branch` and Hindsight memory retention.

---

## Target Board & Variant Architecture

`wadamesh` provides the standalone LVGL touch-UI companion-radio firmware built on top of the MeshCore library (`-DMC_VENDORED_TOUCH_APP`). Target hardware platforms span PlatformIO (`ESP32-S3` / `nRF52840`) and ESP-IDF (`ESP32-P4`) targets:

| Board / Target | MCU | Radio / PMIC | Build Environment / Path | Status |
|---|---|---|---|---|
| **LilyGo T-Deck / T-Deck Plus** | ESP32-S3 | `SX1262` | `pio run -e LilyGo_TDeck_companion_radio_touch` | Stable |
| **Heltec WiFi LoRa 32 V4 + TFT** | ESP32-S3 | `SX1262` + Front-End PA | `pio run -e heltec_v4_tft_companion_radio_usb_tcp_touch` | Stable |
| **Elecrow ThinkNode M9** | nRF52840 / ESP32 | `SX1262` / `LR1110` | `pio run -e ThinkNode_M9_companion_radio_touch` | Beta |
| **Tanmatsu** | ESP32-P4 | `SX1262` | ESP-IDF build in `tanmatsu/` (see [`TANMATSU_PORT.md`](TANMATSU_PORT.md)) | Stable |
| **LilyGo T-Lora Pager** | ESP32-S3 | `SX1262` / `LR1121` + `AXP2101` / `BQ25896` | `pio run -e tlora_pager_sx1262_companion_radio_touch` / `tlora_pager_lr1121_companion_radio_touch` | Beta |
| **RAK WisMesh Tap V2 (RAK3312)** | ESP32-S3 | `SX1262` | `pio run -e rak_tap_v2_companion_radio_touch` | Beta |
| **LilyGo T-Display P4** | ESP32-P4 | `SX1262` / `LR2021` | ESP-IDF build in `tdisplay_p4/` | Beta |
| **Elecrow CrowPanel Advance 3.5 / 5.0 / 10.1** | ESP32-S3 / P4 | `SX1262` | `crowpanel_35_companion_radio_touch`, `crowpanel_50_companion_radio_touch`, `crowpanel_p4_101/` | Beta / Experimental |

### Common Build & Test Commands

```bash
# Build primary stable touch targets
pio run -e LilyGo_TDeck_companion_radio_touch
pio run -e heltec_v4_tft_companion_radio_usb_tcp_touch

# Build beta touch targets
pio run -e ThinkNode_M9_companion_radio_touch
pio run -e tlora_pager_sx1262_companion_radio_touch

# Run native unit test suites
pio test -e native
```

---

## Hardware, RF Regulatory & Legal Invariants

1. **RF Regulatory & ERP/Duty-Cycle Boundaries (`Directive 2014/53/EU (RED)`, `ETSI EN 300 220-1/-2`, `FCC 47 C.F.R. Part 15.247 / Part 97`)**:
   - Sub-GHz LoRa radio configurations (`SX1262`, `LR1110`, `LR1121`, `LR2021`) and board-level external PA/FEM stages (`Heltec V4`) MUST respect regional frequency, conducted power (`dBm`), antenna gain (`dBi`), and duty-cycle limits (`<= 1%` in `868.0–868.6 MHz` SRD `g1`; `<= 10%` in `869.400–869.650 MHz` SRD `g3`).
   - Cryptographic encryption is strictly prohibited on Amateur Radio frequencies (`ITU Radio Regulations Art. 25.2A`, `47 C.F.R. § 97.113(a)(4)`, `§ 16 Abs. 1 Nr. 4 AFuV`).
2. **PMIC & Lithium-Ion Battery Safety (`AXP2101`, `BQ25896`, `BQ27220`)**:
   - Never raise charge termination voltage above `4.20–4.288 V` or disable NTC thermal protection (`0–45 °C`).
3. **Clean-Room Interoperability & Licensing (`GPL-3.0-or-later` / `MIT`)**:
   - All `wadamesh` additions are licensed under `GPL-3.0-or-later` ([`LICENSE`](LICENSE)); files derived from MeshCore retain their upstream `MIT` license headers and attribution in [`NOTICE`](NOTICE).
   - Maintain 100% clean-room provenance (`Directive 2009/24/EC`, `Directive (EU) 2016/943`, `17 U.S.C. § 1201(f)`) and nominative fair use of hardware trademarks (`Article 14(1)(c) EUTMR`, `15 U.S.C. § 1115(b)(4)`).
4. **Zero-PII & OPSEC Hygiene**:
   - Never commit personal names, private email addresses, home locations, GPS coordinates, Wi-Fi SSIDs/passwords, or secret tokens.
   - Keep all primary documentation in clean English while preserving `## Addendum: German Statutory References (Applicable to Residents of Germany)` in [`LEGAL.md`](LEGAL.md).
