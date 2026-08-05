# ArcoDB

Status: alpha single-file object store

ArcoDB is the persistent object-memory layer above ArcoCompyDB. The current
implementation is intentionally small: one file, explicit schemas, numeric
object IDs, and compact ArcoCompyDB records.

It is not the final paged database yet.

## Basic usage

```basic
#IMPORT "arcodb"

db = ArcoDB.Open("people.arcodb")
schema = ArcoDB.SchemaVersion(db, "Customer", ["customerNumber", "name", "email"], 1)
ArcoDB.Catalog(db, schema, "email")

customer = {
    "customerNumber": 1042,
    "name": "Wanda Goodburger",
    "email": "wanda@email.com"
}

id = ArcoDB.Keep(db, schema, customer)
restored = ArcoDB.Recall(db, schema, id)
byEmail = ArcoDB.RecallBy(db, schema, "email", "wanda@email.com")

restored.email = "wanda.goodburger@example.test"
ArcoDB.Replace(db, schema, id, restored)

ArcoDB.Write(db)
```

`Write` uses a sidecar journal:

```text
people.arcodb.journal
```

The journal is written before the main database file is replaced. If a process
stops after the journal is prepared but before the database file is complete,
`ArcoDB.Open(path)` attempts recovery from the journal.

## API

```basic
ArcoDB.Open(path)
ArcoDB.New(path)
ArcoDB.JournalPath(path)
ArcoDB.RecoverJournal(path)

ArcoDB.Schema(db, className, fields)
ArcoDB.SchemaVersion(db, className, fields, version)
ArcoDB.SchemaFor(db, className)

ArcoDB.Keep(db, schema, value)
pointer = ArcoDB.Pointer(schema, id)
pointer = ArcoDB.PointerTo(db, schema, value)
value = ArcoDB.Resolve(db, pointer)
ArcoDB.PointerExists(db, pointer)
ArcoDB.Recall(db, schema, id)
ArcoDB.Catalog(db, schema, field)
ArcoDB.RecallBy(db, schema, field, value)
ArcoDB.FindByCatalog(db, schema, field, value)
ArcoDB.Replace(db, schema, id, value)
ArcoDB.Forget(db, id)
compacted = ArcoDB.Compact(db)
ArcoDB.PrepareWrite(db)
ArcoDB.Write(db)

ArcoDB.RegisterCommand(db, command AS ARCODBFUNCTION)
ArcoDB.RunCommand(db, name, args = [])
ArcoDB.Command(db, name)
ArcoDB.CommandNames(db)
ArcoDB.Scan(db, schema)

ArcoDB.Count(db)
ArcoDB.RebuildCatalogs(db)
ArcoDB.Inspect(db)
```

## Storage model

The alpha store is an ArcoCompy-packed object:

```text
{
    Format,
    Path,
    NextId,
    Schemas,
    Catalogs,
    Commands,
    Records,
    Dirty
}
```

Each record stores:

```text
Id
Class
Version
Payload
Deleted
```

`Payload` is an `ACDB1` ArcoCompyDB compact record.

## Journal

The alpha journal format is an ArcoCompy-packed object:

```text
{
    Format,
    Target,
    Payload
}
```

`Payload` is the complete packed ArcoDB store. `ArcoDB.Write` does this:

1. write the sidecar journal
2. write the main database file
3. clear the journal to an empty file

Because the current shell file API does not expose delete/rename yet, the
journal is cleared rather than removed.

Catalogs are simple field indexes:

```basic
ArcoDB.Catalog(db, customerSchema, "email")
customer = ArcoDB.RecallBy(db, customerSchema, "email", "wanda@email.com")
```

In the alpha store, a catalog maps:

```text
Class.field + value → object ID
```

Catalogs are persisted with the file and are updated by `Keep`, `Replace`, and
`Forget`. Defining a catalog after records already exist backfills the index.

## Compaction

`Forget` tombstones records so object IDs remain stable during normal use.
`ArcoDB.Compact(db)` removes tombstoned records and rebuilds catalogs:

```basic
PRINT ArcoDB.Forget(db, id)
result = ArcoDB.Compact(db)
PRINT result.Removed
```

The result object contains:

```text
Before
After
Removed
Active
```

Compaction does not renumber surviving object IDs.

## Object pointers

ArcoDB supports safe persistent object pointers through `ARCODBPOINTER`.
Pointers store the target class name and object ID. They are useful for
relationships such as:

```text
Order → Customer
Invoice → Account
Comment → Post
```

Create and resolve a pointer:

```basic
customerPtr = ArcoDB.PointerTo(db, customerSchema, Customer("Wanda"))
orderPtr = ArcoDB.PointerTo(db, orderSchema, Order("ORDER-1001", customerPtr))

order = orderPtr.Resolve(db)
customer = order.Customer.Resolve(db)
```

The safer parser-friendly form is:

```basic
ptr = order.Customer
customer = ptr.Resolve(db)
```

Pointer helpers:

```basic
pointer.Label()              ' Customer#1
pointer.Resolve(db)
pointer.Exists(db)
pointer.Forget(db)
ArcoDB.Resolve(db, pointer)
ArcoDB.PointerExists(db, pointer)
```

Pointers are not raw memory addresses. They are durable ArcoDB object
references that remain stable across `Write`, `Open`, and `Compact` because
compaction does not renumber surviving object IDs.

## Class-backed query commands

ArcoDB can register command objects that extend the `ARCODBFUNCTION` base class.
This lets a domain class expose a query factory such as:

```basic
SHARED FUNCTION whoLogQuery() AS ARCODBFUNCTION
    RETURN WhoLogQuery()
END FUNCTION
```

A command object implements `Execute(db, args)`:

```basic
CLASS WhoLogQuery EXTENDS ARCODBFUNCTION
    CONSTRUCTOR()
        SELF.Name = "who"
        SELF.Description = "who was signed in at a given ISO date/time"
    END CONSTRUCTOR

    FUNCTION Execute(db, args)
        moment = args[0]
        rows = []
        schema = ArcoDB.SchemaFor(db, "SignInLog")
        FOR log IN ArcoDB.Scan(db, schema)
            IF log.SignedInAt == moment THEN ignored = Array.Add(rows, log)
        NEXT
        RETURN {"Ok": TRUE, "Error": "", "Rows": rows}
    END FUNCTION
END CLASS
```

Register and run it:

```basic
ignored = ArcoDB.RegisterCommand(db, SignInLog.whoLogQuery())
result = ArcoDB.RunCommand(db, "who", ["2026-07-13T11:30"])
```

This is intentionally an ArcoBASIC command registry, not SQL. Commands can use
normal ArcoBASIC code, classes, catalogs, and `ArcoDB.Scan`. Future ArcoDB
indexes can make these command bodies faster without changing the public shape.

## Current limitations

* Single-file full rewrite on `ArcoDB.Write`.
* No pages yet.
* Catalogs are in-memory/persisted maps, not paged index structures yet.
* Command objects are ArcoBASIC class objects; the defining class code must be loaded before command methods are run.
* Pointers are durable object references, not native/raw memory pointers.
* Journal recovery is a simple sidecar full-store payload, not a page/transaction journal yet.
* No compression yet.
* IDs are numeric and local to the file.
* Compaction is full-store and immediate, not page-local or incremental.

This module exists to prove the object lifecycle before the paged storage engine:

```basic
KEEP → RECALL → REPLACE → FORGET → WRITE
```

See [storage-architecture.md](storage-architecture.md) for the target design.
