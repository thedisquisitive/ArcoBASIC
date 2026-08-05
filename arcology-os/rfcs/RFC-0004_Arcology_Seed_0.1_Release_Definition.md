# RFC-0004: Arcology Seed 0.1 Release Definition

**RFC Number:** RFC-0004 **Title:** Arcology Seed 0.1 Release Definition
**Status:** Draft **Category:** Release Definition **Authors:** Arcology
Project **Created:** 2026-08-03 **Related RFCs:** RFC-0000, RFC-0003

------------------------------------------------------------------------

# 1. Executive Summary

Arcology Seed 0.1 is the first public release of Arcology OS.

Its purpose is **not** to be a modern desktop operating system.

Its purpose is to prove that:

1.  ArcoBASIC can boot real x86-64 hardware through the Arcology boot
    path.
2.  A user can immediately interact with the machine through a built-in
    line-numbered ArcoBASIC environment.
3.  Arcology has established a stable foundation for future
    self-hosting.

Seed intentionally embraces the spirit of classic home computers.

Power on.

Boot.

READY.

------------------------------------------------------------------------

# 2. Motivation

Arcology should demonstrate ownership of the machine as early as
possible without delaying the project behind years of infrastructure
work.

Rather than attempting to deliver users, networking, filesystems,
graphics, packages, and applications simultaneously, Seed focuses on the
smallest coherent operating environment.

------------------------------------------------------------------------

# 3. Goals

Seed SHALL:

-   Boot on real x86-64 UEFI hardware.
-   Display an Arcology startup banner.
-   Present a `READY.` prompt.
-   Provide an interactive line-numbered ArcoBASIC environment.
-   Execute simple BASIC programs.
-   Demonstrate the Arcology toolchain running on real hardware.

------------------------------------------------------------------------

# 4. Non-Goals

Seed SHALL NOT include:

-   ArcFS
-   Prism desktop
-   User accounts
-   Sessions
-   Presence
-   Networking
-   Multitasking
-   Package management
-   Object Substrate
-   Persistent storage
-   Native application ecosystem

These are intentionally deferred.

------------------------------------------------------------------------

# 5. User Experience

The primary interaction model is a classic home-computer prompt.

Example:

``` text
ARCOLOGY SEED 0.1

READY.

>
```

Users type commands immediately.

No login screen.

No desktop.

No launcher.

------------------------------------------------------------------------

# 6. Interactive Environment

Seed supports two forms of interaction.

## Immediate Mode

Commands execute immediately.

Examples:

``` text
PRINT "HELLO"
HELP
LIST
RUN
NEW
CLEAR
OT
```

## Program Mode

Programs are entered with line numbers.

Example:

``` text
10 PRINT "HELLO"
20 GOTO 10
```

Rules:

-   Existing line numbers replace previous lines.
-   Entering only a line number deletes that line.
-   LIST displays the resident program.
-   RUN executes beginning with the lowest numbered line.
-   Completion returns to `READY.`.

------------------------------------------------------------------------

# 7. Required Commands

Seed MUST provide:

-   PRINT
-   LIST
-   RUN
-   NEW
-   HELP
-   CLEAR
-   OT

The `OT` command is the whimsical feature for Seed and displays a random
Oregon Trail-inspired message compiled into the system image.

------------------------------------------------------------------------

# 8. Startup Banner

The exact wording may evolve, but the startup experience MUST remain
brief and welcoming.

The prompt SHALL end with:

``` text
READY.
```

------------------------------------------------------------------------

# 9. Architecture

The execution chain is:

``` text
UEFI
 ↓
Arcology Loader
 ↓
Minimal Arcology Runtime
 ↓
Interactive ArcoBASIC Environment
```

------------------------------------------------------------------------

# 10. Release Philosophy

Seed proves that Arcology exists.

It does not attempt to prove everything Arcology will become.

Every omitted subsystem is omitted intentionally.

------------------------------------------------------------------------

# 11. Definition of Done

Seed is complete when:

1.  The generated image boots on the designated real hardware.
2.  The Arcology banner appears.
3.  The system reaches `READY.`.
4.  Keyboard input functions.
5.  Immediate commands execute.
6.  Line-numbered programs execute correctly.
7.  `OT` functions.
8.  The machine can intentionally halt or reboot safely.
9.  The complete build is reproducible.

------------------------------------------------------------------------

# 12. Future Extensions

Later releases introduce:

-   ArcFS
-   Native executables
-   Embedded Fission
-   Users and Sessions
-   Prism
-   Full Arcology User Space

------------------------------------------------------------------------

# 13. Guiding Principle

> If a feature is not required to reach `READY.`, it probably does not
> belong in Arcology Seed 0.1.
