"""
CLAOS LAN Relay — Bridges plain HTTP from CLAOS to Anthropic's HTTPS API.

Run this on any machine on the same network as the CLAOS VM.
CLAOS only speaks HTTP (no TLS), so this relay handles the HTTPS part.

Usage:
    export ANTHROPIC_API_KEY=sk-ant-...
    python relay.py

The relay listens on 0.0.0.0:8080 and forwards POST /v1/messages
to https://api.anthropic.com/v1/messages with the proper auth headers.
"""

import os
import sys
import json
from http.server import HTTPServer, BaseHTTPRequestHandler
import urllib.request
import urllib.error
import ssl

API_URL = "https://api.anthropic.com/v1/messages"
API_VERSION = "2023-06-01"
LISTEN_PORT = 8080


class RelayHandler(BaseHTTPRequestHandler):
    """Forward requests from CLAOS to the Anthropic API."""

    def do_POST(self):
        api_key = os.environ.get("ANTHROPIC_API_KEY", "")
        if not api_key:
            self._error(500, "ANTHROPIC_API_KEY not set on relay host")
            return

        # Read the request body from CLAOS
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)

        print(f"[CLAOS -> Relay] {len(body)} bytes from {self.client_address[0]}")

        # Forward to Anthropic API
        req = urllib.request.Request(API_URL, data=body, method="POST")
        req.add_header("Content-Type", "application/json")
        req.add_header("x-api-key", api_key)
        req.add_header("anthropic-version", API_VERSION)

        try:
            ctx = ssl.create_default_context()
            with urllib.request.urlopen(req, context=ctx) as resp:
                resp_body = resp.read()
                print(f"[Relay -> CLAOS] {len(resp_body)} bytes, status {resp.status}")
                self.send_response(resp.status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp_body)))
                self.end_headers()
                self.wfile.write(resp_body)
        except urllib.error.HTTPError as e:
            error_body = e.read()
            print(f"[Relay ERROR] Anthropic API returned {e.code}: {error_body[:200]}")
            self.send_response(e.code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(error_body)))
            self.end_headers()
            self.wfile.write(error_body)
        except Exception as e:
            self._error(502, f"Relay error: {e}")

    def _error(self, code, msg):
        body = json.dumps({"error": msg}).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        """Prefix log messages with [Relay]."""
        print(f"[Relay] {args[0]}")


def main():
    if not os.environ.get("ANTHROPIC_API_KEY"):
        print("WARNING: ANTHROPIC_API_KEY not set. Set it before CLAOS sends requests.")
        print("  export ANTHROPIC_API_KEY=sk-ant-...")
        print()

    server = HTTPServer(("0.0.0.0", LISTEN_PORT), RelayHandler)
    print(f"CLAOS Relay listening on http://0.0.0.0:{LISTEN_PORT}")
    print(f"Forwarding to {API_URL}")
    print("Waiting for CLAOS to phone home...\n")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nRelay shutting down.")
        server.server_close()


if __name__ == "__main__":
    main()
