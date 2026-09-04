"""Repository-wide source, configuration, diagnostics, and tooling policy."""

from dataclasses import dataclass
from pathlib import Path
import re


ENVIRONMENT_OWNER = "runtime/app/environment.c"
LOGGER_OWNER = "runtime/log/lf2_log.cpp"
POLICY_OWNERS = frozenset(
    {
        "tools/build/product_symbols.py",
        "tools/build/source_policy.py",
    }
)
FIRST_PARTY_DIRECTORIES = ("docs", "platforms", "runtime", "tests", "tools")
FIRST_PARTY_ROOT_FILES = (
    "AGENTS.md",
    "CMakeLists.txt",
    "README.md",
    "bootstrap.py",
    "pyproject.toml",
    "run.sh",
)
TEXT_SUFFIXES = frozenset(
    {
        ".c",
        ".cmake",
        ".cpp",
        ".h",
        ".hpp",
        ".java",
        ".md",
        ".py",
        ".sh",
        ".toml",
        ".txt",
        ".yaml",
        ".yml",
    }
)
PRODUCT_SUFFIXES = frozenset({".c", ".cpp", ".h", ".hpp"})

# Exact retired interfaces are named here so ordinary discussion of compilation,
# generation, or static analysis is not prohibited.
RETIRED_PATHS = (
    "recompiler",
    "re/entries.tsv",
    "re/functions.tsv",
    "re/insn_vectors.bin",
    "re/instructions.tsv",
    "re/overrides.txt",
    "runtime/cpu/flags.c",
    "runtime/cpu/guest_ops.h",
    "runtime/cpu/insn_test.h",
    "runtime/cpu/strops.c",
    "tests/test_flags.c",
    "tests/test_insn.c",
)
RETIRED_INTERFACES = (
    re.compile(r"\bLF2_RECOMP_SOURCE\b"),
    re.compile(r"\blf2_recomp(?:\.c)?\b"),
    re.compile(r"\brecompiler/"),
    re.compile(r"\bre/(?:entries|functions|instructions|overrides)\.tsv\b"),
    re.compile(r"\bre/insn_vectors\.bin\b"),
    re.compile(r"\bfn_[0-9a-fA-F]{8}__orig\b"),
    re.compile(r"\bLF2_(?:ALTBG|BG|CAPTION|OBJ)_ORIG\b"),
    re.compile(r"\bLF2_SLOW_OBJECT_PARSER\b"),
)

ENVIRONMENT_READ = re.compile(r"\bgetenv\s*\(")
DIRECT_DIAGNOSTICS = (
    re.compile(r"\b(?:stderr|stdout)\b"),
    re.compile(r"(?<![A-Za-z0-9_])(?:std::)?printf\s*\("),
    re.compile(r"(?<![A-Za-z0-9_])(?:std::)?puts\s*\("),
    re.compile(r"(?<![A-Za-z0-9_])perror\s*\("),
    re.compile(r"\bSDL_Log(?:Critical|Error|Warn|Info|Debug|Trace)?\s*\("),
    re.compile(r"\bstd::(?:cerr|cout|clog)\b"),
    re.compile(r"\bwrite\s*\(\s*2\s*,"),
)


@dataclass(frozen=True)
class SourcePolicyViolation:
    path: str
    line: int
    detail: str


def _first_party_files(root: Path) -> list[Path]:
    files = [root / relative for relative in FIRST_PARTY_ROOT_FILES if (root / relative).is_file()]
    for directory in FIRST_PARTY_DIRECTORIES:
        base = root / directory
        if base.is_dir():
            files.extend(path for path in base.rglob("*") if path.is_file() and path.suffix in TEXT_SUFFIXES)
    return sorted(set(files))


def _matching_lines(text: str, pattern: re.Pattern[str]) -> list[int]:
    return [index for index, line in enumerate(text.splitlines(), 1) if pattern.search(line)]


def inspect_source_policy(root: Path) -> tuple[int, list[SourcePolicyViolation]]:
    """Inspect first-party files and return every exact ownership/policy violation."""
    files = _first_party_files(root)
    violations: list[SourcePolicyViolation] = []

    for relative in RETIRED_PATHS:
        if (root / relative).exists():
            violations.append(SourcePolicyViolation(relative, 1, "retired execution interface exists"))

    for path in files:
        relative = path.relative_to(root).as_posix()
        if path.suffix == ".sh" and relative != "run.sh":
            violations.append(SourcePolicyViolation(relative, 1, "project automation must be Python"))
        if path.suffix == ".java" and relative.startswith("tools/"):
            violations.append(SourcePolicyViolation(relative, 1, "tool automation must be Python/Jython"))

        text = path.read_text(encoding="utf-8")
        if relative not in POLICY_OWNERS:
            for pattern in RETIRED_INTERFACES:
                for line in _matching_lines(text, pattern):
                    violations.append(SourcePolicyViolation(relative, line, "retired execution interface referenced"))

        is_product_source = relative.startswith("runtime/") and path.suffix in PRODUCT_SUFFIXES
        if is_product_source and relative != ENVIRONMENT_OWNER:
            for line in _matching_lines(text, ENVIRONMENT_READ):
                violations.append(SourcePolicyViolation(relative, line, "environment read bypasses typed owner"))
        if is_product_source and relative != LOGGER_OWNER:
            for pattern in DIRECT_DIAGNOSTICS:
                for line in _matching_lines(text, pattern):
                    violations.append(SourcePolicyViolation(relative, line, "diagnostic write bypasses LF2/Lucent logger"))
    return len(files), violations
