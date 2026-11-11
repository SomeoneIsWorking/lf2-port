#!/usr/bin/env python3
"""Two LF2s, each in its own network namespace joined by one cable, meet by a pasted code.

The host shows an invite, the friend joins it, both authenticate the path, check that the other
runs the same lf2.exe, and trade one input frame. The namespaces belong to a user namespace of
this user's, so nothing needs root.
"""

from __future__ import annotations

import os
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from runtime_log import payload_line


HOST_ADDRESS = "10.88.0.1/24"
FRIEND_ADDRESS = "10.88.0.2/24"
INVITE = re.compile(r"^invite code: (\S+)$")
DEADLINE_S = 180


def in_user_namespace() -> bool:
    return Path("/proc/self/uid_map").read_text().split()[:3] != ["0", "0", "4294967295"]


class Machine:
    """A network namespace held open by a `sleep`, entered through its /proc entry."""

    def __init__(self) -> None:
        self.holder = subprocess.Popen(["unshare", "--net", "sleep", "infinity"])
        own = os.readlink("/proc/self/ns/net")
        for _ in range(200):
            if os.readlink(f"/proc/{self.holder.pid}/ns/net") != own:
                break
            time.sleep(0.01)
        else:
            raise RuntimeError("the namespace never appeared")
        self.prefix = ["nsenter", f"--net=/proc/{self.holder.pid}/ns/net", "--"]
        self.ip("link set lo up")

    def ip(self, line: str) -> None:
        subprocess.run([*self.prefix, "ip", *line.split()], check=True)

    def start(self, command: list[str], cwd: Path, env: dict[str, str], log: Path) -> subprocess.Popen[bytes]:
        return subprocess.Popen([*self.prefix, *command], cwd=cwd, env=env, stdout=log.open("wb"),
                                stderr=subprocess.STDOUT)

    def close(self) -> None:
        self.holder.kill()
        self.holder.wait()


def wire(host: Machine, friend: Machine) -> None:
    host.ip(f"link add lab0 type veth peer name lab1 netns {friend.holder.pid}")
    host.ip(f"addr add {HOST_ADDRESS} dev lab0")
    friend.ip(f"addr add {FRIEND_ADDRESS} dev lab1")
    host.ip("link set lab0 up")
    friend.ip("link set lab1 up")


def online_lines(log: Path) -> list[str]:
    if not log.is_file():
        return []
    return [payload_line(line) for line in log.read_text(errors="replace").splitlines()
            if "] [online] " in line]


def first_invite(log: Path) -> str | None:
    for line in online_lines(log):
        match = INVITE.match(line)
        if match:
            return match.group(1)
    return None


def exchanged(log: Path) -> bool:
    lines = online_lines(log)
    return any(line.startswith("frame ") and " received: " in line for line in lines) and \
        any(line.startswith("frame ") and " sent: " in line for line in lines)


def stop(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        process.kill()
    process.wait()


def main() -> int:
    if not in_user_namespace():
        os.execvp("unshare", ["unshare", "--user", "--map-current-user", "--keep-caps", "--",
                              sys.executable, *sys.argv])

    root = Path(__file__).resolve().parents[2]
    build = Path(os.environ.get("BUILD") or root / "build" / "clang").resolve()
    game = Path(os.environ.get("GAME") or root / "game").resolve()
    out = root / "scratch" / "online-meeting"
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(SDL_AUDIODRIVER="dummy", LF2_WINDOW="hidden", LF2_UNPACED="1", LF2_CONFIG="",
               LF2_QUIT_AFTER="100000")
    command = [str(build / "lf2"), "lf2.exe"]

    host, friend = Machine(), Machine()
    processes: list[subprocess.Popen[bytes]] = []
    try:
        wire(host, friend)
        host_log, friend_log = out / "host.log", out / "friend.log"
        # Each machine its own user data, so each has its own ZeroTier identity.
        homes = {name: out / f"{name}-home" for name in ("host", "friend")}
        for home in homes.values():
            home.mkdir(exist_ok=True)
        processes.append(host.start(command, game, {**env, "LF2_ONLINE": "host",
                                                    "XDG_CONFIG_HOME": str(homes["host"])}, host_log))
        deadline = time.monotonic() + DEADLINE_S
        invite = None
        while invite is None and time.monotonic() < deadline and processes[0].poll() is None:
            invite = first_invite(host_log)
            time.sleep(0.2)
        if invite is None:
            print(f"  FAIL  the host showed no invite code; see {host_log}")
            return 1
        print("  ok    the host showed an invite code")
        processes.append(friend.start(command, game, {**env, "LF2_ONLINE": invite,
                                                      "XDG_CONFIG_HOME": str(homes["friend"])}, friend_log))
        while time.monotonic() < deadline and not (exchanged(host_log) and exchanged(friend_log)):
            if any(process.poll() is not None for process in processes):
                break
            time.sleep(0.2)
        passed = True
        for name, log in (("host", host_log), ("friend", friend_log)):
            lines = online_lines(log)
            same = "the peer runs the same lf2.exe" in lines
            traded = exchanged(log)
            print(f"  {'ok  ' if same else 'FAIL'}  {name}: the peer runs the same lf2.exe")
            print(f"  {'ok  ' if traded else 'FAIL'}  {name}: sent and received one input frame")
            for line in lines:
                if line.startswith(("connected", "frame ")):
                    print(f"          {line}")
            passed = passed and same and traded
        return 0 if passed else 1
    finally:
        for process in processes:
            stop(process)
        host.close()
        friend.close()


if __name__ == "__main__":
    sys.exit(main())
