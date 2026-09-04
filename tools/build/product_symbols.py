"""Inspect the linked LF2 product for its required and forbidden execution symbols."""

from dataclasses import dataclass


REQUIRED_SYMBOLS = ("lf2_jit_call", "lf2_jit_call_original", "x86p_jit_engine_run")
FORBIDDEN_SYMBOLS = ("lf2_recomp", "x86port_test_oracle")


@dataclass(frozen=True)
class SymbolAudit:
    symbol_lines: int
    missing: tuple[str, ...]
    forbidden: tuple[str, ...]


def inspect_nm_output(output: str) -> SymbolAudit:
    lines = tuple(line for line in output.splitlines() if line.strip())
    missing = tuple(symbol for symbol in REQUIRED_SYMBOLS if not any(symbol in line for line in lines))
    forbidden = tuple(symbol for symbol in FORBIDDEN_SYMBOLS if any(symbol in line for line in lines))
    return SymbolAudit(len(lines), missing, forbidden)
