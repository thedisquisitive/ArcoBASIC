# ArcoCompy, ArcoCompyDB, and ArcoDB Storage Architecture

Status: design direction with ArcoCompy alpha hardening, the first ArcoCompyDB
record layer, and an alpha single-file ArcoDB object store started.

ArcoBASIC storage is intended to grow in layers:

```text
Application Objects
        │
        ▼
   ArcoCompy
        │
        ▼
  ArcoCompyDB
        │
        ▼
     ArcoDB
        │
        ▼
 ArcoCompress
        │
        ▼
       Disk
```

Each layer has one job.

## ArcoCompy

ArcoCompy is the portable object serializer.

It is responsible for:

* primitive values
* dynamic arrays
* objects/maps
* nested object structures
* object-backed class instances
* readable exchange files

Current format:

```text
ACPY1|...
```

`ACPY1` prioritizes readability, debugging, portability, and implementation in
plain ArcoBASIC over storage efficiency.

Current hardening:

* explicit `TryUnpack` result objects
* header validation
* string bounds validation
* array/object count limits
* maximum nesting depth
* trailing-data rejection
* object-key validation

Planned ArcoCompy improvements:

* `ACPY2` integrity metadata such as CRC32 or hash fields
* class version metadata
* unknown-field preservation policy
* binary data blocks for byte arrays, images, sound, and memory blocks
* durable object identifiers for future database pointers
* cycle/shared-reference handling

ArcoCompy should not become a compression algorithm.

## ArcoCompyDB

ArcoCompyDB is the schema-aware storage serializer for database records.

Unlike ArcoCompy, it can assume that the schema and class definition are known.
That lets it store values without repeating field names every time.

Example class:

```basic
CLASS Customer
    customerNumber AS Integer
    name AS String
    email AS String
END CLASS
```

ArcoCompy-style storage is field-name rich. ArcoCompyDB-style storage maps the
fields once:

```text
Customer
    0 = customerNumber
    1 = name
    2 = email
```

Records can then store only the values:

```text
1042|Wanda|wanda@email.com
```

Current alpha implementation:

* explicit schema objects
* `ACDB1` compact record payloads
* values stored in field order
* class name and schema version stored once per record
* `TryUnpackRecord` error objects
* trailing unknown value preservation through `__unknown_values`

Responsibilities:

* compact schema-aware record encoding
* class field mapping
* persistent object IDs
* pointer encoding by object ID rather than embedded object copy
* migration hooks for class versions

## ArcoDB

ArcoDB is the object database layer.

It should feel like persistent object memory, not SQL with BASIC punctuation.

Target lifecycle:

```basic
KEEP customer
RECALL customer BY "wanda@email.com"
customer.name = "Wanda Goodburger"
WRITE store
FORGET customer
```

Responsibilities:

* database files
* pages
* catalogs/indexes
* cache policy
* journals
* object lifecycle
* future replication
* future network synchronization

Current alpha implementation:

* single-file store
* explicit schemas
* numeric object IDs
* `KEEP`, `RECALL`, `REPLACE`, `FORGET`, and `WRITE` as library calls
* alpha catalogs through `ArcoDB.Catalog` and `ArcoDB.RecallBy`
* sidecar journal recovery for interrupted full-file writes
* full-store compaction that removes tombstones and rebuilds catalogs
* full-file rewrite on write
* records stored as ArcoCompyDB `ACDB1` payloads

## Pages

ArcoDB should store objects in pages so large databases do not load entirely
into RAM.

Candidate page sizes:

```text
16 KB
32 KB
64 KB
128 KB
```

A lookup should:

1. query a catalog
2. find an object ID
3. locate the page containing that object
4. load only that page
5. unpack only needed objects

## Catalogs

Catalogs are separate lookup structures:

```basic
CATALOG BY email, customerNumber
```

Conceptually:

```text
wanda@email.com → Object 91A4F220
1042            → Object 91A4F220
```

Catalogs allow `RECALL` without scanning every stored object.

## ArcoCompress

ArcoCompress performs actual compression.

It should handle:

* compressed pages
* compressed backups
* compressed exports
* compressed replication traffic

Compression belongs below ArcoDB and ArcoCompyDB so the upper layers stay
explainable.

## Profiles

Future storage profiles can tune behavior without changing application code:

```basic
PROFILE DESKTOP
PROFILE SERVER
PROFILE PORTABLE
PROFILE EMBEDDED
PROFILE AUDIT
```

Profiles may control:

* page size
* cache size
* compression settings
* journal behavior
* history retention
* durability guarantees

## Implementation phases

1. Harden ArcoCompy `ACPY1`.
2. Add object IDs and class version metadata.
3. Add ArcoCompyDB schema maps and compact records. Started with `ACDB1`.
4. Add a single-file ArcoDB object store. Started without pages.
5. Add catalogs. Started with persisted single-file catalog maps.
6. Add journals and crash recovery. Started with a full-store sidecar journal.
7. Add compaction. Started with full-store tombstone removal.
8. Add ArcoCompress.
9. Add replication/export/import.

The machine should always be able to explain where an object lives, why a page
was loaded, and which catalog found it.
