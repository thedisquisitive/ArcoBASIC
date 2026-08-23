# Arcology Boot Screen Concept

User-authored design spec (2026-08-23), saved verbatim alongside `bootprocess_concept.png` (the
reference mockup). First real implementation: the MVP dashboard wired into
`tests/fixtures/aps-arcology-seed-substrate/aps-arcology-seed-substrate.abas`, replacing that
fixture's own real boot-progress markers with a real System Assembly View driven by the SAME real
milestones (PS/2 init, USB HID enumeration, GOP discovery, identity-map build, `ExitBootServices`,
CR3 switch, GDT/IDT load, terminal ready) — no simulated/fake progress, matching the concept's own
explicit "Emphasize system assembly rather than fake progress" goal.

## Purpose

The Arcology boot screen should transform the early boot experience from a generic framebuffer
console into a recognizable Arcology system interface.

Rather than presenting a wall of scrolling text, the boot screen should visualize the system
assembling itself in real time. This gives the user and developer immediate feedback while
reinforcing Arcology's identity: a system built from authorities, contracts, substrates, and
explicit initialization states.

This concept is designed to be realistic for early framebuffer graphics:

* Simple filled rectangles
* Thin lines
* Basic icons
* Bitmap or pixel fonts
* Minimal animation
* Strong readability at low rendering complexity

---

## Design Goals

* Make the boot process visually identifiable as Arcology.
* Preserve useful technical feedback during early initialization.
* Provide a structured replacement for scrolling boot logs.
* Keep the rendering simple enough for framebuffer output.
* Emphasize system assembly rather than fake progress.
* Support both normal users and engineering/development use cases.

---

## Core Concept

The boot screen presents the machine as a **System Assembly View**.

The display shows:

* The current system identity and platform
* The transition from UEFI firmware to Arcology authority
* A live node graph of core subsystems
* A segmented assembly progress indicator
* A small event feed
* A detail/status panel for the currently important authority or subsystem

The visual idea is that Arcology is not merely "booting." It is **assembling authorities**.

---

## Visual Tone

The screen should feel like:

* A futuristic control interface
* A systems architecture diagram
* A clean, structured engineering display

It should **not** feel like:

* A traditional BIOS screen
* A Linux text dump
* A flashy game UI
* A fake loading screen

### Color Palette

Use a small, consistent palette:

* **Background:** very dark blue-black
* **Primary lines/text:** cyan / blue-white
* **Secondary inactive lines:** dim blue-gray
* **Highlight / transition:** magenta
* **Warning:** amber
* **Failure:** red
* **Success / active:** bright cyan-white

### Typography

Use a framebuffer-friendly pixel or bitmap font:

* Monospaced or near-monospaced
* Sharp and readable
* Minimal anti-aliasing or none
* Large title treatment for primary system identity
* Smaller technical text for logs and details

---

## Screen Structure

The boot screen is divided into five primary zones.

### 1. Header

Top-left:

* `ARCOLOGY`
* `SYSTEM ASSEMBLY`

Top-right:

* Platform label, such as:

  * `UEFI • x86_64`

This immediately establishes brand identity and execution environment.

---

### 2. Authority Transition Diagram

Upper-middle portion:

* A box representing `UEFI`
* A larger central box representing `APS`
* A visual handoff from firmware to Arcology substrate

Suggested labels:

* `UEFI`
* `FIRMWARE`
* `HANDOFF COMPLETE`

Central node:

* `APS`
* `SUBSTRATE`
* `SOVEREIGN`

A severed or crossed link between UEFI and APS indicates control has passed.

This transition is one of the most important symbolic moments in the boot process. It visually
marks the machine becoming Arcology-owned rather than firmware-owned.

---

### 3. Subsystem Assembly Row

A connected row of subsystem panels hangs below the central APS node.

Suggested subsystem panels:

* `MEMORY`
* `ADDRESS SPACE`
* `I/O`
* `ARCFS`
* `INPUT`
* `DISPLAY`

Each panel should contain:

* A small framebuffer-safe icon
* The subsystem name
* Its state
* A small state indicator row of blocks or dots

Example states:

* `ACTIVE`
* `READY`
* `INITIALIZING...`
* `DEGRADED`
* `FAILED`

This row serves as the main visual indicator of how much of the system is online.

---

### 4. Assembly Progress

A small but visible segmented bar below the subsystem row.

Example:

* `ASSEMBLY 17/23`

This is better than a percentage because it represents completed system authorities or
initialization milestones rather than pretending boot is linear.

The progress bar should use simple filled and empty blocks.

---

### 5. Bottom Information Panels

#### Event Feed

Bottom-left panel.

Purpose:

* Show recent meaningful boot events
* Preserve developer visibility
* Avoid overwhelming the screen

Example entries:

* `APS      Memory authority accepted`
* `VRD      Address space committed`
* `CPU      IDT installed`
* `PCI      Enumeration started`

Only a few lines should be visible at once.

#### Status Panel

Bottom-right panel.

Purpose:

* Show focused details for the current authority, event, or subsystem

Example:

* `MEMORY AUTHORITY ESTABLISHED`

Suggested fields:

* `AUTHORITY`
* `CONTRACT`
* `SCOPE`
* `PAGES ONLINE`
* `POLICY`
* `ARCOLOGY CR3`
* `STATUS`

This gives the screen genuine engineering value without turning it into a full text console.

---

## State Language

Arcology should use a consistent state model.

### Not Yet Evaluated

Visual treatment:

* Dark panel
* Dim border
* Low-contrast text

### Initializing

Visual treatment:

* Cyan outline
* Active glow or brighter border
* Optional subtle blinking indicator

### Ready

Visual treatment:

* Bright cyan text
* Stable border
* Small filled indicators

### Active

Visual treatment:

* Stronger cyan-white emphasis
* Confident, stable appearance

### Degraded

Visual treatment:

* Amber highlights
* Clear but non-fatal distinction

### Failed

Visual treatment:

* Red border or red accent
* Error focus in detail panel
* Halt or recovery prompt if necessary

### Authority Transition

Visual treatment:

* Magenta
* Reserved for privileged or meaningful control changes
* Especially appropriate for CR3 handoff, substrate sovereignty, or critical contract transitions

---

## Icon Direction

Icons must be simple enough for framebuffer rendering.

Recommended style:

* 1-color line icons
* Minimal pixel detail
* Geometric forms
* Easily legible at small sizes

Suggested icons:

* **Memory:** dotted chip or memory block
* **Address Space:** wireframe cube or hex grid
* **I/O:** port or connector symbol
* **ArcFS:** stacked layers or storage blocks
* **Input:** keyboard
* **Display:** monitor

Do not use high-detail illustrative icons.

---

## Footer

A thin footer line can contain low-priority system identity information.

Suggested content:

* Version and build number
* A centered boot message
* A right-aligned Arcology motto/status phrase

Example:

* `VER 0.1.0 BUILD 7A1C.20240520`
* `ARCOLOGY IS ASSEMBLING AUTHORITIES`
* `SECURE • SOVEREIGN • INTEGRAL`

This reinforces identity without distracting from the boot state.

---

## Interaction Model

The default screen should remain clean and readable. However, Arcology should support richer
inspection modes.

### Default Mode

* Clean dashboard
* Basic event feed
* Current subsystem or authority details

### Engineering Expansion

Possible toggle keys:

* `Tab`
* `F2`
* `` ` ``
* `~`

Possible expanded views:

* More verbose event feed
* Memory map details
* PRD / VRD details
* Current contract data
* CR3 values
* Timing data
* Device enumeration details

The key principle is that the screen is not fake decoration. It is a visual representation of real
system state.

---

## Failure Behavior

The same interface should also be able to represent failure.

If a subsystem fails:

* The affected node turns red
* Assembly progression halts
* The detail panel shows the reason
* The successful nodes remain visible

This is better than collapsing into an unreadable panic wall.

Example failure display intent:

* `ARCFS FAILURE`
* `ROOT STORAGE CONTRACT REJECTED`
* `Reason: Namespace root object could not be resolved`

Possible actions later:

* `Inspect`
* `Recovery`
* `Retry`

Even if those are not implemented yet, the visual direction should leave room for them.

---

## Why This Fits Arcology

This design fits Arcology because it reflects the system's underlying philosophy.

It emphasizes:

* explicit authority
* explicit contracts
* inspectability
* assembly rather than hidden magic
* continuity between boot environment and full system identity

Arcology should feel like itself from the first frame it owns.

---

## Framebuffer Realism Notes

This concept is intentionally realistic for early implementation.

### Practical Rendering Features

Safe early features:

* solid background fill
* rectangles
* borders
* horizontal and vertical lines
* filled status blocks
* bitmap font text
* simple static icons
* minimal glow approximation through doubled lines or brighter edges

### Avoid Early On

Not recommended for initial implementation:

* gradients
* alpha-heavy compositing
* complex animation
* scrolling regions with heavy redraw logic
* high-detail antialiasing
* elaborate effects requiring many primitives

### Recommended Early Rendering Strategy

1. Paint background
2. Draw frame lines and panel borders
3. Draw the main title and platform label
4. Draw central authority graph
5. Draw subsystem panels
6. Draw segmented assembly bar
7. Draw event feed text
8. Draw status/detail text
9. Update only changed regions between steps if practical

This makes the design achievable while still looking distinctive.

---

## Suggested Initial Boot Sequence Mapping

Example conceptual boot sequence for this screen:

1. Show header and empty layout
2. Activate `UEFI`
3. Show `APS` in provisional state
4. Transition to `SOVEREIGN`
5. Bring up `MEMORY`
6. Bring up `ADDRESS SPACE`
7. Bring up `I/O`
8. Bring up `ARCFS`
9. Bring up `DISPLAY`
10. Bring up `INPUT`
11. Begin session or handoff to higher system UI

This creates a clear visual story rather than an arbitrary progress meter.

---

## First Implementation Recommendation

For the first working version, keep scope tight.

### Minimum Viable Arcology Boot Screen

* Dark background
* `ARCOLOGY` header
* `SYSTEM ASSEMBLY` subtitle
* UEFI → APS diagram
* 4–6 subsystem panels
* Segmented assembly bar
* Event feed with 3–5 lines
* Single status panel
* Cyan + magenta + white palette

That alone would already look dramatically more Arcology than a text dump.

---

## Summary

The Arcology boot screen should be a framebuffer-friendly **system assembly dashboard**.

It should:

* visually communicate the firmware-to-Arcology transition
* show the machine assembling authorities and subsystems
* retain valuable technical information
* look clean, futuristic, and unmistakably Arcology
* remain implementable with simple framebuffer primitives

In short:

**Do not hide the boot process.
Visualize it.
Make the machine becoming Arcology feel intentional.**
