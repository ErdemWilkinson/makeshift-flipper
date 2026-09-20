# On-device AI integration plan: camera (OCR) + TinyML runtime

Status: **plan only, no firmware code written yet.** This exists to turn
"we need a camera and a TinyML runtime someday" into a concrete set of
decisions and a build order, so the first line of integration code has an
actual target to write against instead of guessing.

This does not cover the voice command classifier's TinyML integration
(keyword spotting) in detail — that's a much smaller runtime footprint
(no camera, no frame buffer, an int8 model well under 100KB) and can most
likely share the same runtime decision made here (§3) without its own
separate camera/pin work. It's called out at the end (§7) once that
decision is made.

## 1. What's blocking integration today

From `KNOWN_ISSUES.md` Round 17 and `ocr/README.md`'s own "Hardware
integration status" section:

- No camera driver, camera pin assignment, or frame buffer anywhere in
  `main/`
- No TinyML runtime (TFLite Micro or ESP-DL) linked into the ESP-IDF build
- Unknown: exact ESP32-P4-Pico revision and installed PSRAM size — this
  repo has never stated it, and it changes every answer below
- Unknown: real free-pin budget once a camera's data bus is added on top
  of the current wiring (see §4)

None of this can be resolved by reading code alone — the first two
bullets under "before writing any firmware integration code" in
`ocr/README.md` are genuinely hardware questions, not open-ended
software design ones. This plan assumes reasonable defaults where the
real answer isn't known yet and flags every place that assumption needs
checking against the physical board.

## 2. Camera module selection

| Option | Interface | Resolution | Notes |
|---|---|---|---|
| **OV2640** (Recommended) | DVP (8-bit parallel) | up to 1600x1200, but OCR only needs a small crop | Cheapest, most common ESP32-camera module (same one used on ESP32-CAM boards), huge amount of existing ESP-IDF driver code/examples to reference, outputs JPEG or raw RGB565/YUV422 — **not native grayscale** |
| OV5640 | DVP or MIPI-CSI | up to 2592x1944 | Higher res than needed for line OCR, MIPI variant needs the P4's MIPI-CSI peripheral (more complex driver, less example code available) |
| GC0308 | DVP | 640x480 | Smaller/cheaper, sometimes used on tiny badge-style boards, less driver documentation than OV2640 |

**Recommendation: OV2640 over DVP.** It's the best-documented option for
an ESP32 target, and the OCR model's input (160x32 grayscale) is small
enough that resolution/interface bandwidth headroom isn't a real
differentiator here — driver maturity and pin count are what matter.

**Consequence for the OCR pipeline:** OV2640 doesn't output grayscale
directly. The capture path needs either:
- YUV422 mode and take the Y (luma) channel directly as grayscale (no
  extra conversion cost), or
- RGB565 mode and convert to grayscale in software (`0.299R + 0.587G +
  0.114B` or a cheaper shift-based approximation) before resizing to
  160x32

YUV422 is the better choice if the OV2640 driver supports it cleanly —
it avoids a conversion pass entirely and this model never needed color.

## 3. TinyML runtime selection

| Option | What it is | Fit for this project |
|---|---|---|
| **TFLite Micro / LiteRT for Microcontrollers** (Recommended for OCR) | Google's official microcontroller inference engine, ESP-IDF component `esp-tflite-micro` | `ocr/`'s model is already exported to full-int8 `.tflite` (`ocr/artifacts/turkish_line_ocr_int8.tflite`) specifically for this runtime — no re-export/re-training needed. CNN+BiLSTM architecture (see `ocr/README.md`) needs an LSTM op kernel; confirm it's in TFLite Micro's op resolver (it is, as of recent versions — the `esp-tflite-micro` component tracks upstream) |
| ESP-DL | Espressif's own inference SDK, hand-optimized for Xtensa/RISC-V | Better raw performance on ESP32-P4 specifically (it's Espressif's own silicon), but the OCR model would need re-export to ESP-DL's model format (not just quantization — a different conversion pipeline than the existing `export_tflite.py`), and CTC+BiLSTM support is less certain than TFLite Micro's |

**Recommendation: TFLite Micro (`esp-tflite-micro` component).** The OCR
model is *already built* for this runtime — switching to ESP-DL would
mean redoing the export step and re-validating operator support for no
clear benefit at this model's small size (a 160x32 input, tens of
timesteps of BiLSTM — this isn't the regime where ESP-DL's optimization
advantage would matter much). Re-evaluate only if TFLite Micro's op
resolver turns out to be missing something this model needs (see §6's
first bring-up step).

## 4. Pin plan (camera bus)

Current free-pin inventory from `README.md`'s pin plan (GPIO0-23 already
assigned; **not verified beyond that number** — the real P4-Pico may
expose more GPIOs than have been listed here so far):

- Used: 0, 1, 2, 3, 4, 5, 6, 7, 8, 14, 15, 17, 18, 19, 20, 21, 22, 23
- Free within 0-23: **9, 10, 11, 12, 13, 16**

An OV2640 in 8-bit DVP mode needs roughly:

| Signal | Count | Notes |
|---|---|---|
| D0-D7 (8-bit data bus) | 8 | The big constraint — needs 8 free GPIOs together |
| PCLK, VSYNC, HREF | 3 | Timing signals |
| XCLK | 1 | Camera's master clock input (often driven by the ESP32's LEDC/clock output) |
| SIOD, SIOC (SCCB/I2C-like control) | 2 | Can potentially share the existing RC522 SPI bus's I2C-adjacent pins if none are free, or a new soft-I2C pair |
| PWDN, RESET (optional) | 0-2 | Some modules tie these to always-on/always-reset in hardware instead of using GPIOs |

**This does not fit in the current free-pin set (9-13, 16 = 6 pins).** A
full DVP camera needs 12-14 GPIOs; this board has 6 free within the
already-documented range. Two ways forward:

1. **Confirm the real P4-Pico's GPIO count** — if it exposes GPIOs beyond
   23 (likely, since ESP32-P4 has far more than 24 total), the camera bus
   goes on genuinely free pins in that higher range, and this stops being
   a conflict. This is the first thing to check against the actual board,
   not assumed.
2. If pins are still tight after that, an ESP32-P4 variant with a native
   MIPI-CSI interface (if the board has one broken out) uses far fewer
   GPIOs than 8-bit DVP — worth checking before committing to OV2640/DVP.

**Action item, not yet done: get the real total GPIO count and camera
interface options for the exact P4-Pico board revision in use, before
finalizing pin assignments.**

## 5. Memory budget

Also unconfirmed (see §1) — PSRAM size directly determines whether a
camera frame buffer and the TFLite Micro tensor arena can coexist with
the rest of this firmware's RAM usage (Wi-Fi/BT stacks via the C6 link,
display framebuffer, RFID/IR buffers, error history ring buffer, etc).

Rough sizing, to be confirmed once the model is actually measured on
target hardware (`ocr/README.md`'s own acceptance list already says "not
guessed"):

- OV2640 frame buffer: a single QVGA (320x240) YUV422 frame is ~150KB;
  JPEG mode would be far smaller but adds a decode step before cropping
- OCR model + TFLite Micro tensor arena: the model itself
  (`turkish_line_ocr_int8.tflite`) should be checked for its actual file
  size and `evaluate_tflite.py`'s arena usage as a starting estimate —
  not yet measured against a real allocator budget here
- If PSRAM is small or absent on this board revision, the frame buffer
  alone may need external PSRAM (most ESP32-P4 boards have some, but the
  amount varies by module/vendor)

**Action item: run `ocr/scripts/evaluate_tflite.py` and note the
model's actual file size and any arena-size hints it prints, as a
starting reference — this is a proxy for the real number, not a
replacement for measuring on the target board with `esp-tflite-micro`'s
own arena-sizing tools.**

## 6. Suggested build order

Written as a sequence specifically to surface hardware unknowns as early
as possible, before investing in the harder integration work:

1. **Confirm the board**: exact ESP32-P4-Pico module/revision, installed
   PSRAM, and full GPIO count/broken-out pins (not just what's used so
   far). This blocks §4 and §5 and should happen before any of the rest.
2. **Camera bring-up only** (no OCR model yet): get an OV2640 driver
   capturing raw frames to the display (proves DVP timing, pin
   assignment, and XCLK generation work) — this is a good "hardware
   bring-up" milestone on its own, independent of TinyML.
3. **TFLite Micro component bring-up** (no camera yet): add
   `esp-tflite-micro` to the build, load
   `turkish_line_ocr_int8.tflite`, and run it against a static 160x32
   test image baked into flash (not a live camera frame) — proves the
   runtime links, the op resolver covers this model's ops (CNN +
   BiLSTM + CTC-adjacent ops), and gives a real per-inference latency
   number against `ocr/README.md`'s "≤ 1 second per cropped line" target.
4. **Classical line detection**: thresholding + morphology + connected
   components to find and crop a text line region from a captured frame
   — `ocr/DECISION_NOTES.md` §4 already decided this is non-neural by
   design, so this is ordinary image-processing code, not another model.
5. **Wire it together**: camera frame → line-crop → resize/normalize to
   160x32 grayscale → TFLite Micro inference → greedy CTC decode (same
   algorithm as `ocr/scripts/evaluate.py`'s `greedy_ctc_decode()`, ported
   to C) → display result.

Steps 2 and 3 can happen in parallel (different people/sessions, no
shared code) since neither depends on the other finishing first.

## 7. Voice command classifier's runtime (brief)

Once §3's runtime choice (TFLite Micro) is validated for OCR, the same
component very likely covers the voice command classifier too — it's a
much smaller model (12-way classification over a 1-second 16kHz clip,
no CNN+BiLSTM+CTC decode step, no camera/frame-buffer dependency at
all). No separate runtime decision expected here; revisit only if the
command classifier's own exported `.tflite` (once trained — see
`KNOWN_ISSUES.md` Round 17) turns out to need an op TFLite Micro's
resolver doesn't cover.

Unlike OCR, the voice path needs a microphone (I2S, e.g. INMP441) and
its own free-pin budget — smaller ask than the camera's (I2S typically
needs 3 pins: BCLK/WS/DATA), but still needs a spot in whatever GPIO
count §6 step 1 confirms.
