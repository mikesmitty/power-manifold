#!/usr/bin/env python3
"""Turn web/index.html into web_index.h, the page the controller serves at /.

The output is the body of a C string literal: http.c includes it as the
initialiser of INDEX_HTML. A {{EXPR}} in the page becomes " EXPR " in the
literal, so limits such as {{STR(BUDGET_MIN_W)}} or {{NET_NTP_DEFAULT}} come
from the C headers and the page cannot drift from them.

The minifying is deliberately small: it drops CSS comments, whole-line //
comments in the script, indentation and blank lines. Line breaks stay, so
the script never depends on semicolon insertion surviving a join, and the
markup keeps the whitespace it had. The page must be ASCII; write entities
in the markup and \\u escapes in the script.
"""

import argparse
import pathlib
import re
import sys

PLACEHOLDER = re.compile(r"\{\{\s*([A-Za-z_][A-Za-z0-9_]*(?:\([A-Za-z_][A-Za-z0-9_]*\))?)\s*\}\}")


def minify(page: str) -> str:
    page = re.sub(r"/\*.*?\*/", "", page, flags=re.S)
    out = []
    in_script = False
    for line in page.splitlines():
        line = line.strip()
        if "<script>" in line:
            in_script = True
        if "</script>" in line:
            in_script = False
        if not line or (in_script and line.startswith("//")):
            continue
        out.append(line)
    return "\n".join(out)


def c_text(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True)
    ap.add_argument("page")
    a = ap.parse_args()

    src = pathlib.Path(a.page).read_text()
    bad = sorted({c for c in src if ord(c) > 127})
    if bad:
        sys.exit(f"{a.page}: not ASCII: {''.join(bad)}")
    page = minify(src)
    if "{{" in PLACEHOLDER.sub("", page):
        sys.exit(f"{a.page}: a {{{{ that is not a {{{{NAME}}}} or {{{{STR(NAME)}}}} placeholder")

    lines = [
        f"// Generated from {pathlib.Path(a.page).name} by tools/web_page.py. Do not edit.",
        f"// {len(src)} bytes in, about {len(page)} out before the placeholders expand.",
    ]
    # one C line per page line keeps the header readable in a diff
    for text_line in (page + "\n").splitlines(keepends=True):
        parts = PLACEHOLDER.split(text_line)
        pieces = []
        for k, part in enumerate(parts):
            if k % 2:
                pieces.append(part)
            elif part:
                pieces.append(f'"{c_text(part)}"')
        lines.append("    " + " ".join(pieces))
    pathlib.Path(a.out).write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
