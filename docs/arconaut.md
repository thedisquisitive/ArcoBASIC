# Arconaut

Arconaut is the graphical ArcFS administrator. It is written in ArcoBASIC at
`arcfs-utils/apps/arconaut/arconaut.abas` and built by ArcoFission into the native `arconaut` capsule.

Current Linux scope:

- enumerate block devices with `lsblk`;
- select ArcFS devices and image files;
- create and format ArcFS image files;
- inspect volumes and list the root directory through `arcfs-linux`;
- mount device volumes through udisks and image files through `arcfsctl mount`;
- open mounted volumes in the desktop file manager;
- refresh/install Linux ArcFS support using the packaged helper tools;
- apply supplied `.deb` ArcFS support updates;
- discover and run ArcoBASIC addon scripts from `~/.arcology/arcfs/plugins`.

Build it directly:

```sh
cmake --build build --target arconaut
arcfs-utils/build/generated/arconaut
```

The package launcher starts `/usr/bin/arconaut` directly — the compiled native capsule, not the
`.abas` source file.
