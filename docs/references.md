# Safe references in ArcoBASIC

Status: alpha

ArcoBASIC references are safe object/value links. They are not C/C++ raw
pointers: there is no address arithmetic, no casting numbers into addresses,
and no way to write arbitrary memory.

Create a reference with `REF(value)`:

```basic
score = 10
scoreRef = REF(score)

PRINT scoreRef.Value
scoreRef.Value = 25
PRINT score
```

Output:

```text
10
25
```

## Class and object references

References work with class instances and objects:

```basic
CLASS Player
    Name AS String = ""
END CLASS

player = Player()
player.Name = "Ada"

playerRef = REF(player)
playerRef.Value.Name = "Grace"

PRINT player.Name
```

Output:

```text
Grace
```

## Reference API

```basic
ref = REF(value)
typedRef = REF(value, "TypeName")
PRINT ref.Value
ref.Value = nextValue
PRINT ref.Exists()
ref.Clear()
```

`TYPEOF(ref)` returns:

```text
Reference
```

`CLASSOF(ref)` returns:

```text
REF
```

## Cleared references

Clearing a reference invalidates the reference object. It does not destroy the
original value directly:

```basic
playerRef.Clear()
PRINT playerRef.Exists()
PRINT ISNULL(playerRef.Value)
```

Output:

```text
FALSE
TRUE
```

## Typed references

References can enforce runtime type rules:

```basic
score = 10
scoreRef = REF(score, "Number")

scoreRef.Value = 25      ' ok
scoreRef.Value = "bad"   ' runtime error
```

Typed references also allow `NULL` so a value can be filled in later:

```basic
name = NULL
nameRef = REF(name, "String")

PRINT nameRef.Exists()
PRINT ISNULL(nameRef.Value)

nameRef.Value = "Miso"
PRINT name
```

The reference exposes its expected type:

```basic
PRINT nameRef.TypeName
```

Supported type names are the same runtime-checked names used by `AS`:
`String`, `Number`, `Boolean`, `Array`, `Object`, class names, interfaces, and
`Any`.

You can also use the method form:

```basic
scoreRef.Set(30)
```

## Design rules

ArcoBASIC references intentionally avoid old raw pointer behavior:

* no pointer arithmetic
* no arbitrary address access
* no number-to-pointer casts
* no unsafe dereference crashes
* cleared references return `NULL` through `.Value`
* reference operations stay inside the runtime object model

For persistent database references, use ArcoDB pointers:

```basic
customerPtr = ArcoDB.PointerTo(db, customerSchema, customer)
customer = customerPtr.Resolve(db)
```

See [arcodb.md](arcodb.md) for durable object pointers.
