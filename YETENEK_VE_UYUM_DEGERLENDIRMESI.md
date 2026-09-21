# Makeshift Flipper: Capability, Use-Boundary, and Added-Component Impact Assessment

**Date:** 2026-09-21  
**Scope:** This document is a design and compliance assessment based on the P4
main firmware, C6 companion firmware, and the existing hardware/AI plans in
this repository. It is not legal advice or an on-device validation report.

## How to read this document

A capability is not inherently “legal” or “illegal.” The relevant factors are
authorization, purpose, data, target system, and local law. The
**inappropriate or potentially unlawful use** columns below are not feature
specifications or usage instructions. They record product risks and the
controls that should be put in place.

For this assessment, “authorized use” means at least:

- Owning the hardware, account, card, remote, or network, or having the
  owner's explicit and recorded permission;
- Defining the scope, timing, and data of a test in advance;
- Complying with personal-data, communications-privacy, access-control, and
  radio regulations;
- Sharing findings and logs only with authorized people and under secure
  retention conditions.

> **Validation warning:** Most features present in the code have not been
> validated on physical hardware. An initial P4 boot was observed with the
> earlier SSD1306 arrangement, but the current ST7789 display arrangement is
> unverified; C6 Wi-Fi/BLE end-to-end behavior is also unverified. Therefore,
> “present” below means in code/design scope, not “working.”

## Logical capabilities of the current device

| Area | Authorized and legitimate use in the code/design scope | Inappropriate or potentially unlawful-use risk | Built-in limit / status |
|---|---|---|---|
| 125 kHz RFID reading | Inventorying owned EM4100 tags; lab asset tracking; compatibility testing | Collecting another person's card identifier without authorization or targeting access control | The RDM6300 is read-only. |
| 13.56 MHz NFC / MIFARE Classic | Inspecting an owned test/training card or authorized system; data recovery | Copying an access card to attempt unauthorized physical entry | RC522; focused on 4-byte UIDs. The 7/10-byte UID flow is incomplete/incompatible. |
| MIFARE Classic cloning | Backing up owned test cards; checking weak default keys in a security lab | Bypassing access, billing, or identity controls by copying a card | Reads sectors accessible with its default-key dictionary; target card trailer blocks are not written. This technical limit does not remove the authorization requirement. |
| IR learning and NEC transmission | Backing up an owned TV/air-conditioner remote; home automation; IR receiver/transmitter testing | Controlling another person's device without permission or disrupting service | NEC-focused; at most 16 NVS records. |
| IR direction finding | Roughly measuring the coverage/placement of an owned IR emitter or remote | Monitoring another person's remote-control activity without authorization | Present in code but disabled because of RMT/GPIO conflicts; it is not precise location tracking. |
| Wi-Fi scanning | Coverage/channel inventory on an owned network; authorized wireless site survey | Collecting neighboring SSID/BSSID data outside the stated purpose or profiling | Via the C6; a UART parsing issue for result text is recorded. |
| Passive Wi-Fi monitoring | Viewing owned AP beacon/probe responses, channel, and security mode | Observing nearby device/network activity without permission or creating personal-data profiles | Receive-only; no deauth or packet injection in the design. The C6 is unverified. |
| Passive BLE scanning | Discovering owned BLE devices; advertisement-visibility testing | Tracking people or movement from device address/name/RSSI | Does not connect, access GATT, or support classic Bluetooth. |
| Wi-Fi setup | Connecting the owner's device to the owner's network; offline joystick-based setup | Connecting to a network not owned or authorized by the user | The temporary WPA2 AP password is generated per session; setup HTTP is not TLS-protected. |
| Error log / LAN transfer | Moving diagnostics from an owned device to a local PC | Unnecessary collection or insecure sharing of card/network-use traces | 24 RAM entries; optional PC server uses unauthenticated plain HTTP. |
| Local data libraries | Storing owned IR codes and authorized tag identifiers | Persistently retaining identifiers or radio data belonging to others | IR/RFID libraries use NVS; data becomes a risk if the device is lost. |

## What the current software explicitly does not do

The repository design does not include, or explicitly excludes, these
higher-risk behaviors:

- Wi-Fi deauthentication, management-frame injection, or attack traffic;
- Wi-Fi password cracking, password capture, or unauthorized network-access
  automation;
- BLE connections, pairing, GATT read/write, or classic Bluetooth scanning;
- General-purpose RFID card emulation; writing on the 125 kHz side;
- Writing card trailer blocks; this reduces card-lockout risk;
- Camera, audio recording, OCR, or voice-command runtime integration;
- Remote control, cellular networking, GPS/GNSS, or Sub-GHz radio.

These absences are not a security guarantee: MIFARE Classic cloning, IR replay,
and wireless-environment observation can still cause harm in an unauthorized
context.

## High-risk capabilities intentionally excluded from the product

The hardware platform could be changed by a third party, like any
general-purpose embedded system. The following capabilities are often
marketed as attention-grabbing "multi-tool" features, but they are **not
implemented, planned, or supported** in this repository. They are listed here
to make the project boundary explicit, not as a usage or implementation guide.

| Excluded capability | Why it is high risk | Product position | Safe, authorized alternative |
|---|---|---|---|
| Wi-Fi deauthentication / disassociation | Can intentionally disconnect people and devices from a network and disrupt service | Prohibited; no management-frame injection exists in this project | Passive channel/AP inventory on a network you administer |
| Rogue access point or captive-portal impersonation | Can mislead users into joining a fake network and expose credentials or traffic | Prohibited; setup AP is limited to configuring the owner's device | Use the documented owner-managed setup flow with a unique session password |
| Wi-Fi credential capture, cracking, or password attacks | Targets access credentials and can enable unauthorized network entry | Prohibited; no capture, handshake-collection, or cracking workflow exists | Audit the owner's Wi-Fi configuration and use strong WPA2/WPA3 credentials |
| Active Wi-Fi packet injection or disruptive probing | Can alter nearby network behavior, degrade service, or interfere with other users | Prohibited; monitor mode is receive-only by design | Perform passive site surveys and inspect the owner's AP configuration |
| BLE spoofing, pairing attacks, or GATT access | Can impersonate, track, or access another person's BLE device/data | Prohibited; BLE support is advertisement-only and never connects | Passive visibility testing for devices you own |
| General RFID/NFC emulation or payment/access-card bypass | Could imitate credentials or undermine physical-access and payment controls | Not implemented and outside product scope | Test owned training cards and document system weaknesses through authorized channels |
| RF jamming or high-power transmission | Can interfere with communications, safety systems, and regulated spectrum use | Strictly prohibited; no jamming or external high-gain RF design is included | Receiver-first measurements in a controlled, regulation-compliant lab |
| USB HID injection or malicious peripheral behavior | Can issue unwanted commands to a connected computer or collect data | Not implemented; USB should remain limited to approved update/data-transfer roles | Signed firmware updates and an explicitly selected data-export mode |
| Covert camera, microphone, or location tracking | Enables non-consensual collection of sensitive visual, audio, or location data | Not implemented; any future sensor design needs hardware indicators and explicit consent controls | Visible, user-initiated accessibility or lab features with no default recording |

These boundaries are part of the product identity: the project is intended to
be technically interesting because it integrates multiple local peripherals
and a two-MCU architecture, not because it disrupts, impersonates, or secretly
collects data from other people or systems.

## If a component is added: likely capabilities and boundaries

This table is not a purchasing or implementation plan. Each row also states
the authorization, indicator, and data-protection controls that should become
product requirements if the component is added.

| Potential added component | Legitimate value | Inappropriate or potentially unlawful risk | Safe product boundary |
|---|---|---|---|
| OV2640/similar camera + OCR | Offline reading of text on an owner's document, label, or device display; accessibility | Collecting images of people, documents, screens, or plates without consent; retaining sensitive data | Physical camera indicator, no-recording default, clear preview/consent, and storage disabled by default. |
| I2S microphone + Turkish command model | Hands-free operation and accessibility for the owner | Recording ambient conversations without consent or creating voice profiles | Push-to-talk/hardware mute, visible recording indicator, no raw-audio storage, and only a constrained command vocabulary. |
| microSD / encrypted external storage | Authorized test evidence and user backup | Bulk collection of personal data, card dumps, or environment scans; disclosure after device loss | Encryption, automatic retention expiry, export confirmation, and no default retention of sensitive dumps. |
| RTC + secure logging | Auditable timestamps for authorized tests | Long-term tracking of people or network movement | Short retention, visible log screen, and user-controlled deletion. |
| GNSS/GPS | Coverage mapping or asset location for an owner's field work | Tracking a person or vehicle without authorization | Explicit user start, continuous on-screen location indicator, and history disabled by default. |
| USB-C data interface | Firmware updates and secure export of the owner's logs | Malicious USB behavior, unauthorized data collection, or turning into a harmful peripheral | User-selected signed firmware and a restricted data class; no automatic keyboard/network-device modes. |
| Secure element | Device identity, encrypted settings/logs, and signed updates | Does not add an attack capability by itself; poor key management can cause data loss | Never export keys; define a recovery process and signed-update policy. |
| More capable NFC reader/emulator | Compatibility and security testing of an owned card system | Trying to imitate access-control credentials | Restrict emulation to test cards, require an on-device “authorized test” confirmation and audit record; do not support production access cards. |
| Sub-GHz receiver/transmitter | Lab analysis and protocol compatibility for an owner's remote/sensor | Unauthorized remote replay, interference, or unlawful transmission | Not recommended for this project; if added, allow only regulation-compliant bands/power, receiver-first design, and visible transmission approval. Jamming is strictly out of scope. |
| LTE/LoRa/remote connectivity | Owner status notifications or locating a lost device | Remote surveillance, covert tracking, or unauthorized remote control | Disabled by default, end-to-end authentication, visible pairing, and no remote radio/access feature by default. |
| External antenna / high-gain RF hardware | Authorized laboratory measurement | Longer-range unauthorized collection/transmission and radio-regulation violations | Do not add to the portable product; if required, use only in a controlled, regulation-compliant laboratory setup. |

## Design decisions that keep the product oriented toward legitimate use

1. **Authorization declaration and target verification:** Before actions such
   as cloning, IR transmission, and log export, use a short clear confirmation:
   “I have authorization for this target.” This does not replace legal
   permission, but reduces accidental misuse.
2. **Passive by default:** Keep Wi-Fi/BLE receive-only. Do not include packet
   injection, deauth, password attacks, jamming, or covert-tracking features
   in the product scope.
3. **Data minimization:** Disable persistent storage of Wi-Fi/BLE results and
   card dumps by default; remove it at session end unless the user explicitly
   saves it.
4. **Protection for sensitive records:** Encrypt IR codes, UIDs, and error logs
   held in NVS or microSD; authenticate USB/LAN exports. The existing plain
   HTTP log server is only acceptable on an isolated development LAN.
5. **Visibility:** A camera, microphone, location, or transmission feature
   must not run without a physical LED/icon and deliberate user initiation.
6. **Cloning safeguards:** The MIFARE Classic cloning screen should clearly
   state its authorization requirement, use short-lived memory, avoid
   persistent dumps, and not position production access-card copying as a
   product purpose.
7. **Secure updates:** Use signed firmware, rollback protection, and visible
   versioning. Otherwise, a third party could alter the device's environment-
   interaction capabilities for misuse.
8. **Field validation:** Do not claim any capability “works” until it has
   passed real-hardware testing and user acceptance using
   `HARDWARE_TEST_MATRIX.md`.

## Priority compliance and security work for this revision

1. Disable the C6 Wi-Fi setup's plain HTTP and unauthenticated error-log
   endpoint in production, or replace them with authentication and encryption.
2. Fix the recorded Wi-Fi-name parsing issue that can break UART boundaries,
   using a lossless unambiguous protocol with explicit length/binary encoding.
3. Add a prominent authorization warning, short-lived memory usage, and action
   logging to the NFC cloning flow; do not present cloning production access
   cards as a product goal.
4. Document retention and export rules for passive Wi-Fi/BLE results.
5. Turn the privacy controls above into hardware and software requirements
   before adding a camera, microphone, GNSS, or remote connectivity.

## Malicious acquisition: systematic threat model

It is not possible to enumerate “everything” in an absolute sense: an attacker
can alter physical hardware, firmware, and the connected computer. At that
point, this device can expand like any general-purpose ESP32-based platform.
The record below separates **real capabilities visible in the current
repository** from **transformations that require additional components and/or
firmware changes**. It is a risk inventory, not an attack method, code sample,
frequency plan, or access-bypass guide.

### A. Misuse classes possible with the unmodified current design

| Threat class | Existing device basis | Potential harm | Prerequisite / important limit | Mitigation |
|---|---|---|---|---|
| Physical access-card misuse | RC522 MIFARE Classic read/write and limited cloning | Unauthorized entry to an area; disclosure of card data | The target must be a supported type and its accessible sectors must be readable with available keys; it does not cover every card type | Restrict cloning to a test-card mode, show explicit authorization warning, do not retain dumps, and use a device lock. |
| Identifier inventory / profiling | RFID UID reading and NVS library | Collecting card identifiers that can be linked to people/assets | Requires close physical proximity and a supported card | Default to no saving, encrypt storage, set retention expiry, and log access. |
| Physical-device control | IR learning and NEC replay | Turning another person's compatible IR device on/off/changing it without permission | Requires line of sight, NEC compatibility, and a learned/valid code | Require clear confirmation before transmit, lock screen, encrypt IR codes, and use physical policies in managed environments. |
| Nearby-network discovery | Wi-Fi scan/monitor and BLE advertisement scan | Network/live-device inventory; time-and-place profiling | Device must be within radio range; the design does not perform active attack/connection | Session indicator, automatic cleanup, BSSID/MAC masking, and an authorized-site profile. |
| Privacy violation | Visibility of SSID, BSSID, BLE address/name, and RSSI; error logging | Inference about nearby people, devices, or networks | Passive data can still be personal data; risk rises when combined with time/location | Saving disabled by default, short retention, export confirmation, and staff training. |
| Local-network data disclosure | Sending error logs to a PC over plain HTTP | Exposing context about card/network/BLE use to third parties on the network | Requires the same LAN and an open/unprotected log server | Disable in production; use TLS, authentication, narrow network access, and encrypted logs. |
| Content disclosure after device compromise | IR/RFID library in NVS and diagnostic data in RAM/NVS | Taking or deleting saved remote/UID data | Requires physical access and appropriate debug/firmware access; specifics depend on device configuration | Secure boot/flash encryption, user PIN, signed firmware, and secure deletion. |
| Denial of service / loss of reliability | C6 sessions and recorded protocol/task bugs | Setup, scan, or logging can lock up/restart | The recorded software defects must be triggerable; field validation status varies | Resolve `KNOWN_ISSUES.md` findings, add regression tests, watchdogs, and health screen. |

### B. Classes a malicious holder could create by changing hardware or firmware

| Transformation | Added item | Potential misuse effect | Present in this repository today? | Design/procurement policy |
|---|---|---|---|---|
| Covert visual collection | Camera + recording/transmission firmware | Non-consensual image collection, public-space surveillance, or document/screen capture | No; camera/OCR is planning only | Hardware-wired camera LED, lens cover, no-recording default, and user confirmation before export. |
| Covert audio collection | Microphone + continuous-recording firmware | Non-consensual conversation recording or voice profiling | No; only a limited command classifier is planned | Hardware mute, push-to-talk, recording LED, and no raw-audio storage. |
| Location tracking | GNSS and/or remote connectivity | Tracking a person/vehicle | No | Default-off location history; visible user pairing for remote access. |
| Broader radio misuse | Sub-GHz, high-gain antenna, or additional RF module | Unauthorized remote interaction, longer-range collection, or unlawful transmission | No | Do not include these components in the base product; treat jamming and active interference as prohibited. |
| Broader NFC emulation | Different NFC hardware and custom firmware | Attempting to imitate access/identity systems | No; current design provides no general card emulation | Test-environment profile only, signed firmware, and exclusion of production-card functionality. |
| Remotely controlled surveillance device | LTE/LoRa/internet client + backend | Remote device control, telemetry collection, or covert data exfiltration | No; the current C6 is designed for local functions | Internet egress off by default, end-to-end authentication, and explicit visible user pairing. |
| Peripheral-attack device | USB host/gadget, additional adapters, and new firmware | Unauthorized interaction with connected computers/devices or data collection | No; the repository has no such user feature | Restrict USB to signed updates and approved data transfer. |
| Persistent malicious-firmware platform | Debug access, reflashing, or supply-chain tampering | Hiding the above behaviors behind apparently benign features | A design risk; secure boot/flash encryption are not documented safeguards yet | Secure boot, flash encryption, signed/visible-version firmware, and manufacturing key procedures. |

### C. Impact chains

This device is most readily turned into a harmful tool through one of the
following three chains. The chains show that risk comes not from a single
feature alone, but from combining observation with retention and export.

```text
Nearby-environment observation  -> identifier/radio metadata -> persistent storage -> export
RFID/NFC or IR interaction      -> unauthorized replay/reuse -> physical/device impact
Camera/microphone/GNSS added    -> sensitive-data collection -> remote export -> tracking/surveillance
```

The first chain is possible to a limited degree in the current design. The
second creates legal and security risk only when an unauthorized target/card/
remote is chosen. The third is not present in the current repository, but
arises once hardware, firmware, and transfer infrastructure are added.

### D. Claims that are not supported as current capabilities

It would be inaccurate to record the following as abilities of the current
device:

- Wi-Fi password capture/cracking, Wi-Fi packet injection, or deauth;
- BLE pairing compromise, GATT data reading, or connecting to devices;
- Credit-card/payment-card copying; this requires different technologies and
  lies outside the current RC522/MIFARE Classic flow;
- General RFID/NFC card emulation or copying every card type;
- Signal jamming, cellular interception, GPS tracking, camera/audio recording,
  or internet-based remote control;
- A claim that any feature has successfully worked on physical hardware.

These limits can change if new hardware, firmware, or external services are
added. The product's security model must therefore rely not only on today's
menu but also on signed firmware and a hardware-expansion policy.

## Source scope

- `README.md`: current modules, pin plan, operational limits, and security
  notes.
- `c6-firmware/README.md`: P4–C6 protocol, passive monitor/BLE scan limits,
  and local error-log transfer.
- `HARDWARE_INTEGRATION_PLAN.md`: camera/OCR and microphone-command model in
  planning only.
- `KNOWN_ISSUES.md`: physical-validation status and known C6/P4 security,
  reliability, and parsing issues.
