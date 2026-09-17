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
2. Pull a Turkish-capable model, e.g. `ollama pull qwen2.5:7b` (7B is a
   reasonable size/quality tradeoff for a CPU-only PC; a GPU lets you go
   bigger). Qwen2.5 speaks Turkish well out of the box, no fine-tuning
   needed.
3. By default Ollama only listens on `localhost`, which the C6 can't
   reach. Make it listen on the LAN instead:
   - Windows: set the environment variable `OLLAMA_HOST=0.0.0.0:11434`
     (System Properties → Environment Variables) and restart Ollama.
   - Linux/macOS: `OLLAMA_HOST=0.0.0.0:11434 ollama serve` (or set it in
     Ollama's systemd unit / launch config).
4. Find that PC's LAN IP (`ipconfig` / `ifconfig`) and set `OLLAMA_HOST`
   in `main/wifi_commands.c` (near the top) to match. `OLLAMA_MODEL` there
   must match whatever you pulled in step 2.
5. Rebuild and reflash the C6 firmware after changing those constants.

**Known limits:**

- The PC's IP is hardcoded at build time — if your PC's LAN IP changes
  (e.g. no DHCP reservation), `ASK` will fail until the firmware is
  reflashed with the new address. A static DHCP lease on the PC avoids
  this.
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
