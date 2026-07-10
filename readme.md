# ArcoBASIC

## Universal Embeddable BASIC Runtime & Application Platform

Version: Draft 0.1

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

---

# Project Layout

```text
ArcoBASIC/

    core/
        lexer/
        parser/
        compiler/
        vm/
        runtime/
        object_system/
        modules/

    bindings/
        c/
        cpp/
        csharp/
        python/
        node/
        javascript/
        godot/
        unity/
        unreal/
        java/
        kotlin/
        lua/
        wasm/

    stdlib/
        ArcoMath/
        ArcoString/
        ArcoArray/
        ArcoData/
        ArcoFile/
        ArcoHost/
        ArcoSystem/
        ArcoNet/
        ArcoWeb/
        ArcoCrypt/
        ArcoAudio/
        ArcoImage/
        ArcoUX/
        ArcoTurtle/
        ArcoDebug/
        ArcoTest/
        ArcoPackage/
        ArcoBuild/

    examples/

    docs/
```

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

```basic
CLASS Animal

    FUNCTION Speak()
        RETURN "..."
    END FUNCTION

END CLASS
```

---

# Inheritance

```basic
CLASS Cat EXTENDS Animal

    FUNCTION Speak()
        RETURN "Meow"
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
cat = Cat.NEW()
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
Net.Ping()
Net.ResolveDNS()

TcpClient()
TcpServer()

UdpClient()
UdpServer()

WebSocketClient()
```

Features:

```text
TCP
UDP
HTTP
HTTPS
DNS
WebSockets
File Transfer
Network Discovery
```

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
t = Turtle.NEW()

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
