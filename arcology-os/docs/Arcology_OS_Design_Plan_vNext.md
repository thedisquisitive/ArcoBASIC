# Arcology OS Design Plan

## Status

Arcology OS is being designed from the application and system model
outward. The underlying kernel, microkernel, Unix base, or custom
substrate is deliberately **not selected yet**. The architecture should
determine what foundation is appropriate, rather than allowing an
existing foundation to dictate Arcology's design.

Current desktop direction: **BeOS meets SHODAN** --- responsive,
understandable, tactile, inspectable, and cybernetic without sacrificing
usability.

ArcoBASIC is a first-class part of the operating environment, not merely
a bundled programming language.

------------------------------------------------------------------------

# Pillar 1: Replaceable Object Architecture

## Core Principle

> Everything Arcology can manage is an Object. Objects are defined by
> the interfaces they expose, the capabilities they possess, their state
> and lifecycle, and the relationships they form.

Arcology should avoid maintaining completely separate conceptual models
for applications, devices, drivers, services, memory objects, codecs,
windows, storage providers, and similar entities wherever a common
object model can represent them cleanly.

The low-level system should primarily understand concepts such as:

-   Object
-   Interface
-   Attachment
-   Capability
-   State
-   Event
-   Lifecycle
-   Resource

Objects should favor **composition and polymorphic interfaces** over
deep inheritance trees.

Example:

``` text
NVMe0
    Object
    Implements:
        PCIEndpoint
        BlockStorage
        HealthReporter
        HotPluggable
```

A program may likewise be represented as an object or assembly of
objects:

``` text
ArcoNote
    Object
    Implements:
        Application
        Materializable
        StateProvider
        Inspectable
```

## Universal Lifecycle

Where practical, Arcology should normalize subsystem-specific operations
such as mount/unmount, load/unload, connect/disconnect, and start/stop
into a common lifecycle:

``` text
CREATE
ATTACH
ACTIVATE
SUSPEND
DETACH
DESTROY
```

Not every object must support every lifecycle stage.

Hardware discovery and application startup can therefore use variations
of the same system machinery.

## Objects Can Acquire Behavior

Interfaces may be supplied by attached objects/components.

An initially discovered NVMe controller may expose only a PCI interface.
Attaching a compatible storage implementation can cause the resulting
object assembly to expose `BlockStorage`.

Removing that implementation removes the supplied behavior without
requiring the physical object itself to disappear.

This resembles attaching components to objects in a game engine, but
applies throughout the operating environment.

------------------------------------------------------------------------

# Replaceable Operating-System Components

## The Operating System Is Not Sacred

Arcology ships with known-good reference implementations, but those
implementations are not inherently mandatory.

For example:

``` text
ArcoBlockStorage
    IMPLEMENTS BlockStorage/3
```

A homebrew developer may provide:

``` text
NightshadeStorage
    IMPLEMENTS BlockStorage/3
```

If the replacement satisfies the published interface contract and
required safety/conformance rules, Arcology can use it.

This philosophy may apply to interfaces such as:

``` text
BlockStorage
FileSystem
NetworkStack
AudioMixer
WindowManager
Allocator
ImageDecoder
SchedulerPolicy
Compression
CryptoProvider
PrintService
```

The exact list remains to be designed.

## Per-Object Replacement

Replacement does not necessarily need to be global.

Example:

``` text
NVMe0
    BlockStorage -> NightshadeStorage

NVMe1
    BlockStorage -> ArcoBlockStorage
```

Multiple implementations of the same system interface can coexist.

Where technically safe, Arcology may support live replacement:

``` text
QUIESCE
FLUSH
DETACH old implementation
ATTACH new implementation
VERIFY
RESUME
```

Highly foundational components may require stricter rules, development
mode, elevated capabilities, or reboot-time replacement.

## Interface Explorer and Conformance Tests

Arcology should make homebrew system development unusually approachable.

The system should expose interface specifications directly:

``` text
INTERFACE BlockStorage/3

Methods
    Read
    Write
    Flush
    Trim
    Geometry

Events
    MediaChanged
    Fault

Semantics
    ordering
    atomicity
    failure behavior
    detach behavior
```

Developers should be able to run official conformance tests against an
implementation before attaching it.

The guiding principle is:

> Here is the contract. Implement this.

A developer should not need to reverse-engineer thousands of lines of
internal OS code simply to replace a subsystem.

------------------------------------------------------------------------

# Pillar 2: ArcoBASIC as the Universal Control Language

## ArcoBASIC Is Embedded Throughout Arcology

ArcoBASIC is not merely preinstalled. It is integrated into the
application and object environment.

Native Arcology applications should automatically have access to a
user-facing ArcoBASIC environment, analogous in spirit to Blender's
Python integration, but designed around discoverability, concise syntax,
and pleasant interactive use.

Applications should not each need to invent their own:

-   Macro language
-   Scripting runtime
-   Automation API
-   Plugin scripting system
-   IPC scripting bridge
-   Interactive console

Participation in Arcology's Object Architecture provides much of this
automatically.

## Application-Scoped ArcoBASIC Console

Every native application can expose an ArcoBASIC console whose default
context is the application itself.

Example:

``` basic
> THIS
ArcoNote.Instance.42

> THIS.DOCUMENT.NAME
"arcology-notes.md"

> THIS.DOCUMENT.SAVE
```

Convenient scoped aliases should keep normal use concise:

``` basic
doc = DOCUMENT
PRINT doc.NAME

thing = SELECTION.FIRST
```

Avoid deeply nested API archaeology.

## Capability-Aware System Access

The application's ArcoBASIC environment can access system objects and
perform system operations, but it remains inside the application's
capability/security context.

Example:

``` basic
storage = SYSTEM.GET("Storage.Documents")
PRINT storage.FREE_SPACE
```

If the application has no authorized relationship to a camera,
microphone, network provider, or other resource, its ArcoBASIC console
does not magically bypass that restriction.

The terminal is powerful because it speaks Arcology's object model, not
because it is an automatic privilege escalation mechanism.

## Application-to-Application Automation

Applications expose their object interfaces to ArcoBASIC.

This allows scripts to connect applications without requiring each
application pair to implement a custom protocol.

Example:

``` basic
note = SYSTEM.FIND Application("ArcoNote")
model = SYSTEM.FIND Application("ArcoModel")

note.DOCUMENT.APPEND model.SELECTION.INFO
```

The same mechanism can support automation, plugins, application
extensions, codecs, handlers, and other composable behavior.

## Object Inspection

Any suitably accessible object can potentially be inspected through
ArcoBASIC.

Example:

``` basic
> THIS
System.Device.NVMe0

> THIS.INTERFACES
PCIEndpoint
BlockStorage
HealthReporter

> THIS.HEALTH
GOOD
```

The ArcoBASIC environment therefore serves simultaneously as:

-   REPL
-   Shell
-   Object inspector
-   Automation environment
-   Macro system
-   Debugging surface
-   API explorer
-   System-control language

## Interactive Discovery

Discoverability is mandatory.

Examples:

``` basic
HELP THIS
HELP THIS.DOCUMENT
```

Autocomplete should understand the interfaces exposed by an object:

``` text
THIS.DOCUMENT.
    APPEND
    CLOSE
    EXPORT
    FIND
    NAME
    SAVE
    SELECTION
    TEXT
```

The goal is for users to explore the system interactively without
constantly consulting external documentation.

## GUI and ArcoBASIC Cooperation

Graphical objects should be transferable into the ArcoBASIC environment.

Potential interactions include:

-   Drag a file into the terminal to insert a file-object reference.
-   Drag a running application into the terminal to insert an
    application reference.
-   Drag a system object from an inspector into the terminal.
-   Right-click an object and choose **Open ArcoBASIC Console**.
-   Copy an object's reference for later scripting.

The GUI and terminal are two views into the same object environment
rather than separate worlds.

------------------------------------------------------------------------

# Program and Executable Direction

Arcology is exploring a structured executable model rather than treating
programs as opaque binary blobs.

A native program may be an **object assembly** describing:

-   Identity
-   Components
-   Interfaces
-   Requirements
-   Capabilities
-   Code implementations
-   Resources
-   State
-   Recovery information
-   Diagnostics

Applications may contain independently materializable components.

Example:

``` text
ArcoNote
    Interface       ACTIVE
    Editor          ACTIVE
    Document Model  ACTIVE
    Spellcheck      ACTIVE
    Printing        DORMANT
    Export          DORMANT
```

Components can become active only when required.

Faults may potentially be contained and repaired at the component level
rather than terminating an entire application.

------------------------------------------------------------------------

# Resident RAM Objects

Arcology should treat reusable memory contents as first-class resident
objects rather than assuming every application instance needs
independent copies of all code, libraries, runtimes, and immutable
resources.

Example:

``` text
                  ArcoBASIC Runtime
                    /          \
                   /            \
              ArcoNote        Arconaut

                    PNG Decoder
                    /        \
                   /          \
              ArcoNote      ArcoModel
```

If an appropriate immutable object is already resident, another
application can attach to it rather than load another physical copy.

Potential memory semantics include:

``` text
PRIVATE
SHARED
SHARED_IMMUTABLE
COPY_ON_WRITE
EPHEMERAL
PERSISTENT
```

Potential sharing scopes include:

``` text
INSTANCE
APPLICATION
USER
TRUST_DOMAIN
SYSTEM
```

Security boundaries must be respected. Sharing is semantic and
intentional, not blind cross-domain deduplication.

ArcoBASIC should eventually be able to express these lifetimes and
sharing semantics directly.

------------------------------------------------------------------------

# Interface-Oriented Dependencies

Native Arcology applications should depend primarily on
capabilities/interfaces rather than arbitrary library filenames.

Instead of:

``` text
libpng16.so.16
```

a component might request:

``` text
NEED Image.Decode.PNG >= 2
```

Arcology resolves an appropriate provider.

This same composition mechanism may eventually unify or simplify:

-   Libraries
-   Plugins
-   Services
-   IPC
-   Codecs
-   File handlers
-   Application extensions
-   System components

------------------------------------------------------------------------

# Current Architectural Identity

The two first major defining Arcology OS features are:

## 1. The System Is Replaceable

Explicit polymorphic interfaces allow developers and homebrew hackers to
replace pieces of the operating system, system libraries, application
components, and hardware handlers without rebuilding the entire
environment.

Arcology's official implementations are reference implementations, not
sacred machinery.

## 2. The System Is Conversationally Programmable

Every native application and accessible system object participates in an
integrated ArcoBASIC environment for inspection, automation,
composition, scripting, extension, and control.

Together:

``` text
                 ARCOLOGY OS
                      |
          +-----------+-----------+
          |                       |
    Object Architecture       ArcoBASIC
          |                       |
   Everything exposes       Everything can
      interfaces             be spoken to
          |                       |
          +-----------+-----------+
                      |
                 USER CONTROL
```

A replaceable component is useful.

A programmable component is useful.

A replaceable component that automatically becomes inspectable and
programmable through the same language as the rest of the operating
environment is a defining Arcology OS concept.

------------------------------------------------------------------------

# Deliberately Undecided

The following should remain undecided until the higher-level Arcology
architecture makes the requirements clear:

-   Linux, MINIX, BSD, seL4, or another base
-   Custom low-level substrate
-   Monolithic, microkernel, hybrid, or nontraditional architecture
-   Filesystem
-   Final executable format
-   Final package format
-   IPC implementation
-   Final security architecture
-   Desktop implementation
-   GUI toolkit

Arcology should dictate the substrate, not the substrate dictate
Arcology.

------------------------------------------------------------------------

# Next Design Target

Specify the **Core Arcology Object Model** in enough detail to prototype
it:

1.  Object identity and lifetime
2.  Interface definition and versioning
3.  Attachment/detachment semantics
4.  Capability/security model
5.  Events
6.  State and persistence
7.  Object discovery
8.  Interface conformance testing
9.  Resident/shared memory-object behavior
10. ArcoBASIC object bindings and introspection

Once those primitives are coherent, implement a tiny native Arcology
application as an object assembly and use it to test the model before
selecting the eventual low-level OS foundation.

------------------------------------------------------------------------

# Pillar 3: Capability-Oriented Object Substrate

## Kernel Direction

Arcology OS should use a **custom capability-oriented Object Substrate**
rather than treating a conventional monolithic kernel as the
architectural center of the system.

The privileged substrate should be deliberately small and conservative.
Its job is to provide the mechanisms required for Arcology Objects to
safely exist and interact, while pushing policy and replaceable
functionality into ordinary system Objects.

Conceptually:

``` text
                Arcology OS
                     |
        +------------+------------+
        | Replaceable OS Objects  |
        |                         |
        | Scheduler Policy        |
        | Memory Policy           |
        | Block Storage           |
        | Filesystems             |
        | Network Stack           |
        | Drivers                 |
        | Resource Managers       |
        +------------+------------+
                     |
          +----------+----------+
          |   Object Substrate  |
          |                     |
          | execution mechanism |
          | isolation           |
          | capabilities        |
          | memory mapping      |
          | interrupt mechanism |
          | object dispatch     |
          | attach/detach       |
          +----------+----------+
                     |
                  Hardware
```

The privileged substrate should not become the home of every
operating-system subsystem. It should know as little policy as
practical.

## Mechanism Versus Policy

A major architectural rule is:

> The substrate supplies dangerous low-level mechanisms; replaceable
> Objects supply policy.

For scheduling, the substrate performs the actual safe context-switching
machinery. A replaceable `SchedulerPolicy` Object decides which eligible
execution context should run next.

For memory, the substrate provides page mapping, protection, revocation,
and isolation. Replaceable memory-policy Objects decide caching,
residency, compression, eviction, and shared-object behavior.

This allows traditionally kernel-resident improvements to be developed
without requiring routine modification of the most dangerous privileged
code.

## ArcoBASIC System Development

Arcology should permit system components and traditionally
kernel-resident policies to be authored in ArcoBASIC and compiled
through ArcoFission to appropriate native or intermediate
representations.

Example:

``` basic
OBJECT DaedalusScheduler
IMPLEMENTS SchedulerPolicy/2

FUNCTION Score(task)
    score = task.PRIORITY

    IF task.CLASS = "Interactive" THEN
        score = score + 20
    END IF

    RETURN score
END FUNCTION

END OBJECT
```

A normal development workflow should support:

``` text
EDIT
COMPILE
CONFORMANCE TEST
SANDBOX TEST
SHADOW TEST
ACTIVATE
MONITOR
ROLL BACK
```

without rebooting the operating environment whenever hardware safety
permits.

## Shadow Testing

Replacement system Objects should be testable in shadow mode.

The currently active implementation remains authoritative while the
experimental implementation receives the same relevant inputs and
independently produces proposed decisions.

Arcology can compare metrics such as:

-   latency
-   throughput
-   memory consumption
-   CPU overhead
-   fairness
-   fault behavior
-   power consumption
-   resource pressure behavior

The experimental component does not gain control until explicitly
activated.

## Hot Replacement

Where technically safe, a component replacement should follow a
controlled lifecycle such as:

``` text
QUIESCE
PRESERVE STATE
DETACH CURRENT OBJECT
ATTACH REPLACEMENT
TRANSFER STATE
VERIFY
RESUME
```

The old implementation remains available as a rollback target until the
new implementation is known to be healthy.

## Automatic Rollback

Replaceable foundational Objects require watchdogs and dead-man
recovery.

If a newly activated scheduler, memory policy, network implementation,
storage implementation, or similar component fails its health
requirements, Arcology should automatically restore the last known-good
implementation where possible.

A failed experiment should produce a brief disruption, not automatically
turn the machine into a recovery-media archaeology project.

## Refresh

**Refresh** is an Arcology system operation distinct from reboot.

Refreshing a component means reconstructing the relevant portion of the
active Object graph using the currently selected implementations.

Examples:

``` basic
SYSTEM.REFRESH Network
SYSTEM.REFRESH Storage
SYSTEM.REFRESH Scheduler
```

A refresh may:

``` text
quiesce consumers
preserve transferable state
detach old Objects
materialize updated Objects
restore state
reattach consumers
resume
```

A future `SYSTEM.REFRESH ALL` may rebuild most of the operating
environment without restarting firmware or hardware.

## Substrate Generations

The small privileged substrate itself cannot always be safely
hot-replaced using the same mechanisms.

Substrate development therefore uses **generations**.

Example:

``` text
Current Substrate: Generation 141
Candidate:          Generation 142
Rollback:           Generation 141
```

A candidate generation should be buildable while the current generation
is running, tested in virtualization or an isolated execution
environment, and scheduled as the next substrate.

Where hardware architecture permits, Arcology may eventually support a
kexec-like transition between substrate generations without a complete
firmware reboot. This is an advanced goal rather than a requirement for
the first implementation.

Most Arcology OS development should not require substrate modification.

------------------------------------------------------------------------

# Pillar 4: Central System Manager

## One Place to Understand the Operating Environment

Arcology should provide a flagship graphical **System Manager** that
makes the Object Architecture understandable and manageable without
requiring command-line knowledge.

It combines concepts traditionally scattered among:

-   Task Manager
-   Device Manager
-   Services
-   driver management
-   component/package management
-   security/capability management
-   kernel/module management
-   live diagnostics
-   recovery tools

The design target is deliberately simple:

> Replacing or testing an OS component should be understandable to a
> technically curious user without requiring kernel-development
> knowledge.

## System Overview

The System Manager should expose system health and active
implementations at a glance.

Example:

``` text
SYSTEM

Overview | Objects | Capabilities | Components | Substrate

HEALTH
  System Stable
  183 Objects Active
  41 Shared Objects
  0 Critical Faults

ACTIVE SYSTEM COMPONENTS

Scheduler        BalancedScheduler    [Change] [Inspect]
Memory Policy    ArcMemoryDefault     [Change] [Inspect]
Block Storage    ArcoBlockStorage     [Change] [Inspect]
Network Stack    ArcoNet              [Change] [Inspect]
Audio Mixer      ArcoMixer            [Change] [Inspect]

Substrate Gen 141
Candidate Gen 142                    [Review] [Schedule]
```

## Component Replacement UI

Changing a system component should resemble selecting a compatible
implementation rather than manually editing system internals.

Example:

``` text
Scheduler

Current:
    BalancedScheduler

Available:
    BalancedScheduler
    LowLatencyScheduler
    DaedalusScheduler

[Test Selected]
[Use Selected]
```

Before activation Arcology should present understandable results:

``` text
DaedalusScheduler

Compatibility       PASS
Interface           PASS
Safety checks       PASS
Shadow test         PASS
Rollback available  YES

Observed impact:
    Interactive latency   Better
    CPU fairness          Similar
    Battery use           Slightly higher

[Activate]
```

## Central Capability Management

Every module/Object should expose its capability envelope through the
System Manager.

Example:

``` text
NightshadeStorage

Capabilities
    Storage.Block        ALLOWED
    Hardware.PCI         ALLOWED
    DMA                  ALLOWED
    Network              DENIED
    Audio                DENIED
    User.Documents       DENIED

Granted by:
    System Profile

Used by:
    NVMe0
    NVMe1
```

Users should be able to see:

-   what capabilities an Object has
-   why it has them
-   who granted them
-   what Objects depend on them
-   what the capability allows in plain language

Capability editing should be graphical where appropriate and should
explain consequences rather than exposing only internal flag names.

## Component Inspection

Every replaceable component should answer at least these questions:

1.  What do I do?
2.  What do I need?
3.  What do I provide?
4.  What can I access?
5.  What depends on me?
6.  What happens if I fail?
7.  Can I be replaced live?
8.  Can I be rolled back?

A component inspector may show:

``` text
ArcoNet

Status          ACTIVE
Version         4.2
Interface       NetworkStack/3

Capabilities
    Network.Raw
    Memory.Shared
    Hardware.PCI

Attached Objects
    Ethernet0
    WiFi0

Consumers
    ArcoBrowser
    ArcoUpdate
    ArcoSH

CPU             0.8%
Memory          42 MB
Shared Memory   18 MB

[Open ArcoBASIC]
[Test]
[Suspend]
[Refresh]
[Replace]
```

## System Profiles

Arcology should support named system profiles describing a selected
collection of implementations.

Example:

``` text
Standard
Audio Workstation
Low Power
Experimental
My Weird Frankenstein Build
```

A profile may select:

``` text
Scheduler       DaedalusScheduler
Memory Policy   AggressiveCache
Storage         NightshadeStorage
Network         ArcoNetExperimental
Substrate       Gen142
```

Profiles make experimentation, specialization, recovery, and switching
between known configurations straightforward.

A known-good profile should always be available for recovery.

## Substrate Management

Substrate generations should be managed from the same System Manager.

Example:

``` text
SUBSTRATE

Current
    Generation 141
    Stable

Available
    Generation 142
    Tested

Changes
    Memory-map optimization
    Interrupt-routing fix
    New capability primitive

Compatibility
    100%

[Test in Sandbox]
[Schedule Refresh]
[Use Next Restart]
```

The System Manager should clearly identify the rollback generation
before activation.

## ArcoBASIC Integration

Every suitable Object inspector should offer **Open ArcoBASIC**.

The graphical System Manager and ArcoBASIC console are two interfaces to
the same Object environment.

A user may inspect a component graphically, then open:

``` basic
> THIS
System.Network.ArcoNet

> THIS.STATUS
"ACTIVE"

> HELP THIS

> SYSTEM.TEST THIS
```

The GUI should remain sufficient for ordinary administration. ArcoBASIC
provides deeper exploration and automation without creating a separate
administrative universe.

------------------------------------------------------------------------

# Updated Defining Arcology OS Features

Arcology OS currently has four major architectural pillars:

## 1. Replaceable Object Architecture

System functionality is represented through explicit polymorphic
interfaces and composable Objects. Reference implementations can be
replaced without rebuilding the operating environment.

## 2. ArcoBASIC as Universal Control Language

Native applications and accessible system Objects participate in an
integrated ArcoBASIC environment for scripting, inspection, automation,
composition, extension, and control.

## 3. Capability-Oriented Object Substrate

A deliberately small privileged substrate supplies execution, isolation,
capability enforcement, memory mapping, interrupt handling, and other
essential mechanisms while replaceable Objects provide most
operating-system policy and services.

System improvements can be authored in ArcoBASIC, compiled, tested,
shadowed, activated, refreshed, and rolled back live wherever
technically safe.

## 4. Central System Manager

The active operating environment is visually manageable as a collection
of interchangeable Objects. Component selection, capability management,
testing, shadow testing, replacement, rollback, profiles, refresh
operations, and substrate generations are centralized and deliberately
easy to understand.

Together:

``` text
                       ARCOLOGY OS
                            |
          +-----------------+-----------------+
          |                                   |
   Object Architecture                    ArcoBASIC
          |                                   |
   interchangeable                     programmable
     components                           objects
          |                                   |
          +-----------------+-----------------+
                            |
                     System Manager
                            |
              test / swap / inspect / recover
                            |
                  Capability Substrate
                            |
                         Hardware
```

The operating environment should be hackable without being hostile,
replaceable without being fragile, and powerful without requiring the
user to memorize implementation plumbing.

------------------------------------------------------------------------

# Filesystem Direction: Object Storage and Arcology Paths

## Core Filesystem Principle

Arcology's filesystem direction is **object-aware storage** rather than
treating files primarily as anonymous byte streams identified by paths.

A stored item has stable object identity and semantics. Human-visible
files and folders are namespace views of those objects.

This supports future design work around:

-   stable identity across moves and renames
-   semantic backup policies
-   application-state ownership
-   disposable caches versus protected user data
-   built-in version history
-   chunk-level deduplication
-   ransomware-resistant history
-   transactional changes
-   object-aware corruption recovery
-   integration with Arcology's resident RAM Object model

The intended principle is:

> Arcology stores Objects. Files and folders are human-facing views of
> those Objects.

## Arcology Path Syntax

Arcology OS **does not use `/` or `\` as path hierarchy separators**.

The hierarchy separator is:

``` text
:
```

Absolute Arcology paths therefore look like:

``` text
:usr:share:arcology:config
:people:daedalus:documents:arcology:design.md
:system:components:storage
:apps:arconote:state:preferences
:devices:nvme0
:volumes:backup:images
```

This syntax is deliberately independent of Unix and Windows path
conventions.

## Namespace Semantics

The `:` separator represents **namespace traversal**, not necessarily
literal physical directory traversal on disk.

For example:

``` text
:system:components:storage
```

means to begin at the Arcology namespace root and resolve:

``` text
system
    -> components
        -> storage
```

The underlying objects may be stored, shared, deduplicated, or
represented in ways that do not correspond to a traditional nested
directory layout.

## Paths Are Not Identity

A path is a human-facing route to an Object, not the Object's permanent
identity.

For example:

``` text
:people:daedalus:documents:foo.txt
```

may resolve to:

``` text
ObjectID 84F2A1...
```

Moving the item to:

``` text
:projects:arcology:foo.txt
```

does not inherently change its ObjectID. Applications and system
components holding an authorized object reference can continue referring
to the same stored object.

This avoids treating renames and moves as changes of identity.

## Relative Paths

Relative namespace traversal also uses `:`:

``` text
documents:foo.txt
images:icon.png
```

The exact syntax for parent traversal and other special namespace
operations remains deliberately undecided. Arcology should not
automatically inherit `..`, drive letters, Unix mount syntax, or other
legacy path conventions without evaluating whether they fit the object
namespace.

## Hard Syntax Rule

For native Arcology OS interfaces, documentation, APIs, ArcoBASIC, and
filesystem tools:

> **Use `:` for Arcology path hierarchy. Do not use `/` or `\` for
> native Arcology paths.**

Compatibility environments may translate foreign path conventions at
their boundaries, but native Arcology software should use the Arcology
namespace syntax.

------------------------------------------------------------------------

# Arcology Preboot Environment

## Purpose

Arcology should ship with its own boot environment rather than treating
a conventional GRUB-style kernel menu as the primary startup model.

The Preboot Environment selects and validates a **complete Arcology
system composition**, not merely a kernel image. A bootable composition
may include the System Profile, Substrate Generation, Scheduler Policy,
Memory Policy, Storage Provider, Filesystem Provider, and other
foundational Objects.

Preboot, System Manager, and the running OS should operate on the same
underlying system/profile metadata rather than maintaining separate
configuration universes.

## Visual Direction

The Preboot Environment should deliberately contrast with Arcology's
denser BeOS-meets-SHODAN desktop.

> Calm, clean, large, visual, and confidence-inspiring.

The primary interface should use **large graphical tiles**, generous
spacing, strong typography, and progressive disclosure. The inspiration
is the simplicity of modern graphical startup managers rather than BIOS
utilities or text-heavy boot menus.

``` text
+--------------------------------------------------------------+
|                       ARCOLOGY                               |
|                                                              |
|                  Choose an Environment                       |
|                                                              |
|      +----------------+   +----------------+                 |
|      |    STANDARD    |   |   EXPERIMENT   |                 |
|      |   Gen 141      |   |   Gen 142      |                 |
|      |   Stable       |   |   Candidate    |                 |
|      +----------------+   +----------------+                 |
|                                                              |
|      +----------------+   +----------------+                 |
|      |     AUDIO      |   |    RECOVERY    |                 |
|      |  WORKSTATION   |   |  Last Known    |                 |
|      |  Low Latency   |   |     Good       |                 |
|      +----------------+   +----------------+                 |
|                                                              |
|        Diagnostics     Console     System Details             |
+--------------------------------------------------------------+
```

Selection should use restrained visual emphasis such as a soft halo,
border, or elevation rather than noisy animation.

## Progressive Disclosure

The first screen should expose only information required to make a
startup decision.

``` text
STANDARD
Generation 141
Stable
Last boot successful
```

Selecting **Details** may reveal:

``` text
Substrate        Generation 141
Scheduler        BalancedScheduler
Memory Policy    ArcMemoryDefault
Storage          ArcoBlockStorage
Filesystem       ArcFS
Profile Health   Verified
```

Technical depth remains available without forcing every user to confront
it during routine startup.

## Profiles as Boot Targets

Preboot primarily presents **system profiles/environments**, not merely
operating-system installations. Examples may include Standard, Audio
Workstation, Experimental, Low Power, and Recovery. These are the same
profiles managed from Arcology's System Manager.

## Failure and Recovery UX

Failures should be presented visually and in plain language.

``` text
EXPERIMENTAL
Generation 142
Needs Attention
```

Opening it may show:

``` text
Last start failed during:
Memory Policy initialization

Recommended:
Use ArcMemoryDefault

[Repair and Start]
[Start Anyway]
[Inspect]
```

Preboot should understand Arcology interface requirements well enough to
detect incompatible or missing providers before attempting normal
startup. When a safe compatible provider is available, Preboot may offer
a one-action repair rather than dropping the user into a recovery shell.

## Automatic Rollback

Preboot should understand at least:

``` text
CURRENT
LAST KNOWN GOOD
CANDIDATE
RECOVERY
```

A new substrate generation or foundational profile change should not
immediately destroy the previous known-good composition. Candidate
configurations become known-good only after successful startup and
health validation.

## Transactional Boot Configuration

Substrate generations and foundational system configurations should be
installed alongside the currently working configuration.

``` text
Generation 141    KNOWN GOOD
Generation 142    CANDIDATE
```

Generation 141 remains intact while Generation 142 is evaluated.

## Preboot ArcoBASIC

Advanced users should have access to a minimal ArcoBASIC environment
from Preboot. It does not need the entire normal Arcology runtime, but
should expose safe recovery and inspection functionality.

``` basic
SYSTEM.PROFILES
SYSTEM.SUBSTRATES
SYSTEM.BOOT "Experimental"
SYSTEM.ROLLBACK SUBSTRATE
```

The graphical interface remains the default and preferred route for
ordinary startup and recovery.

## Boot Materialization

Startup conceptually follows:

``` text
Firmware
    |
Arcology Preboot
    |
Validate hardware
    |
Read system Object metadata
    |
Select profile
    |
Select substrate generation
    |
Validate interface graph
    |
Materialize system Objects
    |
Verify health
    |
Enter Arcology
```

During startup, Arcology may present a clean materialization status view
rather than traditional scrolling boot logs:

``` text
DISCOVERING MACHINE

CPU             READY
MEMORY          READY
STORAGE         READY
OBJECT STORE    READY

MATERIALIZING PROFILE

Scheduler       ATTACHED
Memory          ATTACHED
Filesystem      ATTACHED
Network         ATTACHED
Prism           ATTACHED

SYSTEM COHERENT

ENTERING ARCOLOGY
```

Detailed diagnostic logs remain available when requested.

## Visual Relationship to the Desktop

Arcology intentionally uses two related but different presentation
modes.

**Preboot:** calm, minimal, large targets, low information density,
clear state, progressive disclosure, recovery-oriented.

**Desktop:** BeOS-inspired usability, SHODAN/cybernetic visual
character, information-rich when desired, highly inspectable Object
environment, integrated ArcoBASIC control.

The machine should not visually scream at the user simply because it has
just powered on. The deeper cybernetic character emerges after entering
the operating environment or opening advanced Preboot tools.

## Preboot Design Principle

> Arcology Preboot makes complete system compositions as easy to select,
> test, repair, roll back, and understand as choosing a large visual
> tile.

------------------------------------------------------------------------

# Hardware-Level ArcoBASIC Roadmap (ArcoFission)

## Guiding Principle

Arcology OS is authored in ArcoBASIC from firmware to desktop.
Developers express hardware intent while ArcoFission owns
machine-specific lowering.

## Architecture

-   ArcoBASIC
-   Parser / Type System
-   A-MIR (Arcology Machine IR)
-   Architecture Backend (x86-64, ARM64, future)
-   Native Machine Code
-   Optional readable assembly output

## Bare-Metal Profile

``` basic
#PROFILE BAREMETAL
#TARGET X86_64
#RUNTIME NONE
```

No operating system, libc, heap, or filesystem is assumed unless
explicitly provided.

## Language Requirements

-   Fixed-width integer and pointer types
-   Packed structures
-   Alignment directives
-   Physical and virtual address types
-   Volatile memory support

## Hardware Semantic Layer

Examples:

``` basic
CPU.Interrupts.Disable
CPU.MemoryBarrier FULL
IO.Read8 port
IO.Write8 port, value
MMIO.Read32 address
Atomic.CompareExchange target, expected, replacement
```

These are compiler-recognized semantics rather than handwritten
assembly.

## Interrupts

Interrupt handlers are first-class language constructs. ArcoFission
generates the entry/exit stubs, ABI handling, register preservation, and
return sequence.

## Calling Conventions

Calling conventions and exports are declarative:

``` basic
#CALLCONV UEFI
#EXPORT "efi_main"
```

## Memory Layout

Section placement and alignment are expressed declaratively instead of
linker scripts.

## Unsafe Blocks

Unsafe hardware operations are explicitly marked with `UNSAFE`, allowing
compiler reporting and auditing.

## Emulator Target

An Arcology virtual hardware target should exist before extensive
bare-metal work, allowing CPU, MMIO, timer, interrupt, framebuffer,
serial, and storage features to be tested safely.

## Development Milestones

1.  Fixed-width primitive types
2.  Bare-metal compilation profile
3.  A-MIR hardware primitives
4.  x86-64 backend
5.  UEFI bindings
6.  Produce a bootable EFI executable entirely from ArcoBASIC
7.  Exit UEFI Boot Services while remaining in ArcoBASIC
8.  Use this as the foundation for Arcology Preboot
