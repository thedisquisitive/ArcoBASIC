# ArcoBASIC xbps-src package definition

This directory is a Void Linux `xbps-src` package definition for ArcoBASIC.

For normal alpha release builds, use the project helper:

```sh
scripts/build-void-native-package.sh --void-packages /path/to/void-packages --out arcoalpha
```

The helper creates a local source tarball, copies it into the `void-packages`
source cache, writes the matching checksum into the template, builds with
`xbps-src`, and copies the `.xbps` plus repository metadata into `arcoalpha/`.

Manual use from a `void-packages` checkout is still possible:

```sh
cp -a packaging/void/srcpkgs/arcobasic /path/to/void-packages/srcpkgs/
cp dist/source/arcobasic-0.1.0.tar.gz /path/to/void-packages/hostdir/sources/arcobasic-0.1.0/
cd /path/to/void-packages
./xbps-src -A x86_64 pkg arcobasic
```

The generated archive from:

```sh
scripts/build-linux-packages.sh --xbps-src
```

contains this same `srcpkgs/arcobasic` structure for easier copying.

Do not use a host-built `xbps-create` package as a release artifact. Building
inside `xbps-src` is what records the correct Void runtime dependencies.
