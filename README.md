# ErdemFlip / Makeshift Flipper

An open, two-MCU ESP32 handheld for authorized, local hardware experiments.
The ESP32-P4 runs the interface and peripheral logic; an ESP32-C6 provides
Wi-Fi and passive Bluetooth Low Energy discovery.

> **Prototype status:** The firmware has CI builds and host-side logic tests,
> but the current ST7789 display configuration and the C6 integration still
> require real-hardware validation. Features described below are implemented
> scope, not claims of field-proven operation.

![Technical overview](docs/makeshift-flipper-technical-overview.jpg)

## Purpose and boundaries

This project is for devices, cards, remotes, accounts, and networks that you
own or are explicitly authorized to test. It is intentionally designed around
local, consent-based use:

- Wi-Fi monitoring is receive-only; there is no deauthentication, packet
  injection, password capture, or cracking functionality.
- BLE support is passive advertisement discovery only; it does not pair,
  connect, or access GATT data.
- RFID/NFC and IR features must be used only with authorized test targets.
- Camera/OCR, microphone, GPS, Sub-GHz, remote control, and cellular features
  are **not** part of the current firmware.

See the [capability and use-boundary assessment](YETENEK_VE_UYUM_DEGERLENDIRMESI.md)
for the detailed feature, privacy, safety, and added-component evaluation.

## Current firmware scope

| Area | Implemented scope | Validation status |
| --- | --- | --- |
| Interface | 240x240 ST7789 color UI, analog joystick navigation, standalone BACK button, error history | Current display wiring needs hardware validation |
| RFID/NFC | 125 kHz EM4100 reads; RC522 MIFARE Classic UID read, saved UID library, limited authorized clone flow | Hardware validation pending |
| Infrared | NEC receive, learn, save, browse, delete, and transmit | Hardware validation pending |
| Wi-Fi | C6-assisted scan, owner-managed setup, passive channel-hopping AP monitor | C6 end-to-end validation pending |
| Bluetooth | Passive BLE advertisement scan | C6 end-to-end validation pending |
| Diagnostics | On-device rolling error history and optional local-LAN export | Hardware/network validation pending |

## Architecture

```text
ESP32-P4 (main/)                         ESP32-C6 (c6-firmware/)
  UI, input, display                       Wi-Fi + passive BLE operations
  RFID/NFC and IR drivers                  Local setup and diagnostics transport
          \                                 /
           \--- UART: P4 GPIO18/19 -------/
```

The two targets are independent ESP-IDF projects. Build and flash each one
separately; see [the C6 README](c6-firmware/README.md) for its commands and
UART protocol.

## Build

Install and export ESP-IDF v5.3.1 or newer, then build the P4 firmware from
the repository root:

```sh
idf.py set-target esp32p4
idf.py build
idf.py -p COMx flash monitor
```

Build the companion C6 firmware separately:

```sh
cd c6-firmware
idf.py set-target esp32c6
idf.py build
```

CI builds both firmware targets and runs host-side unit tests on every push
and pull request. The host suite can also be run locally in a Bash/GCC-capable
environment:

```sh
bash tests/run_tests.sh
```

## Hardware and validation

The pin assignments, expected behavior, and validation order are intentionally
kept outside this overview:

- [Hardware test matrix](HARDWARE_TEST_MATRIX.md) — the checkable real-device
  verification list.
- [Hardware integration plan](HARDWARE_INTEGRATION_PLAN.md) — future camera
  and constrained voice-command integration analysis; neither is implemented.
- [Known issues](KNOWN_ISSUES.md) — confirmed fixes, open risks, and items
  awaiting real-hardware evidence.

Do not mark a feature as working until it has passed the relevant row in the
hardware test matrix.

## Repository map

| Path | Role |
| --- | --- |
| `main/` | ESP32-P4 firmware: UI, input, RFID/NFC, IR, diagnostics, and C6 link |
| `c6-firmware/` | ESP32-C6 companion firmware: Wi-Fi and passive BLE work |
| `tests/` | Hardware-independent host tests for parsers, storage libraries, UI entry, and diagnostics |
| `.github/workflows/build.yml` | CI for both ESP-IDF targets and the host tests |
| `HARDWARE_TEST_MATRIX.md` | Hardware acceptance checklist |
| `YETENEK_VE_UYUM_DEGERLENDIRMESI.md` | Capability, use-boundary, privacy, and added-component assessment |
| `SHARING.md` | Accurate, safe project-sharing checklist and post drafts |

## Project hygiene

OCR and ASR data, models, training reports, and virtual environments belong to
their dedicated research repositories, not to this firmware repository. The
root `.gitignore` excludes those local experiment directories so firmware
history and GitHub releases stay small, reproducible, and reviewable.

## Sharing the project

Use [SHARING.md](SHARING.md) after a real-device demonstration is available.
Until then, keep the prototype-status notice above and do not represent the
technical overview image as a hardware demo.
