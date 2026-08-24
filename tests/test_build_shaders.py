#!/usr/bin/env python3
"""Focused tests for the committed multi-format shader generator."""

from __future__ import annotations

import io
import stat
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "build"))

import build_shaders


class ShaderGeneratorTest(unittest.TestCase):
    def setUp(self) -> None:
        (ROOT / "scratch").mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(
            prefix="test-build-shaders-", dir=ROOT / "scratch"
        )
        self.root = Path(self.temporary.name)
        self.shader_dir = self.root / "runtime" / "shaders"
        self.shader_dir.mkdir(parents=True)
        (self.shader_dir / "colour.frag").write_text(
            "fragment source\n", encoding="utf-8"
        )
        (self.shader_dir / "colour.vert").write_text(
            "vertex source\n", encoding="utf-8"
        )
        self.tools = self.root / "tools"
        self.tools.mkdir()
        self.glslc = self.write_tool(
            "glslc",
            """
args = sys.argv[1:]
output_index = args.index("-o")
source = pathlib.Path(args[output_index - 1])
pathlib.Path(args[output_index + 1]).write_bytes(b"SPIRV:" + source.read_bytes())
""",
        )
        self.shadercross = self.write_tool(
            "shadercross",
            """
args = sys.argv[1:]
source = pathlib.Path(args[0])
output = pathlib.Path(args[args.index("--output") + 1])
stage = args[args.index("--stage") + 1].encode("ascii")
output.write_bytes(b"MSL:" + stage + b":" + source.read_bytes())
""",
        )
        self.environment = {
            "PATH": str(self.tools) + ":" + str(Path(sys.executable).parent),
            "GLSLC": str(self.glslc),
            "SHADERCROSS": str(self.shadercross),
        }

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write_tool(self, name: str, body: str) -> Path:
        path = self.tools / name
        path.write_text(
            f"#!{sys.executable}\nimport pathlib\nimport sys\n{body}", encoding="utf-8"
        )
        path.chmod(path.stat().st_mode | stat.S_IXUSR)
        return path

    def run_generator(self, *arguments: str) -> tuple[int, str]:
        output = io.StringIO()
        with redirect_stdout(output):
            status = build_shaders.main(
                arguments, root=self.root, environment=self.environment
            )
        return status, output.getvalue()

    def test_regenerate_writes_spirv_and_msl_from_same_glsl(self) -> None:
        status, output = self.run_generator()

        self.assertEqual(0, status, output)
        generated = self.shader_dir / "gen"
        expected = {
            "colour_spv.h",
            "colour_msl.h",
            "colour_vert_spv.h",
            "colour_vert_msl.h",
        }
        self.assertEqual(expected, {path.name for path in generated.iterdir()})
        fragment_spirv = (generated / "colour_spv.h").read_text(encoding="ascii")
        fragment_msl = (generated / "colour_msl.h").read_text(encoding="ascii")
        vertex_msl = (generated / "colour_vert_msl.h").read_text(encoding="ascii")
        self.assertIn("83,80,73,82,86", fragment_spirv)
        self.assertIn("ctest shaders checks both formats", fragment_msl)
        self.assertIn("static const unsigned char colour_msl[]", fragment_msl)
        self.assertIn("static const unsigned char colour_vert_msl[]", vertex_msl)
        self.assertIn(
            "77,83,76,58,118,101,114,116,101,120,58,83,80,73,82,86", vertex_msl
        )

    def test_check_detects_both_formats_stale_after_glsl_change(self) -> None:
        self.assertEqual(0, self.run_generator()[0])
        self.assertEqual(0, self.run_generator("--check")[0])

        (self.shader_dir / "colour.frag").write_text(
            "changed fragment\n", encoding="utf-8"
        )
        status, output = self.run_generator("--check")

        self.assertEqual(1, status)
        self.assertIn("colour_spv.h is stale", output)
        self.assertIn("colour_msl.h is stale", output)
        self.assertIn("shaders: FAILED", output)

    def test_missing_tools_skip_check_but_refuse_regeneration(self) -> None:
        missing_environment = {
            "PATH": "",
            "GLSLC": "missing-glslc",
            "SHADERCROSS": "missing-shadercross",
        }
        output = io.StringIO()
        with redirect_stdout(output):
            check_status = build_shaders.main(
                ["--check"], root=self.root, environment=missing_environment
            )
            regenerate_status = build_shaders.main(
                [], root=self.root, environment=missing_environment
            )

        self.assertEqual(77, check_status)
        self.assertEqual(1, regenerate_status)
        self.assertIn("libsdl-org/SDL_shadercross", output.getvalue())
        self.assertIn("Set GLSLC and SHADERCROSS", output.getvalue())

    def test_check_keeps_spirv_evidence_when_shadercross_is_missing(self) -> None:
        self.assertEqual(0, self.run_generator()[0])
        without_shadercross = dict(self.environment)
        without_shadercross["SHADERCROSS"] = "missing-shadercross"

        output = io.StringIO()
        with redirect_stdout(output):
            current_status = build_shaders.main(
                ["--check"], root=self.root, environment=without_shadercross
            )
        self.assertEqual(77, current_status)
        self.assertIn("SPIR-V is current", output.getvalue())
        self.assertIn("MSL is unchecked", output.getvalue())

        (self.shader_dir / "colour.frag").write_text(
            "changed fragment\n", encoding="utf-8"
        )
        output = io.StringIO()
        with redirect_stdout(output):
            stale_status = build_shaders.main(
                ["--check"], root=self.root, environment=without_shadercross
            )
        self.assertEqual(1, stale_status)
        self.assertIn("colour_spv.h is stale", output.getvalue())
        self.assertIn("MSL is also unchecked", output.getvalue())

    def test_empty_source_directory_refuses_to_report_success(self) -> None:
        for source in self.shader_dir.iterdir():
            source.unlink()

        status, output = self.run_generator("--check")

        self.assertEqual(1, status)
        self.assertIn("NOTHING was compiled or checked", output)


if __name__ == "__main__":
    unittest.main()
