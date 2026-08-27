# RFC-0048: Substrate Networking Fabric and Remote Development Transport

RFC Number: RFC-0048
Title: Substrate Networking Fabric and Remote Development Transport
Status: Draft -- Milestone N0 Phase 1 (real NIC identification, real PCI class-0x02 scan) and
remaining N0 work (generic Network Interface Contract + real VirtIO-net legacy PCI transport:
discovery, I/O-BAR mapping, reset, feature negotiation, MAC/link-state readback) implemented and
QEMU-proven; steered by RFC-0048-Steering-01 (2026-08-26) -- see that document for the revised
upper-layer direction (Arcology Interconnect supersedes the original disposable ARCODEV/1 milestone
starting at N5). Virtqueue setup and frame transmit/receive (the remainder of N0) and Milestones
N1-N9 not started
Category: Substrate / Networking / Development Infrastructure
Authors: Arcology Project
Created: 2026-08-26
Last Updated: 2026-08-26
Supersedes: None
Superseded By: None
Related RFCs: RFC-0000, RFC-0005, RFC-0006

## 1. Executive Summary

This RFC defines the initial native networking architecture for Arcology OS.

Networking SHALL exist as a substrate-level service rather than as functionality owned by a userspace process, application runtime, or monolithic kernel-style subsystem. The substrate SHALL own the mechanisms required to discover network hardware, exchange frames, maintain protocol state, and expose networking capabilities through Arcology contracts.

The first implementation is intentionally narrow. Its purpose is not to provide a complete general-purpose network stack. Its purpose is to make an Arcology test system reachable over a local Ethernet network so that developers can:

- verify link and protocol operation;
- remotely access the Arcology shell;
- inspect the running system;
- transfer files and development artifacts;
- stage system updates without rebuilding boot media for every iteration; and
- reboot into staged changes when live replacement is unsafe.

The initial implementation SHALL target wired Ethernet and IPv4. It SHALL begin with one or more explicitly supported NIC backends rather than attempting universal hardware support.

The planned bring-up sequence is:

```
Hardware Enumeration
        ↓
NIC Driver
        ↓
Ethernet II
        ↓
ARP
        ↓
Static IPv4
        ↓
ICMP Echo
        ↓
UDP
        ↓
DHCP
        ↓
TCP
        ↓
Arcology Remote Development Service
        ↓
Remote Shell + File Transfer + Staged Update
```

Networking SHALL NOT depend on the existence of AEX userspace. Once userspace exists, applications SHALL consume the already-operational networking fabric through contracts rather than gaining direct access to NIC hardware or substrate internals.

## 2. Motivation

Arcology OS can boot on real hardware, execute substrate code, provide a small local shell, access MMIO, and perform hardware enumeration. Continued development on physical test benches is currently constrained by the need to replace or rewrite boot media whenever a new build must be tested.

This development loop is unnecessarily expensive:

```
Modify
  ↓
Build
  ↓
Rewrite ISO / USB / disk image
  ↓
Move or reboot test hardware
  ↓
Boot
  ↓
Test
  ↓
Repeat
```

A minimal native network stack changes the development loop to:

```
Modify
  ↓
Build
  ↓
Connect to running Arcology system
  ↓
Push artifact
  ↓
Stage or execute
  ↓
Test
  ↓
Repeat
```

This is especially valuable before conventional userspace exists. Network hardware and protocol processing do not inherently require userspace; they require hardware access, memory management, timing, buffer management, and execution facilities that already belong to the substrate.

Arcology also requires an architectural boundary that will remain valid after user applications are introduced. Implementing networking as a substrate fabric now avoids later redesign around a socket API or privileged network daemon that does not match Arcology's contract-oriented architecture.

## 3. Goals

This RFC intends to accomplish the following:

- Define networking as a first-class Arcology substrate service.
- Establish a clean separation between network hardware drivers, protocol mechanisms, and consumers.
- Bring up Ethernet networking before AEX userspace is required.
- Support at least one virtual NIC suitable for deterministic development and at least one selected physical Ethernet NIC family when practical.
- Support static IPv4 configuration as the earliest usable network configuration mode.
- Support ARP and ICMP echo sufficiently to validate basic LAN connectivity.
- Add UDP and DHCP for automatic IPv4 configuration.
- Add a minimal reliable TCP implementation sufficient for the Arcology remote development transport.
- Provide a remotely accessible Arcology shell for trusted development networks.
- Provide reliable file upload and download.
- Provide verified staging of system artifacts and updates.
- Prevent an interrupted or invalid remote update from silently replacing the last known bootable system state.
- Define internal asynchronous semantics that can later be exposed through AEX contracts without redesigning the entire network stack.
- Establish testable boundaries suitable for autonomous implementation agents.

## 4. Non-Goals

The initial implementation defined by this RFC does not attempt to provide:

- universal Ethernet controller support;
- Wi-Fi;
- Bluetooth networking;
- IPv6;
- VLAN configuration;
- bridging;
- advanced routing;
- network namespaces;
- production firewall management;
- VPN support;
- multicast application APIs;
- high-performance multi-queue NIC operation;
- TCP offload engines;
- advanced congestion-control algorithms;
- zero-copy application networking;
- POSIX socket compatibility;
- BSD sockets as the canonical Arcology networking API;
- SSH server compatibility;
- TLS as a prerequisite for initial laboratory bring-up;
- production Internet exposure;
- automatic replacement of live substrate code;
- arbitrary remote writes to physical memory or MMIO;
- remote kernel-debugger-equivalent authority;
- a complete package manager;
- AEX userspace networking APIs beyond the architectural contract boundary described herein.

These capabilities MAY be introduced by later RFCs.

## 5. Terminology

### 5.1 Networking Fabric

The substrate service responsible for coordinating network interfaces, protocol modules, endpoints, routing decisions, packet flow, and networking contracts.

The Networking Fabric is a logical Arcology service. It is not required to be implemented as one monolithic binary or source module.

### 5.2 NIC Backend

A hardware-specific or virtual-device-specific implementation capable of transmitting and receiving Ethernet frames through a particular network controller.

Examples may include VirtIO-net or a supported Intel Ethernet family.

### 5.3 Driver-Side Contract

The interface between a NIC backend and the Networking Fabric. It allows hardware-specific code to publish network-interface capabilities without requiring protocol code to understand device-specific registers.

### 5.4 Consumer Contract

A networking capability exposed by the Networking Fabric to another Arcology subsystem or, later, an AEX application.

### 5.5 Endpoint

A logical communication endpoint managed by the Networking Fabric. An endpoint MAY represent a datagram flow, reliable stream, listener, or future Arcology-native transport.

### 5.6 ARCODEV

The provisional name used in this RFC for the Arcology remote development transport and service. The final product name MAY change without altering the architecture.

### 5.7 Staged Update

An artifact that has been transferred, validated, and recorded as eligible for activation, but has not yet replaced the currently active artifact.

### 5.8 Trusted Development Network

A physically or administratively controlled local network used for Arcology development. The initial ARCODEV implementation SHALL be considered suitable only for such an environment unless stronger authentication and encryption are subsequently specified.

## 6. Requirements

### 6.1 Architectural Requirements

- Networking MUST execute independently of AEX userspace.
- The substrate MUST retain authority over NIC hardware resources.
- Ordinary future AEX applications MUST NOT receive unrestricted MMIO, DMA, interrupt, or NIC-ring access merely to perform networking.
- NIC-specific logic MUST be separated from protocol-specific logic.
- Protocol consumers MUST NOT depend directly on a particular NIC implementation.
- The Networking Fabric MUST support more than one NIC backend without requiring protocol-stack duplication.
- Internal networking operations SHOULD use asynchronous request/completion semantics.
- Blocking convenience wrappers MAY exist, but MUST NOT be the only internal execution model.
- Packet ownership and buffer lifetime MUST be explicit.
- Malformed inbound packets MUST NOT be able to cause unchecked memory access.

### 6.2 Initial Hardware Requirements

- The first implementation MUST support at least one deterministic development NIC backend.
- VirtIO-net SHOULD be used for virtualized bring-up when compatible with the project's current QEMU environment.
- At least one selected physical Ethernet controller SHOULD be supported for test-bench validation.
- Additional NIC families MUST be added behind the same driver-side contract.

### 6.3 Initial Protocol Requirements

The first protocol milestone MUST support:

- Ethernet II frame transmission and reception;
- MAC addressing;
- ARP request and reply;
- static IPv4 configuration;
- ICMP echo request and reply.

The second protocol milestone MUST add:

- UDP;
- DHCP client operation.

The third protocol milestone MUST add:

- TCP sufficient for reliable bidirectional byte streams;
- connection establishment;
- ordered delivery;
- retransmission required for reliable operation on an ordinary LAN;
- connection teardown;
- bounded resource handling for incomplete or abusive connections.

### 6.4 Remote Development Requirements

ARCODEV MUST support, at minimum:

- connection to an Arcology test system;
- session establishment;
- remote shell command submission;
- command output retrieval;
- file upload;
- file download;
- artifact metadata inspection;
- staged activation requests;
- reboot requests.

ARCODEV SHOULD support:

- build identifier reporting;
- substrate version reporting;
- network diagnostics;
- storage-space checks before upload;
- checksums or cryptographic hashes for transferred artifacts;
- atomic staging metadata updates;
- resumable or restartable transfers in a later revision.

ARCODEV MUST NOT implicitly activate a newly transferred substrate artifact merely because transfer succeeded.

### 6.5 Update Safety Requirements

- Uploaded update artifacts MUST be written to a staging location before activation.
- The completed artifact MUST be verified before it is marked staged.
- Verification MUST include size and integrity validation.
- Cryptographic hash verification SHOULD be used when the required primitive is available.
- A failed validation MUST leave the current active artifact unchanged.
- Interrupted transfer MUST NOT produce an apparently valid staged artifact.
- Updates to components carrying the active remote session SHOULD default to activation on reboot rather than live replacement.
- Live substrate replacement MUST be governed by a separate explicit safety contract or RFC before being considered generally supported.

## 7. Architecture

### 7.1 High-Level Architecture

```
                    ┌──────────────────────────┐
                    │      Local Shell         │
                    │   / Development Service  │
                    └────────────┬─────────────┘
                                 │
                    Consumer / Internal Contract
                                 │
                    ┌────────────▼─────────────┐
                    │    Networking Fabric     │
                    │                          │
                    │  Endpoint Management     │
                    │  Protocol State          │
                    │  Address Configuration   │
                    │  Packet Dispatch         │
                    └──────┬───────────┬───────┘
                           │           │
                     ┌─────▼────┐ ┌────▼─────┐
                     │ TCP/UDP  │ │ ARP/ICMP │
                     └─────┬────┘ └────┬─────┘
                           └──────┬─────┘
                                  │
                             ┌────▼────┐
                             │  IPv4   │
                             └────┬────┘
                                  │
                           ┌──────▼──────┐
                           │ Ethernet II │
                           └──────┬──────┘
                                  │
                         Driver-Side Contract
                                  │
                ┌─────────────────┴─────────────────┐
                │                                   │
         ┌──────▼──────┐                     ┌──────▼──────┐
         │ VirtIO-net  │                     │ Physical NIC│
         │   Backend   │                     │   Backend   │
         └──────┬──────┘                     └──────┬──────┘
                │                                   │
                └─────────────────┬─────────────────┘
                                  │
                               Hardware
```

### 7.2 Layer Ownership

**NIC Backend**

Responsible for:

- device initialization;
- hardware queues or descriptor rings;
- DMA setup;
- interrupt or polling integration;
- physical link state where available;
- transmitting raw Ethernet frames;
- receiving raw Ethernet frames;
- publishing MAC address and interface capabilities.

The NIC backend SHALL NOT implement TCP, DHCP, DNS, or application protocols.

**Ethernet Layer**

Responsible for:

- Ethernet II framing;
- source and destination MAC handling;
- EtherType dispatch;
- frame-size validation;
- passing payloads to registered protocol handlers.

**ARP Layer**

Responsible for IPv4-to-MAC resolution on directly reachable Ethernet networks.

ARP state MUST expire or otherwise be replaceable. The initial implementation MAY use a small fixed-size cache suitable for test systems.

**IPv4 Layer**

Responsible for:

- IPv4 packet construction and parsing;
- address validation;
- basic routing selection;
- protocol dispatch;
- checksum validation;
- local-address recognition.

IPv4 fragmentation MAY initially be unsupported. Unsupported fragmented packets MUST be rejected safely.

**ICMP Layer**

The initial ICMP implementation MUST support enough ICMP echo behavior to validate bidirectional IP communication.

Additional ICMP messages MAY be implemented as needed for protocol correctness.

**UDP Layer**

Responsible for datagram send/receive and checksum handling as required by the selected IPv4 behavior.

**DHCP Client**

Responsible for acquiring and renewing basic IPv4 network configuration.

The DHCP client SHOULD obtain:

- local IPv4 address;
- subnet mask;
- default gateway;
- DNS server information, even if DNS is not yet consumed by the first development-service milestone;
- lease timing information.

Static configuration MUST remain available as a diagnostic and fallback path.

**TCP Layer**

The initial TCP implementation is a compatibility and development transport, not a claim of production Internet-grade TCP completeness.

It MUST nevertheless preserve reliable ordered stream semantics for supported conditions.

### 7.3 Networking Fabric State

A network interface SHOULD move through states conceptually equivalent to:

```
ABSENT
  ↓
DISCOVERED
  ↓
INITIALIZING
  ↓
LINK_READY
  ↓
ADDRESSING
  ↓
CONFIGURED
  ↓
OPERATIONAL
```

Failure states SHOULD preserve diagnostic information and permit retry where safe.

### 7.4 Internal Asynchronous Model

Network operations SHOULD be representable using requests and completion events similar to:

```
NET_REQUEST_OPEN
NET_REQUEST_CONNECT
NET_REQUEST_LISTEN
NET_REQUEST_SEND
NET_REQUEST_RECEIVE
NET_REQUEST_CLOSE

NET_EVENT_CONNECTED
NET_EVENT_ACCEPTED
NET_EVENT_RX_READY
NET_EVENT_TX_COMPLETE
NET_EVENT_CLOSED
NET_EVENT_ERROR
```

Exact names are non-normative.

The architectural requirement is that protocol processing MUST NOT assume that a caller can block indefinitely inside the networking implementation.

This allows later AEX contracts to map naturally onto the same execution model.

## 8. Contract Model

### 8.1 Driver-Side Network Interface Contract

Each NIC backend SHOULD expose a logical interface with operations equivalent to:

```
NetworkInterface.Start()
NetworkInterface.Stop()
NetworkInterface.GetCapabilities()
NetworkInterface.GetMAC()
NetworkInterface.GetLinkState()
NetworkInterface.Transmit(frame)
```

Receive completion SHOULD be delivered asynchronously to the Networking Fabric.

The exact ABI SHALL be specified by implementation documentation or a follow-up contract RFC if required.

### 8.2 Future Consumer Contracts

Once AEX userspace is available, the Networking Fabric SHOULD expose capability-oriented contracts such as:

```
Network.Endpoint
Network.Resolve
Network.Interface
Network.Service
Network.Route
Network.Raw
Network.Policy
```

Applications SHOULD request communication intent rather than hardware ownership.

A future application contract may conceptually express:

```
CONTRACT network.outbound {
    protocol = tcp
    destinations = ["example.service:443"]
}
```

The example is illustrative and does not define final AEX manifest syntax.

### 8.3 Raw Networking

Raw Ethernet or IP packet access MUST be treated as a privileged capability.

Ordinary applications MUST NOT receive raw packet access by default.

## 9. Remote Development Service

### 9.1 Purpose

The remote development service exists to shorten the Arcology physical-hardware development cycle.

It is the first substantial consumer of the Networking Fabric, but SHALL remain architecturally separate from it.

### 9.2 Initial Transport

The initial service MAY use a simple Arcology-native framed protocol over TCP rather than SSH.

This protocol is provisionally referred to as ARCODEV/1.

Its design SHOULD prioritize:

- trivial implementation;
- deterministic parsing;
- explicit message lengths;
- bounded memory allocation;
- machine readability;
- forward versioning;
- diagnostic clarity.

A conceptual command set is:

```
HELLO
AUTH
INFO
EXEC
PUT
GET
STAT
HASH
STAGE
ACTIVATE
REBOOT
CLOSE
```

Exact wire encoding is outside the normative scope of this RFC and SHOULD be defined before implementation of the remote service begins.

### 9.3 Shell Integration

EXEC requests SHOULD pass through the same command-dispatch mechanism used by the local Arcology shell.

The remote service SHOULD NOT maintain an independent duplicate command implementation.

Local and remote invocation should converge on a common shell command layer:

```
Local Keyboard ───────┐
                      ├──> Shell Command Dispatcher ──> Arcology Services
ARCODEV Session ──────┘
```

This avoids divergence between local and remote diagnostics.

### 9.4 File Transfer

The remote service MUST support binary-safe file transfer.

The receiver MUST know the expected length before committing the transfer as complete.

A received file SHOULD first be created under a temporary or incomplete identity.

Conceptually:

```
PUT :staging:network_new.aex 184320 SHA256=...
        ↓
write temporary object
        ↓
verify received length
        ↓
verify integrity
        ↓
rename / commit as staged artifact
```

### 9.5 Activation

Transfer, staging, and activation MUST be separate operations.

A normal substrate update flow SHOULD be:

```
PUSH
  ↓
VERIFY
  ↓
STAGE
  ↓
REBOOT-TO-STAGED
  ↓
BOOT VALIDATION
  ↓
COMMIT
```

A later RFC MAY define automatic rollback if a staged boot fails health validation.

## 10. Bootstrap Configuration

Before DHCP is available, the local shell MUST provide enough configuration to assign a static IPv4 address to an interface.

Conceptual commands may include:

```
net list
net status
net set-address 192.168.50.20/24
net set-gateway 192.168.50.1
net up
ping 192.168.50.10
```

Exact shell syntax is non-normative.

After DHCP is implemented, the shell SHOULD permit:

```
net dhcp
```

and SHOULD display the resulting configuration.

## 11. User Experience

The initial human user is an Arcology developer or technician operating a test bench.

The local shell SHOULD expose concise networking status rather than requiring raw diagnostic structure inspection for routine bring-up.

Example:

```
NETWORK STATUS

Interface:   net0
Driver:      virtio-net
MAC:         52:54:00:12:34:56
Link:        UP
Mode:        DHCP
IPv4:        192.168.50.24/24
Gateway:     192.168.50.1
RX Frames:   1842
TX Frames:   991
ARCODEV:     LISTENING
```

Failures SHOULD identify the layer at which operation stopped:

```
NIC ............. PASS
LINK ............ PASS
ETHERNET ........ PASS
ARP ............. PASS
IPv4 ............ PASS
DHCP ............ FAIL: no offer received
TCP ............. NOT TESTED
ARCODEV ......... NOT STARTED
```

The system SHOULD make it obvious whether a failure is hardware, link, address configuration, protocol, or remote-service related.

## 12. Developer Experience

The Networking Fabric SHOULD provide stable abstractions that prevent consumers from manually constructing NIC descriptors or directly manipulating hardware queues.

A substrate consumer should conceptually be able to perform operations equivalent to:

```
endpoint = Network.Open(Stream)
Network.Connect(endpoint, address, port)
Network.Send(endpoint, data)
Network.Close(endpoint)
```

Internal implementations MAY use lower-level structures, but those details MUST remain behind the network service boundary.

Networking development SHOULD support deterministic test injection where practical. Protocol parsing should be testable using synthetic frames without requiring a physical NIC.

NIC drivers SHOULD be testable against common driver-side contract tests.

## 13. Security Considerations

### 13.1 Trust Boundary

Network input is untrusted even on a laboratory LAN.

All packet parsers MUST validate lengths before accessing fields.

All externally supplied lengths, offsets, sequence information, options, and message sizes MUST be treated as hostile input.

### 13.2 Remote Development Exposure

The first ARCODEV implementation is a development feature, not a production remote-management service.

Unless strong cryptographic authentication and confidentiality are implemented, ARCODEV:

- MUST NOT be enabled by default in production builds;
- MUST NOT be exposed directly to the public Internet;
- SHOULD default to trusted development networks only;
- SHOULD provide an obvious indication when active.

### 13.3 Authentication

ARCODEV MUST require explicit session authorization before remote command execution or file modification.

A bootstrap implementation MAY use a pre-shared development credential if stronger identity infrastructure does not yet exist.

Credentials MUST NOT be transmitted or stored in a form that is falsely represented as secure.

If the initial transport lacks encryption, documentation MUST state that fact clearly.

### 13.4 Command Authority

Remote shell authority MUST be explicit.

Remote access MUST NOT silently grant additional hardware capabilities beyond those already available to the shell command dispatcher.

Dangerous operations SHOULD be separately identifiable and auditable.

### 13.5 Resource Exhaustion

The networking implementation MUST place bounds on:

- receive queues;
- transmit queues;
- ARP cache size;
- TCP connection count;
- retransmission state;
- incomplete file transfers;
- command size;
- remote output buffering.

An unauthenticated peer MUST NOT be able to force unbounded substrate allocation.

## 14. Privacy Considerations

The initial networking stack does not require telemetry or external reporting.

The substrate MUST NOT transmit diagnostic, device, or user information to external systems unless explicitly requested by a network operation or later service.

ARCODEV MAY expose system information to an authenticated developer session. Implementations SHOULD document which fields are available remotely.

Remote command logs and transfer history MAY be retained for diagnostics. If retained, their storage location and lifecycle SHOULD be documented.

## 15. Accessibility Considerations

Networking configuration and diagnostics SHALL remain usable through the text shell without requiring graphical interaction.

Status output SHOULD:

- avoid relying solely on color;
- provide textual state labels;
- use stable command names;
- provide actionable error descriptions;
- remain usable over both local and remote shell sessions.

Any future graphical network interface SHOULD expose the same underlying diagnostics rather than hiding failure information.

## 16. Performance Considerations

The first implementation prioritizes correctness and architectural clarity over maximum throughput.

It MAY initially use:

- one receive queue;
- one transmit queue;
- polling during early bring-up;
- fixed-size protocol tables;
- conservative packet-buffer counts;
- straightforward copy-based packet paths.

The architecture MUST NOT require those limitations permanently.

Packet processing SHOULD avoid unnecessary allocation in interrupt context.

Receive and transmit paths SHOULD have explicit ownership rules to prevent buffer leaks and double release.

Protocol tables and queues MUST remain bounded.

Performance optimization MUST NOT bypass packet validation or contract boundaries.

## 17. Compatibility

This RFC extends the Arcology substrate architecture without requiring AEX userspace.

It is compatible with the project's existing hardware bring-up approach because network drivers consume hardware enumeration, MMIO, interrupts or polling, timing, and memory primitives already belonging to lower substrate layers.

Future AEX networking MUST be layered over the Networking Fabric rather than replacing it.

POSIX or BSD socket compatibility MAY later be implemented as a compatibility service or library. Such compatibility MUST NOT redefine the substrate's canonical networking architecture.

SSH MAY later be implemented as an ARCODEV-compatible frontend or separate service once suitable cryptographic facilities exist.

## 18. Reference Implementation Plan

### 18.1 Milestone N0 — NIC Transport

Deliverables:

- selected NIC backend initialized;
- MAC address readable;
- link state observable where supported;
- raw Ethernet frame transmit;
- raw Ethernet frame receive;
- frame counters and diagnostics.

Acceptance test:

A known frame emitted by Arcology is observable by another host or packet capture, and a known inbound Ethernet frame reaches the Arcology Ethernet layer intact.

### 18.2 Milestone N1 — LAN Reachability

Deliverables:

- Ethernet II dispatch;
- ARP;
- static IPv4;
- IPv4 checksum handling;
- ICMP echo request/reply;
- local shell network diagnostics.

Acceptance test:

Arcology and another LAN host can successfully exchange ICMP echo traffic using static IPv4 configuration.

### 18.3 Milestone N2 — Automatic Addressing

Deliverables:

- UDP;
- DHCP client;
- lease state;
- default gateway configuration;
- DNS server information capture.

Acceptance test:

Arcology boots on a standard development LAN, obtains a valid IPv4 lease, and successfully pings its gateway without manual address entry.

### 18.4 Milestone N3 — Reliable Transport

Deliverables:

- TCP listener;
- TCP outbound connect capability;
- reliable ordered transfer;
- connection teardown;
- bounded connection state;
- retransmission adequate for test-LAN operation.

Acceptance test:

A host can establish a TCP connection with Arcology and exchange a multi-packet byte stream without corruption.

### 18.5 Milestone N4 — Remote Shell

Deliverables:

- versioned ARCODEV framing;
- session authentication;
- shared local/remote shell command dispatcher;
- remote command execution;
- stdout/stderr/result return;
- disconnect cleanup.

Acceptance test:

A developer can connect from another computer, authenticate, run a harmless Arcology shell command, receive its output, and disconnect cleanly.

### 18.6 Milestone N5 — Remote File Transport

Deliverables:

- upload;
- download;
- temporary/incomplete transfer state;
- length verification;
- integrity verification;
- atomic completion/staging identity.

Acceptance test:

A binary artifact transferred to Arcology is byte-for-byte identical to its source and an interrupted transfer is never exposed as a complete staged artifact.

### 18.7 Milestone N6 — Staged Development Update

Deliverables:

- artifact staging metadata;
- validation before activation;
- reboot-to-staged operation;
- preserved previous bootable artifact;
- shell diagnostics for active and staged versions.

Acceptance test:

A developer can remotely push a new Arcology artifact, stage it, reboot the test bench into it, and retain a known recovery path if the new artifact is invalid.

## 19. Testing Strategy

### 19.1 Unit Tests

Unit tests SHOULD cover:

- Ethernet parsing;
- ARP parsing and cache behavior;
- IPv4 parsing and checksum validation;
- ICMP parsing;
- UDP parsing;
- DHCP state transitions;
- TCP state transitions;
- remote protocol framing;
- transfer integrity logic;
- malformed and truncated packets;
- boundary conditions and integer overflow.

Protocol parsing SHOULD be testable from byte arrays without physical hardware.

### 19.2 Integration Tests

Integration testing SHOULD use QEMU and a deterministic virtual NIC before physical-hardware testing where possible.

Tests SHOULD include:

- guest-to-host ping;
- host-to-guest ping;
- DHCP lease acquisition;
- TCP stream exchange;
- remote shell command execution;
- upload/download round trip;
- interrupted transfer recovery;
- staged update metadata persistence.

### 19.3 Physical Hardware Tests

At least one physical test bench SHOULD validate:

- NIC discovery;
- MMIO or I/O resource handling as applicable;
- DMA operation;
- interrupt or polling behavior;
- real Ethernet link;
- DHCP;
- TCP;
- ARCODEV shell;
- remote file push.

Physical validation MUST record the NIC vendor/device identifiers and hardware model.

### 19.4 Negative Tests

The implementation MUST test malformed input including:

- undersized Ethernet frames;
- invalid EtherTypes where applicable;
- truncated ARP payloads;
- invalid IPv4 header length;
- total-length inconsistencies;
- invalid checksums;
- unsupported IPv4 fragmentation;
- malformed UDP lengths;
- malformed DHCP options;
- invalid TCP offsets;
- oversized ARCODEV frames;
- unauthenticated commands;
- interrupted uploads;
- invalid staged-artifact hashes.

### 19.5 Regression Tests

Once a network milestone passes, later networking changes MUST NOT regress earlier milestones.

A TCP implementation that breaks ICMP bring-up, for example, is not conformant merely because the TCP-specific tests pass.

## 20. AI Implementation Guidance

Autonomous coding agents implementing this RFC SHALL follow these boundaries.

### 20.1 Implementation Order

Agents MUST implement milestones in dependency order unless an existing implementation already satisfies an earlier milestone.

Preferred order:

```
N0 NIC Transport
N1 Ethernet + ARP + Static IPv4 + ICMP
N2 UDP + DHCP
N3 TCP
N4 ARCODEV Remote Shell
N5 File Transfer
N6 Staged Update
```

Agents MUST NOT jump directly to SSH, HTTP, TLS, DNS-dependent services, or production networking unless separately instructed.

### 20.2 Required Boundaries

Agents MUST:

- keep NIC-specific code behind a driver-side interface;
- keep packet parsing independent from shell UI code;
- reuse the existing shell command dispatcher for remote shell operation where technically possible;
- avoid introducing a second shell implementation;
- keep transfer and activation separate;
- preserve current boot artifacts during staged-update development;
- use existing Arcology memory, MMIO, enumeration, and filesystem facilities rather than introducing parallel infrastructure without justification;
- add tests with each protocol layer;
- document any required assumptions about DMA, interrupts, timers, cache attributes, or physical-address translation.

### 20.3 Forbidden Shortcuts

Agents MUST NOT:

- give application code direct NIC hardware ownership as a networking API;
- implement protocol parsing with unchecked pointer arithmetic;
- assume all inbound packets are well formed;
- allocate unbounded memory based on peer-controlled sizes;
- mark incomplete uploads as valid;
- overwrite the active substrate image as the first step of an update;
- silently replace the contract architecture with POSIX sockets;
- require userspace to complete N0 through N6;
- claim broad hardware compatibility from QEMU-only testing.

### 20.4 Required Deliverables Per Milestone

Every milestone implementation SHOULD include:

- source changes;
- unit tests;
- integration test or reproducible test procedure;
- shell-visible diagnostics;
- documentation of supported and unsupported behavior;
- failure logging sufficient to identify the failing network layer.

### 20.5 Stop Conditions

An agent MUST stop and report rather than invent architecture when:

- required MMIO or DMA semantics are absent or contradictory;
- the NIC requires an unsupported interrupt mechanism and no approved polling fallback exists;
- buffer ownership cannot be determined from existing substrate APIs;
- activation would require unsafe live replacement not covered by an accepted contract;
- filesystem semantics cannot guarantee that a staged artifact remains distinguishable from an incomplete transfer;
- implementation requires modification of a separately governed security primitive not covered by the assigned work.

### 20.6 Acceptance Principle

Each milestone MUST produce independently observable value.

Networking work is not required to be "complete" before it becomes useful.

A successful ICMP milestone is valid before TCP exists. A successful remote-shell milestone is valid before general-purpose AEX networking exists.

## 21. Future Extensions

Potential future RFCs may define:

- IPv6 and neighbor discovery;
- DNS resolver service;
- Arcology-native service discovery;
- secure ARCODEV authentication;
- TLS;
- SSH compatibility;
- network policy and firewall contracts;
- per-AEX network capability manifests;
- raw-packet capability contracts;
- multiple simultaneous interfaces;
- Wi-Fi;
- VLANs and bridging;
- advanced routing;
- VPN and encrypted overlay networks;
- zero-copy packet paths;
- checksum and segmentation offload;
- multi-queue NICs;
- network telemetry;
- remote debugger facilities;
- automatic staged-boot health validation;
- transactional rollback;
- safe hot replacement of selected substrate services;
- Arcology-native transport protocols that allow applications to request communication intent rather than TCP/IP-specific implementation.

## 22. Definition of Done

This RFC reaches its initial implementation goal when all of the following are true:

1. Arcology initializes at least one supported Ethernet NIC on a real test system.
2. The Networking Fabric can transmit and receive Ethernet II frames.
3. Arcology can resolve a LAN peer through ARP.
4. Arcology can communicate over static IPv4.
5. Arcology can successfully exchange ICMP echo traffic with another LAN host.
6. Arcology can obtain IPv4 configuration through DHCP.
7. Arcology can maintain a reliable TCP connection suitable for development traffic.
8. A remote development client can authenticate to an Arcology test bench.
9. The remote client can invoke the existing Arcology shell command path and receive command output.
10. The remote client can upload and download binary files without corruption.
11. Uploaded system artifacts are transferred into an incomplete/staging state before validation.
12. Invalid or interrupted uploads cannot become active artifacts.
13. A validated artifact can be staged for activation without immediately replacing the running system.
14. The system provides a documented reboot-to-staged development workflow.
15. Networking remains operational without AEX userspace.
16. The architecture exposes a defined path for future contract-based AEX networking without requiring replacement of the substrate network stack.
17. QEMU/virtual regression tests and at least one physical-hardware validation are documented.

---

## Revision History

| Version | Date | Summary |
|---------|------|---------|
|0.1|2026-08-26|Initial draft, saved verbatim as submitted, design-only.|
|0.2|2026-08-26|Milestone N0 Phase 1 (real NIC identification, Section 6.2/18.1) real and QEMU-proven. Reuses RFC-0047's own `GpuDiscovery` shape verbatim (itself reused from RFC-0045's `UsbXhci.DiscoverPci` raw-PCI-scan pattern) -- same `PciConfigReadU32`/`PciConfigAddressPort`/`PciConfigDataPort` machinery, same bus/device/function walk -- scanned for PCI base class 0x02 (Network Controller) instead of 0x03 (Display) or 0x0C/0x03/0x30 (USB xHCI). `NetDiscovery.Discover` (`stdlib/network_discovery_policy.abas`) records up to 4 real network-class devices found (bus/device/function, vendor/device ID, class/subclass/prog-if, all 6 real BARs) into a fixed scratch table; `NetDiscoveryVendorCode` recognizes VirtIO (Red Hat, Inc., vendor 0x1AF4/6900) first, matching Section 6.2's own naming of VirtIO-net as this project's first deterministic development NIC backend. Proven by new smoke test `systems_network_nic_discovery_probe_smoke`: a positive case attaches a real QEMU `virtio-net-pci` device and confirms it is found, correctly classified as Network Controller/Ethernet (class 2/0/0), and correctly identified as VirtIO by its real vendor ID; a negative control with no network device attached confirms zero false matches. Deliberately not claimed yet: MMIO/BAR mapping, virtqueue setup, frame transmit/receive, or any protocol layer above raw PCI discovery -- Milestone N0 Phase 2 (driver-side transport) and Milestones N1-N6 remain real, separate, later increments.|
|0.3|2026-08-26|RFC-0048-Steering-01 (Networking Development Steering Addendum) received; N0 Phase 1 preserved as-is per its own Section 3 (no defect or architectural incompatibility was found). Continued "remaining N0 work" per the addendum's Section 11/13 acceptance direction. Added `stdlib/network_interface_contract.abas`, a real, generic driver-side Network Interface Contract (`NetworkInterface.Start/Stop/GetCapabilities/GetMAC/GetLinkState`) implemented as an explicit backend-tag dispatch -- matching this project's own established provider-tag-dispatch idiom (`LoopInvokeSlot`, `volumes_policy.abas`) rather than true polymorphism, which this backend does not have. Exactly one NIC backend is registered today (`stdlib/virtio_net_policy.abas`, a real VirtIO-net legacy PCI transport): PCI discovery scoped to VirtIO's own vendor ID (0x1AF4) plus base class 0x02, firmware-driver eviction reusing RFC-0045 Phase 3's own EFI_PCI_IO_PROTOCOL-based `DisconnectController` pattern, I/O-BAR mapping (Memory Space/Bus Master enable, real BAR0 I/O-indicator-bit check), device reset, and real minimal feature negotiation -- this driver accepts only the two feature bits it actually understands and uses (VIRTIO_NET_F_MAC, VIRTIO_NET_F_STATUS), ANDed against what the device actually offers, not a blind echo of every offered feature. Proven by new smoke test `systems_virtio_net_transport_smoke`, run through the generic contract rather than the backend directly: a real QEMU legacy virtio-net-pci device (`disable-modern=on` for pure legacy I/O-BAR transport, `vectors=0` for a deterministic legacy device-specific-config offset at +0x14 rather than +0x18, a fixed `mac=` for a fully deterministic expected value) is discovered, mapped, reset, and negotiated, and its real MAC address (byte-exact match) and real link-up state are read back correctly; a no-NIC negative control confirms `NetworkInterface.Start` fails honestly with no crash. Both cases passed on the first real QEMU run. Full regression suite: 116/116 passing. Deliberately not claimed yet, per the addendum's own Section 12 non-derailment rule: virtqueue setup, frame transmit/receive, or packet ownership semantics -- the next real N0 increment, not implemented ahead of that proof. Milestones N1-N9 (Ethernet, ARP/static IPv4/ICMP, DHCP, TCP, Interconnect, development contracts, observability, remote assistance, native management) remain real, separate, later increments.|
