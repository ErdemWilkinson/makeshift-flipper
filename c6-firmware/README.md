# C6 companion firmware

A separate ESP-IDF project running on the ESP32-C6. Interprets
line-based commands coming over UART from the P4 and carries out the
Wi-Fi operations. Has no dependency on the P4's own firmware — it's
built and flashed to the C6 entirely on its own.

## Protocol (P4 ↔ C6, UART, 115200 8N1, line-terminated with `\n`)

```
P4 -> C6: SCAN
C6 -> P4: NET:<ssid>,<rssi>   (0 or more lines)
C6 -> P4: SCANDONE

P4 -> C6: CONNECT:<ssid>,<password>
C6 -> P4: OK   or   FAIL

P4 -> C6: SEND:<ip>:<port>:<data>
C6 -> P4: SENT   or   FAIL

P4 -> C6: ASK:<question>
C6 -> P4: ANSWER:<chunk>   (1 or more lines, answer split to fit the line limit)
C6 -> P4: ANSWERDONE   or   ASKFAIL
```

The P4-side counterpart to this protocol lives in `../main/net/c6_link.c`.

## AI bridge ("Ask AI" menu item) — requires a PC running Ollama

`ASK:<question>` is handled by `wifi_commands_ask()` in `main/wifi_commands.c`,
which relays the question to [Ollama](https://ollama.com) running on a PC on
the same Wi-Fi network the C6 is connected to (STA mode), and relays the
answer back. This is plain, unauthenticated HTTP on your local network —
fine for a hobby LAN, not something to expose beyond it.

**One-time PC-side setup:**

1. Install Ollama on a PC that will stay on and reachable on the same
   network as the C6.
2. Pull a Turkish-capable model, e.g. `ollama pull qwen3:8b` (a noticeable
   quality step up from qwen2.5 at a similar size; 8B is a reasonable
   size/quality tradeoff, a GPU lets you go bigger). No fine-tuning needed.
   **If you're on Qwen3, also disable "thinking mode"** — it's on by
   default in Ollama and adds tens of seconds of latency before the answer
   even starts (measured ~54s vs ~4s for the same prompt with it off),
   which risks tripping this project's own `OLLAMA_TIMEOUT_MS`/
   `ASK_TIMEOUT_MS`. This firmware already sends `"think": false` in the
   request body (`wifi_commands_ask()` in `main/wifi_commands.c`) to
   handle this — it's a no-op for models that don't support the field.
3. By default Ollama only listens on `localhost`, which the C6 can't
   reach. Make it listen on the LAN instead:
   - Windows: set the environment variable `OLLAMA_HOST=0.0.0.0:11434`
     (System Properties → Environment Variables) and restart Ollama.
   - Linux/macOS: `OLLAMA_HOST=0.0.0.0:11434 ollama serve` (or set it in
     Ollama's systemd unit / launch config).
4. Find that PC's LAN IP (`ipconfig` / `ifconfig`) and run
   `idf.py menuconfig` in `c6-firmware/`, then set it under
   **"Makeshift Flipper C6 -- Ask AI (Ollama bridge)"** →
   `MAKESHIFT_OLLAMA_HOST`. Set `MAKESHIFT_OLLAMA_MODEL` there to match
   whatever you pulled in step 2 (these used to be hardcoded `#define`s
   in `main/wifi_commands.c`; they're Kconfig options now, so this step
   doesn't require editing source).
5. Rebuild and reflash the C6 firmware after changing the config.

**Known limits:**

- The PC's IP is set at build time via Kconfig — if your PC's LAN IP
  changes (e.g. no DHCP reservation), `ASK` will fail (or silently go to
  whatever device now holds that address) until the firmware is
  reflashed with the new one. A static DHCP lease on the PC avoids this.
- No auth on the Ollama HTTP endpoint — anything on the same LAN segment
  could also reach it. Acceptable for a home network, not for anything
  more exposed.
- One request at a time, synchronous — same blocking pattern as
  `SETUP`/`CONNECT`. A slow PC or a large model can make `ASK` take up to
  `OLLAMA_TIMEOUT_MS` (60s) before giving up.

## Pin plan

The P4 side of the breadboard report is fixed: P4 GPIO18(TX)→C6 RX, P4
GPIO19(RX)→C6 TX. **Which GPIOs you use on the C6 side depends on your
own board** — `UART_TX_GPIO`/`UART_RX_GPIO` at the top of
`main/uart_link.c` currently assume GPIO6/7; change them to match your
actual wiring.

## Building

```
idf.py set-target esp32c6
idf.py build
idf.py -p COMx flash monitor
```

## Limits / not yet implemented

- `SEND` opens and closes a new TCP connection every time (no
  persistent socket)
- Wi-Fi credentials (ssid/password) aren't stored persistently — every
  `CONNECT` reconnects using whatever the P4 sends that time
- Error reporting is just `FAIL` — the reason (wrong password, network
  not found, connection refused) isn't distinguished. Could be extended
  to something like `FAIL:<reason>` if needed
- No UDP support, TCP only
