# qe6502

`qe6502` is a cycle-exact 6502/65C02 CPU emulator core with a cycle-oriented bus interface. It provides a small native C API, a stable shared-library ABI surface, a C++17 wrapper, and optional JavaScript/WebAssembly bindings.

The core is organized around explicit bus ticks. The caller owns memory and devices, services one bus request at a time, and passes the read byte back to the CPU on the next tick. `qe6502` is a CPU core, not a complete machine emulator.

The fast native C API keeps the complete CPU state in a 16-byte `qe6502_t`. The stable ABI API uses a fixed 64-byte opaque context, and save/load snapshots are also fixed at 64 bytes.

`qe6502` has no hidden mutable global state and performs no memory/device I/O internally; independent CPU contexts are suitable for multithreaded use.

## CPU models

The public model constants cover:

- NMOS 6502
- NES/2A03-style 6502 behavior
- WDC 65C02
- Rockwell 65C02
- Synertek 65C02

The selected model controls the opcode matrix, bus-cycle behavior, and CPU-specific instruction behavior used by the core.

## Which API should I use?

Use the **fast static C API** when `qe6502` is compiled directly into an emulator, test harness, game tool, or other native program.

```c
#include <qe6502/qe6502.h>
```

```cmake
target_link_libraries(my_program PRIVATE qe6502::static)
```

Use the **stable shared C ABI** when the CPU core crosses a dynamic-library boundary, plugin boundary, FFI boundary, scripting boundary, or WebAssembly-style boundary.

```c
#include <qe6502/qe6502_abi.h>
```

```cmake
target_link_libraries(my_program PRIVATE qe6502::shared)
```

Use the **C++ wrapper** when writing C++17 code and you want a small RAII-style wrapper around the static C core.

```cpp
#include <qe6502/cpu.hpp>
```

```cmake
target_link_libraries(my_program PRIVATE qe6502::cpp)
```

## Vendoring into another CMake project

The most direct integration is to vendor this repository and add it as a subdirectory. In subproject builds, tests and install rules are off by default; the options below make that explicit.

```cmake
set(QE6502_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(QE6502_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(external/qe6502)

target_link_libraries(my_emulator PRIVATE qe6502::static)
# or:
# target_link_libraries(my_emulator PRIVATE qe6502::cpp)
```

## Status

`qe6502` is pre-1.0 and under active development.

The first official public release is currently targeted for October 2026, subject to
API/ABI stabilization and completion of the current correctness baseline.

The core project currently includes C, C++, ABI, and WebAssembly integration
surfaces. Additional Python, C#, and Rust bindings are planned/in progress.

## Build from source

Native debug build:

```sh
cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
```

Native release build:

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release
ctest --test-dir build/release --output-on-failure
```

Install:

```sh
cmake --install build/release --prefix /path/to/prefix
```

Then consume it from another CMake project:

```cmake
find_package(qe6502 CONFIG REQUIRED)
target_link_libraries(my_program PRIVATE qe6502::static)
```

Component checks are also supported:

```cmake
find_package(qe6502 CONFIG REQUIRED COMPONENTS C Static ABI CXX)
```

Available installed targets depend on the build options:

- `qe6502::static`
- `qe6502::shared`
- `qe6502::cpp`

## Build options

| Option                 |                                           Default | Meaning                                                                             |
| ---------------------- | ------------------------------------------------: | ----------------------------------------------------------------------------------- |
| `QE6502_BUILD_STATIC`  |                                              `ON` | Build the fast static C library.                                                    |
| `QE6502_BUILD_SHARED`  |                                              `ON` | Build the stable shared ABI library.                                                |
| `QE6502_BUILD_CPP`     |                                              `ON` | Build and install the C++ wrapper. Requires `QE6502_BUILD_STATIC=ON`.               |
| `QE6502_BUILD_TESTS`   | `${BUILD_TESTING}` top-level, `OFF` as subproject | Build tests and harnesses. Turn this off for dependency/package builds.             |
| `QE6502_BUILD_WASM`    |                                             `OFF` | Enable the WebAssembly build mode.                                                  |
| `QE6502_ENABLE_WERROR` |                                             `OFF` | Treat warnings as errors. Intended for maintainer/CI builds, not package consumers. |
| `QE6502_INSTALL`       |               `ON` top-level, `OFF` as subproject | Install headers, libraries, CMake package files, and pkg-config files.              |

For a dependency-style build without harnesses:

```sh
cmake -S . -B build/package -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF \
  -DQE6502_BUILD_TESTS=OFF \
  -DQE6502_ENABLE_WERROR=OFF
cmake --build build/package
cmake --install build/package --prefix /path/to/prefix
```

## pkg-config

When installed, `qe6502` also generates pkg-config metadata for non-CMake users:

```sh
pkg-config --cflags --libs qe6502
pkg-config --cflags --libs qe6502-abi
pkg-config --cflags --libs qe6502-cpp
```

The pkg-config files are generated only for the corresponding enabled build products.

## Minimal C example

```c
#include <qe6502/qe6502.h>

int main(void)
{
    qe6502_t cpu = {0};
    cpu.model = qe6502_model_nmos;

    qe6502_tick_t tick = qe6502_restart(&cpu);
    unsigned char input = 0;

    for (;;) {
        if ((tick.status & qe6502_status_writing) != 0u) {
            /* Store tick.bus at tick.address in your memory/device map. */
        } else {
            /* Load input from tick.address in your memory/device map. */
            input = 0;
        }

        tick = qe6502_tick(&cpu, input);
    }
}
```

## Minimal C++ example

```cpp
#include <qe6502/cpu.hpp>

int main()
{
    qe6502::cpu cpu(qe6502::model::nmos);

    cpu.restart();
    unsigned char input = 0;

    for (;;) {
        if (cpu.is_write()) {
            /* Store cpu.bus_data() at cpu.bus_address() in your memory/device map. */
        } else {
            /* Load input from cpu.bus_address() in your memory/device map. */
            input = 0;
        }

        cpu.tick(input);
    }
}
```

The CPU does not own memory. `restart()` prepares the first bus request. On each iteration, writes store the outgoing bus byte, while reads load a byte from the caller's memory/device map and pass it to `tick(input)`.

## Minimal JavaScript / WebAssembly example

```js
import { loadQe6502Node, Model } from "./qe6502.js";

const qe = await loadQe6502Node("./qe6502_js.wasm");
const cpu = qe.createCpu(Model.nmos);

try {
  cpu.restart();
  let input = 0;

  for (;;) {
    if (cpu.isWrite()) {
      /* Store cpu.busData() at cpu.busAddress() in your memory/device map. */
    } else {
      /* Load input from cpu.busAddress() in your memory/device map. */
      input = 0;
    }

    cpu.tick(input);
  }
} finally {
  cpu.dispose();
}
```

The JavaScript wrapper exposes the same bus-driven execution model as the C and C++ APIs. In browsers, use `loadQe6502Browser()` instead of `loadQe6502Node()`. JavaScript CPU objects own a WASM-side context, so call `dispose()` when the CPU is no longer needed.

## Save/load snapshots

`qe6502` supports a stable fixed-size 64-byte save/load snapshot format. A snapshot captures the complete CPU state, including the current internal bus-cycle phase, so restored execution resumes deterministically rather than only restoring the visible registers.

The same snapshot format is exposed through the native C API, the stable ABI API, the C++ wrapper, and the JavaScript/WASM binding, so snapshots can be shared between bindings that use the same snapshot format. For stable ABI consumers, the reference entry points are `qe6502abi_save()` and `qe6502abi_load()`.

A small C++ wrapper example:

```cpp
qe6502::cpu cpu(qe6502::model::nmos);
cpu.restart();

qe6502::cpu_snapshot snapshot = cpu.save();

/* ... later ... */

qe6502::cpu restored(snapshot);
```

## Correctness testing

`qe6502` uses several complementary tests and harnesses:

- Klaus2m5 functional ROM tests for supported 6502/65C02 CPU models.
- SingleStepTests/ProcessorTests instruction tests, checked cycle by cycle against memory bus activity and final CPU/register state.
- `netlist_lockstep` tests against `perfect6502` as an NMOS 6502 netlist/bus-timing oracle.
- Save/load replay tests that checkpoint and restore execution during long-running test programs.

The original SingleStepTests/ProcessorTests repository is at <https://github.com/SingleStepTests/ProcessorTests>. The current 65x02 test packages used by the browser manual runner are at <https://github.com/SingleStepTests/65x02>.

The ProcessorTests data is not vendored in this repository because the complete test corpus is large. It can be run in two ways:

1. Download the ProcessorTests/65x02 JSON packages locally and run the native single-step runner.
2. Build the JavaScript/WASM manual harness and run the SingleStepTests browser runner against either GitHub-hosted raw test data or local JSON files.

Example native single-step commands after a native release build:

```sh
./build/release/harness/singlestep/singlestep --model nmos --path /path/to/65x02/6502/v1 --all
./build/release/harness/singlestep/singlestep --model nes  --path /path/to/65x02/nes6502/v1 --all
./build/release/harness/singlestep/singlestep --model wdc  --path /path/to/65x02/wdc65c02/v1 --all
./build/release/harness/singlestep/singlestep --model rw   --path /path/to/65x02/rockwell65c02/v1 --all
./build/release/harness/singlestep/singlestep --model st   --path /path/to/65x02/synertek65c02/v1 --all
```

Example browser/manual flow:

```sh
cmake --preset release_wasm
cmake --build --preset release_wasm
python3 -m http.server -d build/release_wasm/harness/js_manual 8000
```

Then open `http://localhost:8000/` and choose the SingleStepTests runner. The page can fetch the test data from GitHub or load local `00.json` .. `ff.json` files.

`netlist_lockstep` compares `qe6502` against <https://github.com/mist64/perfect6502>. `perfect6502` simulates the original NMOS 6502 netlist, so the lockstep harness is used as a bus-timing reference for selected NMOS 6502 scenarios.

Some undocumented NMOS opcodes have unstable or implementation-sensitive final register behavior. For `0x0B`, `0x2B`, `0x4B`, `0x6B`, `0x8B`, `0xAB`, and `0xBB`, `qe6502` favors compatibility with SingleStepTests/ProcessorTests 6502 final register state rather than matching `perfect6502` in the lockstep oracle.

## Interrupt behavior notes

`qe6502` implements the normal IRQ and NMI behavior expected by software. Some transistor-level interrupt corner cases are intentionally not modeled exactly in order to keep the CPU hot path small and fast. In particular, `qe6502` does not claim exact behavior for BRK hijacking or NMI-loss edge cases.

## Tests and harnesses

Tests and harnesses are built only when `QE6502_BUILD_TESTS=ON`. Maintainer presets enable both tests and warning-as-error mode:

```sh
cmake --preset debug_native
cmake --build --preset debug_native
ctest --test-dir build/debug_native --output-on-failure

cmake --preset release_native
cmake --build --preset release_native
ctest --test-dir build/release_native --output-on-failure
```

For WebAssembly/JavaScript harnesses:

```sh
cmake --preset release_wasm
cmake --build --preset release_wasm
ctest --test-dir build/release_wasm --output-on-failure
```

Package users should normally keep `QE6502_BUILD_TESTS=OFF` and `QE6502_ENABLE_WERROR=OFF`.

## License

`qe6502` is distributed under the MIT License. See [LICENSE](LICENSE) for details.
