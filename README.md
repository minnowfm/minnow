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
data/                  Packaging: .desktop file, icon, AppStream metainfo
```

## Known limitations

- No thumbnailing, drag-and-drop, or service menus yet
- No multi-window support
- Window corners aren't rounded - Qt can't round a native top-level window without going frameless, which would drop KDE's native titlebar and controls
