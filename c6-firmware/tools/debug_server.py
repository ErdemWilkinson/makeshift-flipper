#!/usr/bin/env python3
"""Error log server -- optional PC-side helper for the "Errors" -> Send
feature on the Makeshift Flipper device.

The device keeps its own error history entirely on-device and works fully
offline. This script is only used if the user chooses to upload that
history for safekeeping/inspection: it accepts a POST of the device's
recorded errors and appends them to error_log.jsonl next to this script.
No AI/Ollama involved -- this is a plain logger, nothing more.

Run it with:

    python debug_server.py

It listens on 0.0.0.0:8765 by default (see PORT below) so the C6, which is
on the same LAN, can reach it. No authentication -- fine for a home LAN,
not for anything more exposed (see KNOWN_ISSUES.md).
"""

import json
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

PORT = 8765
LOG_PATH = Path(__file__).parent / "error_log.jsonl"


def append_entries(entries: list) -> int:
    received_at = datetime.now(timezone.utc).isoformat()
    count = 0
    with LOG_PATH.open("a", encoding="utf-8") as f:
        for entry in entries:
            module = str(entry.get("module", "unknown"))[:64]
            code = str(entry.get("code", "unknown"))[:64]
            ago_s = entry.get("ago_s", None)
            record = {
                "received_at": received_at,
                "module": module,
                "code": code,
                "ago_s": ago_s,
            }
            f.write(json.dumps(record, ensure_ascii=False) + "\n")
            count += 1
    return count


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[debug_server] {self.address_string()} - {fmt % args}")

    def do_POST(self):
        if self.path != "/logs":
            self.send_response(404)
            self.end_headers()
            return

        length = int(self.headers.get("Content-Length", 0))
        # A full DIAG_HISTORY_CAPACITY (24) batch of small JSON objects is
        # a few KB at most -- refuse anything wildly larger outright
        # rather than reading it into memory.
        if length <= 0 or length > 65536:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b'{"error":"invalid content length"}')
            return

        try:
            data = json.loads(self.rfile.read(length).decode("utf-8"))
            entries = data.get("entries", [])
            if not isinstance(entries, list):
                raise ValueError("entries must be a list")
        except (json.JSONDecodeError, UnicodeDecodeError, ValueError):
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b'{"error":"invalid json"}')
            return

        count = append_entries(entries)

        response = json.dumps({"status": "ok", "count": count}).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)


def main():
    server = HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"Error log server listening on 0.0.0.0:{PORT}, logging to {LOG_PATH}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
