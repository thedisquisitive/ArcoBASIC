# UTF-16 Constant Encoding

Status: WP-007 (UTF-16 Constant Support)
Depends on: `arcology-os/docs/systems/uefi-bindings.md` (confirms `EFI_TEXT_STRING`'s `String` parameter is
`CHAR16*`)

`include/arco/utf16.hpp::encode_utf16_null_terminated` converts a UTF-8 ArcoBASIC string literal
into the null-terminated UTF-16 code-unit sequence UEFI's text-output protocol requires.

## Encoding Rules

- Standard UTF-8 decoding: 1-byte (ASCII, `0xxxxxxx`), 2-byte (`110xxxxx 10xxxxxx`), 3-byte
  (`1110xxxx 10xxxxxx 10xxxxxx`), and 4-byte (`11110xxx 10xxxxxx 10xxxxxx 10xxxxxx`) sequences are
  all supported.
- Code points in the Basic Multilingual Plane (U+0000-U+FFFF, excluding surrogates) encode to a
  single UTF-16 code unit.
- Code points above U+FFFF (up to U+10FFFF) encode to a standard UTF-16 surrogate pair: subtract
  `0x10000`, then emit `0xD800 + (adjusted >> 10)` followed by `0xDC00 + (adjusted & 0x3FF)`.
- A `char16_t` value `0` (`u'\0'`) is always appended as the final element, terminating the string
  the way `CHAR16*` C strings are terminated.

## Rejected Cases ("reject unsupported source cases clearly," Packet WP-007)

| Case | Why it is rejected |
|---|---|
| An embedded NUL byte (`\0`) anywhere in the source string | A null-terminated string cannot represent a NUL byte in its content -- everything after it would be silently invisible to any code that reads up to the terminator. Rejecting this at compile time turns a silent, hard-to-diagnose truncation bug into an immediate, clear error. |
| An invalid UTF-8 lead byte (e.g. `0xFF`, `0xFE`, or a bare continuation byte `10xxxxxx` in lead position) | Not valid UTF-8; there is no code point to encode. |
| A truncated multi-byte sequence (source ends mid-character) | Same as above -- no complete code point is present. |
| An invalid continuation byte (does not match `10xxxxxx`) | Same as above. |
| A UTF-8 encoding of a surrogate code point (U+D800-U+DFFF) | Surrogates are UTF-16 encoding artifacts, not valid standalone Unicode code points (this is sometimes called CESU-8 or WTF-8 and is deliberately not accepted here); encoding one would produce ambiguous or corrupt UTF-16. |
| A code point above U+10FFFF | Outside the range Unicode (and therefore UTF-16) can represent. |

Every rejection raises a `std::runtime_error` with a message identifying which of the above
occurred; `Parser::validate_utf16_arguments` (`src/frontend/parser.cpp`) wraps this into a source-location
diagnostic (`token_error`) of the form:

```text
string argument cannot be encoded as UTF-16: <reason>.
```

## Where This Applies

`Parser::validate_utf16_arguments` runs only for plain string-literal arguments passed to a call
whose receiver is a function parameter with a known UEFI type (the same
`current_function_parameter_types_` tracking WP-006 introduced for field-chain validation). This
keeps the check precisely scoped to the systems/UEFI call surface: an ordinary hosted-mode `PRINT
"..."` or a string passed to any non-UEFI function is never affected, even if it happens to contain
an embedded NUL or malformed bytes -- verified by a regression case in
`arcology-os/tests/systems/systems_utf16_encoding_smoke.sh`.

Non-literal string arguments (a variable, an expression, a function call result) are not statically
checked, matching the literal-only scope `validate_fixed_width_initializer` already established in
WP-002.

## What This Work Package Does Not Do

- Does not store the encoded UTF-16 code units anywhere yet (A-MIR's `CONST` instruction for a
  string literal still holds the original UTF-8 quoted text, unchanged from WP-004). "Preserve
  constant lifetime for the duration of the call" (Packet WP-007) is a real-memory/data-section
  concern that only becomes concrete once WP-009 lays out a PE image's data section; there is
  nothing to place in memory yet at the compiler-internals stage this milestone is at. Encoding is
  available and validated now so WP-008/009 have a ready, tested function to call when they need
  the actual bytes.
- Does not change how strings are rendered anywhere in A-MIR, bytecode, or the interpreter -- purely
  a new, separately-testable encoding utility plus a scoped compile-time validation, both additive.
