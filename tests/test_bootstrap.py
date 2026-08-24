"""Fresh-checkout provisioning must own both external source checkouts."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))
import bootstrap


class BootstrapCheckoutTest(unittest.TestCase):
    def setUp(self) -> None:
        (REPO / "scratch").mkdir(exist_ok=True)
        self.tempdir = tempfile.TemporaryDirectory(dir=REPO / "scratch")
        self.root = Path(self.tempdir.name) / "checkout" / "lf2"
        self.root.mkdir(parents=True)
        self.root_patch = mock.patch.object(bootstrap, "ROOT", self.root)
        self.root_patch.start()
        self.env_patch = mock.patch.dict(
            bootstrap.os.environ,
            {"PORT_ASSETS_DIR": "", "SHARED_DIR": "", "SHARED_GIT_BASE": ""},
        )
        self.env_patch.start()

    def tearDown(self) -> None:
        self.root_patch.stop()
        self.env_patch.stop()
        self.tempdir.cleanup()

    @staticmethod
    def create_assets(target: Path) -> None:
        for relative in bootstrap.PORT_ASSET_FILES:
            path = target / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()

    @mock.patch.object(bootstrap.shutil, "which", return_value="/usr/bin/git")
    def test_missing_port_assets_are_cloned_and_pinned_in_scratch(
        self, _which: mock.Mock
    ) -> None:
        target = self.root / "scratch" / "deps" / "port-assets"

        def clone(
            command: list[str], **kwargs: object
        ) -> subprocess.CompletedProcess[str]:
            self.assertEqual(kwargs, {"check": False})
            if command[1] == "clone":
                target.mkdir(parents=True)
            else:
                self.assertEqual(
                    command,
                    [
                        "/usr/bin/git",
                        "-C",
                        str(target),
                        "checkout",
                        "--detach",
                        bootstrap.PORT_ASSETS_REVISION,
                    ],
                )
                self.create_assets(target)
            return subprocess.CompletedProcess(command, 0)

        with mock.patch.object(bootstrap.subprocess, "run", side_effect=clone) as run:
            resolved = bootstrap.ensure_port_assets()
        self.assertEqual(resolved, target.resolve())
        self.assertEqual(
            run.call_args_list[0].args[0],
            [
                "/usr/bin/git",
                "clone",
                "https://github.com/SomeoneIsWorking/port-assets.git",
                str(target),
            ],
        )
        self.assertEqual(run.call_count, 2)

    def test_explicit_environment_path_is_validated_without_mutation(self) -> None:
        chosen = self.root / "chosen"
        shared = self.root / "ignored" / "port-assets"
        self.create_assets(chosen)
        self.create_assets(shared)
        with mock.patch.dict(
            bootstrap.os.environ,
            {"PORT_ASSETS_DIR": "chosen", "SHARED_DIR": "ignored"},
        ):
            with mock.patch.object(bootstrap.subprocess, "run") as run:
                self.assertEqual(bootstrap.ensure_port_assets(), chosen.resolve())
            run.assert_not_called()

    @mock.patch.object(bootstrap.shutil, "which", return_value="/usr/bin/git")
    def test_shared_root_is_the_clone_target(self, _which: mock.Mock) -> None:
        target = self.root / "shared-root" / "port-assets"
        with mock.patch.dict(
            bootstrap.os.environ,
            {"PORT_ASSETS_DIR": "", "SHARED_DIR": "shared-root"},
        ):

            def run_git(
                command: list[str], **kwargs: object
            ) -> subprocess.CompletedProcess[str]:
                self.assertEqual(kwargs, {"check": False})
                if command[1] == "clone":
                    target.mkdir(parents=True)
                else:
                    self.create_assets(target)
                return subprocess.CompletedProcess(command, 0)

            with mock.patch.object(
                bootstrap.subprocess, "run", side_effect=run_git
            ) as run:
                self.assertEqual(bootstrap.ensure_port_assets(), target.resolve())
        self.assertEqual(run.call_args_list[0].args[0][-1], str(target))

    def test_complete_legacy_shared_checkout_is_reused(self) -> None:
        legacy = self.root.parent.parent / "shared" / "port-assets"
        self.create_assets(legacy)
        with mock.patch.object(bootstrap.subprocess, "run") as run:
            self.assertEqual(bootstrap.ensure_port_assets(), legacy.resolve())
        run.assert_not_called()

    @mock.patch.object(bootstrap.shutil, "which", return_value="/usr/bin/git")
    def test_partial_checkout_names_the_required_svg(self, _which: mock.Mock) -> None:
        target = self.root / "scratch" / "deps" / "port-assets"
        (target / "sets").mkdir(parents=True)
        with self.assertRaisesRegex(SystemExit, "sets/devices/keyboard.svg"):
            bootstrap.ensure_port_assets()

    def test_existing_game_tree_does_not_look_up_an_installer(self) -> None:
        game = self.root / bootstrap.GAME_EXE
        game.parent.mkdir(parents=True)
        game.touch()
        with mock.patch.object(bootstrap, "ensure_installer") as installer:
            bootstrap.ensure_game_tree()
        installer.assert_not_called()

    def test_build_passes_the_resolved_asset_path_to_cmake(self) -> None:
        venv_python = self.root / ".venv" / "bin" / "python"
        venv_python.parent.mkdir(parents=True)
        venv_python.touch()
        binary = self.root / "scratch" / "build-clang" / "lf2"
        port_assets = self.root / "scratch" / "deps" / "port-assets"

        def build(
            command: list[str], **kwargs: object
        ) -> subprocess.CompletedProcess[str]:
            self.assertEqual(kwargs["env"]["PORT_ASSETS_DIR"], str(port_assets))
            binary.parent.mkdir(parents=True)
            binary.touch()
            return subprocess.CompletedProcess(command, 0)

        with (
            mock.patch.object(bootstrap, "BINARY", binary),
            mock.patch.object(bootstrap.subprocess, "run", side_effect=build),
        ):
            bootstrap.build(venv_python, port_assets)

    @mock.patch.object(bootstrap.shutil, "which", return_value="/usr/bin/git")
    def test_missing_rmlui_is_initialized_as_a_submodule(
        self, _which: mock.Mock
    ) -> None:
        rmlui = self.root / "third_party" / "RmlUi"

        def update(
            command: list[str], **kwargs: object
        ) -> subprocess.CompletedProcess[str]:
            self.assertEqual(
                command,
                ["/usr/bin/git", "submodule", "update", "--init", "--recursive"],
            )
            self.assertEqual(kwargs, {"cwd": self.root, "check": False})
            rmlui.mkdir(parents=True)
            (rmlui / "CMakeLists.txt").touch()
            return subprocess.CompletedProcess(command, 0)

        with mock.patch.object(bootstrap.subprocess, "run", side_effect=update) as run:
            bootstrap.ensure_submodules()
        run.assert_called_once()

    @mock.patch.object(bootstrap.shutil, "which", return_value=None)
    def test_missing_git_names_the_unprovisioned_checkout(
        self, _which: mock.Mock
    ) -> None:
        with self.assertRaisesRegex(SystemExit, "git is required.*port-assets"):
            bootstrap.ensure_port_assets()

    @mock.patch.object(bootstrap.shutil, "which", return_value="/usr/bin/git")
    def test_failed_port_assets_clone_refuses_without_a_traceback(
        self, _which: mock.Mock
    ) -> None:
        failure = subprocess.CompletedProcess(["git", "clone"], 1)
        with (
            mock.patch.object(bootstrap.subprocess, "run", return_value=failure),
            self.assertRaisesRegex(
                SystemExit, "port-assets checkout did not initialize"
            ),
        ):
            bootstrap.ensure_port_assets()


if __name__ == "__main__":
    unittest.main()
