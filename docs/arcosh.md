# ArcoSH

## The ArcoBASIC Shell

Version: Draft 0.1

---

# Overview

ArcoSH is a modern command shell built on top of the ArcoBASIC runtime.

Its goal is to provide the power of:

* Bash
* PowerShell
* CMD
* Python scripting

while retaining the readability and approachability of BASIC.

ArcoSH is not intended to merely execute commands.

ArcoSH is intended to make system administration feel like programming instead of string manipulation.

---

# Design Goals

## Human Readable

Traditional shell scripts often become difficult to read:

```bash
if [ -f "$FILE" ]; then
    grep "test" "$FILE" | awk '{print $1}'
fi
```

ArcoSH:

```basic
IF File.Exists(file) THEN

    results = File.ReadText(file)

    IF results CONTAINS "test" THEN
        PRINT results
    END IF

END IF
```

---

## Cross Platform

One script should run on:

```text
Windows
Linux
Future BSD Support
```

without modification whenever possible.

---

## Object-Oriented Administration

Traditional shells pass text.

ArcoSH passes objects.

Instead of:

```bash
ps aux
```

ArcoSH:

```basic
FOR proc IN Host.Processes()

    PRINT proc.Name
    PRINT proc.MemoryMB

NEXT
```

---

## Safe By Default

Dangerous operations require:

```basic
REQUIRE raw_disk
REQUIRE process_control
REQUIRE service_control
```

before execution.

---

# Architecture

```text
ArcoSH

    │

    ▼

Interactive Shell

    │

    ▼

ArcoBASIC Runtime

    │

    ▼

Built-In Modules

    ├─ ArcoHost
    ├─ ArcoFile
    ├─ ArcoNet
    ├─ ArcoSystem
    ├─ ArcoData
    ├─ ArcoUX

    │

    ▼

Operating System
```

---

# Shell Modes

## Interactive

```text
C:\>
```

or

```text
user@machine>
```

Interactive command execution.

---

## Script

```text
script.arcsh
```

Execute and exit.

---

## REPL

Interactive BASIC interpreter.

```basic
PRINT "Hello"
```

Output:

```text
Hello
```

---

# File Extensions

## Standard Script

```text
.arc
```

Pure ArcoBASIC.

---

## Shell Script

```text
.arcsh
```

ArcoSH enhanced script.

---

# Linux Shebang Support

```basic
#!/usr/bin/env arcosh

PRINT "Hello World"
```

---

# Windows Support

```powershell
arcosh script.arcsh
```

File association should allow:

```text
Double Click
```

execution.

---

# Variables

```basic
name = "Daedalus"

PRINT name
```

---

# Environment Variables

```basic
PRINT ENV("PATH")

ENV("TEMP") = "C:\Temp"
```

---

# Conditionals

```basic
IF Host.IsWindows() THEN

    PRINT "Windows"

ELSE

    PRINT "Linux"

END IF
```

---

# Loops

```basic
FOR proc IN Host.Processes()

    PRINT proc.Name

NEXT
```

---

# Functions

```basic
FUNCTION Backup()

    PRINT "Backing up"

END FUNCTION
```

---

# Classes

```basic
CLASS BackupJob

    Source
    Destination

END CLASS
```

---

# Running Commands

## Simple

```basic
RUN "dir"
```

---

## Capture Output

```basic
result = RUN("ipconfig")

PRINT result.Output
```

---

## Exit Code

```basic
result = RUN("ping google.com")

PRINT result.ExitCode
```

---

## Standard Error

```basic
PRINT result.Error
```

---

# Pipelines

Traditional:

```bash
ipconfig | findstr IPv4
```

ArcoSH:

```basic
RUN "ipconfig" | FIND "IPv4"
```

or

```basic
result =
    RUN("ipconfig")
        .Filter("IPv4")
```

---

# Aliases

```basic
ALIAS ll = "dir"
```

Linux:

```basic
ALIAS ll = "ls -al"
```

---

# Profiles

Executed on startup:

```text
~/.arcosh/profile.arcsh
```

Windows:

```text
%USERPROFILE%\.arcosh\profile.arcsh
```

---

# Command History

Features:

```text
Persistent History
Search History
History Expansion
History Export
```

---

# Auto Completion

Support:

```text
Commands
Files
Folders
Services
Processes
Variables
Functions
Modules
```

---

# Built-In Commands

## Help

```basic
HELP
```

---

## Version

```basic
VERSION
```

---

## Clear Screen

```basic
CLS
```

---

## Exit

```basic
EXIT
```

---

# ArcoHost Integration

## Operating System

```basic
PRINT Host.OSName()

PRINT Host.OSVersion()
```

---

## Processes

```basic
FOR proc IN Host.Processes()

    PRINT proc.Name

NEXT
```

---

## Services

```basic
service = Host.Service("nginx")

service.Restart()
```

---

## Hardware

```basic
PRINT Host.CPU()
PRINT Host.Memory()
PRINT Host.SerialNumber()
```

---

## Printers

```basic
FOR printer IN Host.Printers()

    PRINT printer.Name

NEXT
```

---

# ArcoFile Integration

## Copy

```basic
File.Copy(
    source,
    destination
)
```

---

## Move

```basic
File.Move()
```

---

## Delete

```basic
File.Delete()
```

---

## Search

```basic
files =
    File.Find(
        "*.txt"
    )
```

---

# ArcoNet Integration

## Ping

```basic
PRINT Net.Ping("1.1.1.1")
```

---

## DNS

```basic
PRINT Net.ResolveDNS(
    "google.com"
)
```

---

## HTTP

```basic
response =
    Net.HttpGet(
        "https://example.com"
    )
```

---

# ArcoSystem Integration

## Disks

```basic
FOR disk IN System.Disks()

    PRINT disk.Name

NEXT
```

---

## Partitions

```basic
FOR part IN disk.Partitions()

NEXT
```

---

## Memory

```basic
System.MemoryUsage()
```

---

## Services

```basic
System.RestartService(
    "nginx"
)
```

---

# ArcoData Integration

## JSON

```basic
obj =
    Json.Parse(
        text
    )
```

---

## CSV

```basic
table =
    Csv.Load(
        file
    )
```

---

# Administration Examples

## Restart Nginx

```basic
service =
    Host.Service("nginx")

IF service.Exists THEN

    service.Restart()

ELSE

    PRINT "Not Found"

END IF
```

---

## List Large Processes

```basic
FOR proc IN Host.Processes()

    IF proc.MemoryMB > 500 THEN

        PRINT proc.Name

    END IF

NEXT
```

---

## Backup Folder

```basic
File.CopyDirectory(
    source,
    destination
)
```

---

# Permissions

Capabilities required.

Examples:

```basic
REQUIRE filesystem.write
REQUIRE network.client
REQUIRE service.control
```

---

# Security Model

Runtime permissions:

```text
filesystem.read
filesystem.write

network.client
network.server

printer.access

process.control
service.control

raw_disk
raw_memory

kernel_access
driver_access
```

---

# Prompt Customization

Example:

```basic
PROMPT =
    "[" +
    Host.User() +
    "@" +
    Host.Hostname() +
    "]> "
```

---

# Future TUI Features

Using ArcoUX.

```basic
menu =
    UX.Menu()
```

Examples:

```text
File Browsers
Service Managers
Network Tools
Package Managers
Monitoring Dashboards
```

---

# Future GUI Features

ArcoUX windows directly from shell scripts.

```basic
window =
    UX.Window(
        "Backup Tool"
    )
```

---

# Plugin System

Users may install modules.

```basic
PACKAGE.INSTALL(
    "ArcoGit"
)
```

Examples:

```text
ArcoGit
ArcoDocker
ArcoKubernetes
ArcoCloud
ArcoVMWare
ArcoHyperV
```

---

# Packaging

Scripts may compile into:

```text
Standalone Executables
Bytecode Packages
Installers
Portable Applications
```

using ArcoBuild.

---

# Long-Term Vision

ArcoSH should become:

* A replacement for CMD
* A replacement for Bash
* A replacement for PowerShell for many users
* A cross-platform administration environment
* A scripting language
* A systems automation platform
* A rapid application environment

while remaining true to the original BASIC philosophy:

```text
Type commands.
Solve problems.
Get work done.
```
