# RFC-0048 Steering Addendum 01: Arcology OS Networking Development Steering Addendum

Document Type: RFC Steering / Implementation Addendum
Status: Active Steering
Applies To: RFC-0048 -- Substrate Networking Fabric and Remote Development Transport
Development Baseline: N0 Phase 1 Complete
Created: 2026-08-26
Last Updated: 2026-08-26

## 1. Purpose

This document updates the implementation direction of the Arcology OS networking work without invalidating or restarting completed N0 Phase 1 development.

The existing Networking Fabric RFC remains the implementation foundation. The additions in this steering document refine the long-term architecture before higher layers become entrenched.

The immediate networking objective remains unchanged:

Bring up a stable, substrate-native Ethernet networking path suitable for development on virtual and physical Arcology test systems.

However, the networking subsystem SHALL now be developed with the expectation that it will become the foundation of:

- Arcology-to-Arcology secure interconnection;
- remote administration;
- remote assistance and remote desktop;
- standardized operating-system-level RMM;
- machine-to-machine contract invocation;
- development deployment and debugging;
- structured network observability and troubleshooting.

N0 Phase 1 work SHALL be preserved unless a concrete defect or architectural incompatibility is discovered.

## 2. Arcology Subsystem Design Requirement

Every major Arcology subsystem SHOULD attempt to solve one to three identifiable problems in traditional implementations, rather than merely reproduce existing operating-system facilities under new names.

Networking SHALL explicitly target the following initial problems.

### 2.1 Problem: Driver and Protocol Entanglement

Traditional networking implementations commonly allow hardware-specific assumptions and capability quirks to leak upward into protocol and application layers.

Arcology SHALL instead use a strict driver-side network interface contract.

NIC implementations SHALL publish capabilities to the Networking Fabric. Higher layers SHALL consume those capabilities without depending on the identity or implementation details of the physical adapter.

The intended model is:

```
Physical / Virtual NIC
        ↓
NIC Backend
        ↓
Network Interface Contract
        ↓
Networking Fabric
        ↓
Protocol / Service Contracts
```

Adding a second NIC family SHOULD NOT require modifications to Ethernet, ARP, IPv4, TCP, Interconnect, or application-facing contract logic except where a previously unidentified generic capability is genuinely required.

### 2.2 Problem: Ambiguous Packet and DMA Ownership

Packet buffers, descriptor rings, asynchronous completion, and DMA lifetimes are a common source of networking defects.

Arcology networking SHALL maintain explicit ownership semantics for packet and frame resources.

At any point, a resource MUST have a defined owner or defined shared-read state. Transfer of ownership MUST be explicit.

A typical receive path may conceptually follow:

```
NIC Backend
    ↓ ownership transfer
Networking Fabric
    ↓
Ethernet Handler
    ↓
Protocol Handler
    ↓
Release / Recycle
```

No layer SHALL assume that another layer will implicitly free or preserve a packet buffer.

### 2.3 Problem: Troubleshooting Without Context

Traditional operating systems expose networking state across unrelated commands and facilities such as sockets, process lists, firewall rules, DNS tools, routing tools, packet utilities, browser tooling, and vendor-specific scripts.

Arcology SHALL treat connection provenance, intent, policy, and health as native connection metadata.

A technician SHOULD NOT need to correlate twenty independent tools to answer basic questions such as:

- What application opened this connection?
- Which user or session caused it?
- Is this a website, service call, human remote session, update, file transfer, management connection, or unknown traffic?
- Which contract authorized it?
- Which interface and route are being used?
- What DNS result produced the destination?
- Where did a failed connection fail?

This requirement begins influencing data structures as soon as endpoint and connection objects are introduced.

## 3. N0 Status and Protection of Completed Work

N0 Phase 1 is considered complete development work for steering purposes.

This document SHALL NOT cause N0 Phase 1 to be rewritten merely to adopt future naming, management, remote-support, or observability features.

Existing work SHOULD only be modified during N0 if one of the following is true:

- the current implementation prevents multiple NIC backends from sharing the same driver-side contract;
- packet ownership cannot be determined safely;
- the implementation permanently couples higher protocol layers directly to hardware-specific structures;
- a completed API makes later asynchronous operation impossible without replacement;
- a memory-safety or correctness issue is identified.

Everything else MAY be adapted at the next natural integration point.

The rule is:

Steer forward. Do not restart networking bring-up.

## 4. Revised Networking Direction

The original bring-up sequence remains broadly valid:

```
NIC Transport
    ↓
Ethernet II
    ↓
ARP
    ↓
Static IPv4
    ↓
ICMP
    ↓
UDP
    ↓
DHCP
    ↓
TCP
```

After the reliable transport milestone, development SHALL no longer proceed directly toward a disposable remote-shell-specific protocol.

Instead, TCP SHALL become the first carrier for the Arcology Interconnect architecture.

The revised upper-layer direction is:

```
TCP / Future Transports
        ↓
Arcology Interconnect
        ↓
Identity / Trust / Authorization
        ↓
Contract Multiplexing
        ↓
 ┌───────────────┬────────────────┬─────────────────┬──────────────────┐
 │ Development   │ Remote Assist  │ Management/RMM  │ Service Contract │
 │ Contracts     │ Contracts      │ Contracts       │ Invocation       │
 └───────────────┴────────────────┴─────────────────┴──────────────────┘
```

The networking stack SHALL remain usable independently of Interconnect.

Interconnect SHALL be a consumer of networking, not a replacement for it.

## 5. Arcology Interconnect

### 5.1 Objective

Arcology SHALL develop a native secure machine-to-machine interconnect intended to succeed traditional SSH/RPC/remote-support layering on Arcology platforms.

The Interconnect SHALL NOT be designed as merely a proprietary remote terminal.

It SHALL provide an authenticated, capability-aware relationship between Arcology systems and services.

Its responsibilities are expected to include:

- endpoint identity;
- authentication;
- trust establishment;
- authorization;
- contract discovery;
- contract activation visibility;
- multiplexed logical channels;
- structured request/response operations;
- event delivery;
- binary streams;
- interactive shell streams;
- remote display streams;
- development and management operations.

The provisional wire-protocol name MAY be Arcology Interconnect Protocol (AIP). Final naming is outside this steering document.

### 5.2 Structured Operations Over Terminal Emulation

A remote shell MAY be exposed through Interconnect, but shell execution SHALL be only one contract family.

Management tooling SHOULD use structured operations wherever possible.

For example, a deployment client SHOULD ultimately invoke a module staging operation directly rather than execute shell text and scrape its output.

Conceptually:

```
Interconnect.Invoke {
    contract: Substrate.ModuleManager
    operation: Stage
    artifact: network.aex
}
```

This allows command-line, graphical, automated, and RMM clients to share the same underlying management behavior.

## 6. Active and Inactive Contract Semantics

Arcology remote capabilities SHALL follow the existing active/inactive contract model.

For remote-capable contracts, inactive MUST mean absent from the active system graph, not merely disabled at a user-interface level.

An inactive remote contract SHOULD have no externally reachable behavior associated with that capability.

For example, when RemoteSupport is inactive:

```
Discover(RemoteSupport)
    → NOT_PRESENT
```

The implementation SHOULD ensure that an inactive remote contract has, where applicable:

- no published remote service;
- no active listener associated solely with that contract;
- no remote session accept path;
- no standing ephemeral session credentials;
- no remote-control input path;
- no discoverable capability advertisement.

Activation MAY instantiate the required resources:

```
Activate Contract
    ↓
Publish Capability
    ↓
Bind Interconnect Service
    ↓
Create Session Identity / Keys
    ↓
Permit Authorized Discovery
```

Deactivation SHOULD reverse the process:

```
Deactivate Contract
    ↓
Revoke Capability
    ↓
Terminate Associated Sessions
    ↓
Unpublish Service
    ↓
Destroy Ephemeral Session State
    ↓
Release Resources
```

Persistent policy or trust configuration MAY remain stored while the runtime capability is inactive.

General rule:

Inactive contracts MUST NOT retain externally reachable runtime behavior unless a separate contract explicitly defines that behavior as persistent.

## 7. Remote Assistance and Remote Desktop

Remote assistance SHALL be treated as a standardized Arcology capability rather than a permanently privileged vendor agent.

A support session SHOULD be capability-scoped.

Candidate capabilities include:

```
RemoteDisplay.View
RemoteDisplay.Control
Input.Keyboard.Inject
Input.Pointer.Inject
Clipboard.Read
Clipboard.Write
File.Transfer
Audio.Receive
Audio.Send
Diagnostics.Read
Shell.Remote
```

Granting one capability MUST NOT implicitly grant unrelated capabilities.

For example, granting remote display and keyboard control MUST NOT inherently grant filesystem access, an administrative shell, or arbitrary substrate authority.

Remote assistance SHOULD support temporary activation and automatic expiry.

A consumer system MAY default to:

```
RemoteSupport = INACTIVE
```

and activate it only when assistance is requested.

Enterprise or unattended systems MAY use persistent trust policy while still applying explicit capability grants.

### 7.1 Remote Display Transport

The first remote desktop implementation MAY use a straightforward framebuffer or surface-delta model.

Later implementations MAY use compositor-aware information to reduce bandwidth.

A possible progression is:

```
Raw Surface
    ↓
Dirty Region / Delta Surface
    ↓
Compressed Surface Updates
    ↓
Compositor-Aware Remote Display
```

The contract surface SHOULD remain stable enough that transport optimization does not require remote-support applications to be redesigned.

## 8. Operating-System-Level RMM

Remote Monitoring and Management SHALL be considered an operating-system-standardized capability family, not a requirement to install a third-party privileged agent.

Arcology SHALL expose authoritative management information and operations through standardized contracts.

The implementation SHOULD evolve toward contract families such as:

```
System.Identity
System.Inventory
System.Health
System.Telemetry
System.Events
Software.Inventory
Software.Deploy
Software.Update
Storage.Health
Network.Diagnostics
Security.Posture
Service.Control
RemoteSupport
Shell.Remote
Recovery.Control
```

These names are conceptual and MAY change during contract specification.

### 8.1 Capability-Scoped Management

A management organization or client SHALL be granted only the contracts permitted by policy.

An RMM relationship MUST NOT inherently imply unrestricted substrate or user-data access.

Example policy:

```
GRANT {
    System.Inventory.Read
    System.Health.Read
    System.Events.Subscribe
    Software.Inventory.Read
    Software.Deploy
    RemoteSupport.Request
}

DENY {
    User.Files.Read
    Storage.RawAccess
    Security.Policy.Disable
}
```

### 8.2 Event-Driven Monitoring

Arcology management SHOULD prefer event-driven subscriptions over constant high-frequency polling where practical.

For example:

```
SUBSCRIBE Storage.Health
WHEN State changes

SUBSCRIBE System.Health
WHEN Temperature exceeds policy threshold

SUBSCRIBE Software.Update
WHEN Update.Required becomes TRUE
```

Periodic snapshots MAY remain available where appropriate.

### 8.3 Substrate Availability

Core management capability SHOULD remain available independently of the normal graphical desktop or ordinary AEX applications where technically possible.

This is a primary advantage of substrate-level management.

Failure of a user's shell, graphical environment, profile, or ordinary application SHALL NOT automatically imply loss of management capability.

## 9. Network Observability and Troubleshooting

Arcology networking SHALL be designed so that troubleshooting information is gathered as part of normal connection lifecycle management rather than reconstructed later through unrelated tools.

### 9.1 Connection Provenance

A managed connection SHOULD retain metadata sufficient to identify:

- owning application or substrate service;
- owning application instance;
- associated user or interactive session, if any;
- contract that authorized the connection;
- declared connection intent;
- destination name originally requested;
- resolved destination address;
- transport protocol;
- selected interface;
- route or gateway selection;
- connection state;
- policy decision;
- relevant health and timing data.

### 9.2 Connection Intent

Arcology SHOULD classify connection purpose explicitly rather than infer purpose from port numbers.

Candidate connection kinds include:

```
HumanInteractive
Website
ApplicationService
SystemService
RemoteSupport
Management
FileTransfer
Update
Development
PeerService
Unknown
```

The classification SHOULD originate from the requesting contract or service whenever possible.

A TCP connection to port 443 MUST NOT automatically be labeled a website merely because HTTPS commonly uses that port.

### 9.3 Connection Inspection

A technician-facing connection view SHOULD be able to present information similar to:

```
Connection #1842

Owner
    Application: ArcoBrowser
    Instance: 72
    User Session: 3
    Interactive: Yes

Intent
    Kind: Website
    Destination: docs.example.com
    Requested By: Browser.Tab[4]

Path
    Interface: Ethernet0
    Local: 192.168.1.47:51422
    Remote: 203.0.113.18:443
    Gateway: 192.168.1.1
    DNS Result: 203.0.113.18

Transport
    Protocol: TCP
    State: ESTABLISHED
    RTT: 21 ms

Policy
    Contract: Network.Web.Outbound
    Decision: ALLOW
```

Exact presentation is non-normative.

### 9.4 Failure Provenance

Failed connection attempts SHOULD retain enough temporary diagnostic state to identify the failure stage.

Examples include:

```
Link
Address Configuration
ARP / Neighbor Resolution
Routing
DNS Resolution
Contract Authorization
Policy Evaluation
Transport Establishment
Remote Authentication
Application Protocol
```

A diagnostic system SHOULD be able to report, for example:

```
Application: ArcoMail
Destination: mail.example.com
Result: FAILED
Failure Stage: DNS Resolution
Reason: Resolver Timeout

Ethernet Link ....... PASS
Address ............. VALID
Gateway ............. REACHABLE
DNS Server .......... 192.168.1.1
DNS Response ........ TIMEOUT
```

This is preferred over requiring the technician to manually chain separate diagnostic commands.

### 9.5 Diagnostic Intent

Utilities such as ICMP echo SHALL remain useful internal primitives, but they SHOULD NOT define the technician-facing troubleshooting model.

Instead of requiring the technician to know which primitive to run first, Arcology SHOULD provide diagnostic intent such as:

```
network diagnose Internet
network diagnose example.com
network inspect ArcoMail
network inspect connection 1842
```

The diagnostic subsystem MAY internally perform:

- link checks;
- address validation;
- ARP checks;
- route checks;
- ICMP tests;
- DNS tests;
- TCP establishment tests;
- contract and policy evaluation.

The output SHOULD answer the troubleshooting question, not merely expose raw primitive results.

## 10. Data Model Steering

The following future data requirements SHOULD influence endpoint and connection object design when those structures are introduced.

A connection record SHOULD be capable of referencing or storing:

```
ConnectionID
OwnerID
OwnerType
SessionID
ContractID
ConnectionKind
RequestedName
ResolvedAddress
LocalEndpoint
RemoteEndpoint
InterfaceID
RouteID
Protocol
State
PolicyDecision
CreatedTimestamp
LastActivityTimestamp
DiagnosticState
```

Not every field must be implemented during early protocol bring-up.

The requirement is to avoid designing endpoint structures that make provenance impossible or require invasive redesign later.

Diagnostic history MUST be bounded.

Arcology SHALL NOT require indefinite storage of complete user network activity merely to provide troubleshooting capability.

Retention policy and privacy behavior SHALL be specified before persistent network history is enabled by default.

## 11. Revised Development Milestones

This steering document adopts the following milestone direction.

**N0 -- NIC Transport**

N0 Phase 1 -- COMPLETE

Completed work SHALL be retained.

No rewrite is required by this addendum unless a violation of the architectural conditions in Section 3 is identified.

Remaining N0 Work

Continue establishing the generic NIC transport boundary and validate that the first backend does not leak hardware-specific assumptions upward.

Acceptance direction:

- the NIC can initialize;
- transmit and receive paths operate;
- frame ownership is defined;
- interface identity and MAC are available;
- capabilities can be reported;
- a future second backend can attach behind the same boundary.

**N1 -- Ethernet Fabric**

Implement Ethernet II framing, validation, EtherType dispatch, MAC handling, and connection to the NIC transport contract.

No Interconnect-specific behavior belongs here.

**N2 -- Local IPv4 Reachability**

Implement:

- ARP;
- static IPv4 configuration;
- minimal routing for directly connected network and configured gateway;
- ICMP echo.

Primary milestone:

Arcology can reliably exchange ICMP traffic with another machine on the development LAN.

**N3 -- Automatic Addressing**

Implement:

- UDP;
- DHCP client;
- basic obtained DNS configuration storage.

Static configuration MUST remain available.

**N4 -- Reliable Transport**

Implement the TCP subset required for reliable Arcology development and Interconnect bring-up.

Do not expand N4 into a production-complete Internet TCP implementation unless required for correctness of the supported use case.

**N5 -- Interconnect Foundation**

Replace the previously planned disposable ARCODEV/1 remote-shell protocol milestone with the smallest viable Arcology Interconnect implementation.

N5 SHOULD establish:

- protocol/version negotiation;
- Arcology endpoint identity;
- session establishment;
- structured framing;
- logical channel multiplexing;
- contract/service advertisement;
- bounded message handling;
- clean session termination.

Strong cryptographic identity MAY be phased according to availability of Arcology crypto primitives, but insecure operation MUST remain restricted to explicitly trusted development environments.

**N6 -- Development Contracts**

Implement the first Interconnect consumers:

- remote shell;
- system information;
- binary file transfer;
- artifact verification;
- staging;
- reboot control.

This SHALL replace the original concept of making remote development itself the transport architecture.

**N7 -- Network Observability**

Introduce first-class connection inspection and failure provenance before application networking becomes widespread.

At minimum, connection records SHOULD expose:

- owner;
- intent;
- destination;
- protocol;
- interface;
- state;
- authorization source;
- failure stage where applicable.

**N8 -- Remote Assistance Foundation**

Implement initial contract-scoped remote assistance.

Minimum expected capability:

- remote display view;
- optional keyboard/pointer control;
- explicit activation/deactivation;
- session termination on contract deactivation;
- no implicit filesystem or shell access.

**N9 -- Native Management Profile**

Define and implement the first Arcology OS management contract profile suitable for development systems and later RMM clients.

Initial scope SHOULD favor read-only health/inventory/event capabilities plus explicitly authorized development and remote-support actions.

## 12. Explicit Non-Derailment Rules

During current networking bring-up, implementation agents MUST NOT stop N0-N4 work to implement:

- full RMM dashboards;
- remote desktop codecs;
- public relay infrastructure;
- NAT traversal;
- production identity PKI;
- persistent telemetry databases;
- graphical network troubleshooting UI;
- distributed transparent contracts;
- SSH compatibility;
- third-party management integrations;
- IPv6 solely because Interconnect will eventually benefit from it.

These are architectural targets, not prerequisites for basic Ethernet bring-up.

Agents SHOULD create extension points and preserve metadata where inexpensive, then continue the current milestone.

The steering principle is:

Do not implement tomorrow's feature today; do avoid making tomorrow's feature impossible.

## 13. Immediate Steering for Current Development

Because N0 Phase 1 is already complete, the immediate development instructions are:

- Continue N0 according to the existing RFC.
- Preserve the current working NIC backend.
- Ensure the NIC/backend boundary is generic rather than protocol-specific.
- Document packet/frame ownership at each asynchronous boundary.
- Add interface capability reporting only where it naturally fits current N0 work.
- Do not implement RMM, Interconnect, or remote desktop during N0.
- When endpoint/connection structures appear in later milestones, reserve a clean mechanism for owner, contract, intent, and diagnostic metadata.
- Continue toward Ethernet II, ARP, static IPv4, and ICMP without changing the milestone order.
- When TCP becomes usable, begin the Interconnect foundation rather than implementing a throwaway standalone ARCODEV protocol.
- Treat the existing local shell command dispatcher as the future target of the remote-shell contract rather than duplicating shell functionality.

## 14. Future RFC Split

This steering addendum intentionally avoids fully specifying several large subsystems.

Separate RFCs SHOULD eventually define:

- Arcology Interconnect Protocol and Trust Architecture
- Arcology Native Management / RMM Contract Standard
- Arcology Remote Assistance and Remote Display Contracts
- Arcology Network Observability and Diagnostic Model

The current Networking Fabric RFC SHOULD remain responsible for network transport and protocol infrastructure.

This separation prevents basic networking implementation from being blocked by higher-level architecture while preserving a clear development destination.

## 15. Acceptance Criteria for This Steering Change

This steering change is successfully incorporated when:

- N0 Phase 1 remains valid and usable;
- current NIC work continues without unnecessary rewrite;
- future remote development is targeted at Arcology Interconnect rather than a disposable SSH-like protocol;
- active/inactive remote contracts are understood as runtime capability presence/absence;
- remote assistance is capability-scoped rather than equivalent to unrestricted machine control;
- OS-native RMM is recognized as a standardized contract family;
- endpoint/connection designs preserve the ability to attach provenance and intent;
- network troubleshooting is designed around authoritative connection context and diagnostic intent;
- implementation agents are explicitly prevented from prematurely building the later features during low-level network bring-up.

## 16. Architectural Summary

The intended long-term stack is now:

```
                         Arcology Applications / Tools
                                    │
                 ┌──────────────────┼────────────────────┐
                 │                  │                    │
          Development          Remote Assist        Management/RMM
          Contracts            Contracts            Contracts
                 │                  │                    │
                 └──────────────────┼────────────────────┘
                                    │
                          Arcology Interconnect
                                    │
                        Identity / Trust / Policy
                                    │
                         Networking Contracts
                                    │
                         Networking Fabric
                                    │
                    TCP / UDP / IPv4 / ARP / ICMP
                                    │
                             Ethernet II
                                    │
                    Network Interface Contract
                                    │
                              NIC Backend
                                    │
                                Hardware
```

Across the entire stack, Arcology networking SHALL pursue three defining properties:

1. Contract-native hardware abstraction.
2. Explicit resource and connection ownership.
3. Networking that knows who requested a connection, why it exists, what authorized it, and how it is behaving.

The immediate implementation remains N0/N1 networking bring-up. The architecture above exists to ensure that the work being completed now becomes the foundation for Arcology's later interconnect, management, support, and diagnostic capabilities rather than another conventional network stack that must eventually be worked around.

---

## Implementation Log

|Date|Summary|
|----|-------|
|2026-08-26|Addendum received and saved verbatim, design-only for the sections beyond N0. Immediately continued "remaining N0 work" per Section 11/13: added a real, generic driver-side Network Interface Contract (`stdlib/network_interface_contract.abas`, an explicit backend-tag dispatch -- `NetworkInterface.Start/Stop/GetCapabilities/GetMAC/GetLinkState` -- matching this project's own established provider-tag-dispatch idiom) sitting in front of a real VirtIO-net legacy PCI backend (`stdlib/virtio_net_policy.abas`): PCI discovery scoped to VirtIO's own vendor ID, firmware-driver eviction (reusing RFC-0045 Phase 3's own confirmed pattern), I/O-BAR mapping, device reset, and real minimal feature negotiation (this driver accepts only VIRTIO_NET_F_MAC and VIRTIO_NET_F_STATUS, not a blind echo of every device-offered feature). Proven by new smoke test `systems_virtio_net_transport_smoke`, run through the generic contract rather than the backend directly: a real QEMU legacy virtio-net-pci device (`disable-modern=on`, `vectors=0` for a deterministic legacy config-space offset, fixed `mac=` for a deterministic expected value) is discovered, mapped, reset, negotiated, and its real MAC (byte-exact) and real link-up state read back correctly; a no-NIC negative control confirms `NetworkInterface.Start` fails honestly with no crash. Full regression suite remains green. Deliberately not claimed yet, per Section 12's own non-derailment rule: virtqueue setup, frame transmit/receive, and packet ownership semantics -- the next real N0 increment, not implemented ahead of that proof.|
