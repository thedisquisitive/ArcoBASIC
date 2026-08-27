#!/usr/bin/env python3
"""Real, dependency-free (stdlib-only) check that a P6 PPM screendump has a genuinely rendered
text region -- counts visible non-background ink pixels within a given
rectangle and compares against a minimum fraction. Used by
systems_aps_arcology_seed_substrate_smoke.sh to confirm RFC-0045 Phase 5's own real on-screen
terminal actually drew something, not just that the fixture reported "GOP READY" without ever
touching the framebuffer. No PIL/Pillow dependency, since this project's own CI environment isn't
guaranteed to have it installed.
"""
import sys

def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(b"P6"):
        raise ValueError("not a raw P6 PPM")
    # Tokenize the 3 header fields (width, height, maxval), skipping whitespace and '#' comments,
    # then the pixel data starts immediately after a single whitespace byte following maxval.
    pos = 2
    fields = []
    while len(fields) < 3:
        while data[pos:pos+1].isspace():
            pos += 1
        if data[pos:pos+1] == b"#":
            while data[pos:pos+1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while not data[pos:pos+1].isspace():
            pos += 1
        fields.append(int(data[start:pos]))
    pos += 1  # the single whitespace byte after maxval
    width, height, maxval = fields
    pixels = data[pos:]
    return width, height, pixels

def main():
    if len(sys.argv) != 7:
        print("usage: check_ppm_text_region.py <ppm> <x0> <y0> <x1> <y1> <min_fraction>", file=sys.stderr)
        return 2
    path, x0, y0, x1, y1, min_fraction = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]), float(sys.argv[6])
    width, height, pixels = read_ppm(path)
    if x1 > width or y1 > height:
        print(f"FAIL: requested region exceeds real image size {width}x{height}", file=sys.stderr)
        return 1
    ink = 0
    total = 0
    for y in range(y0, y1):
        row_base = y * width * 3
        for x in range(x0, x1):
            off = row_base + x * 3
            r, g, b = pixels[off], pixels[off + 1], pixels[off + 2]
            total += 1
            if r > 24 or g > 24 or b > 24:
                ink += 1
    fraction = ink / total if total else 0.0
    print(f"ink={ink} total={total} fraction={fraction:.4f}")
    if fraction < min_fraction:
        print(f"FAIL: ink-pixel fraction {fraction:.4f} below required {min_fraction}", file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
