# CLAUDE.md — Project Instructions for Claude & Autonomous Agents (`wadamesh`)

Always read and follow these canonical specifications before exploring or modifying this repository:

1. **[`AGENTS.md`](AGENTS.md)** — Master agent engineering guide, target board/variant matrix (`LilyGo T-Deck`, `Heltec V4 TFT`, `ThinkNode M9`, `Tanmatsu`, `T-Lora Pager`), PlatformIO build commands, and RF/PMIC safety invariants.
2. **[`docs/agents/autonomous-pipeline.md`](docs/agents/autonomous-pipeline.md)** — Full 6-Stage Autonomous Development, Dual GitHub PR Review, Remediation, and Automated Merge Pipeline specification, enforced by [`.github/workflows/autonomous-pr-review.yml`](.github/workflows/autonomous-pr-review.yml).
3. **[`LEGAL.md`](LEGAL.md)** — 8-pillar legal framework (EU CRA `Regulation (EU) 2024/2847` & PLD `Directive (EU) 2024/2853` non-commercial FOSS exemption, clean-room interoperability, nominative fair use, `Directive 2014/53/EU (RED)` / `ETSI EN 300 220` / `FCC Part 15.247` RF compliance, `AXP2101` / `BQ25896` battery safety, and the mandatory `## Addendum: German Statutory References (Applicable to Residents of Germany)`).
4. **[`SECURITY.md`](SECURITY.md)** & **[`docs/agents/domain.md`](docs/agents/domain.md)** — Vulnerability reporting, OPSEC/Zero-PII rules, and domain modeling conventions.
