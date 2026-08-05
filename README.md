# ArcoBASIC

## Universal Embeddable BASIC Runtime & Application Platform

Version: Draft 0.1

This repository is the umbrella workspace for the full Arcology project. It contains the
ArcoBASIC language and tools, the `arcology-os/` systems layer, the standalone
`arcology-commons/` social network, and the `lazarus/` recovery appliance. Each component keeps a
clear boundary, but all four are first-class parts of Arcology.

---

# Philosophy

ArcoBASIC is a modern embeddable programming language inspired by classic BASIC dialects while embracing modern software development.

The goals of ArcoBASIC are:

* Easy to learn
* Easy to embed
* Easy to sandbox
* Easy to extend
* Cross-platform
* Suitable for education
* Suitable for professional tools
* Suitable for games
* Suitable for automation
* Suitable for systems programming

ArcoBASIC should feel familiar to users of:

* GW-BASIC
* QBASIC
* QuickBASIC
* Visual Basic
* FreeBASIC
* MSWLogo
* Python
* Lua

while remaining lightweight enough to embed into:

* Desktop applications
* Mobile applications
* Game engines
* Plugins
* Operating systems
* Drivers
* Embedded systems
* Web applications

---

# Architecture

```text
Applications
    │
    ▼

Bindings
 ├─ C++
 ├─ C
 ├─ C#
 ├─ Python
 ├─ JavaScript
 ├─ Node.js
 ├─ Godot
 ├─ Unity
 ├─ Unreal
 ├─ Java/Kotlin
 ├─ Lua
 └─ WebAssembly

    │
    ▼

Arco Runtime API

    │
    ▼

Arco VM
 ├─ Compiler
 ├─ Parser
 ├─ Lexer
 ├─ Bytecode Engine
 ├─ Object System
 ├─ Module Loader
 ├─ Debugger
 └─ Security Layer

    │
    ▼

Arco Standard Library
```

# Building

## Linux / macOS

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Build a Debian package and run the interactive installer:

```sh
scripts/build/build-deb.sh
scripts/install/install-deb-wizard.sh --latest
```

The installer installs the `.deb`, runs the doctor, initializes the ArcoSH
profile, offers prompt presets with live preview, can activate built-in mods
such as ArcoGotchi, and can configure ArcoSH as the login shell.

Build Linux packages for broader distro testing:

```sh
scripts/build/build-linux-packages.sh --all
```

This emits:

* `.deb` for Debian, Ubuntu, Mint, Pop!_OS, and related distros.
* `.rpm` for Fedora, RHEL, openSUSE, Mageia, and related distros when
  `rpmbuild` is installed.
* `-xbps-src.tar.gz` with a Void Linux `xbps-src` template.
* `.tar.gz` portable install tree for unknown or unsupported distros.

If you only need the portable fallback:

```sh
scripts/build/build-linux-packages.sh --tar
```

## ArcoFission Runtime Capsules

ArcoFission can reveal compiler stages, write `.arcof-text` bytecode, run that
bytecode, and build a Linux ELF64 runtime capsule from an ArcoBASIC source
file:

```sh
ArcoFission build examples/hello.bas -o hello
./hello
```

This is the default compiler model for alpha: native executable outside,
ArcoFission bytecode VM inside. The generated ELF64 embeds the prepared bytecode
and links it against the ArcoFission runtime from the active CMake build tree,
which keeps behavior aligned with `ArcoFission compile-run` and gives the
project a portable path for future Windows/macOS capsules.

Use `.arcof` output or the explicit `bytecode` command when you want the
intermediate bytecode artifact:

```sh
ArcoFission bytecode examples/hello.bas -o hello.arcof
ArcoFission run hello.arcof
```

For a release Void Linux `.xbps`, build inside Void's `xbps-src` environment:

```sh
scripts/build/build-void-native-package.sh --void-packages /path/to/void-packages --out dist/void
```

That produces `dist/void/arcobasic-<version>_1.<arch>.xbps` and repository
metadata. Testers can install from the folder with:

```sh
sudo xbps-install --repository=/path/to/dist/void arcobasic
```

Void packages should not be created by wrapping a host-built install tree with
`xbps-create`; that can miss or mismatch runtime libraries such as GLFW and
libcurl. The native `xbps-src` path records the correct Void dependencies.

## UEFI Systems Target (x86-64)

ArcoFission can also compile a restricted "freestanding" subset of ArcoBASIC
directly into a bootable UEFI application (PE32+), with no C, C++, or
handwritten assembly involved at any point:

```sh
ArcoFission build arcology-os/tests/fixtures/uefi-hello/hello.abas -o hello.efi --target uefi-x86_64
arcology-os/scripts/run/run-uefi-hello.sh hello.efi "Hello from ArcoBASIC"
```

See [`arcology-os/docs/systems/README.md`](arcology-os/docs/systems/README.md) for prerequisites, the
full directive/type/binding surface this target supports, troubleshooting,
and an architecture overview of the pipeline (lexer -> parser -> A-MIR ->
x86-64 machine code -> PE32+ image).

## Windows

From Windows Terminal or PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\build\build-windows.ps1 -RunTests
```

The script installs/checks CMake, Ninja, and Visual Studio Build Tools through
`winget`, then builds `arcosh.exe` and `arco_cli.exe`. Use
`-SkipDependencyInstall` on machines that already have the toolchain installed.

---

# Project Layout

```text
apps/                 Executable entry points: arco, arcosh, and ArcoFission
src/                  Private C++ implementation, grouped by subsystem
  frontend/           Lexer, parser, canonical AST, and internal headers
  runtime/            Hosted interpreter/runtime
  compiler/           Shared A-MIR, bytecode, native capsule, and systems integration
  shell/              ArcoSH implementation
  gui/                Real and stub GUI backends
  bindings/           Binding implementations
include/              Public embedding API and compatibility headers
cmake/                Dependency, test, and installation definitions
tests/                Generic ArcoBASIC unit, integration, and fixture coverage
stdlib/               Importable ArcoBASIC standard-library modules
examples/             Runnable example programs
tutorials/            Guided ArcoSH programs
scripts/              Generic build, install, run, and ArcoSH tools
docs/                 Generic ArcoBASIC user and developer documentation
arcology-os/          Arcology OS systems library, UEFI examples, tests, tooling, RFCs, and docs
arcology-commons/     Standalone Arcology Commons social network written in ArcoBASIC
lazarus/              Arcology Lazarus recovery appliance and live environment
```

See [`docs/project-layout.md`](docs/project-layout.md) for library boundaries, dependency rules,
and guidance for placing new files.

---

# Syntax Modes

## Classic BASIC Mode

```basic
10 PRINT "HELLO"
20 GOTO 10
```

## Modern Mode

```basic
PRINT "HELLO"

WHILE TRUE
    PRINT "HELLO"
WEND
```

Both compile into the same AST and bytecode.

---

# Variables

```basic
x = 5
name = "Daedalus"
alive = TRUE
```

Optional:

```basic
LET x = 5
```

---

# Types

```text
Null
Boolean
Number
String
Array
Object
Function
Class Instance
Host Object
```

---

# Arrays

```basic
numbers = [1,2,3,4]
```

---

# Objects

```basic
person = {
    "name": "Zach",
    "age": 37
}
```

---

# Conditionals

```basic
IF x > 10 THEN
    PRINT "BIG"
ELSE
    PRINT "SMALL"
END IF
```

---

# Loops

## FOR

```basic
FOR i = 1 TO 10
    PRINT i
NEXT
```

## FOR EACH

```basic
FOR item IN items
    PRINT item
NEXT
```

## WHILE

```basic
WHILE TRUE
    PRINT "LOOP"
WEND
```

---

# Functions

```basic
FUNCTION Add(a,b)
    RETURN a + b
END FUNCTION
```

---

# Classes

See [docs/classes.md](docs/classes.md) for the full alpha class guide.

```basic
CLASS Animal
    Name AS String = "unknown"

    CONSTRUCTOR(name AS String)
        SELF.Name = name
    END CONSTRUCTOR

    FUNCTION Speak() AS String
        RETURN SELF.Name + " makes a sound"
    END FUNCTION

END CLASS
```

---

# Inheritance

```basic
CLASS Cat EXTENDS Animal

    FUNCTION Speak() AS String
        RETURN SUPER.Speak() + " and meows"
    END FUNCTION

END CLASS
```

---

# Polymorphism

```basic
FUNCTION MakeNoise(a AS Animal)
    PRINT a.Speak()
END FUNCTION
```

---

# Object Creation

```basic
cat = Cat("Miso")
```

---

# Self

```basic
SELF.Name
```

---

# Super

```basic
SUPER.Speak()
```

---

# Modules

```basic
IMPORT ArcoMath
IMPORT ArcoNet
```

---

# Error Handling

```basic
TRY

    RiskyThing()

CATCH err

    PRINT err.Message

END TRY
```

---

# Async Support

```basic
AWAIT WaitSeconds(1)
```

Future feature.

---

# Runtime Limits

```text
Instruction Limits
Memory Limits
Execution Timeouts
Interrupt Requests
Module Restrictions
Capability Restrictions
```

---

# Embedding API

## C API

```c
ArcoRuntime* arco_create_runtime();
void arco_destroy_runtime();

int arco_run_string(
    ArcoRuntime* runtime,
    const char* code
);
```

---

# Host Integration

## Register Function

```cpp
runtime.register_function(
    "print",
    callback
);
```

---

# Host Objects

```cpp
runtime.register_object(
    "player",
    playerObject
);
```

---

# Global Variables

```cpp
runtime.set_global("AppName","Arco");
```

---

# Security Model

Every runtime starts sandboxed.

Capabilities are granted explicitly.

```text
filesystem.read
filesystem.write
network.client
network.server
printer.access
process.control
service.control
webview
raw_disk
raw_memory
kernel_access
driver_access
```

---

# Standard Library

---

# ArcoMath

Mathematics

```basic
Math.Sin()
Math.Cos()
Math.Sqrt()
Math.Pow()
Math.Clamp()
Math.Lerp()
Math.Random()
```

---

# ArcoString

String utilities

```basic
Upper()
Lower()
Trim()
Split()
Replace()
Contains()
```

---

# ArcoArray

Array helpers

```basic
Push()
Pop()
Sort()
Reverse()
Find()
Filter()
Map()
```

---

# ArcoFile

Filesystem abstraction

```basic
File.Read()
File.Write()
File.Copy()
File.Move()
File.Delete()
```

---

# ArcoData

Data serialization

Supported formats:

```text
JSON
CSV
XML
INI
YAML
Binary
```

---

# ArcoHost

Host operating system access

```basic
Host.OSName()
Host.OSVersion()
Host.Processes()
Host.Services()
Host.Printers()
```

Capabilities:

```text
System Info
Task Management
Printer Access
Service Management
Environment Variables
```

---

# ArcoSystem

Low-level systems programming

Target Uses:

```text
Bootloaders
Drivers
Kernels
Firmware
Disk Utilities
Memory Utilities
Operating Systems
```

Features:

```basic
Memory.Read()
Memory.Write()

Disk.ReadSector()
Disk.WriteSector()

Port.In()
Port.Out()
```

Capabilities:

```text
Raw Memory
Raw Disk
Kernel Access
Driver Access
```

---

# ArcoNet

Networking

```basic
IF Network.Available() THEN
    response = Network.Get("https://example.com")
    IF response.Ok THEN PRINT response.Body
END IF

response = Network.Post("https://example.com/api", "{\"ok\":true}", "application/json")
download = Network.Download("https://example.com/file.txt", "file.txt")

PRINT Network.UrlEncode("arco basic")
dns = Net.ResolveDNS("example.com")
FOR address IN dns.Addresses
    PRINT address
NEXT

client = Net.TcpConnect("example.com", 80)
IF client.Ok THEN
    Net.TcpSend(client.Client, "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n")
    chunk = Net.TcpRead(client.Client, 4096)
    PRINT chunk.Data
    Net.TcpClose(client.Client)
END IF

Net.Ping()

TcpClient()
TcpServer()

UdpClient()
UdpServer()

WebSocketClient()
```

Features:

```text
HTTP
HTTPS
TCP
UDP
DNS
WebSockets
File Transfer
Network Discovery
```

Current implementation: `Network.Available`, `Network.Get`, `Network.Post`,
`Network.Download`, URL encoding helpers, query-string construction, and
`Network.ResolveDNS` / `Net.ResolveDNS`, and synchronous TCP client helpers
`Net.TcpConnect`, `Net.TcpSend`, `Net.TcpRead`, and `Net.TcpClose`. HTTP
helpers use libcurl when available. UDP, ping, TCP servers, and WebSockets are
planned.

Networking utility scripts:

```sh
arcosh examples/network_probe.abas
arcosh examples/network_fetch.abas https://example.com
arcosh examples/network_download.abas https://example.com out.html
arcosh examples/network_post.abas https://example.com/api '{"ok":true}'
arcosh examples/network_dns.abas localhost
arcosh examples/network_tcp_probe.abas example.com 80 "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n"
```

`network_probe.abas` is safe to run offline unless you pass a URL as its second
argument.

---

# ArcoNav

Terminal Web Browser

```sh
arcosh examples/arconav.abas https://example.com
arcosh examples/arconav.abas https://example.com --dump
```

ArcoNav is a lightweight ArcoBASIC browser for terminal sessions. It fetches
pages with `Network.Get`, renders readable text, extracts links, supports
numbered link navigation, `g URL`, `b` back, `r` reload, and `q` quit.

---

# ArcoSH Mods

User-space Shell Mods

```basic
Mod.Install("my-mod.abas", "my-mod")
Mod.Activate("my-mod")
FOR mod IN Mod.List()
    PRINT mod.Name + " active=" + STRING(mod.Active)
NEXT
Mod.Deactivate("my-mod")
```

Mods install into `~/.arcosh/mods`. Active mod names are saved in
`~/.arcosh/mods/enabled.txt`, so they load again on the next ArcoSH startup.
Use `Mod.Load(name)` to load an installed mod immediately in the current
session.

There is also a small ArcoBASIC manager utility:

```sh
arcosh examples/arcosh_mods.abas list
arcosh examples/arcosh_mods.abas install path/to/mod.abas my-mod
arcosh examples/arcosh_mods.abas activate my-mod
arcosh examples/arcosh_mods.abas deactivate my-mod
```

Built-in shipped mods can be installed by name:

```sh
arcosh examples/arcosh_mods.abas install-builtin arcogotchi
arcosh examples/arcosh_mods.abas activate arcogotchi
```

ArcoGotchi adds a small terminal pet to ArcoSH. After activation and the next
startup, use `gotchi`, `gotchi-feed`, `gotchi-play`, `gotchi-nap`, and
`gotchi-rename NAME`.

---

# ArcoCompy

Value packing and restoration

```basic
#IMPORT "compy"

data = {"Name": "Ada", "Level": 7, "Inventory": ["key", "lamp"]}
packed = ArcoCompy.Pack(data)
restored = ArcoCompy.Unpack(packed)

PRINT restored.Name
```

ArcoCompy is the alpha serialization library for ArcoBASIC values. It packs
`NULL`, booleans, numbers, strings, arrays, objects, and object-backed class
instances into the `ACPY1` text format. Use `ArcoCompy.TryUnpack()` when loading
untrusted or user-editable data so corrupted payloads produce an error object
instead of surprising script flow. See [docs/arcocompy.md](docs/arcocompy.md)
and [docs/storage-architecture.md](docs/storage-architecture.md).

ArcoCompyDB is the first database-oriented layer. It packs records by schema
field order instead of repeating field names in every stored object:

```basic
#IMPORT "compydb"

schema = ArcoCompyDB.Schema("Customer", ["customerNumber", "name", "email"])
packed = ArcoCompyDB.PackRecord(schema, {
    "customerNumber": 1042,
    "name": "Wanda",
    "email": "wanda@email.com"
})
```

See [docs/arcocompydb.md](docs/arcocompydb.md).

ArcoDB is the first persistent object-memory layer. The alpha version is a
single-file store with explicit schemas, numeric object IDs, and compact
ArcoCompyDB record payloads:

```basic
#IMPORT "arcodb"

db = ArcoDB.Open("people.arcodb")
schema = ArcoDB.Schema(db, "Customer", ["customerNumber", "name", "email"])
ArcoDB.Catalog(db, schema, "email")
id = ArcoDB.Keep(db, schema, customer)
restored = ArcoDB.Recall(db, schema, id)
byEmail = ArcoDB.RecallBy(db, schema, "email", customer.email)
ArcoDB.Write(db)
```

ArcoDB also supports class-backed command objects for domain-specific queries.
A class can expose a factory such as `whoLogQuery() AS ARCODBFUNCTION`, register
it with `ArcoDB.RegisterCommand`, and run it with `ArcoDB.RunCommand`.
ArcoDB also has durable object pointers through `ARCODBPOINTER`, useful for
relationships like `Order -> Customer`. See `examples/arcodb_commands.abas` and
`examples/arcodb_pointers.abas`.

See [docs/arcodb.md](docs/arcodb.md).

---

# Commons Framework

`#IMPORT "commons"` provides the first framework layer for community-style
applications: request/response records, route matching, validation results,
explainable feed items, moderation reports/actions, and audit entries.

```basic
#IMPORT "commons"

router = Commons.Router()
router = Commons.AddRoute(router, "GET", "/communities/:id", "ShowCommunity", "Community page")
match = Commons.MatchRoute(router, "GET", "/communities/photo")
PRINT match.Params.id
```

See [arcology-commons/docs/framework.md](arcology-commons/docs/framework.md) and
`arcology-commons/examples/commons_arcology_seed.abas`.

---

# Arcology Commons v0.1a

`#IMPORT "arcology"` starts the standalone Arcology Commons social network's domain layer on top
of `commons` and `arcodb`. Arcology Commons is written in ArcoBASIC and is not part of Arcology OS.

```basic
#IMPORT "arcology"

app = Arcology.Open("arcology-commons/var/local/arcology-v01a.arcodb")
ignored = Arcology.CreateUser(app, "ada", "Ada Lovelace")
ignored = Arcology.CreateCommunity(app, "photography", "Photography")
ignored = Arcology.JoinCommunity(app, "ada", "photography")
post = Arcology.Post(app, "photography", "ada", "Sunset walk", "Meet at the library")

feed = Arcology.FeedForUser(app, "ada")
FOR item IN feed.Items
    PRINT item.Title + " -- " + item.Reason
NEXT
```

See [arcology-commons/docs/README.md](arcology-commons/docs/README.md) and
`arcology-commons/examples/arcology_v01a.abas`.

Static HTML export is available before the live web server exists:

```sh
ARCOBASIC_STDLIB=arcology-commons/stdlib \
  arcosh arcology-commons/examples/arcology_export_site.abas \
  arcology-commons/var/local/arcology-v01a.arcodb arcology-commons/dist/commons
```

Serve that static UI from ArcoBASIC:

```sh
arcology-commons/scripts/run/serve-arcology.sh
```

Or choose paths and a port explicitly:

```sh
ARCOBASIC_STDLIB=arcology-commons/stdlib \
  arcosh arcology-commons/examples/arcology_serve_static.abas \
  arcology-commons/var/local/arcology-v01a.arcodb arcology-commons/dist/commons 8080
```

---

# Safe References

ArcoBASIC uses safe references instead of raw C/C++ pointers:

```basic
playerRef = REF(player)
playerRef.Value.Name = "Grace"

scoreRef = REF(score)
scoreRef.Value = 25
```

References expose `.Value`, `.Exists()`, and `.Clear()` while staying inside
the runtime object model. There is no pointer arithmetic or arbitrary memory
access. See [docs/references.md](docs/references.md) and
`examples/safe_refs.abas`.

## ArcoMart demo

`examples/arcomart.abas` is a small persistent storefront simulation built in
ArcoBASIC:

```sh
arcosh --safe examples/arcomart.abas arcomart.arcodb
```

It supports adding/editing/removing products, receiving stock, customer
checkout, low-stock reporting, sales reporting, saving, and compacting the
underlying ArcoDB file.

---

# ArcoWeb

Web Development

## Server

```basic
server = Web.Server()
```

## Routing

```basic
server.Get("/")
```

## Web UI

```basic
view = WebView()
```

Features:

```text
HTTP Server
REST APIs
WebSockets
Templates
Embedded WebViews
```

---

# ArcoCrypt

Cryptography

Features:

```text
SHA256
SHA512
BLAKE3

AES-GCM
ChaCha20

Argon2

Ed25519
X25519

Secure Random
```

---

# ArcoAudio

Audio Processing

Features:

```text
MIDI
Sample Playback
Wave Files
Recording
Streaming
Synthesizers
DSP Utilities
```

Potential Future:

```text
VST Hosting
Plugin Development
```

---

# ArcoImage

Image Processing

Features:

```text
PNG
JPEG
WEBP
BMP
GIF

Drawing
Resizing
Filters
Sprites
```

---

# ArcoUX

Cross-platform GUI Framework

Backends:

```text
Native
Qt
ImGui
Godot
Unity
Unreal
Web
```

Example:

```basic
window = UX.Window("Hello")

button = UX.Button("Click")

window.Add(button)

window.Show()
```

---

# ArcoTurtle

MSWLogo-inspired turtle graphics

Example:

```basic
FD 100
RT 90
FD 100
```

Modern API:

```basic
t = Turtle()

t.Forward(100)
t.Right(90)
```

Features:

```text
Lines
Shapes
Colors
Animation
Education
```

---

# ArcoDebug

Debugger Integration

Features:

```text
Breakpoints
Watch Variables
Tracing
Call Stacks
Memory Inspection
Profiling
```

---

# ArcoTest

Testing Framework

Example:

```basic
TEST "Addition"

ASSERT Add(2,2) = 4

END TEST
```

Features:

```text
Unit Tests
Assertions
Benchmarks
Coverage
```

---

# ArcoPackage

Package Manager

```basic
PACKAGE.INSTALL("ArcoNet")
```

Features:

```text
Repositories
Dependencies
Versioning
Publishing
```

---

# ArcoBuild

Build System

Features:

```text
Script Compilation
Bytecode Packaging
Application Packaging
Resource Bundles
Cross Compilation
Deployment
```

---

# Supported Targets

## Desktop

```text
Windows
Linux
macOS
```

## Mobile

```text
Android
iOS
```

## Web

```text
WebAssembly
```

## Game Engines

```text
Godot
Unity
Unreal
```

## Embedded

```text
Microcontrollers
Custom Operating Systems
Firmware
```

---

# Official Bindings

```text
C
C++
C#
Python
JavaScript
Node.js
Godot
Unity
Unreal
Java
Kotlin
Lua
WebAssembly
```

---

# Long-Term Vision

ArcoBASIC becomes:

* A scripting language
* An educational language
* A systems language
* An automation language
* A game scripting language
* A rapid application language
* An embeddable runtime platform

while remaining recognizable as a BASIC dialect and preserving the spirit of:

```text
Type code.
Run code.
Build cool things.
```
