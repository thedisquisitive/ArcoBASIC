# Arcology Commons

Arcology Commons is a standalone social network written in ArcoBASIC. It is a sibling project to
Arcology OS, not an operating-system component.

Its application modules are intentionally outside ArcoBASIC's core `stdlib/`. The included launcher
sets `ARCOBASIC_STDLIB` to the Commons module directory; installed copies place the modules alongside
the standard library so normal `#IMPORT "commons"` and `#IMPORT "arcology"` statements keep working.

## Layout

- `stdlib/` — Commons framework and social-network domain modules.
- `examples/` — seed, persistence, static-export, and HTTP-serving programs.
- `scripts/run/` — local development launcher.
- `tests/` — application and persistence tests.
- `docs/` — framework and application documentation.
- `dist/` and `var/` — generated sites and local application state (ignored by Git).

From the repository root, run the development server with:

```sh
arcology-commons/scripts/run/serve-arcology.sh
```
