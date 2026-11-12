#!/usr/bin/env python3
"""Two LF2s meet through the Online menu, directly or over ZeroTier alone.

Both players work the port menu with a pad. The host hosts a game and lists it; the friend finds
it in the open games, served by a lobby server on the lab, and joins it. The friend's game takes
the host's state; the host starts a VS match and the friend's pad claims player 2, then the
friend walks player 2 while the host walks player 1. Each logs player 2's position and a final
state hash every sixty frames: player 2 must move on both screens and the hashes agree on every
frame both reached.
Direct: each in its own network namespace joined by one cable that delays and drops packets, so
late inputs must rewind and resimulate frames. `--zerotier`: the direct path off, both on a lab with only Lucent's lab ZeroTier root
to reach each other through. The namespaces belong to a user namespace of this user's, so nothing
needs root.
"""

from __future__ import annotations

import argparse
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
# libzt binds no loopback, so the ZeroTier lab sits on a dummy interface.
LAB_ADDRESS = "10.77.0.1"
ROOT_PORT = 39993
INVITE = re.compile(r"^invite code: (\S+)$")
FINAL = re.compile(r"^frame (\d+) final hash ([0-9a-f]{16}), (\d+) frames resimulated$")
# Final hashes both peers must log before the run counts as played: past the friend's last press.
REPORTS = 32
DEADLINE_S = 480
# The cable's impairment, both ways: inputs arrive frames late, so a changed one rewinds.
NETEM = "delay 60ms 20ms loss 10%"


def pad_script(buttons: tuple[str, ...], anchor: str, first: int, every: int, count: int) -> str:
    return ",".join(f"{buttons[i % len(buttons)]}@{anchor}+{first + i * every}" for i in range(count))


# The port menu: open it, up to the ONLINE tab, open the page, then down its rows.
MENU = ["start@modemenu+60", "up@modemenu+90", "south@modemenu+120"]
# Host: HOST, then down past Your code and Join by code to the listing switch, turned on.
HOST_MENU = MENU + ["down@modemenu+150", "south@modemenu+180", "down@modemenu+210", "down@modemenu+240",
                    "down@modemenu+270", "south@modemenu+300"]
# Friend: down past Host, Your code, Join by code, the listing switch and Refresh to the first open game.
FRIEND_MENU = MENU + [f"down@modemenu+{300 + 30 * i}" for i in range(6)] + ["south@modemenu+500"]
# Once both play, the host starts a VS match as player 1: VS mode, its fighter and team, then the
# computer players, the stage and the start, as leave_match_test.py does for one player.
HOST_MATCH = ["south@online+60"] + [f"south@charselect+{t}" for t in (58, 118, 178, 238)] + \
    ["up@charselect+298", "up@charselect+358"] + [f"south@charselect+{t}" for t in (418, 618, 838)] + \
    ["up@overlay+99", "up@overlay+159", "south@overlay+219"]
# The friend's pad claims player 2 between the host's presses: join, fighter, team.
FRIEND_MATCH = [f"south@charselect+{t}" for t in (88, 148, 208)]
# In the match: the host walks player 1 up and down, the friend walks player 2 right, then left.
HOST_MOVES = pad_script(("up", "down"), "match", 60, 23, 40)
FRIEND_MOVES = pad_script(("right",), "match", 60, 7, 30) + "," + pad_script(("left",), "match", 300, 7, 15)
PADS = {"host": ",".join(HOST_MENU + HOST_MATCH) + "," + HOST_MOVES,
        "friend": ",".join(FRIEND_MENU + FRIEND_MATCH) + "," + FRIEND_MOVES}
# The friend's page as it presses JOIN, then player 2 running right while both fighters are in view.
FRAME_DUMP = "@modemenu+495,@match+90,@match+110,@match+130"
# Player 2's fighter, as LF2_COOP_TRACK logs it.
TRACK = re.compile(r"^coop track: frame (\d+) entry 1 x=(-?\d+) y=-?\d+ z=(-?\d+)")
LOBBY_PORT = 8077


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
    for machine, device in ((host, "lab0"), (friend, "lab1")):
        subprocess.run([*machine.prefix, "tc", "qdisc", "add", "dev", device, "root", "netem", *NETEM.split()],
                       check=True)


def lab(machine: Machine) -> None:
    machine.ip("link add lab0 type dummy")
    machine.ip(f"addr add {LAB_ADDRESS}/24 dev lab0")
    machine.ip("link set lab0 up")


def start_root(machine: Machine, build: Path, out: Path) -> tuple[subprocess.Popen[bytes], Path]:
    subprocess.run(["cmake", "--build", str(build), "--target", "lucent_zerotier_root"], check=True,
                   stdout=subprocess.DEVNULL)
    roots = out / "roots"
    roots.unlink(missing_ok=True)
    root = machine.start([str(build / "third_party" / "lucent" / "lucent_zerotier_root"), str(out / "root"),
                          f"{LAB_ADDRESS}/{ROOT_PORT}", str(roots)], out, os.environ.copy(), out / "root.log")
    for _ in range(300):
        if roots.is_file() and roots.stat().st_size:
            return root, roots
        if root.poll() is not None:
            break
        time.sleep(0.1)
    raise RuntimeError(f"the lab root wrote no root set; see {out / 'root.log'}")


def start_lobby_server(machine: Machine, build: Path, out: Path) -> subprocess.Popen[bytes]:
    subprocess.run(["cmake", "--build", str(build), "--target", "lucent_lobby_server"], check=True,
                   stdout=subprocess.DEVNULL)
    return machine.start([str(build / "third_party" / "lucent" / "lucent_lobby_server"), "--port", str(LOBBY_PORT)],
                         out, os.environ.copy(), out / "lobby.log")


def tagged_lines(log: Path, tag: str) -> list[str]:
    if not log.is_file():
        return []
    return [payload_line(line) for line in log.read_text(errors="replace").splitlines()
            if f"] [{tag}] " in line or f"] [{tag}:" in line]


def online_lines(log: Path) -> list[str]:
    return tagged_lines(log, "online")


def first_invite(log: Path) -> str | None:
    for line in online_lines(log):
        match = INVITE.match(line)
        if match:
            return match.group(1)
    return None


def final_hashes(log: Path) -> dict[int, tuple[str, int]]:
    hashes = {}
    for line in online_lines(log):
        match = FINAL.match(line)
        if match:
            hashes[int(match.group(1))] = (match.group(2), int(match.group(3)))
    return hashes


def player_two_x(log: Path) -> list[int]:
    xs = []
    for line in tagged_lines(log, "coop-debug"):
        match = TRACK.match(line)
        if match:
            xs.append(int(match.group(2)))
    return xs


def played(log: Path) -> bool:
    return len(final_hashes(log)) >= REPORTS


def stop(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        process.kill()
    process.wait()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--zerotier", action="store_true", help="meet over ZeroTier alone, through a lab root")
    options = parser.parse_args()
    if not in_user_namespace():
        os.execvp("unshare", ["unshare", "--user", "--map-current-user", "--keep-caps", "--",
                              sys.executable, *sys.argv])

    root = Path(__file__).resolve().parents[2]
    build = Path(os.environ.get("BUILD") or root / "build" / "clang").resolve()
    game = Path(os.environ.get("GAME") or root / "game").resolve()
    out = root / "scratch" / ("online-meeting-zerotier" if options.zerotier else "online-meeting")
    out.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(SDL_AUDIODRIVER="dummy", LF2_WINDOW="hidden", LF2_QUIT_AFTER="100000", LF2_COOP_TRACK="1")
    command = [str(build / "lf2"), "lf2.exe"]

    processes: list[subprocess.Popen[bytes]] = []
    helpers: list[subprocess.Popen[bytes]] = []
    if options.zerotier:
        host = friend = Machine()
        machines = [host]
    else:
        host, friend = Machine(), Machine()
        machines = [host, friend]
    expected_path = "connected over ZeroTier" if options.zerotier else "connected directly"
    try:
        if options.zerotier:
            lab(host)
            root_process, roots = start_root(host, build, out)
            helpers.append(root_process)
            env.update(LF2_ONLINE_ZEROTIER_ONLY="1", LF2_ZEROTIER_ROOTS=str(roots))
            lobby_address = LAB_ADDRESS
        else:
            wire(host, friend)
            lobby_address = HOST_ADDRESS.split("/")[0]
        helpers.append(start_lobby_server(host, build, out))
        host_log, friend_log = out / "host.log", out / "friend.log"
        # Each machine its own user data, so each has its own ZeroTier identity.
        homes = {name: out / f"{name}-home" for name in ("host", "friend")}
        for home in homes.values():
            home.mkdir(exist_ok=True)
        settings = {}

        def frames(name: str) -> dict[str, str]:
            """Each player's frames in <out>/<name>-frames, FRAMES=<LF2_FRAME_DUMP spec> or FRAME_DUMP."""
            (out / f"{name}-frames").mkdir(exist_ok=True)
            return {"LF2_FRAME_DUMP": os.environ.get("FRAMES", FRAME_DUMP), "LF2_DUMP_DIR": str(out / f"{name}-frames")}

        for name in ("host", "friend"):
            settings[name] = out / f"{name}.cfg"
            settings[name].write_text(f"lobby_server http://{lobby_address}:{LOBBY_PORT}\nnostr_lobbies off\n")
        processes.append(host.start(command, game, {**env, **frames("host"), "LF2_VIRTUAL_PAD": PADS["host"],
                                                                "LF2_CONFIG": str(settings["host"]),
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
        processes.append(friend.start(command, game, {**env, **frames("friend"), "LF2_VIRTUAL_PAD": PADS["friend"],
                                                                    "LF2_CONFIG": str(settings["friend"]),
                                                                    "XDG_CONFIG_HOME": str(homes["friend"])},
                                      friend_log))
        while time.monotonic() < deadline and not (played(host_log) and played(friend_log)):
            if any(process.poll() is not None for process in processes):
                break
            time.sleep(0.2)
        asked = "asked a listed game for a seat" in tagged_lines(friend_log, "lobby")
        answered = "answered a player who asked for a seat" in tagged_lines(host_log, "lobby")
        joined = asked and answered and "joining the invite" in online_lines(friend_log)
        print(f"  {'ok  ' if joined else 'FAIL'}  the friend asked a listed game for a seat and joined by its answer")
        passed = joined
        for name, log in (("host", host_log), ("friend", friend_log)):
            lines = online_lines(log)
            same = "the peer runs the same lf2.exe" in lines
            path = expected_path in tagged_lines(log, "p2p")
            desync = any(line.startswith("desync") for line in lines)
            print(f"  {'ok  ' if path else 'FAIL'}  {name}: {expected_path}")
            print(f"  {'ok  ' if same else 'FAIL'}  {name}: the peer runs the same lf2.exe")
            print(f"  {'ok  ' if not desync else 'FAIL'}  {name}: no desync")
            for line in lines:
                if line.startswith("connected"):
                    print(f"          {line}")
            passed = passed and same and path and not desync
        for name, log in (("host", host_log), ("friend", friend_log)):
            xs = player_two_x(log)
            moved = len(xs) >= 4 and max(xs) - min(xs) >= 40 and max(xs) > xs[0]
            print(f"  {'ok  ' if moved else 'FAIL'}  {name}: the friend's presses walked player 2"
                  f" (x {xs[0] if xs else '-'} to {max(xs) if xs else '-'} and back to {xs[-1] if xs else '-'},"
                  f" {len(xs)} samples)")
            passed = passed and moved
        host_hashes, friend_hashes = final_hashes(host_log), final_hashes(friend_log)
        common = sorted(set(host_hashes) & set(friend_hashes))
        agree = len(common) >= REPORTS and all(host_hashes[f][0] == friend_hashes[f][0] for f in common)
        print(f"  {'ok  ' if agree else 'FAIL'}  both reached {len(common)} reports with equal final hashes")
        resimulated = {name: max((r for _, r in hashes.values()), default=0)
                       for name, hashes in (("host", host_hashes), ("friend", friend_hashes))}
        rewound = options.zerotier or all(resimulated.values())
        print(f"  {'ok  ' if rewound else 'FAIL'}  frames resimulated: host {resimulated['host']},"
              f" friend {resimulated['friend']}" + (" (lab root, no impairment)" if options.zerotier else ""))
        agree = agree and rewound
        for frame in common[:3] + common[-2:]:
            print(f"          frame {frame}: host {host_hashes[frame][0]} friend {friend_hashes[frame][0]}"
                  f" (resimulated {host_hashes[frame][1]}/{friend_hashes[frame][1]})")
        return 0 if passed and agree else 1
    finally:
        for process in processes:
            stop(process)
        for helper in helpers:
            helper.terminate()
            helper.wait()
        for machine in machines:
            machine.close()


if __name__ == "__main__":
    sys.exit(main())
