#!/usr/bin/env python3
"""A lab lobby server's front that lists no open games until a log says enough has happened.

online_meeting_test.py --held-listing points the friend at this instead of lucent_lobby_server,
so the host's game reaches the friend's list only after the friend's page has drawn `frames`
frame dumps; every other request, and the list after that, goes to the real server unchanged.
"""

from __future__ import annotations

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import urllib.error
import urllib.request


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--upstream", required=True, help="the lobby server's base URL")
    parser.add_argument("--log", type=Path, required=True, help="the friend's log")
    parser.add_argument("--frames", type=int, required=True, help="frame dumps before the list shows")
    options = parser.parse_args()

    def released() -> bool:
        if not options.log.is_file():
            return False
        return options.log.read_text(errors="replace").count("frame dump: wrote") >= options.frames

    class Handler(BaseHTTPRequestHandler):
        def forward(self) -> None:
            if self.command == "GET" and self.path.startswith("/v1/lobbies") and not released():
                self.reply(200, b'{"lobbies":[]}')
                return
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            request = urllib.request.Request(options.upstream + self.path, data=body, method=self.command)
            if self.headers.get("Content-Type"):
                request.add_header("Content-Type", self.headers["Content-Type"])
            try:
                with urllib.request.urlopen(request, timeout=10) as answer:
                    self.reply(answer.status, answer.read())
            except urllib.error.HTTPError as answer:
                self.reply(answer.code, answer.read())

        def reply(self, status: int, body: bytes) -> None:
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        do_GET = forward
        do_PUT = forward
        do_POST = forward

        def log_message(self, *_: object) -> None:
            pass

    ThreadingHTTPServer(("0.0.0.0", options.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
