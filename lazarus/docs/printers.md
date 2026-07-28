# Printer Management

Administration includes a service-backed Printer Management page for SMART
reports and service documentation.

Supported operations:

- List configured CUPS printers and the current default.
- Discover IPP Everywhere and AirPrint printers through DNS-SD.
- Automatically create queues for compatible printers through `cups-browsed`.
- Install a discovered printer from the administration interface.
- Add a network printer by IPv4 address, hostname, or full CUPS device URI.
  Hostnames are resolved to numeric IPv4 addresses before CUPS configuration.
- Automatically try IPP Everywhere, secure IPP, and JetDirect/PCL in order.
- Select IPP, secure IPP, or JetDirect explicitly when automatic detection is
  inappropriate for the printer.
- Set the default printer.
- Submit the CUPS test page.
- Remove a selected printer after typing `REMOVE`.

The unprivileged GTK process does not run CUPS tools. It sends structured
requests to `lazarus-service`, which validates printer names, network
addresses, and URI schemes and invokes CUPS commands without a shell. Discovery
uses `ippfind`; configuration uses `lpadmin`. CUPS receives a bounded 30-second
attempt for each connection method. The accepted URI and driver are
returned to the interface, and a successfully added printer becomes the default
report printer. Configuration changes require a valid administrator session.
The root-owned backend is enrolled in CUPS' `lpadmin` system group while the
unprivileged GTK kiosk user is not, so the GUI cannot administer CUPS directly.

Lazarus OS starts CUPS, Avahi, and `cups-browsed` as separate OpenRC services.
The discovery policy creates local queues for driverless printers announced on
the local network and deliberately excludes queues merely shared by another
CUPS server. A printer that does not advertise itself can still be configured
with a conventional URI such as:

```text
ipp://192.168.1.50/ipp/print
ipps://printer.local/ipp/print
socket://192.168.1.50:9100
```

Installed appliances retain CUPS state on their system disk. Live appliances
also mirror printer definitions and PPD data to configured image storage. The
`lazarus-printers` OpenRC service restores that state before CUPS starts.
