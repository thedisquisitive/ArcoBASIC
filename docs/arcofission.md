# ArcoFission

ArcoFission is the ArcoBASIC compiler pipeline tool. In the current alpha it can inspect compiler stages, emit hosted bytecode, run hosted bytecode, build Linux ELF64 runtime capsules, and build UEFI x86-64 PE32+ images for the freestanding systems target.

## Build

Build the tool from the repository root:

```sh
cmake -S . -B build
cmake --build build --target ArcoFission
```

The native capsule builder expects the `ArcoFission` executable to come from a CMake build tree so it can reuse the ArcoBASIC static libraries beside that executable. It does not require your shell's current directory to be the build directory.

## Inspect Compiler Stages

Reveal the parsed AST:

```sh
build/ArcoFission reveal examples/hello.bas at AST
```

Reveal A-MIR:

```sh
build/ArcoFission reveal examples/hello.bas at A-MIR
```

Reveal hosted bytecode:

```sh
build/ArcoFission reveal examples/hello.bas at BYTECODE
```

The `--stage` spelling is also accepted:

```sh
build/ArcoFission reveal examples/hello.bas --stage BYTECODE
```

## Bytecode

Write `.arcof-text` bytecode:

```sh
build/ArcoFission bytecode examples/hello.bas -o hello.arcof
```

`build` also writes bytecode when the output path ends in `.arcof`:

```sh
build/ArcoFission build examples/hello.bas -o hello.arcof
```

Run bytecode:

```sh
build/ArcoFission run hello.arcof
```

Compile and run in one step:

```sh
build/ArcoFission compile-run examples/hello.bas
```

## Linux Native Capsules

Build a Linux ELF64 runtime capsule:

```sh
build/ArcoFission build examples/hello.bas -o hello
./hello
```

The explicit native command is equivalent:

```sh
build/ArcoFission native examples/hello.bas -o hello
./hello
```

In the alpha compiler model, the executable is native on the outside and runs the ArcoFission bytecode VM on the inside. Native capsules embed a compact binary bytecode payload and link it with the ArcoFission runtime from the active CMake build tree. The hosted VM prepares typed slots and fused numeric bytecode for common loop arithmetic before execution.

Native capsules are currently Linux-only ELF64 outputs.

## Instruction Limits

Hosted run/build commands accept an operator instruction limit:

```sh
build/ArcoFission compile-run program.abas --instruction-limit 50000000
build/ArcoFission run program.arcof --instruction-limit unlimited
build/ArcoFission native program.abas -o program --instruction-limit 50000000
```

`unlimited` disables instruction-count termination for that invocation.

## UEFI Target

Build a freestanding UEFI x86-64 PE32+ image:

```sh
build/ArcoFission build arcology-os/tests/fixtures/uefi-hello/hello.abas -o hello.efi --target uefi-x86_64
```

Use `--entry NAME` to select a different entry function:

```sh
build/ArcoFission build kernel.abas -o kernel.efi --target uefi-x86_64 --entry Main
```

## Exit Codes

ArcoFission exits with:

```text
0  success
1  source intake, build, or runtime failure
2  command-line usage or option error
```
