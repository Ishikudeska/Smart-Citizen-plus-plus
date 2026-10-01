# SCX (working title)

A Star Citizen localization editor and P4K explorer: a C++ / Qt Quick
successor that combines Smart Citizen's string editor and enhancement
generator with unp4k/unforge's archive and DataForge tooling in one app.

> The name is a placeholder. It lives in one place, `cmake/AppIdentity.cmake`.

## Layout

| Path | What |
|---|---|
| `src/engine/` | Qt-free format library: P4K archives, DataForge, CryXML, game-data export |
| `src/core/` | Application logic (Qt Core): settings, INI merge/apply, enhancement generator, blueprints, i18n |
| `src/app/` | Executable: C++ view-models and the QML frontend (`src/app/qml/`) |
| `tests/` | Qt Test suites, run through CTest |
| `tools/parity/` | Dev-only drivers that compare output against the original Python/.NET tools |
| `packaging/` | Deployment and installer scripts |

## Build

Requires Qt 6.8+ with the MinGW kit (developed against Qt 6.12.0 /
MinGW 13.1 at `C:\Qt`). Other paths: copy a preset into
`CMakeUserPresets.json` and override `SC_QT_DIR` / `SC_MINGW_DIR`.

```powershell
cmake --preset mingw-debug
cmake --build --preset mingw-debug
ctest --preset mingw-debug
```

Qt Creator opens the folder directly (File → Open File or Project →
`CMakeLists.txt`) and picks up the presets.

Third-party libraries (zstd, zlib, pugixml) are downloaded and built by CMake
on first configure, pinned by SHA-256 in `cmake/Dependencies.cmake`.

## License

Apache License 2.0. See `NOTICE` for attributions (Smart Citizen, unp4k) and
third-party licenses.
