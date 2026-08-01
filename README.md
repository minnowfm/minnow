# Minnow

A simple, lightweight file manager for KDE, built against Qt6 and KDE Frameworks (KIO). Meant as a smaller alternative to Dolphin.

| Light | Dark |
| --- | --- |
| ![Minnow, light theme](docs/screenshot-light.png) | ![Minnow, dark theme](docs/screenshot-dark.png) |

## Features

- Grid and list views, switchable per folder
- Per-folder sorting, independent of other folders
- Places sidebar (Home, Documents, Downloads, Pictures, Music, Videos, Trash), each can be hidden
- Pin folders to the sidebar, group them into your own custom sections
- Mounted drives shown automatically below your pins
- Address bar navigation, back/forward/up history, name filtering
- Adjustable icon size (Small/Medium/Large/Huge)
- Copy, cut/paste, rename, new folder, move to trash, permanent delete (all via KIO)
- Undo/redo for everything except permanent delete
- Compress files/folders to a `.zip`, or extract `.zip`/`.tar`/`.tar.gz`/`.tar.bz2`/`.tar.xz` archives, without blocking the UI on large archives
- Right-click menu shows the common actions by default, with a "Show More Options" entry for the rest
- Open a specific directory from the command line: `minnow /some/path`

## Installing

**Arch Linux (AUR):** two packages, `minnow` (builds from source) and `minnow-bin` (prebuilt).

```sh
yay -S minnow
# or
yay -S minnow-bin
```

**Snap:** [snapcraft.io/minnow](https://snapcraft.io/minnow)

```sh
sudo snap install minnow
```

**From source:** see Building and Running below.

## Dependencies

- Qt6 (Widgets)
- KDE Frameworks 6: CoreAddons, ConfigWidgets, WidgetsAddons, KIO
- CMake ≥ 3.16, a C++17 compiler

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

To install system-wide instead:

```sh
cmake --install build --prefix /usr/local
```

## Running

```sh
./build/minnow              # opens your home directory
./build/minnow /some/path   # opens a specific directory (or a file's parent directory)
```

## Known limitations

**On KDE Plasma, "Show in folder" from a browser may still open Dolphin instead of Minnow, even with Minnow set as your default file manager.** This isn't a Minnow bug - browsers use the `org.freedesktop.FileManager1` D-Bus interface for this (not just the `inode/directory` MIME association), and on Plasma that name is held from login by a persistent `dolphin --daemon` (tied to `plasma-dolphin.service`) that refuses to hand it over to anything else. Every non-Dolphin file manager on Plasma runs into the same thing. The only fix is disabling Dolphin's background service:

```sh
systemctl --user mask plasma-dolphin.service
# then log out and back in
```

Reversible with `systemctl --user unmask plasma-dolphin.service`.

## Testing

```sh
ctest --test-dir build --output-on-failure
```

Covers the path-computation logic (folder navigation, rename/mkdir destination paths).

## Contributing

PRs are welcome - open one if you'd like to fix something or add a feature. No guarantees it'll get merged, but if it's solid I'm happy to take it.
