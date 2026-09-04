"""Structural policy for LF2's native/JIT execution boundary."""

from dataclasses import dataclass
from pathlib import Path
import re
REQUIRED_FILES = (
    "CMakeLists.txt",
    "runtime/cpu/jit_executor.c",
    "runtime/cpu/jit_executor.h",
    "runtime/overrides/native_override.c",
    "runtime/overrides/native_override.h",
)


@dataclass(frozen=True)
class Violation:
    path: str
    detail: str


def inspect(root: Path) -> tuple[int, list[Violation]]:
    """Return the number of inspected files and every execution-boundary violation."""
    violations = [
        Violation(relative, "required native/JIT boundary file is missing")
        for relative in REQUIRED_FILES
        if not (root / relative).is_file()
    ]
    inspected = len(REQUIRED_FILES)

    override_definitions: set[str] = set()
    for path in (root / "runtime" / "overrides").glob("*.c"):
        if path.name == "native_override.c":
            continue
        override_definitions.update(
            re.findall(r"\bvoid\s+fn_([0-9a-f]{8})\s*\(void\)", path.read_text(encoding="utf-8"))
        )
    registry_path = root / "runtime" / "overrides" / "native_override.c"
    inspected += 1
    registered = (
        set(re.findall(r"^\s*X\(([0-9a-f]{8})\)", registry_path.read_text(encoding="utf-8"), re.MULTILINE))
        if registry_path.is_file()
        else set()
    )
    for address in sorted(override_definitions - registered):
        violations.append(Violation("runtime/overrides/native_override.c", f"native function {address} is not registered"))
    for address in sorted(registered - override_definitions):
        violations.append(Violation("runtime/overrides/native_override.c", f"registered function {address} has no definition"))

    cmake_path = root / "CMakeLists.txt"
    cmake = cmake_path.read_text(encoding="utf-8") if cmake_path.is_file() else ""
    if "set(LF2_JIT_EXECUTOR runtime/cpu/jit_executor.c)" not in cmake:
        violations.append(Violation("CMakeLists.txt", "the product does not name its single JIT adapter"))
    if "TARGET x86port_runtime" not in cmake or "target_link_libraries(lf2 PRIVATE x86port_runtime)" not in cmake:
        violations.append(Violation("CMakeLists.txt", "the adapter does not require the x86port_runtime product target"))
    product_surfaces = "\n".join(
        (root / relative).read_text(encoding="utf-8")
        for relative in ("CMakeLists.txt", "bootstrap.py", "runtime/app/options.c", "runtime/app/environment_keys.inc")
        if (root / relative).is_file()
    )
    for selector in ("LF2_CPU_ENGINE", "LF2_CPU_INTERPRETER", "--cpu-interpreter"):
        if selector in product_surfaces:
            violations.append(Violation("product configuration", f"explicit gameplay interpreter selector returned: {selector}"))

    return inspected, violations
