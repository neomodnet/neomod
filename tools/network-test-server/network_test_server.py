#!/usr/bin/env python3
# minimal CORS-enabled HTTP server for the NetworkTest app (src/App/Tests/NetworkTest.cpp).
#
# browsers enforce CORS, so the wasm/emrun build cannot fetch arbitrary third-party hosts
# (example.com & co. don't send Access-Control-Allow-Origin, so every cross-origin response is
# blocked and shows up as status 0). this serves a few known routes with ACAO: * so the exact
# same suite runs against the exact same responses from both the native (curl) and browser
# (fetch) builds, fully locally and offline.
#
# usage: python3 network_test_server.py [port]   (default 8423, matching NetworkTest's default base_url)
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BODY_OK = b"neomod NetworkTest OK\n"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    # cancelling a request tears the connection down mid-flight; swallow the resulting resets
    # (they surface on the keep-alive read loop and the final flush) so they don't print tracebacks.
    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError):
            pass

    def finish(self):
        try:
            super().finish()
        except (ConnectionResetError, BrokenPipeError):
            pass

    def _send(self, code, body=b""):
        self.send_response(code)
        # ACAO on every response (including 404) so the browser can read the status cross-origin
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Private-Network", "true")  # Chrome PNA preflight insurance
        self.send_header("Cache-Control", "no-store")  # force a real network hit every time
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            try:
                self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError):
                pass  # client cancelled mid-response; expected for the /slow cancellation cases

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/ok":
            self._send(200, BODY_OK)
        elif path == "/slow":
            time.sleep(1.0)  # stay in flight long enough to be cancelled mid-transfer, even at 60Hz
            self._send(200, b"slow ok\n")
        else:
            self._send(404, b"not found\n")

    def do_OPTIONS(self):
        self._send(204)

    def log_message(self, *args):
        pass  # quiet


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8423
    srv = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"NetworkTest server on http://127.0.0.1:{port} (routes: /ok, /slow, * -> 404)", flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
