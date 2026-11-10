# Legal, Trademark, Interoperability & RF Regulatory Compliance Notice

## 1. Independent Open-Source Project & Trademark Nominative Fair Use

**`wadamesh` — Unofficial Non-Commercial Open-Source MeshCore Touch-UI Companion-Radio Firmware** is an **independent, non-commercial, community-developed free and open-source software project** providing an LVGL touchscreen user interface and board hardware support for open microcontroller development boards running on top of the open-source MeshCore radio stack.

- **No Affiliation or Endorsement:** This project is **not affiliated with, authorized by, sponsored by, or endorsed by LilyGo (Shenzhen Xin Yuan Electronic Technology Co., Ltd.), Heltec Automation, Elecrow, Seeed Studio, Semtech Corporation (`LoRa`, `SX1262`), Espressif Systems (`ESP32-S3`), Nordic Semiconductor (`nRF52840`), STMicroelectronics, or Ripple Radios / MeshCore.**
- **Indication of Intended Purpose & Nominative Fair Use:**
  - Pursuant to **Article 14(1)(c) of Regulation (EU) 2017/1001 (EUTMR)**, settled Court of Justice of the European Union jurisprudence (*Case C-228/03 — Gillette Company and Gillette Group Finland Oy v. LA-Laboratories Ltd Oy*; *Case C-63/97 — Bayerische Motorenwerke AG (BMW) and BMW Nederland BV v. Ronald Karel Deenik*), and **US Nominative Fair Use (`15 U.S.C. §§ 1115(b)(4), 1125`**, *New Kids on the Block v. News America Publishing, Inc.*, 971 F.2d 302 (9th Cir. 1992)), third-party brand, chip, and hardware names are referenced strictly as a descriptive indication of intended purpose and nominative fair use to identify compatible target hardware and protocols:
    - All hardware model names—including **`T-Deck`**, **`T-Deck Pro`**, **`T-Deck Plus`**, **`T-Lora Pager`**, **`T-Display P4`**, **`Heltec V4`**, **`ThinkNode M9`**, **`CrowPanel`**, **`SenseCAP Indicator`**, **`WisMesh Tap`**, and **`Tanmatsu`**—are used strictly as indications of intended purpose to identify compatible target development hardware.
    - **`LoRa`**, **`LoRaWAN`**, **`SX1262`**, **`LR1110`**, **`LR1121`**, and **`LR2021`** are registered trademarks or trademarks of **Semtech Corporation**.
    - **`ESP32`**, **`ESP32-S3`**, **`ESP32-P4`**, and **`ESP32-C6`** are trademarks of **Espressif Systems (Shanghai) Co., Ltd.**
    - **`nRF52840`** is a trademark of **Nordic Semiconductor ASA**.
    - **`STM32`** is a registered trademark of **STMicroelectronics International N.V.**
    - **`MeshCore`** and **`Ripple Radios`** are trademarks or designations of **Scott Powell / rippleradios.com**.
  - Zero proprietary vendor logos, misleading trade dress, or claims of official hardware certification are included in this repository.

---

## 2. Open-Source License Provenance, Statutory Interoperability & Trade Secret Safe Harbor

### 2.1 Open-Source License Provenance & Multi-License Attribution
`wadamesh` is distributed as open-source software combining MIT-licensed MeshCore companion-radio components and modifications with a GPL-3.0-or-later LVGL touchscreen user interface:
- **Root [`LICENSE`](LICENSE) & [`NOTICE`](NOTICE):** Preserves upstream `MeshCore` (`Copyright (c) 2025 Scott Powell / rippleradios.com`, MIT License), upstream `wadamesh` (`Copyright (c) 2026 Kaj Schittecat and the wadamesh contributors`, GPL-3.0-or-later), and `Copyright (c) 2026 TinkeringDao` for modifications, hardware bring-up, and legal/regulatory documentation under the **MIT License** (for MeshCore-derived components and upstreamable hooks) and **GPL-3.0-or-later** (for the combined work).
- **Third-Party Component Licenses Preserved in Repository:**
  - [`src/ui-touch/FONT_LICENSES.md`](src/ui-touch/FONT_LICENSES.md) — SIL Open Font License 1.1 (`Montserrat` and `Noto Sans` font families).
  - [`arch/esp32/AsyncElegantOTA/LICENSE`](arch/esp32/AsyncElegantOTA/LICENSE) — MIT License (`Copyright (c) 2019 Ayush Sharma`).
  - [`lib/minimp3/LICENSE`](lib/minimp3/LICENSE) — CC0 1.0 Universal (`lieff/minimp3`).
  - [`deploy/site/vendor/esp-web-tools-10.4.0/LICENSE`](deploy/site/vendor/esp-web-tools-10.4.0/LICENSE) — Apache License 2.0 (`Nabu Casa, Inc.`).

### 2.2 European Union (`Directive 2009/24/EC (Articles 1(2), 5(3), 6, 8)` & `Article 3(1)(b) of Directive (EU) 2016/943`)
All peripheral driver sequences (I2C/SPI register initialization tables, PMIC/charger telemetry, audio codec clock dividers, IMU/compass sensor bring-up, and LoRa radio framing) were developed using publicly documented datasheets, permissive open-source BSPs, and lawful clean-room hardware observation:
1. **Exclusion of Functional Interfaces, Register Maps & Protocols from Copyright (`Directive 2009/24/EC Article 1(2)`):**
   - Under **Article 1(2) of Directive 2009/24/EC** and settled CJEU precedent (*Case C-406/10 — SAS Institute Inc. v. World Programming Ltd*, paras. 39–46), ideas, principles, hardware register addresses, pinout mappings, peripheral initialization sequences, and radio packet formats are **excluded from copyright protection**.
2. **Right to Observe, Study, and Test (`Directive 2009/24/EC Article 5(3)`):**
   - Permits the lawful owner or user of hardware and software to observe, study, and test its functioning without prior authorization in order to determine the underlying ideas, principles, and hardware bus transactions during normal operation.
3. **Mandatory Right of Interoperability (`Directive 2009/24/EC Article 6`):**
   - Authorizes independent analysis necessary to achieve the **interoperability** of an independently created computer program (`wadamesh` firmware) with microcontroller peripherals, displays, sensors, and LoRa mesh networks.
4. **Statutory Nullity of Contrary EULA Provisions (`Directive 2009/24/EC Article 8`):**
   - Under **Article 8 of Directive 2009/24/EC**, any contractual or End-User License Agreement (EULA) restriction purporting to limit or waive interoperability or observation rights under Articles 5(3) or 6 is **statutorily null and void**.
5. **Lawful Hardware Inspection & Reverse Engineering (`Article 3(1)(b) of Directive (EU) 2016/943`):**
   - Under **Article 3(1)(b) of Directive (EU) 2016/943**, obtaining technical information by observing, examining, disassembling, or testing a product that has been made publicly available on the market or is in the lawful possession of the observer free from any legally valid duty to limit acquisition is explicitly lawful.

### 2.3 United States (`17 U.S.C. §§ 102(b), 107, 1201(f)` & DTSA `18 U.S.C. § 1839(6)(B)`)
1. **Uncopyrightable Systems, Methods of Operation & Merger (`17 U.S.C. § 102(b)`):**
   - Microcontroller GPIO assignments, I2C/SPI register offsets, command opcodes, and radio framing parameters constitute uncopyrightable functional systems, methods of operation, and hardware facts necessary for compatibility (*Baker v. Selden*, 101 U.S. 99 (1879); *Lexmark Int'l, Inc. v. Static Control Components, Inc.*, 387 F.3d 522 (6th Cir. 2004); *Google LLC v. Oracle America, Inc.*, 593 U.S. 1 (2021)).
2. **Fair Use & Statutory Interoperability Exemptions (`17 U.S.C. §§ 107, 1201(f)`):**
   - Independent analysis and firmware development for the sole purpose of achieving hardware and protocol interoperability with lawfully owned devices are protected under **17 U.S.C. § 107** and **17 U.S.C. § 1201(f)** (*Sega Enterprises Ltd. v. Accolade, Inc.*, 977 F.2d 1510 (9th Cir. 1992); *The Chamberlain Group, Inc. v. Skylink Technologies, Inc.*, 381 F.3d 1178 (Fed. Cir. 2004)).
3. **Defend Trade Secrets Act (DTSA) Safe Harbor (`18 U.S.C. § 1839(6)(B)`):**
   - Under federal law (**`18 U.S.C. § 1839(6)(B)`**), reverse engineering, independent derivation, and clean-room observation of a lawfully acquired product on the open market are expressly excluded from the definition of "improper means".

---

## 3. Non-Circumvention of Technological Protection Measures (TPMs) (`Directive 2009/24/EC` & `17 U.S.C. §§ 1201(a), 1201(f)`)

Under **Directive 2009/24/EC (Articles 1(2), 5(3), 6, 8)** and **US `17 U.S.C. §§ 102(b), 107, 1201(a), 1201(f)`**, `wadamesh` **does not circumvent, bypass, decrypt, or defeat any effective Technological Protection Measure (TPM)**:

- **Open Developer Microcontroller Targets Only (`ESP32-S3`, `ESP32-P4`, `nRF52840`):** `wadamesh` targets general-purpose open developer boards (`LilyGo T-Deck`, `T-Deck Pro`, `Heltec V4`, `Elecrow ThinkNode M9`, `Tanmatsu`) explicitly marketed and sold to developers for custom firmware development via PlatformIO, Arduino, and ESP-IDF.
- **Standard Vendor USB Serial / UF2 Bootloaders:** Firmware images are compiled from source and flashed by the hardware owner using standard, manufacturer-documented USB CDC/JTAG serial (`esptool.py` / ESP Web Tools) or UF2 mass-storage bootloaders.
- **Zero TPM Circumvention or Key Extraction:** This repository contains **zero eFuse exploits, secure-boot bypasses, flash-encryption attacks, or extracted proprietary cryptographic keys**.

---

## 4. Authorized Local Owner Execution Only & US CFAA (`18 U.S.C. § 1030`)

In accordance with **Authorized Local Owner Execution Only** principles and the US Computer Fraud and Abuse Act (**`18 U.S.C. § 1030 (CFAA)`**):

- **No Access-Security Circumvention or Unlawful Interception:** `wadamesh` is neither designed nor adapted to overcome any technical access security barrier, intercept non-public transmissions, or serve as an unauthorized computer intrusion tool. Mesh channel communication uses standard, user-configured MeshCore keys chosen by the local operator.
- **Authorized Local Owner Execution Only:** The lawful owner of a physical microcontroller device possesses full authority to flash, configure, and modify firmware and NVS storage on their own hardware. Flashing or modifying third-party devices without the explicit authorization of the hardware owner is strictly prohibited.
- **Computer Fraud and Abuse Act (`18 U.S.C. § 1030 (CFAA)`):** Under *Van Buren v. United States*, 593 U.S. 374 (2021), installing and operating open-source firmware on one's own microcontroller hardware does not access any computer "without authorization" or "exceed authorized access".

---

## 5. Non-Commercial Open-Source Software Exemption (EU CRA & EU PLD)

- **EU Cyber Resilience Act Exemption (`Regulation (EU) 2024/2847`, Recitals 15 & 18; `Art. 2(5)`; `Art. 3(13)-(14)`):**
  - Pursuant to **Article 2(5)** and **Recitals 15 & 18 of Regulation (EU) 2024/2847 (Cyber Resilience Act — CRA)**, this repository is developed, maintained, and supplied strictly outside the course of any commercial activity as non-commercial free and open-source software.
  - Zero monetization, paid support, commercial services, or commercial data harvesting take place in connection with this repository. Accordingly, the authors and contributors are neither **"manufacturers" (`Art. 3(13) CRA`)** nor **"open-source software stewards" (`Art. 3(14) CRA`)**, and this project is **exempt from CE-marking (`Art. 28 CRA`)**, conformity assessment, and manufacturer/steward obligations under the CRA.
- **EU Product Liability Directive Exemption (`Directive (EU) 2024/2853`, `Art. 2(2)` & Recitals 14, 15, 24):**
  - Pursuant to **Article 2(2)** and **Recitals 14, 15, and 24 of Directive (EU) 2024/2853 (Product Liability Directive — PLD)**, free and open-source software developed or supplied outside the course of a commercial activity is excluded from the scope of strict product liability under the Directive. Any third party who subsequently integrates, bundles, flashes onto commercial hardware for sale, or monetizes this firmware in the course of a commercial activity assumes sole legal and regulatory responsibility as a manufacturer.

---

## 6. Radio Frequency (RF), Duty Cycle & Telecom Regulatory Compliance (`Directive 2014/53/EU (RED)`, `ETSI EN 300 220-1/-2`, `ETSI EN 300 328`, `ERC/REC 70-03`, `Decision (EU) 2017/1483`, `FCC 47 C.F.R. Part 15.247 / Part 97`)

> [!IMPORTANT]
> **`wadamesh` drives physical sub-GHz LoRa radio transceivers (`Semtech SX1262`, `LR1110`, `LR1121`, `LR2021`) and 2.4 GHz Wi-Fi / Bluetooth Low Energy (BLE) radios (`Espressif ESP32-S3` / `ESP32-P4` / `ESP32-C6` and `Nordic nRF52840`).** Source code distribution does not constitute placing radio equipment on the market. **The hardware operator is solely and exclusively responsible for complying with all radio spectrum, transmit power, duty-cycle, and licensing laws in their jurisdiction.**

### 6.1 Statutory & Regulatory Framework
Under:
- **European Union:** **Directive 2014/53/EU (Radio Equipment Directive — RED)**, harmonized standards **ETSI EN 300 220-1 / EN 300 220-2** (Short Range Devices in the 25 MHz to 1 000 MHz range) and **ETSI EN 300 328** (2.4 GHz wideband transmission systems), **CEPT/ERC Recommendation 70-03 (`ERC/REC 70-03`)**, and **Commission Implementing Decision (EU) 2017/1483**; and
- **United States:** **FCC `47 C.F.R. Part 15.247`** (unlicensed ISM spread-spectrum / digital modulation intentional radiators in `902–928 MHz` and `2400–2483.5 MHz`) and **`47 C.F.R. Part 97`** (Amateur Radio Service),

the **hardware operator is solely and exclusively responsible** for verifying and configuring:

1. **Lawful Frequency Band Selection & Sub-Band Limits:**
   - **European Union (`868 MHz` SRD Band — `ETSI EN 300 220-1/-2` / `ERC/REC 70-03 Annex 1` / `Decision (EU) 2017/1483`):**
     - **SRD Sub-band `g1` (`868.000–868.600 MHz`):** Maximum radiated power **`<= 25 mW` ERP (`+14 dBm`)** and strict **`<= 1%` duty cycle** (or polite spectrum access LBT+AFA).
     - **SRD Sub-band `g3` (`869.400–869.650 MHz`):** Maximum radiated power **`<= 500 mW` ERP (`+27 dBm`)** and strict **`<= 10%` duty cycle** (`250 kHz` channel bandwidth).
   - **United States / Canada (`902–928 MHz` US ISM — `FCC 47 C.F.R. Part 15.247`):**
     - Operation within **`902–928 MHz`** subject to maximum conducted output power (`<= 1 W` / `+30 dBm`), EIRP limits (`<= +36 dBm` with `< 6 dBi` antenna), power spectral density (`<= +8 dBm / 3 kHz`), and minimum 6 dB bandwidth (`>= 500 kHz`) or FHSS channel-hopping rules.
   - **Other Jurisdictions (`433 MHz`, `915 MHz`, `920–925 MHz` AS923/AU915/KR920/IN865):**
     - Must strictly match the national frequency allocation table of the country in which the transceiver is physically located.
2. **Conducted Transmit Power (`dBm`), External PA Gain & Antenna Gain (`dBi`):**
   - Boards incorporating front-end power amplifiers (e.g., `Heltec V4` or external FEM stages) can exceed license-exempt ERP/EIRP limits if configured at maximum software `dBm` or paired with high-gain antennas (`dBi`). The operator must account for board PA gain, feedline loss, and antenna gain (`ERP = P_tx_dBm + G_dBi - 2.15 dB`) so radiated emissions never exceed statutory limits.
3. **Duty Cycle & Airtime Enforcement:**
   - In jurisdictions enforcing hourly duty-cycle caps (`<= 1%` in `868.0–868.6 MHz`, `<= 10%` in `869.400–869.650 MHz`), the operator must configure spreading factor (`SF`), bandwidth (`BW`), hop limits, and beacon/flood intervals to stay strictly below the legal duty-cycle ceiling.
4. **Disabling RF Transmission (`TX`) Where Unauthorized:**
   - Operating a radio transmitter on an unallocated frequency, above permitted ERP/EIRP, or without a required license is an administrative or criminal offense. Where transmission is not authorized in the operator's location (including aboard aircraft or in restricted zones), the operator must **disable RF transmission (`TX`)** or disconnect/terminate the transceiver prior to powering the device.

---

## 7. AS IS Warranty Disclaimer, Lithium-Ion Battery & PMIC (`AXP2101` / `BQ25896`) Hardware Risk Disclaimer

THE SOFTWARE, FIRMWARE CONFIGURATIONS, AND DOCUMENTATION ARE PROVIDED **"AS IS"**, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT, UNDER THE **MIT LICENSE** AND **GNU GENERAL PUBLIC LICENSE VERSION 3** (SEE [`LICENSE`](LICENSE)).

- **AS IS Warranty Disclaimer & Limitation of Liability:**
  - This open-source software and documentation are provided free of charge outside the course of commercial activity on an **"AS IS"** basis. To the fullest extent permitted by applicable law, the authors and contributors assume no warranty for freedom from defects and shall not be liable for any direct, indirect, incidental, special, exemplary, or consequential damages arising from flashing, configuring, or operating this firmware.
- **Lithium-Ion / LiPo Battery & PMIC (`AXP2101` / `BQ25896` / `BQ27220`) Safety Warning:**
  - Several target boards (`LilyGo T-Deck`, `T-Deck Pro`, `T-Lora Pager`, `Heltec V4`, `Elecrow ThinkNode M9`, `Tanmatsu`) include software-configurable power management ICs (`AXP2101`), battery chargers (`BQ25896`), and fuel gauges (`BQ27220`).
  - Modifying charge current (`mA`), termination voltage (`4.10–4.35 V`), NTC temperature limits, or battery capacity settings beyond the cell manufacturer's rated datasheet specifications can cause **lithium plating, cell swelling, thermal runaway, fire, or explosion**.
  - Never leave lithium-ion or lithium-polymer cells charging unattended, never exceed the rated charge current (`C-rate`) or maximum charge voltage of your specific cell, and never bypass hardware or NTC thermal protection. Flashing and operating this firmware is performed **entirely at the hardware owner's sole risk**.

---

## Addendum: German Statutory References (Applicable to Residents of Germany)

For contributors, maintainers, and operators residing in Germany, the English legal provisions above correspond directly to the following national statutory provisions under German law:

1. **Trademark Indication of Intended Purpose (`§ 23 Abs. 1 Nr. 3 MarkenG` & BGH `I ZR 6/10`):**
   - Section 23(1) No. 3 of the German Trademark Act (*Markengesetz* — `§ 23 Abs. 1 Nr. 3 MarkenG`, *Bestimmungsangabe*) and settled German Federal Court of Justice jurisprudence (BGH `I ZR 6/10`) authorize the descriptive use of third-party trademarks and hardware designations where necessary to indicate the intended purpose or compatibility of software and accessories.
2. **Software Copyright, Observation & Interoperability (`§§ 69a Abs. 2 & 5, 69d Abs. 3, 69e, 69f Abs. 2, 69g Abs. 2, 95a UrhG`):**
   - **Exclusion of Ideas & Interfaces (`§ 69a Abs. 2 S. 2 UrhG`):** Ideas and principles underlying computer program elements, including functional interfaces, hardware register maps, and protocols, are not protected by copyright.
   - **Observation & Testing (`§ 69d Abs. 3 UrhG`) and Interoperability (`§ 69e UrhG`):** Authorize lawful users to observe, study, and test program functioning and to perform analysis required to achieve interoperability of an independently created program.
   - **Mandatory Nature (`§ 69g Abs. 2 UrhG`):** Contractual provisions contrary to `§ 69d Abs. 2, 3` or `§ 69e UrhG` are statutorily null and void (*nichtig*).
   - **Computer Program TPM *Lex Specialis* (`§ 69a Abs. 5`, `§ 69f Abs. 2`, `§ 95a UrhG`):** Under `§ 69a Abs. 5 UrhG`, the general anti-circumvention provisions of `§§ 95a–95d UrhG` do not apply to computer programs, which are governed exclusively by `§ 69f Abs. 2 UrhG`; `wadamesh` targets open developer bootloaders and circumvents zero protection measures.
3. **Trade Secret Reverse-Engineering Safe Harbor (`§ 3 Abs. 1 Nr. 2 lit. a & b GeschGehG`):**
   - Section 3(1) No. 2 of the German Trade Secrets Act (*Geschäftsgeheimnisgesetz* — `§ 3 Abs. 1 Nr. 2 lit. a & b GeschGehG`) explicitly permits acquiring technical information by observing, examining, disassembling, or testing a publicly available product (`lit. a`) or a product in the lawful possession of the observer free from any valid duty to limit acquisition (`lit. b`).
4. **German IT Criminal Law & Hardware Owner Authority (`§§ 202a–202c, 303a StGB` & `§ 903 BGB`):**
   - In accordance with German Federal Constitutional Court jurisprudence (*BVerfG, 2 BvR 2233/07*) and property owner authority under `§ 903 BGB` (*Befugnisse des Eigentümers*), flashing and configuring firmware on one's own microcontroller hardware (*eigene Daten*, `§ 303a StGB`) is fully authorized and does not circumvent any third-party access security barrier (*Zugangssicherung*, `§§ 202a–202c StGB`).
5. **Statutory Gratuity Liability Privilege (`§§ 521, 523, 524, 599, 600 BGB`):**
   - For gratuitous, non-commercial open-source software provision (*unentgeltliche Überlassung*), liability under the German Civil Code (*Bürgerliches Gesetzbuch* — `§§ 521, 523, 524, 599, 600 BGB`) is limited to intent (*Vorsatz*) and gross negligence (*grobe Fahrlässigkeit*), with no warranty for material or legal defects (*Sach- oder Rechtsmängel*).
6. **German Radio Equipment & Spectrum Regulations (`FuAG`, `TKG`, `BNetzA Vfg 99/2022` / `Vfg 34/2020`, `AFuG` / `AFuV`):**
   - Radio operation in Germany is governed by the Radio Equipment Act (*Funkanlagengesetz* — `FuAG`), the Telecommunications Act (*Telekommunikationsgesetz* — `TKG`), the Federal Network Agency general spectrum allocations for Short Range Devices (*Allgemeinzuteilungen der Bundesnetzagentur* — `BNetzA Vfg 99/2022` / `Vfg 34/2020`), and—where operating on amateur bands—the Amateur Radio Act and Ordinance (`AFuG` / `§ 16 Abs. 1 Nr. 4 AFuV`, which prohibits encrypted transmissions on amateur radio frequencies).
