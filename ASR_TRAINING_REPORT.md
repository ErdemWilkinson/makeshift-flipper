# Turkish ASR Stage 2 training report

**Date:** 2026-09-20<br>
**Status:** research baseline completed; **not approved for deployment**

## Scope

This experiment validates a local Turkish automatic-speech-recognition
(ASR) pipeline. It is a character-level CTC baseline trained on Mozilla
Common Voice Scripted Speech. It is not the small, fixed-command,
whisper-aware classifier intended for the ESP32-P4 Pico firmware.

## Data

The source corpus was Mozilla Common Voice Scripted Speech Turkish 27.0.
Metadata-only filtering removed malformed transcripts, implausible durations
and speaking rates, duplicate prompts, and excessive samples from one
speaker.

| Split | Accepted clips | Audio hours | Speakers |
|---|---:|---:|---:|
| Train | 9,663 | 10.45 | 28 |
| Development | 10,258 | 11.03 | 162 |
| Test | 10,512 | 13.22 | 1,477 |

The Stage 2 run used 9,000 train clips and 2,000 development clips. A small
Turkish Common Voice Spontaneous Speech set (50 transcribed clips from 13
speakers) was reserved as an external natural-speech evaluation set.

## Training configuration

| Setting | Value |
|---|---|
| Model | 1D convolution + bidirectional GRU + CTC character head |
| Input | 16 kHz mono audio; 40-bin mel spectrogram |
| Output alphabet | Turkish Latin characters plus space and apostrophe |
| Parameters | 63,955 |
| Batch size | 8 |
| Maximum epochs | 30 |
| Early stopping | 3 epochs without development-loss improvement |
| Completed epochs | 22 |
| Best development loss | 76.40 at epoch 19 |

Training stopped early after no new best development loss during the final
three epochs. This prevented unnecessary continued fitting.

## Independent evaluation

Greedy CTC decoding was evaluated separately from training. WER can exceed
100% because insertions count as errors; it must not be interpreted as a
negative accuracy percentage.

| Evaluation set | Samples | Character accuracy | CER | WER |
|---|---:|---:|---:|---:|
| Held-out scripted speech | 500 | 40.6% | 59.4% | 101.2% |
| External spontaneous speech | 50 | 35.4% | 64.6% | 100.5% |

## Result and decision

Stage 2 improves on the earlier small Stage 0 run, which emitted empty
transcripts on the same evaluation design. It now produces Turkish-like
character sequences, but word recognition remains unreliable. The model is
therefore **not suitable for general transcription or ESP32-P4 deployment**.

The deployment path remains a separate int8, fixed-label command classifier
trained with recordings of the actual commands in normal voice, low voice,
and whisper styles. The current acceptance gate for that model is at least
85% whispered-command accuracy, at least 92% normal-command accuracy, and
at most 2% false accepts for unknown speech/silence.

## Local reproducibility

Training data, personal voice recordings, model binaries, and detailed
evaluation JSON are intentionally kept out of Git. They can be regenerated
locally from the ignored `voice/` training workspace using the Common Voice
archives and the documented training scripts.
