# Makeshift Flipper / EMAG

An open, single-MCU handheld for authorized, local hardware experiments.
One **ESP32-C6** runs everything — the interface and peripheral logic
(display, input, RFID/NFC, IR) plus its own Wi-Fi and passive Bluetooth
Low Energy radio.

> **New:** the device moved from a two-chip design (ESP32-P4 main MCU +
> ESP32-C6 radio companion over UART) to a **single ESP32-C6 that runs both
> the UI and the radio directly** — no second chip, no UART link. The former
> P4 is freed for a separate lab/TinyML project.

## Where the firmware lives

All firmware, configuration, and hardware docs are in
**[`MakeshiftFlipper/`](MakeshiftFlipper/)**.

Start here:

- **[MakeshiftFlipper/README.md](MakeshiftFlipper/README.md)** — full
  project overview, capabilities, build, and repository map.
- [C6 standalone hardware plan](MakeshiftFlipper/C6_STANDALONE_HARDWARE_PLAN.md)
  — pin map, power tree, BOM, and bring-up order.
- [Capability and threat assessment](MakeshiftFlipper/CAPABILITY_AND_THREAT_ASSESSMENT.md)
  — capability, use-boundary, privacy, and added-component evaluation.
- [MCU architecture decision](MakeshiftFlipper/MCU_ARCHITECTURE_DECISION.md)
  — why the design went single-chip.

## Status

Builds clean for the ESP32-C6 and passes host-side logic tests. The
hardware has not yet been assembled — pin wiring, the shared SPI bus, the
RIGHT-button/I2C-SDA sharing, and Wi-Fi+BLE coexistence still need
real-hardware validation. See the firmware README for the exact scope.

## Responsibility

This project is only for devices, cards, remotes, accounts, and networks
that you own or are explicitly authorized to test. Wi-Fi monitoring is
receive-only and BLE support is passive discovery only. Building or owning
this device does not authorize anything it is technically able to do — see
the [capability and threat assessment](MakeshiftFlipper/CAPABILITY_AND_THREAT_ASSESSMENT.md)
for the full boundaries and built-in limits.
