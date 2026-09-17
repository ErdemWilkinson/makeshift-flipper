#!/usr/bin/env python3
"""Debug AI helper server -- runs on the PC alongside Ollama.

The C6 firmware can't write files or do multi-step reasoning on its own, so
this tiny HTTP server sits between it and Ollama for the "Debug AI" menu
feature: it takes an error report from the Makeshift Flipper device, asks
Ollama to diagnose it (user mistake vs. device/firmware/hardware issue),
appends the result to debug_log.md next to this script, and returns a
verdict + short explanation for the device's OLED to show.

This is a separate process from Ollama itself -- Ollama only generates
text, it has no notion of "append this to a log file" or the two-step
"diagnose, then classify" flow this feature wants. Run it with:

    python debug_server.py

It listens on 0.0.0.0:8765 by default (see PORT below) so the C6, which is
on the same LAN, can reach it. No authentication -- same trust model as the
Ollama bridge itself (see c6-firmware/README.md's "Known limits"): fine for
a home LAN, not for anything more exposed.
"""

import json
import urllib.request
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

PORT = 8765
OLLAMA_URL = "http://127.0.0.1:11434/api/generate"
OLLAMA_MODEL = "qwen3:8b"  # keep in sync with c6-firmware's Kconfig MAKESHIFT_OLLAMA_MODEL
LOG_PATH = Path(__file__).parent / "debug_log.md"

SYSTEM_PROMPT = (
    "Sen bir gomulu sistem (ESP32 tabanli 'Makeshift Flipper' cihazi) icin "
    "hata teshis asistanisin. Sana bir modul adi ve hata kodu verilecek. "
    "Gorevin: bu sorunun kullanicidan mi (yanlis kullanim, kablolama, "
    "eksik adim) yoksa sistemden mi (donanim arizasi, firmware hatasi, "
    "kablo/pin sorunu) kaynaklandigini kisaca degerlendirmek. "
    "Cevabini SADECE su JSON formatinda ver, baska hicbir metin ekleme: "
    '{"verdict": "user"|"system"|"unknown", "explanation": "<kisa turkce aciklama, en fazla 2 cumle>"}'
)


def ask_ollama(module: str, code: str, note: str) -> dict:
    prompt = f"Modul: {module}\nHata kodu: {code}\n"
    if note:
        prompt += f"Kullanicinin notu: {note}\n"

    body = json.dumps({
        "model": OLLAMA_MODEL,
        "prompt": prompt,
        "system": SYSTEM_PROMPT,
        "stream": False,
        "think": False,  # see c6-firmware/README.md -- Qwen3's thinking mode adds ~50s of latency
        "format": "json",
    }).encode("utf-8")

    req = urllib.request.Request(
        OLLAMA_URL, data=body, headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        payload = json.loads(resp.read().decode("utf-8"))

    raw = payload.get("response", "")
    try:
        parsed = json.loads(raw)
        verdict = parsed.get("verdict", "unknown")
        explanation = parsed.get("explanation", raw.strip())
    except (json.JSONDecodeError, AttributeError):
        # Model didn't follow the JSON format instruction -- fall back to
        # a best-effort guess from the raw text rather than failing outright.
        verdict = "unknown"
        explanation = raw.strip() or "AI'dan gecerli bir cevap alinamadi."

    if verdict not in ("user", "system", "unknown"):
        verdict = "unknown"
    return {"verdict": verdict, "explanation": explanation}


def append_log(module: str, code: str, note: str, result: dict) -> None:
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    entry = (
        f"## {timestamp} -- {module}\n\n"
        f"- **Hata kodu:** `{code}`\n"
        + (f"- **Kullanici notu:** {note}\n" if note else "")
        + f"- **Teshis:** {result['verdict']}\n"
        f"- **Aciklama:** {result['explanation']}\n\n"
    )
    is_new = not LOG_PATH.exists()
    with LOG_PATH.open("a", encoding="utf-8") as f:
        if is_new:
            f.write("# Makeshift Flipper -- Debug AI log\n\n")
        f.write(entry)


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[debug_server] {self.address_string()} - {fmt % args}")

    def do_POST(self):
        if self.path != "/debug":
            self.send_response(404)
            self.end_headers()
            return

        length = int(self.headers.get("Content-Length", 0))
        # Refuse absurdly large bodies outright rather than reading them
        # into memory -- this endpoint only ever expects a few short fields.
        if length <= 0 or length > 8192:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b'{"error":"invalid content length"}')
            return

        try:
            data = json.loads(self.rfile.read(length).decode("utf-8"))
            module = str(data.get("module", "unknown"))[:64]
            code = str(data.get("code", "unknown"))[:64]
            note = str(data.get("note", ""))[:512]
        except (json.JSONDecodeError, UnicodeDecodeError):
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b'{"error":"invalid json"}')
            return

        try:
            result = ask_ollama(module, code, note)
        except Exception as exc:  # noqa: BLE001 -- want to report *any* failure to the device
            print(f"[debug_server] Ollama request failed: {exc}")
            self.send_response(502)
            self.end_headers()
            self.wfile.write(b'{"error":"ollama request failed"}')
            return

        append_log(module, code, note, result)

        response = json.dumps(result).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)


def main():
    server = HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Debug AI server listening on 0.0.0.0:{PORT}, logging to {LOG_PATH}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
