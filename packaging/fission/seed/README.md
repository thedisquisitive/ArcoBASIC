# Fission seed compiler

`fission-native-linux-x86_64` is the native Arcology Fission compiler
(`fission/cli/fission_build.abas` compiled by itself), checked in so that
`scripts/install/install-fission.sh` installs in seconds instead of
bootstrapping from the legacy C++ compiler.

- Linux x86-64 only. SHA-256 in `fission-native-linux-x86_64.sha256`; the
  installer refuses a seed that does not match.
- It is a verified fixpoint of the sources it was committed with: compiling
  `fission/` with it reproduces it byte for byte.
- Refresh it with `scripts/build/refresh-fission-seed.sh` whenever `fission/`
  changes the compiler, and commit the new binary with the change. Anyone can
  rebuild from nothing with `BOOTSTRAP=1 scripts/install/install-fission.sh`.
