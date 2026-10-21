# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## About This Project

This project is derived from the [Pamplejuce](https://github.com/sudara/pamplejuce) template — a JUCE audio plugin template using CMake, C++23, and modern CI/CD. It builds cross-platform (macOS, Windows, Linux) with support for multiple plugin formats (VST3, AU, AUv3, CLAP, Standalone).

The template provides the build system, CI/CD, and project structure. The plugin-specific logic lives in `source/`.

## Build Commands

Use Xcode with the existing `Builds` directory. Builds default to Debug for development; use Release when making music if Debug causes high CPU use or audio dropouts.

Debug builds stay in `Builds/Lumiere_artefacts/Debug` and never replace installed plugins. Only Release AU/VST3 builds deploy to the user plugin folders. Deployment verifies a staged bundle and moves the previous copy into `Builds/plugin-backups`. Reload the plugin in the DAW after installing Release; an already-loaded instance keeps its old binary.

```bash
# Configure (run once, or after CMakeLists.txt changes)
cmake -B Builds -G Xcode

# Open the generated project in Xcode
open Builds/Lumiere.xcodeproj

# Build the three plugin targets (avoids the failing Benchmarks target)
cmake --build Builds --config Debug --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone

# Build optimized plugins when needed
cmake --build Builds --config Release --target Lumiere_VST3 Lumiere_AU Lumiere_Standalone
```

To run tests explicitly, build `Tests` first, then run `ctest --test-dir Builds -C Debug --verbose --output-on-failure`. Do not build Benchmarks unless asked.

On macOS for universal binaries, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` when configuring.

## Project Configuration

- Internal project/target name: `Lumiere`
- Plugin display name: `Lumiere Test v0.1`
- Company: `Beyond Analog`
- Type: audio effect
- Bundle ID: `com.beyondanalog.lumiere`
- Manufacturer/plugin codes: `BADA` / `NOR1` (preserved from the existing project)
- Intel IPP and distribution code signing are disabled for now.
- GitHub Actions workflows are retained as `.yml.disabled` files so CI/CD does not run. Review the commented platform, IPP, and signing sections before re-enabling them.

## Project Structure

- `source/` - Plugin source code (PluginProcessor, PluginEditor)
- `tests/` - Catch2 test files
- `benchmarks/` - Catch2 benchmark files
- `cmake/` - CMake modules (Tests.cmake, Benchmarks.cmake, Assets.cmake, etc.)
- `modules/` - Git submodules: clap-juce-extensions, melatonin_inspector
- `JUCE/` - JUCE framework (git submodule)
- `assets/` - Binary resources (auto-included via juce_add_binary_data)
- `packaging/` - Installer resources and scripts

## Architecture

**SharedCode Library**: The `SharedCode` INTERFACE library links plugin source code to both the main plugin target and the Tests target, avoiding ODR violations.

**CMake Modules**:
- `PamplejuceVersion.cmake` - Reads VERSION file, optional auto-bump patch level
- `PamplejuceFunctions.cmake` - One include that defines the `pamplejuce_*` functions below (and turns off their legacy include-time behavior)
- `PamplejuceIPP.cmake` - Intel IPP integration (optional)

Target setup happens via explicit function calls in `CMakeLists.txt`, not at include time:
- `pamplejuce_add_assets()` - Includes all files in assets/ as binary data (from `Assets.cmake`)
- `pamplejuce_add_tests()` - Configures the Catch2 test target (from `Tests.cmake`)
- `pamplejuce_add_benchmarks()` - Configures the Catch2 benchmark target (from `Benchmarks.cmake`)
- `pamplejuce_shared_code_defaults()` - C++23, fast math (from `SharedCodeDefaults.cmake`)
- `pamplejuce_xcode_prettify()` - Xcode folder/scheme cleanup (from `XcodePrettify.cmake`)

Note: after `include(PamplejuceFunctions)`, a plain `include(Tests)` (or Assets, etc.) is a no-op - call the function instead.

**Test Discovery**: Uses `catch_discover_tests()` with `PRE_TEST` discovery mode for Xcode compatibility.

## Key Configuration

Edit `CMakeLists.txt` to customize:
- `PROJECT_NAME` - Internal name (no spaces)
- `PRODUCT_NAME` - Display name in DAWs (can have spaces)
- `COMPANY_NAME` - Used for bundle name
- `BUNDLE_ID` - macOS bundle identifier
- `FORMATS` - Plugin formats to build (Standalone AU VST3 AUv3)
- `PLUGIN_MANUFACTURER_CODE` / `PLUGIN_CODE` - 4-character plugin IDs

Version is read from the `VERSION` file in project root.

## Code Quality

Always resolve any compile warnings encountered during builds. Warnings should be treated as errors and fixed before considering a task complete.

Note: LSP/clangd often reports false positive diagnostic errors (like "undeclared identifier", "file not found") because it doesn't have full context of the JUCE module system. Ignore these unless the actual build fails.

## Includes

JUCE modules include common standard library headers (`<vector>`, `<algorithm>`, `<string>`, `<memory>`, etc.) so you don't need to add those explicitly in JUCE code. Adding them is harmless but redundant.

## Threading Model

JUCE plugins have two main threads:

- **Audio thread**: Runs `processBlock` — must be realtime-safe (see below). Never block, allocate, or lock.
- **Message thread**: Runs UI callbacks, parameter listeners, and timer callbacks. Owns the `MessageManager`.

To communicate between them:
- **Simple values**: Use `std::atomic` or JUCE's `AudioParameterFloat`/`AudioParameterBool` (which are atomic under the hood)
- **Larger data**: Use a lock-free queue (e.g. `moodycamel::ReaderWriterQueue`) to pass data from message → audio thread
- **Audio → UI updates**: Use `juce::AsyncUpdater` or `juce::Timer` on the message thread to poll state — never call UI code from the audio thread

## Realtime Safety

For anything in the audio thread / hot DSP path (e.g. `processBlock`):
- Allocate in constructors or `prepareToPlay`, not while rendering audio
- Avoid dynamic allocations and container growth (`std::vector::push_back`, map insertion, string building)
- Prefer fixed-size storage (`std::array`, preallocated buffers, fixed-capacity queues)
- Keep operations deterministic and lock-free where possible

## Adding Dependencies

**JUCE Modules** live in `modules/` as git submodules. Add with `git submodule add`, then `add_subdirectory` and link to `SharedCode` in `CMakeLists.txt`. Some useful ones:

- [melatonin_inspector](https://github.com/sudara/melatonin_inspector) — runtime component debugger (already included)
- [melatonin_blur](https://github.com/sudara/melatonin_blur) — fast cross-platform blurs for C++ UI (shadows, glows, frosted glass)
- [melatonin_perfetto](https://github.com/sudara/melatonin_perfetto) — performance tracing with Perfetto, great for profiling `processBlock` and paint calls
- [gin](https://github.com/FigBug/gin) — large collection of utilities (DSP, UI components, LookAndFeel, etc.)

**Non-JUCE C++ libraries** should be added via [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) which is already configured. CPM downloads and caches dependencies at configure time — no submodule needed:

```cmake
CPMAddPackage("gh:nlohmann/json@3.11.3")
target_link_libraries(SharedCode INTERFACE nlohmann_json::nlohmann_json)
```

Some useful CPM libraries:
- [nlohmann/json](https://github.com/nlohmann/json) — JSON parsing/serialization
- [cameron314/readerwriterqueue](https://github.com/cameron314/readerwriterqueue) — lock-free single-producer/single-consumer queue, ideal for audio↔message thread communication

## Code Style

Uses `.clang-format` with Allman-style braces, 4-space indentation, no column limit.

## Local Notes
- Dev machine is a Mac. Configure with: cmake -B Builds -G Xcode
- Build only the plugin targets: Lumiere_VST3, Lumiere_AU, Lumiere_Standalone
  (cmake --build Builds --config Debug --target <name>)
- The Benchmarks target currently fails to build. Don't build it unless asked.
- Never edit anything in JUCE/ or modules/.
- After every change, build all three plugin targets and fix errors and warnings before finishing.
- Make small changes and briefly explain what you changed. If something is unclear, ask.
