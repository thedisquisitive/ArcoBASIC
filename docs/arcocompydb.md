# ArcoCompyDB

Status: alpha record layer

ArcoCompyDB is the schema-aware record packing layer that sits between
ArcoCompy and the future ArcoDB page/catalog engine.

It is not a full database yet. It solves one storage problem: when the schema is
known, records should not repeat field names for every object.

## Basic usage

```basic
#IMPORT "compydb"

schema = ArcoCompyDB.SchemaVersion(
    "Customer",
    ["customerNumber", "name", "email"],
    1
)

customer = {
    "customerNumber": 1042,
    "name": "Wanda Goodburger",
    "email": "wanda@email.com"
}

packed = ArcoCompyDB.PackRecord(schema, customer)
result = ArcoCompyDB.TryUnpackRecord(schema, packed)

IF result.Ok THEN
    restored = result.Value
    PRINT restored.name
ELSE
    PRINT result.Error
END IF
```

## Format

ArcoCompyDB records start with:

```text
ACDB1|
```

The current alpha record contains:

1. class name
2. schema version
3. value count
4. packed values in schema field order

For a schema:

```basic
["customerNumber", "name", "email"]
```

the record stores values in that order, without storing the field names beside
each value.

The individual values still use ArcoCompy's value tags, so nested arrays and
objects remain supported.

## API

```basic
ArcoCompyDB.Format()
ArcoCompyDB.Schema(className, fields)
ArcoCompyDB.SchemaVersion(className, fields, version)
ArcoCompyDB.EmptyRecord(schema)

ArcoCompyDB.PackRecord(schema, value)
ArcoCompyDB.TryUnpackRecord(schema, payload)
ArcoCompyDB.TryUnpackRecordWithLimits(schema, payload, maxDepth, maxItems)
ArcoCompyDB.UnpackRecord(schema, payload)

ArcoCompyDB.SaveRecord(path, schema, value)
ArcoCompyDB.LoadRecord(path, schema)
```

## Class-backed records

Unpacked records include:

```basic
__class
__version
```

If the matching class is loaded in the runtime, the restored object can use
class methods because alpha class instances are object-backed.

## Unknown values

When a newer record has extra trailing values that an older schema does not
name, ArcoCompyDB preserves them in:

```basic
record.__unknown_values
```

If the record is packed again, those unknown values are appended back to the
record. This is the first step toward unknown-field preservation for schema
migration.

## Current limitations

* No page store yet.
* No catalogs/indexes yet.
* No object pointer encoding yet.
* Schema definitions are explicit arrays, not generated from class metadata yet.
* Unknown values are preserved positionally, not by field name.

See [storage-architecture.md](storage-architecture.md) for the larger storage
plan.
