# Bit vectors

`BITVECTOR` is an immutable, packed sequence of zero and one values. A `BITS` literal accepts
underscores as visual separators while preserving leading zeroes:

```basic
LET genome AS BITVECTOR = BITS "0001_1011"
PRINT LEN(genome)
PRINT genome[3]
PRINT Bits.ToString(Bits.Flip(genome, 0))
```

Indexing is zero-based. `+` concatenates two bit vectors. Transforming operations return a new
value, so assignment may safely share the immutable original.

The core API is `Bits.FromString`, `Bits.ToString`, `Bits.FromArray`, `Bits.ToArray`, `Bits.Get`,
`Bits.Set`, `Bits.Flip`, `Bits.Count`, `Bits.Slice`, `Bits.Replace`, and `Bits.Reverse`. Indices and
lengths must be integral and in range; bit values must be numeric zero or one.

Bit vectors are currently hosted values and are rejected under `#RUNTIME NONE`.
