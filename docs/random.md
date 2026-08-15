# Deterministic Pseudorandom Numbers

ArcoBASIC's hosted runtime provides convenient default randomness and explicit reproducible
generators. Seeded generators use the PCG XSH RR 64/32 algorithm specified by RFC-0021, so the same
seed, sequence, and ordered calls produce identical results in the interpreter, bytecode VM, and
hosted native runtime capsule.

This API is not cryptographically secure. Do not use it for passwords, keys, session identifiers,
tokens, salts, nonces, or security decisions.

## Default generator

Every runtime owns a separate default generator:

```basic
PRINT Math.Random()                 ' Number in [0, 1)
PRINT Random.Float()                ' same default stream
PRINT Random.Integer(1, 6)          ' inclusive bounds
PRINT Random.Choice(["a", "b"])
```

`Math.Random()` is the zero-argument compatibility alias for `Random.Float()`.

## Reproducible generators

```basic
rng = Random.Create(42)

PRINT Random.Float(rng)
PRINT Random.Integer(0, 100, rng)
PRINT Random.Choice(["north", "south", "east", "west"], rng)

Random.Destroy(rng)
```

`Random.Create(seed, sequence)` accepts non-negative integral `Number` values through `2^53 - 1`.
The sequence defaults to `54`. Different sequence values select independent PCG streams.

Omit the seed or pass `NULL` to request automatic, non-reproducible seeding:

```basic
rng = Random.Create()
```

Automatic seeding provides variation, not cryptographic security.

## Lifecycle

`Random.Create` returns an opaque `RANDOM` handle. Assigning or passing the handle preserves
identity; it does not clone state. Calls through copied handles consume the same stream.

```basic
rng = Random.Create(7)
same = rng
PRINT Random.Float(rng)
PRINT Random.Float(same)            ' next value from rng
Random.Destroy(rng)
```

`Random.Reseed(generator, seed, sequence)` resets an explicit generator. Destroyed, null, stale,
and wrong-type handles produce deterministic runtime errors. The default generator cannot be
destroyed or reseeded.

## Independent state clones

`Random.Clone(generator)` returns a new explicit generator at exactly the source generator's
current state. It consumes no random values and does not change the source. Subsequent calls advance
the two handles independently.

```basic
rng = Random.Create(7)
ignored = Random.Float(rng)
copy = Random.Clone(rng)

PRINT Random.Integer(0, 100, rng)
PRINT Random.Integer(0, 100, copy)  ' same value, then independent states

Random.Destroy(copy)
Random.Destroy(rng)
```

Unlike assignment, cloning creates a distinct resource with its own destruction lifecycle. Only a
live explicit `RANDOM` handle can be cloned; the runtime-owned default generator cannot be cloned.

## Collections

```basic
rng = Random.Create(99)
source = [1, 2, 3, 4, 5]

one = Random.Choice(source, rng)
three = Random.Sample(source, 3, rng)
mixed = Random.Shuffle(source, rng)

PRINT source                       ' unchanged
Random.Destroy(rng)
```

`Random.Sample` samples without replacement. `Random.Shuffle` returns a shuffled shallow copy.
Neither function changes the source array.

## Limits and errors

- Integer bounds are inclusive, non-negative safe integers.
- The inclusive integer range size may not exceed `2^32`.
- `Random.Choice` rejects an empty array.
- A sample count must be between zero and the source length.
- Seed, sequence, bounds, and counts reject fractional, negative, non-finite, and unsafe values.
- `Random.*` and `Math.Random()` are hosted services and are rejected under `#RUNTIME NONE`.

See `examples/random.abas` for a runnable example and
`arcology-os/rfcs/RFC-0021_Deterministic_Pseudorandom_Number_Generation.md` for the normative
contract.
