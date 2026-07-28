# ArcoCompy

Status: alpha

ArcoCompy is ArcoBASIC's value packing library. Its main job is to serialize
ArcoBASIC values so scripts can save, copy, transmit, and restore structured
state.

It is similar in purpose to serialization or pickling, but intentionally
Arco-flavored:

* regular viewable ArcoBASIC source
* deterministic text format
* length-prefixed strings
* support for nested arrays and objects
* support for class instances because alpha class instances are object-backed

Import it with:

```basic
#IMPORT "compy"
```

## Basic usage

```basic
#IMPORT "compy"

data = {"Name": "Miso", "Level": 7, "Inventory": ["key", "lamp"], "Alive": TRUE}

packed = ArcoCompy.Pack(data)
result = ArcoCompy.TryUnpack(packed)

IF result.Ok THEN
    restored = result.Value
    PRINT restored.Name
    PRINT restored.Inventory[0]
ELSE
    PRINT result.Error
END IF
```

## Saving and loading files

In ArcoSH or another host with file helpers:

```basic
#IMPORT "compy"

profile = {"Prompt": "arcosh> ", "Mods": ["arcogotchi"]}

ArcoCompy.Save("profile.acpy", profile)
loaded = ArcoCompy.Load("profile.acpy")

PRINT loaded.Prompt
```

`ArcoCompy.Unpack(payload)` remains the short form. It returns the restored
value when the payload is valid and `NULL` when it is not.

Use `ArcoCompy.TryUnpack(payload)` when a script needs to distinguish a real
packed `NULL` from damaged input:

```basic
result = ArcoCompy.TryUnpack(payload)
IF result.Ok THEN
    PRINT result.Value
ELSE
    PRINT "Damaged save: " + result.Error
END IF
```

Use `ArcoCompy.TryUnpackWithLimits(payload, maxDepth, maxItems)` for stricter
loading in tools that accept untrusted input.

## Packing class objects

Alpha class instances are object-backed values marked with runtime class
metadata. ArcoCompy preserves the object fields, including the class marker.
If the class definition is loaded before unpacking, the restored object can
still use class methods.

```basic
#IMPORT "compy"

CLASS SaveSlot
    Name AS String = ""
    Level AS Number = 1

    CONSTRUCTOR(name AS String, level AS Number)
        SELF.Name = name
        SELF.Level = level
    END CONSTRUCTOR

    FUNCTION Label() AS String
        RETURN SELF.Name + "@" + STRING(SELF.Level)
    END FUNCTION
END CLASS

slot = SaveSlot("Miso", 7)
packed = ArcoCompy.Pack(slot)
restored = ArcoCompy.Unpack(packed)

PRINT restored.Label()
PRINT ISA(restored, "SaveSlot")
```

When a packed class instance is nested inside another object, assign it to a
variable before calling methods in the current alpha parser:

```basic
restored_player = restored.Player
PRINT restored_player.Label()
```

## Supported values

ArcoCompy currently supports:

* `NULL`
* booleans
* numbers
* strings
* arrays
* objects
* alpha class instances, as object-backed values

## Format

ArcoCompy data starts with:

```text
ACPY1|
```

After that, each value is encoded with a compact tag:

```text
Z                 NULL
T                 TRUE
F                 FALSE
N<number>;        Number
S<length>:<text>  String
A<count>:...      Array
O<count>:...      Object key/value pairs
```

Strings are length-prefixed instead of backslash-escaped, so arbitrary text is
safer to store.

## Safety checks

The alpha unpacker now validates:

* the `ACPY1|` header
* unexpected end-of-payload while reading tags
* string lengths against the remaining payload
* negative lengths
* array and object counts against a caller-selectable item limit
* maximum nesting depth
* object keys being encoded as strings
* unknown value tags
* trailing data after a valid packed value

These checks are intentionally in the ArcoBASIC stdlib implementation so the
format remains inspectable.

## Current alpha limitations

* Cyclic object graphs are not supported.
* Object identity is not preserved; shared references are restored as ordinary
  copied values.
* Private/protected class fields are still object fields and may be packed.
* The format is text-based, not compressed binary storage yet.
* Integrity hashes/checksums are not part of `ACPY1` yet.

Future versions can add binary blocks, checksums, class-version metadata, and
object identity while keeping `ACPY1` readable for alpha users.

See [storage-architecture.md](storage-architecture.md) for the planned
ArcoCompyDB, ArcoDB, and ArcoCompress layering.
