#!/usr/bin/env python3
"""Regenerates a BundledXSource() ArcoBASIC function from a real, human-editable .abas file, for
arcosh's own bundled plugins (arcosh/plugins/*.abas -> the embedded copies in arcosh.abas -- see
that file's own header comment on the "Bundled plugins" section for why these are embedded as
string constants rather than read from a file at runtime).

Usage:
    python3 scripts/arcosh/embed_plugin_source.py <source.abas> <FunctionName> > out.abas

Paste the generated function's body over the existing BundledXSource() function in
arcosh/src/arcosh.abas. This is NOT run automatically as part of the build -- these files change
rarely, and running an extra code-generation step on every compile would cost real build time for
no benefit; regenerate by hand, once, whenever arcosh/plugins/*.abas or stdlib/curses.abas change.

Verify a regeneration didn't lose anything by round-tripping it: write the generated function's own
BundledXSource() call's return value back out to a file with File.WriteText, then diff that against
the original .abas file -- they should be identical except for a harmless trailing newline (str.
split('\\n') on a file that ends with a newline always produces one extra empty trailing element,
which this script already drops, so the embedded copy has no MORE lines than the original, just
one fewer trailing blank one).
"""

import sys


def escape_line_as_arcobasic_string_literal(line: str) -> str:
    # Produce an ArcoBASIC string EXPRESSION (not just a literal) for this one line of source,
    # splitting on any embedded double-quote and re-joining with + Chr(34) + so the OUTER
    # ArcoBASIC (the one that will contain this as a string literal) never sees an embedded ".
    parts = line.split('"')
    pieces = []
    for i, part in enumerate(parts):
        pieces.append('"' + part.replace("\\", "\\\\") + '"')
        if i != len(parts) - 1:
            pieces.append("Chr(34)")
    return " + ".join(pieces)


def main() -> None:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <source.abas> <FunctionName>", file=sys.stderr)
        raise SystemExit(2)
    src_path, func_name = sys.argv[1], sys.argv[2]
    with open(src_path, "r") as f:
        lines = f.read().split("\n")
    # Drop a single fully-trailing blank line from the split (an ordinary file ends with "\n",
    # which str.split turns into one extra empty trailing element that isn't a real line).
    if lines and lines[-1] == "":
        lines.pop()
    print(f"FUNCTION {func_name}()")
    print("    RETURN String.Join([")
    for i, line in enumerate(lines):
        comma = "," if i != len(lines) - 1 else ""
        print(f"        {escape_line_as_arcobasic_string_literal(line)}{comma}")
    print("    ], Chr(10))")
    print("END FUNCTION")


if __name__ == "__main__":
    main()
