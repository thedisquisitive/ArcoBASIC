# Agent Packet 017 — ArcoBASIC color packing

Added source-level `ColorRGB`, `ColorBGR`, and opaque-channel aliases under
`arcology-os/stdlib/graphics_color.abas`. Channel arithmetic is performed by ArcoBASIC and lowers
to the existing fixed-width integer backend; no renderer-specific packed constants are required in
application code.

`systems_arco_basic_color_smoke` validates AST, A-MIR arithmetic, and freestanding PE32+ emission.
