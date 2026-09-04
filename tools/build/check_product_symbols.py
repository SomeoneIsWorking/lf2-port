#!/usr/bin/env python3
"""Audit a linked LF2 executable for the product-only JIT boundary."""

import argparse
from pathlib import Path
import subprocess
import sys

from product_symbols import inspect_nm_output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nm", required=True)
    parser.add_argument("--product", required=True, type=Path)
    args = parser.parse_args()
    if not args.product.is_file():
        raise SystemExit(f"product-symbols: missing product: {args.product}")
    result = subprocess.run(
        [args.nm, "-a", str(args.product)], check=True, capture_output=True, text=True
    )
    audit = inspect_nm_output(result.stdout)
    if audit.missing or audit.forbidden:
        print(f"product-symbols: FAILED after inspecting {audit.symbol_lines} symbol lines")
        for symbol in audit.missing:
            print(f"  required JIT symbol is missing: {symbol}")
        for symbol in audit.forbidden:
            print(f"  forbidden non-product execution symbol is linked: {symbol}")
        return 1
    print(
        f"product-symbols: ok; inspected {audit.symbol_lines} symbol lines; "
        "required JIT entry points present; 0 forbidden execution symbols matched"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
