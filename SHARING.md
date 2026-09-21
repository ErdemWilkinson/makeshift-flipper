# Makeshifter-Emag launch kit

Use this only after publishing the repository and replacing the conceptual
cover image with a short, unedited recording of the real device. Keep the
prototype-status line in the README until hardware validation is complete.

## One-minute demo checklist

Record a 20-30 second clip with no cuts:

1. Power on and show the boot screen.
2. Move through the menu with the joystick.
3. Read a tag you own *or* learn and send a command to an IR device you own.
4. Stop on the result screen, then show the repository URL in the post.

Avoid recording or publishing third-party card data, Wi-Fi contents, or IR
actions on devices you do not own or have permission to test.

## Reddit

**Title:** I built an ESP32-P4 handheld for testing my own RFID, IR, Wi-Fi and Bluetooth devices

**Body:**

I am building Makeshifter-Emag, an open ESP32-P4 + ESP32-C6 handheld that
keeps common maker experiments in one joystick-driven interface: RFID/NFC UID
reading, IR learning/transmit, passive Wi-Fi AP monitoring, passive BLE
advertisement scanning, and phone-based Wi-Fi setup.

The interesting part for me is keeping the design inspectable: two ESP-IDF
projects, a small UART protocol between the chips, and no cloud dependency.
This is still an early hardware prototype. Both firmware projects compile,
but I am only claiming real behavior after on-device tests. Here is the short
demo: [attach GIF/video]. Source and wiring notes: [GitHub URL].

I would value feedback on the P4/C6 split, pin plan, and the first hardware
test checklist.

Best-fit communities: r/embedded, r/esp32, r/TinyML, r/LocalLLaMA (only when
posting the voice-command work), and r/Turkey. Read each community's current
self-promotion rules before posting; use r/MachineLearning only when the ML
artifact and evaluation results are ready.

## Show HN

**Title:** Show HN: Makeshifter-Emag – an open ESP32-P4 handheld for your own RFID, IR and radio experiments

**Text:**

I am building a DIY, ESP-IDF-based handheld around an ESP32-P4 with an
ESP32-C6 companion. The project puts authorized RFID/NFC UID reading, IR
learn/transmit, passive Wi-Fi AP monitoring, passive BLE scanning, and simple
Wi-Fi setup behind a joystick-driven interface.

The source includes both firmwares, the UART protocol, a pin plan, known
issues, and a hardware test matrix. It compiles under ESP-IDF 5.3.1 but is not
yet hardware-validated, so I am looking for design feedback rather than
claiming a finished product. Demo: [attach GIF/video]. Repo: [GitHub URL].

## X / Twitter thread

1. I am building **Makeshifter-Emag**: an open ESP32-P4 handheld for exploring my own RFID/NFC tags, IR remotes, Wi-Fi APs and BLE devices. [demo GIF/video]
2. The goal is a modifiable, offline-first maker tool: P4 for the UI and peripherals, C6 for Wi-Fi/Bluetooth, connected over a tiny UART protocol.
3. The repo includes the pin plan, firmware, known issues, and a hardware test matrix. It compiles, but I am keeping the prototype label until real-device tests are complete.
4. Source: [GitHub URL] #ESP32 #EmbeddedSystems #TinyML

## LinkedIn

I am documenting a hands-on embedded systems project: Makeshifter-Emag, an
open ESP32-P4 handheld that brings together authorized tag reading, IR remote
learning, passive Wi-Fi/Bluetooth observation, and a joystick-driven UI.

The most useful part of the project so far has been learning how to split work
between an ESP32-P4 and ESP32-C6, define a small serial protocol, and write
down the unverified assumptions in a hardware test matrix instead of hiding
them. The firmware builds; real-device validation is next. [demo GIF/video]

Repository: [GitHub URL]
