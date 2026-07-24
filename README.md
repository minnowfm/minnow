# Minnow

A simple, lightweight file manager for KDE - built from scratch against Qt6 and KDE Frameworks (KIO), as a smaller alternative to Dolphin.

| Light | Dark |
| --- | --- |
| ![Minnow, light theme](docs/screenshot-light.png) | ![Minnow, dark theme](docs/screenshot-dark.png) |

## Features

- Grid (icon) and list (details) views, switchable per folder - each folder remembers its own view mode
- Per-folder sorting (column + order), independent of other folders
- Places sidebar: Home, Documents, Downloads, Pictures, Music, Videos, Trash - each can be shown or hidden
- Pin any folder to the sidebar, optionally into your own custom sections (create/delete sections from the sidebar's right-click menu)
- Mounted drives shown automatically below your pins
- Address bar navigation, back/forward/up history, name filtering
- Adjustable icon size (Small/Medium/Large/Huge), defaulting to large icons
- File operations via KIO: copy, cut/paste, rename, new folder, move to trash, permanent delete
- Undo/redo (`Ctrl+Z` / `Ctrl+Shift+Z`) for copy, move, trash, mkdir, and rename - permanent delete is intentionally not undoable
- `Delete` to trash the current selection, `Shift+Delete` to delete it permanently
- Open a specific directory from the command line: `minnow /some/path`

## Dependencies

- Qt6 (Widgets)
- KDE Frameworks 6: CoreAddons, ConfigWidgets, WidgetsAddons, KIO
- CMake ≥ 3.16, a C++17 compiler

## Building

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## Running

```sh
./build/minnow              # opens your home directory
./build/minnow /some/path   # opens a specific directory (or a file's parent directory)
```

## Installing

```sh
cmake --install build --prefix /usr/local
```

This installs the `minnow` binary, a `.desktop` file, an icon, and an AppStream metainfo file to the standard FHS locations (`bin`, `share/applications`, `share/icons/hicolor/scalable/apps`, `share/metainfo`).

## Packaging

Packaging manifests/scripts live under `packaging/`, one subdirectory per format. All of them
currently point at the `v0.1.0` tag as a placeholder — replace it once a real release tag exists.

- **AUR** — maintained separately in the AUR itself, not in this repo.
- **Flatpak** (`packaging/flatpak/net.minnow.Minnow.yaml`) — builds against `org.kde.Platform`.
  Build locally with `flatpak-builder`:
  ```sh
  flatpak-builder --user --install build-dir packaging/flatpak/net.minnow.Minnow.yaml
  ```
  To publish on Flathub, submit this manifest (with a real tag) as a PR to
  [flathub/flathub](https://github.com/flathub/flathub) following their submission process.
- **Snap** (`packaging/snap/snapcraft.yaml`) — uses the `kde-neon` extension. Build with:
  ```sh
  cd packaging/snap && snapcraft
  ```
  Publish to the Snap Store with `snapcraft upload`. Note strict confinement means Minnow only
  gets access to the home directory and removable media by default, not the whole filesystem.
- **.deb** (`packaging/deb/`) — run `packaging/deb/build.sh` (needs `dpkg-dev`, `debhelper`).
  Output goes to `dist/`.
- **.rpm** (`packaging/rpm/`) — run `packaging/rpm/build.sh` (needs `rpm-build`). Output goes to
  `dist/`.

### apps.kde.org

There's no separate manual submission step for apps.kde.org — its catalog is generated from
AppStream metadata (`data/net.minnow.Minnow.metainfo.xml`) for apps that are discoverable
through Flathub or distro repos. Getting Minnow onto Flathub (see above) with valid metainfo
is what makes it eligible to be picked up there.

## Testing

Automated tests cover the pure path-computation logic (folder navigation, rename/mkdir destination paths) via QTest:

```sh
ctest --test-dir build --output-on-failure
```

## Project layout

```
src/                  Application source
  main.cpp            Entry point, CLI argument parsing
  MainWindow.*         Toolbar, views, navigation, file operations, context menus
  PlacesSidebar.*      Sidebar: places, pinned sections, drives
  FileOperations.*     KIO job wrappers (copy/move/trash/rename/mkdir), undo recording
  PathUtils.*          Pure URL/path helpers, shared and unit-tested
tests/                 QTest unit tests
data/                  .desktop file, icon, AppStream metainfo
packaging/             Flatpak manifest, Snap manifest, .deb/.rpm build scripts
```

## Known limitations

- No thumbnailing or service menus yet
- No multi-window support
- Window corners aren't rounded - Qt can't round a native top-level window without going frameless, which would drop KDE's native titlebar and controls
