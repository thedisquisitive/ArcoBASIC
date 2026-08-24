
# RFC-0047: Native Display Driver (Intel First, AMD-Portable)

**RFC Number:** RFC-0047
**Title:** Native Display Driver
**Status:** Draft (Phase 0 implemented and QEMU-proven — see RFC-0007 Section 15/16 and commit
`8c301ff`; Phase 1 not started)
**Category:** Systems / Graphics

---

# 1. Executive Summary

Arcology is a real product, not a hardware bring-up exercise. A real product needs real graphics
performance: web browsing, Blender, games, a coding environment with real fonts, and an
intent-based UI that doesn't look or feel like a command line wearing a costume. None of that is
reachable on a framebuffer this project only ever writes to with individual, uncombined 32-bit
MMIO stores.

This RFC is the real, committed roadmap for getting Arcology off UEFI's borrowed GOP framebuffer
and onto this project's own real display driver — mode-setting from a cold GPU state, with
acceleration, without depending on firmware having already done the work. It starts with Intel
(the most thoroughly, publicly documented display architecture of the three major vendors, and the
one this project's own current real hardware — the user's Lenovo ThinkPad E15 Gen 2 test unit —
may or may not actually carry; Phase 1's own first task is finding out which GPU is actually
there), and is deliberately architected so the parts that generalize (mode-set state machine,
plane/surface model, the ArcoBASIC-facing `GRAPHICS.*` surface) don't have to be rewritten to add
AMD later — only the register-level backend underneath swaps.

This is not a small project. It is comparable in scale to RFC-0045's own USB/xHCI work — this
project's own prior largest undertaking — and likely larger. It gets a real RFC and a real phased
plan for exactly that reason, the same treatment RFC-0045 and RFC-0039 (ArcFS) both got before a
single register was touched.

---

# 2. Goals

- Real display mode-setting from a cold GPU state, independent of what firmware already set up.
- Real write performance suitable for continuous, interactive redraw — not a one-shot boot splash.
- A vendor-neutral `GRAPHICS.*` ArcoBASIC surface: application/UI code targets this project's own
  API, never a register offset directly.
- An architecture where adding a second vendor backend (AMD) is a new backend module, not a
  rewrite of anything above the mode-set/plane layer.
- Real acceleration once mode-setting is proven: 2D blit at minimum, so a compositor-style UI
  (windows, real-time redraw, intent-based interaction) is actually feasible at real frame rates.

---

# 3. Non-Goals (this RFC, first phases)

- 3D acceleration, video decode, or any GPU compute path. Real future work, explicitly out of
  scope until 2D mode-setting and blit are both proven.
- Multi-monitor / hotplug. Single fixed display first.
- A generalized, N-vendor plugin driver model decided up front. The AMD backend's own real shape
  gets decided when AMD work actually starts, informed by what the Intel backend's own real
  architecture turns out to need — guessing a "generic driver interface" before a second real
  backend exists risks the same mistake RFC-0038's own block-storage substrate deliberately avoided
  (see its own "provider" pattern, decided from two real, working backends, not zero).

---

# 4. Why GOP Was Never Going to Be Enough

RFC-0045's own substrate work proved a real linear framebuffer, handed over by UEFI's GOP
(Graphics Output Protocol), survives `ExitBootServices` and is genuinely usable for a boot
dashboard and an interactive terminal. That was true, and it was the right first increment — it's
exactly how this project's own boot dashboard and RFC-0007 Program Mode terminal both render today.

It was never going to scale to a real product's UI, for two separable reasons:

1. **Write performance.** Every pixel this project has ever drawn goes through an individual
   32-bit MMIO store. Commit `8c301ff` (this same session) found and fixed the specific, real
   instance of this — the framebuffer's own memory type defaulted to effectively Uncacheable,
   making every write a fully serialized bus transaction — with a real, vendor-neutral MTRR
   Write-Combining fix. That fix is real and it matters, but it is a floor-raiser, not a ceiling
   this project can build a full desktop-class UI on top of. Software-only pixel pushing, even at
   full write-combined throughput, has no path to real 2D-accelerated compositing, let alone 3D.
2. **No mode control after `ExitBootServices`.** GOP's own `SetMode` needs Boot Services. Once this
   project exits them (which the whole Polymorphic Substrate architecture depends on), the display
   mode is permanently whatever firmware last chose. No real product ships that constraint.

---

# 5. Phase 0 — Write-Combining Framebuffer (DONE, QEMU-proven, commit `8c301ff`)

Real prerequisite work, useful independently of everything else in this RFC: new x86-64 encoder
primitives (`mov_rax_cr0`/`mov_cr0_rax`, `rdmsr`, `wrmsr`, `wbinvd`) and their full ArcoBASIC-facing
`CPU.*` plumbing (parser, type-checker, AST-to-AMIR lowering, codegen — the exact same pipeline
`CPU.ReadCR3`/`CPU.WriteCR3` already used), verified with a standalone probe under QEMU (a real
CR0, the textbook-correct local APIC base via `CPU.ReadMSR(27)`, real MTRR capability bits, and a
working write/read-back round trip) before being wired into `aps-arcology-seed-substrate.abas`'s
own `EnableFramebufferWriteCombining`.

**Known limitation, explicitly not closed by this phase**: QEMU cannot prove the real performance
benefit — its own MMIO emulation doesn't model real memory-type-driven bus timing. Only the user's
own real hardware can confirm the actual frame-rate improvement. See `.agents/reports/WP-033-...`
for the real hardware round this shipped in.

---

# 6. Phase 1 — Real GPU Identification (not started)

Before any register in a display engine gets touched, this project needs to know, for real, which
GPU the test hardware actually has. This is not assumed — Lenovo's own E15 Gen 2 line was sold with
both Intel and AMD configurations, and the user has separately asked this RFC stay AMD-portable,
which is itself a signal worth taking at face value rather than assuming Intel is even the correct
first target for THIS SPECIFIC test machine.

**Real, concrete, startable work**, reusing RFC-0045's own established raw-PCI-scan pattern
(`PciConfigReadU32`/`UsbXhci.DiscoverPci`'s own bus/device/function walk) verbatim in shape, scanned
for PCI base class `0x03` (Display Controller) instead of `0x0C`/`0x03`/`0x30` (USB xHCI):

- Real PCI bus scan, reporting every real display-class device found: vendor ID, device ID, base
  class/subclass/prog-if, and every BAR (for the eventual MMIO mapping this project already knows
  how to do — `UsbXhci.MapMmio`'s own pattern).
- Real vendor identification: `0x8086` = Intel, `0x1002` = AMD/ATI, `0x10DE` = NVIDIA (out of scope
  as a driver target, but honestly reported if found).
- For an Intel device, extract the real device ID and cross-reference it against Intel's own
  published generation tables (PCI IDs are public and stable; this project keeps its own small,
  explicit lookup table rather than a heuristic) to determine which display-engine generation this
  is actually targeting.
- Real acceptance evidence: run under real QEMU (`-vga` variants don't emulate a real Intel/AMD
  GPU, so this phase's own real proof is necessarily thinner than RFC-0045's driver phases were —
  documented honestly, not papered over) AND on the user's own real hardware, reporting back
  exactly what silicon is actually there.

This phase's own output — the real, confirmed vendor and generation — decides Phase 2's own scope.
Undecided on purpose until that's known.

---

# 7. Phase 2 — Minimal Mode-Set (not started, scope depends on Phase 1's result)

Get one real, fixed resolution onto the screen from a cold GPU state, no firmware GOP mode involved
at all. This is the single hardest, highest-risk phase in the whole RFC — display engine
bring-up has none of USB's forgiving failure modes (a bad USB transfer gives you an error code; a
bad display engine register write gives you a black screen and no diagnostic feedback at all).

Real, necessary sub-steps, informed directly by how RFC-0045's own xHCI work actually went:

- Real MMIO BAR mapping for the display engine's own register block (reusing `MapExtra1GbRegion`'s
  own dynamically-computed identity-map extension — already proven, already general-purpose).
- Real panel/output power-sequencing for whatever connector this specific hardware actually uses
  (eDP is near-certain for a laptop's own built-in panel) — this is genuinely one of the more
  fragile, generation-and-panel-specific parts of real bring-up, and the primary reason "generic"
  is a soft claim rather than a hard one even within one vendor.
- Real CRTC/plane/encoder configuration for one fixed, known-good mode (matching this project's own
  "prove the seam, don't guess the cathedral" precedent from RFC-0045's own phased build-out).
- A real, own-driven GTT (Graphics Translation Table) or equivalent, so the framebuffer this
  driver's own plane reads from is a real address this project's own page tables already
  understand — no dependency on GOP's own memory layout at all past this point.

Firmware's own display driver naturally stays bound to the GPU until something disconnects it —
RFC-0045's own real `UsbXhci.DisconnectFirmwareDriver` finding (OVMF's xHCI driver silently racing
this project's own raw-PCI driver for the same hardware) is very likely to recur here in some form
and should be watched for from the start, not discovered the hard way a second time.

---

# 8. Phase 3 — Real 2D Acceleration (not started)

Once Phase 2 proves a real, driver-owned mode-set, add a real blit/fill engine — the actual
prerequisite for a compositor-style, intent-based UI redrawing at real interactive frame rates
rather than a CPU walking every pixel by hand. Scope (blit only vs. blit+basic 2D primitives)
decided once Phase 2's own real register-level access is proven; guessing further than that now
would be exactly the kind of undocumented scope assumption Section 7.2-style AI guidance in this
project's other RFCs warns against.

---

# 9. Why Intel First (and What Stays Reusable for AMD)

Intel's own display-engine Programmer's Reference Manuals are public, complete, and (relative to
the other two vendors) unusually well documented — a real, material factor given this project has
no NDA access to anything. AMD's own register documentation is also public for many generations,
which is exactly why AMD is a real, planned Phase (not a "maybe"), just not first.

**What generalizes without a rewrite**: the `GRAPHICS.*` ArcoBASIC-facing surface, the plane/surface
data model application code targets, the MTRR/write-combining setup (Phase 0, already vendor
neutral by construction), the PCI-discovery pattern (Phase 1's own scan is written generically
against PCI class codes, not an Intel-specific search), and the MMIO-mapping/GTT-equivalent
machinery this project already owns from RFC-0045.

**What does not generalize, and isn't pretended to**: the actual display-engine register layout,
mode-set sequence, and panel power sequencing are real, vendor-and-generation-specific work each
time. Anyone who tells you "generic GPU driver" skips that part is not being honest about the
problem — this RFC isn't going to do that either.

---

# 10. AI Implementation Guidance

- Do not skip Phase 1. Guessing the GPU vendor/generation instead of actually discovering it real,
  under real QEMU and on the user's own real hardware, is exactly the kind of assumption RFC-0044
  already found expensive once (its own literal "index 1" GOP-handle guess was wrong on real
  hardware geometry).
- Phase 2 is the real risk phase. Treat every register write as a genuine unknown until proven
  otherwise under QEMU first, then real hardware — the same discipline that caught RFC-0045's own
  firmware/guest xHCI ownership race and this session's own real CR3/identity-map hardware-only
  freeze. A bad display-engine write's own failure mode (black screen, no diagnostic output at
  all) is worse than anything this project has hit so far; over-verify before every real hardware
  write in this specific RFC, not just before shipping.
- Report real findings honestly, including negative ones — if Phase 1 finds AMD hardware on the
  actual test machine, that changes what "first" means for this specific project's own real
  hardware, and that finding gets acted on, not talked around.

---

# 11. Revision History

| Version | Date | Summary |
|---------|------|---------|
| 0.1 | 2026-08-23 | Initial draft. Written directly after a real user pushback: unaccelerated framebuffer writes are not viable for a real product (web browsing, Blender, games, coding, an intent-based UI), and whatever the fix is has to stay usable for AMD, not just Intel. Phase 0 (write-combining framebuffer, a real, vendor-neutral MTRR fix, not a GPU driver) already implemented and QEMU-proven in the same session, commit `8c301ff`. Phases 1-3 (GPU identification, minimal mode-set, 2D acceleration) are real, scoped, not started. |
