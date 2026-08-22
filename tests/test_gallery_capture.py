#!/usr/bin/env python3
"""Offline invariants for the deterministic README gallery capture tool."""

from __future__ import annotations

from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import capture_readme_gallery as gallery  # noqa: E402


class RecipeTests(unittest.TestCase):
    def test_five_candidates_and_exact_dimensions(self) -> None:
        gallery.validate_recipes()
        actual = {
            candidate.filename: (candidate.width, candidate.height)
            for run in gallery.RUNS
            for candidate in run.candidates
        }
        self.assertEqual(
            actual,
            {
                "demo-match-widescreen.png": (1920, 1080),
                "port-menu-overview.png": (1920, 1080),
                "port-menu-graphics.png": (1920, 1080),
                "port-menu-controls.png": (1920, 1080),
                "stage-mode-pve-ultrawide.png": (3440, 1440),
            },
        )

    def test_stage_route_is_the_exact_48_input_recipe(self) -> None:
        actions = gallery.stage_pad_actions()
        self.assertEqual(len(actions), 48)
        self.assertEqual(
            actions[:13],
            (
                "south@modemenu+60",
                "south@charselect+58",
                "south@charselect+118",
                "south@charselect+178",
                "south@charselect+238",
                "up@charselect+298",
                "up@charselect+358",
                "south@charselect+418",
                "south@charselect+618",
                "south@charselect+838",
                "up@overlay+99",
                "up@overlay+159",
                "south@overlay+219",
            ),
        )
        self.assertEqual(
            actions[13:32],
            tuple(f"right@match+{frame}" for frame in range(60, 601, 30)),
        )
        self.assertEqual(
            actions[32:45],
            tuple(
                f"south@match+{frame}"
                for frame in (
                    180,
                    300,
                    420,
                    540,
                    660,
                    780,
                    900,
                    1020,
                    1140,
                    1260,
                    1380,
                    1500,
                    1620,
                )
            ),
        )
        self.assertEqual(actions[-3:], ("east@match+240", "east@match+720", "east@match+1200"))

    def test_rmlui_runs_use_exact_inputs_and_chronological_dumps(self) -> None:
        graphics = next(item for item in gallery.RUNS if item.name == "graphics")
        self.assertEqual(
            graphics.pad_actions,
            (
                "start@modemenu+60",
                "down@modemenu+140",
                "down@modemenu+180",
                "down@modemenu+220",
                "south@modemenu+260",
            ),
        )
        self.assertEqual(
            [candidate.dump_spec for candidate in graphics.candidates],
            ["@modemenu+96", "@modemenu+316"],
        )
        self.assertEqual(graphics.candidates[0].page_marker, "rmlui metrics:")
        self.assertEqual(graphics.candidates[1].page_marker, "rmlui page: graphics")
        controls = next(item for item in gallery.RUNS if item.name == "controls")
        self.assertEqual(
            controls.pad_actions,
            (
                "start@modemenu+60",
                "down@modemenu+140",
                "down@modemenu+180",
                "down@modemenu+220",
                "down@modemenu+260",
                "south@modemenu+300",
            ),
        )
        self.assertEqual(controls.candidates[0].dump_spec, "@modemenu+356")
        self.assertEqual(gallery.game_command(Path("/build/lf2")), ["/build/lf2", "lf2.exe"])

    def test_environment_is_deterministic_and_drops_unrelated_lf2_flags(self) -> None:
        run = gallery.RUNS[0]
        env = gallery.capture_environment(
            {"PATH": "/bin", "LF2_BAD_PROBE": "1", "LF2_MODE": "stage"},
            run,
            Path("/scratch/demo"),
        )
        self.assertEqual(env["PATH"], "/bin")
        self.assertNotIn("LF2_BAD_PROBE", env)
        self.assertEqual(env["LF2_MODE"], "demo")
        self.assertEqual(env["LF2_FRAME_DUMP"], "@match+120")
        self.assertEqual(env["LF2_WINDOW_SIZE"], "1920x1080")
        self.assertEqual(env["LF2_VIRTUAL_PAD"], "south@modemenu+60")

    def test_png_conversion_has_no_geometry_operation(self) -> None:
        command = gallery.png_command("/usr/bin/magick", Path("raw.ppm"), Path("candidate.png"))
        self.assertEqual(
            command,
            [
                "/usr/bin/magick",
                "raw.ppm",
                "-strip",
                "-define",
                "png:compression-level=9",
                "candidate.png",
            ],
        )
        self.assertFalse({"-resize", "-crop", "-extent", "-scale"}.intersection(command))


class PathTests(unittest.TestCase):
    def test_accepts_only_a_child_of_project_scratch(self) -> None:
        expected = (ROOT / "scratch" / "readme_gallery").resolve()
        self.assertEqual(gallery.validate_output_path(expected), expected)
        with self.assertRaises(gallery.GalleryError):
            gallery.validate_output_path((ROOT / "scratch").resolve())
        with self.assertRaises(gallery.GalleryError):
            gallery.validate_output_path((ROOT / "docs" / "screenshots").resolve())
        with self.assertRaises(gallery.GalleryError):
            gallery.validate_output_path((ROOT / "scratch" / ".." / "docs").resolve())
        with self.assertRaises(gallery.GalleryError):
            gallery.validate_output_path(Path("/tmp/readme_gallery"))


if __name__ == "__main__":
    unittest.main()
