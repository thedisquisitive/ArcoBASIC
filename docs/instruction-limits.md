# Hosted instruction limits

Hosted ArcoBASIC programs may request a finite execution budget at the application entry point:

```basic
#INSTRUCTION_LIMIT 10000000
```

The value must be a decimal integer from 1 through 9007199254740991. It is compile metadata, not
permission: embedded hosts reject source requests by default and may authorize them with a hard
maximum. Standalone `arco_cli` and hosted ArcoFission execution authorize finite source requests.

An operator can supersede source metadata for one invocation:

```text
arco_cli --instruction-limit 50000000 program.abas
ArcoFission compile-run program.abas --instruction-limit 50000000
ArcoFission run program.arcof --instruction-limit unlimited
```

ArcoFission hosted native builds accept the same option. Numeric zero
is rejected; the explicit, case-insensitive `unlimited` spelling disables instruction-count
termination. Unlimited execution can consume CPU indefinitely and does not relax filesystem,
network, memory, or other host permissions.

Precedence is host hard maximum, operator override, source request, then the 100,000 default.
Requests beyond host policy fail before the first statement and are never silently clamped.
