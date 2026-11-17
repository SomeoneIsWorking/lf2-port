#!/usr/bin/env python3
"""A lab lobby server's front that changes the open games until a log says enough has happened.

online_meeting_test.py points the friend at this instead of lucent_lobby_server. Until the
friend's page has drawn `frames` frame dumps, `--hold` lists no open games and `--decoy` lists a
copy of the first one, under another id, ahead of it. Every other request, and the list after
that, goes to the real server unchanged.
"""

from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import urllib.error
import urllib.request


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--upstream", required=True, help="the lobby server's base URL")
    parser.add_argument("--log", type=Path, required=True, help="the friend's log")
    parser.add_argument("--frames", type=int, required=True, help="frame dumps before the real list shows")
    change = parser.add_mutually_exclusive_group(required=True)
    change.add_argument("--hold", action="store_true", help="list no open games until then")
    change.add_argument("--decoy", action="store_true", help="list a decoy ahead of the first game until then")
    options = parser.parse_args()

    def released() -> bool:
        if not options.log.is_file():
            return False
        return options.log.read_text(errors="replace").count("frame dump: wrote") >= options.frames

    def with_decoy(body: bytes) -> bytes:
        listed = json.loads(body)
        if listed["lobbies"]:
            first = listed["lobbies"][0]
            # Its contact is the real one reversed: an ask to it reaches nobody.
            decoy = dict(first, id="decoy", title="Decoy", host="d" * 64, contact=first["contact"][::-1])
            listed["lobbies"].insert(0, decoy)
        return json.dumps(listed).encode()

    class Handler(BaseHTTPRequestHandler):
        def forward(self) -> None:
            listing = self.command == "GET" and self.path.startswith("/v1/lobbies") and not released()
            if listing and options.hold:
                self.reply(200, b'{"lobbies":[]}')
                return
            length = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(length) if length else None
            request = urllib.request.Request(options.upstream + self.path, data=body, method=self.command)
            if self.headers.get("Content-Type"):
                request.add_header("Content-Type", self.headers["Content-Type"])
            try:
                with urllib.request.urlopen(request, timeout=10) as answer:
                    body = answer.read()
                    self.reply(answer.status, with_decoy(body) if listing else body)
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
