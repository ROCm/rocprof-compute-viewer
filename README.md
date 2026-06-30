# ROCprof Compute Viewer

For pre-built binaries, see [releases](https://github.com/ROCm/rocprof-compute-viewer/releases), or the [GitHub Actions](https://github.com/ROCm/rocprof-compute-viewer/actions) artifacts for bleeding-edge builds.

## Table of Contents
- [Summary](#summary)
  - [Requirements](#requirements)
- [Rocprof Compute Viewer](#using-the-rocprof-compute-viewer)
  - [Hotspot Tab](#hotspot-tab)
  - [Instructions View](#instructions-view)
  - [Occupancy and Dispatches Plots](#occupancy-and-dispatches-plots-tab)
  - [Left Side Panel](#left-side-panel)
  - [Compute Unit and Utilization Views](#compute-unit-and-utilization-views)
  - [Counters](#counters)
  - [Global View](#global-view)
  - [Summary](#summary-1)
  - [Flamegraph View](#flamegraph-view)
- [Troubleshooting](#troubleshooting)
- [Building from Source](#building-from-source)
- [Viewing traces from the rocprofiler-sdk API](#viewing-traces-from-the-rocprofiler-sdk-api)
- [Hidden Latency](#hidden-latency)

## Summary

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected using rocprofv3.
The tool interprets the rocprofv3 thread trace output, which are directories named ui_output\_agent\_{agent_id}\_dispatch\_{dispatch_id}. It includes:

* Trace -> ISA -> Source visualization
* Hotspot analysis.
* Memory ops to waitcnt dependency.
* Occupancy visualization
* Flamegraph view (per-target-CU/SIMD source/ISA stack rollup, plus a global marker flamegraph when SQTT instrumentation is present)
* Hidden latency analysis.
* SQTT instrumentation marker visualization — LLVM pass (`.sqtt_funcmap` ELF section).

There are two input formats:
* **Rocprofv3 UI output**: A directory containing `filenames.json` (standard rocprofv3 output).
* **ATT files**: A directory of raw `.att`/`.out` thread-trace files, as extracted from rocprofiler-sdk. See [Viewing traces from the rocprofiler-sdk API](#viewing-traces-from-the-rocprofiler-sdk-api).

In the GUI, pick the matching entry under **Menu -> Import**:
* **Import -> Rocprofv3 UI Output** for the JSON directory (or paste the full path into "Ui path").
* **Import -> ATT Trace Files...** to select raw `.att`/`.out` files.

From the command line the input format is auto-detected from the path:

```bash
# Open a rocprofv3 ui_output_* directory (JSON path)
./rocprof-compute-viewer <dir_to_ui_folder>
```

For information on how to generate thread trace data, see the documentation on [using rocprofv3 to collect thread trace](https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/how-to/using-thread-trace.html). Also, see the [ROCprof Compute Viewer documentation](https://rocm.docs.amd.com/projects/rocprof-compute-viewer/en/latest/).

### Requirements
For rocprofv3 to generate thread trace data correctly, the following components are required:

* AQLprofile:
  * ROCm 7.x, or
  * [Build from source](https://github.com/ROCm/rocm-systems/tree/develop/projects/aqlprofile)
  * If rocprofv3 errors out with "INVALID_SHADER_DATA", this means the particular version of aqlprofile and Decoder are incompatible.

* Rocprofiler-sdk:
  * ROCm 7.x, or
  * [Build from source](https://github.com/ROCm/rocm-systems/tree/develop/projects/rocprofiler-sdk)

* ROCprof Trace Decoder — used in two independent places:
  * **By rocprofv3**, to turn captured thread trace into its JSON/UI output. Bundled with rocprofv3 since **ROCm 7.13**, so nothing extra is needed. On **ROCm < 7.13**, install it from [source](https://github.com/ROCm/rocm-systems/tree/develop/projects/rocprof-trace-decoder).
  * **By RCV**, to open raw `.att`/`.out` directly without rocprofv3 — this is a separate, RCV-side link to the decoder. CMake fetches it by default unless disabled. Requires V2 API (SOVERSION 0.2, built with `VERSION_MINOR=2`); the default disassembly backend is `amd_comgr`. See [Trace-decoder support](#trace-decoder-support).

## Using the ROCprof Compute Viewer

### Shortcuts and Interactions

* Multiple tabs on top widget:
  * Ctrl+Left mouse on tab headers will keep multiple tabs open
  * Left click will switch tabs normally
* Plots:
  * Mousewheel zooms in/out horizontally
  * Ctrl + Mousewheel zooms vertically
  * Right click + drag for panning
  * Ctrl + Left mouse on will reset axis to default
* Compute Unit and Utilization:
  * A/D for panning
  * Mousewheel for vertical scroll
  * Shift + Mousewheel for horizontal scroll
  * Ctrl + Mousewheel for zoom in/out
  * Right click and drag for measuring cycles (also Global View)

### Hotspot Tab

![Alt text](docs/data/hotspot.png)

The Hotspot tab displays a histogram of instruction costs.

* Vertical axis ("Cycles"): Total accumulated latency cycles for each bin, based on the bin's center value.
* The number of bins and histogram range can be adjusted in Edit → Hotspot Options. Clicking a bin highlights the first and last ISA lines contained in it.
* The hotspot is computed over all waves within the "WaveView Clock Range".
* 'IMMED' instructions (e.g., s_nop, s_waitcnt, s_barrier) may appear to have over-represented cycles since waves in a SIMD often wait concurrently.
* Idle time is not computed into hotspot, only execute and stall.

### Instructions View

![Alt text](docs/data/isaview.png)

The ISA view contains a list of instructions with their Hitcount and Latency cost.
If debug symbols are present, rocprofv3 snapshots the related source files, which are shown on the right.

* The cost can be calculated as a mean or sum of the selected wave, mean or sum of all waves, or display a particular loop iteration.
* Arrows link memory operations to the s_waitcnt waiting on them. They are per-wave: another wave that took a different execution path may present a different set of arrows/links.
* Left or right on the right side of the instruction takes the trace bar to the SQTT token executing that instruction. This is true for Utilization and Compute Unit tabs as well.
* Left click on a token highlights (in green) the ISA line corresponding to that instruction.
* Hover or Click on an ISA line to highlight the corresponding source line. The opposite way is also possible.
  * Clicking on a source line permanently highlights the ISA lines until the user clicks on the same or another line.
* Hidden latency analysis runs automatically for gfx10+ thread traces and can also be run from Analyze -> Hidden Latency. After it runs, the instruction latency dropdown can show Total latency or Nonhidden Latency, and source hotspots can optionally include or exclude hidden latency.

### Occupancy and Dispatches plots tab
* Keys:
   * Plots can be zoomed in and out with mousewheel
   * Holding Left Ctrl zooms in and out on the vertical axis
   * Click and drag to select an area.
   * Right click and drag for panning.
   * Clicking on a token in the waveview (Trace) will add a blue marker to identify the cycle of that token.
* The Highlighted region shows what is visible from the "CU" and "Utilization" tabs.

> **Note:** The standalone **Wave States** tab (a vertical slice of the Compute Unit view showing the number of active waves in each state: IDLE, EXEC, STALL, WAIT, for the `target_cu`) has been temporarily removed. Its functionality will eventually be absorbed into the **Counters** view. This does not affect the wave-state coloring within the Compute Unit view itself.

* Occupancy tab shows occupancy per Shader Engine, in number of waves.

![Alt text](docs/data/occupancy.png)

* Kernel Dispatches tab shows occupancy per kernel - usually relevant when there are multiple kernels running on different streams.

![Alt text](docs/data/dispatch.png)

### Left Side Panel

  ![Alt text](docs/data/left.png)
* "Shader" (Engine), "SIMD", "Slot" (Wave slot within a SIMD) and "WID" (A wave ID counter for that slot) boxes allows the user to select which Wave to focus on.
  * This is defined as the target wave.
  * The interactions in the 'Instruction' tab apply only to the target wave: Token-to-ISA mapping, loop iteration navigation, etc.
* The WaveView Clock range defines the visible cycles in the "Compute Unit" and "utilization" tabs, as well as the "Hotspot" calculation.
  * By default, set to the first cycle target wave, to a little after the last cycle of the target wave.
  * Reduce the start/end range to make navigation easier.
  * Increase start/end range to see more waves, or get a more general hotspot calculation.
* "GlobalView Zoom" defines the zoom level of the GlobalView tab [0,15].
* "WaveView zoom" defines the zoom level in the trace shown [0,10].
* "Iteration" defines the iteration of the current selected token.
  * Left click on a token to update the iteration.
  * This value can be edited, scrolling the view to same instruction on a different loop iteration
    * Makes navigating loops easier.
    * The iteration is defined as the n-th time that same instruction was executed for each wave (starting at zero).
* "Search" searches for a specific text on the instruction view. E.g. search for ds_ to find the first lds instruction.
* "History" contains the history (token+cycle) of previously selected tokens. It can be used to go back to a previous location.

### Compute Unit and Utilization Views
* Displays the trace aggregated either per-wave (Compute Unit) or per SIMD (Utilization).
* Right click and drag to measure number of cycles.
* Left click highlights the ISA corresponding to that token.
  * If nothing happens, likely that token could not be matched with the ISA. Check for warnings at rocprofv3 output.
* A/D keys can be used for panning.
* Zoom level controlled by "waveview zoom" on left side panel or Ctrl+MouseWheel.

#### Compute Unit:
* Displays the trace separated per SIMD-Slot (e.g. 2-6).

![Alt text](docs/data/cu.png)

#### Utilization:
* Displays the trace per type of instruction (VALU, VMEM, SCALAR, OTHER).
* Hides IMMED type tokens as multiple waves can be executing them in parallel.
* Hides stalled time, displays only issue (gfx) or execution (gfx10+).
* Can be used to identify bubbles.
* May have overlapping tokens from different waves slots, in that case only one will be displayed.

![Alt text](docs/data/util.png)

### Counters:

Displays a plot of counter collection over time

#### Collecting basic counters
* Up to 8 counters can be added, with 4 recommended
* Only SQ counters are allowed.
* On Mi300, "--att-perfcounter-ctrl 3" has a polling rate of 120~240 cycles
* Syntax in rocprofv3:
```bash
rocprofv3 --att-perfcounter-ctrl 3 --att-perfcounters "SQ_VALU_MFMA_BUSY_CYCLES SQ_INSTS_VALU SQ_INSTS_MFMA SQ_INST_LEVEL_LDS"
```
* Alternatively, one can define SIMD Masks in which counters only increment for a particular SIMD:
  * Use ":0xMask"
  * Default to 0xF (all SIMDs increment counter)
  * By filtering SIMD and CU (in Edit -> Counters Shown), this allows per-SIMD counter collection streaming
```bash
# This enables SQ_INSTS_VALU for all SIMDs, and show individual SQ_INSTS_SALU counters per SIMD [0,1,2]
rocprofv3 --att-perfcounter-ctrl 3 --att-perfcounters "SQ_INSTS_VALU:0xF SQ_INSTS_SALU:0x1 SQ_INSTS_SALU:0x2 SQ_INSTS_SALU:0x4"

# In rocm 7.13+, the following parameter is available to generate counters only for the target CU.
# This is recommended when using a high polling rate:
rocprofv3 --att-perfcounter-target-only 1 [...]
```

Counters can be used to visualize specific types of hardware utilization. For instance:
* SQ_INST_LEVEL_LDS - Measures current number of in-flight LDS instructions.
* SQ_VALU_MFMA_BUSY_CYCLES - Measures current MFMA hardware utilization.

#### Derived Counters

The RCV allows user-defined derived counters to be edited in realtime. Defined in Edit -> Derived Counters
* Some simple derived counters are provided by default (MFMA_util, VALU_util, LDS_util...)
* Use "Help" button to see the derived counter syntax.
* Create, Delete and Edit user-defined derived counters.
* If multiple files are present, the currently selected widget tab defines which derived counter list to show.

The left list shows the list of Raw (basic) counters collected with SQTT, along with their shapes=(XCC, SE, CU, Time).

![Alt text](docs/data/counter_far.png)
![Alt text](docs/data/counter_close.png)

#### Global View
The Global View presents a comprehensive trace of all waves across enabled Shader Engines, with each wave color-coded by kernel.

* Hovering over the trace display additional information, such as which kernel that wave is running, the cu/simd/slot and wave duration.
* The "Global View" can be compared with the Kernel Dispatches plot.
* Right click and drag to measure number of cycles.

![Alt text](docs/data/globalv.png)

#### Summary
The summary is a feature available only on MI2xx and MI3xx GPUs. It displays 3 pieces of information:
* Average instruction cost for the whole trace, separated by idle, issue and stall.
* Average hardware utilization by instruction type (VALU, VMEM, LDS, ...)
* Per-compute hardware utilization values and accumulated counters.

To enable the summary view, use the following parameters:

```bash
# SQ_ACTIVE_INST_X collects activity for token type X.
# For summary, a collection interval of 10 is enough.
rocprofv3 --att-perfcounter-ctrl 10 --att-perfcounters "SQ_BUSY_CU_CYCLES SQ_VALU_MFMA_BUSY_CYCLES SQ_ACTIVE_INST_VALU SQ_ACTIVE_INST_LDS SQ_ACTIVE_INST_VMEM SQ_ACTIVE_INST_FLAT SQ_ACTIVE_INST_SCA SQ_ACTIVE_INST_MISC"

# or using the convenience parameter
rocprofv3 --att-activity 10
```

* Per-CU rates are averaged over the period in which any wave was present in the CU.
* Peak rates indicate maximum across any given cycle, adding all Shaders and CUs.
* Utilization for counter X is computed as:
  * max_over_cycles(add_over_cu(X))/max_over_cycles(add_over_cu(SQ_BUSY_CU_CYCLES)) for peak rates.
  * add_all(X)/add_all(SQ_BUSY_CU_CYCLES) for other values.

![Alt text](docs/data/summary.png)

### Flamegraph View

The Flamegraph View (which replaces the previous Explorer View) rolls up latency into a stack so you can quickly find the most expensive code paths.

- Frames are sized by accumulated latency cycles; wider frames cost more.
- The stack is built per target CU/SIMD over the source and ISA, so you can drill from a source line down to the individual instructions.
- Hover a frame to see its latency; click to zoom into that frame.
- After hidden latency analysis runs, the flamegraph can be weighted by Total latency or Nonhidden Latency. Tooltips show the total, nonhidden and hidden cycle breakdown.
- When the trace contains SQTT instrumentation markers, a separate global marker flamegraph is also available, rolling up time spent inside instrumented regions.
- Marker flamegraphs can also use Total latency or Nonhidden Latency; see [Hidden Latency](#hidden-latency) for the marker limitation.

## Troubleshooting:

If the RCV does not display anything except "Occupancy" and stats_*.csv file is empty:

  * Thread Trace only receives detailed information from the target_cu.
  * If the application does not populate the target_cu, then nothing will be traced.
  * For possible solutions, the [rocprofv3 documentation](https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/how-to/using-thread-trace.html#troubleshooting)

## Building from source

By default, the project builds with QT 6.8.
To build with QT5, use:
```bash
cmake -DQT_VERSION_MAJOR=5 ..
```
For QT 6.4, use:
```bash
cmake -DQT_VERSION_MINOR=4 ..
```

### Operating system, QT version and support list

|   OS   | Recommended QT Version | Support |
| ------ | :-------------: | :-----: |
| Win 11  |   6.8+          |    ✔    |
| MacARM |   6.4+          |    ✔    |
| Macx86 |   6.4+          | Partial |
| WSL2   |   6.4+          |    ✔    |
| Ub 24  |   6.4+          |    ✔    |
| Ub 22  |   5.15          | Partial |

### MacOS (Homebrew)

Install Qt5 or Qt6:

```bash
brew install qt@5
brew install qt@6
```

Configure CMake and build:
```bash
mkdir build
cd build
cmake .. -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)
# or
cmake .. -DQT_VERSION_MAJOR=5 -DCMAKE_PREFIX_PATH=$(brew --prefix qt@5)
make -j
```

### Linux

Install Qt6 (Qt5 for ubuntu22):

```bash

# Ubuntu 22.04
sudo apt install -y qtbase5-dev qt5-qmake cmake build-essential

# Ubuntu 24.04+
sudo apt install -y libgl1 qt6-base-dev qmake6 build-essential

```

Configure cmake and build:

```bash
mkdir build && cd build

#Qt 6.4
cmake .. -DQT_VERSION_MINOR=4
#Qt 5.15
cmake .. -DQT_VERSION_MAJOR=5

make -j
```

### Trace-decoder support

Trace-decoder support lets RCV open directories of raw `.att` / `.out` files (as extracted from rocprofiler-sdk thread-trace output) directly, without needing rocprofv3 to convert them to JSON first. This is RCV's own link to the decoder and is independent of the decoder that rocprofv3 uses internally (bundled since ROCm 7.13).

By default, CMake fetches and builds the decoder automatically when `TRACE_DECODER_ROOT` is not provided, except on macOS where fetching is disabled by default. To build a JSON-only viewer without fetching the decoder, pass `-DRCV_FETCH_TRACE_DECODER=OFF`. To use a pre-built decoder instead, pass `-DTRACE_DECODER_ROOT=...`.

##### Disassembly backend (optional)

A disassembly backend lets the decoder produce ISA for raw `.att`/`.out` inputs. It is **only needed if you want built-in disassembly** — if you instead supply `code.json` (see [Generating ISA/source correlation](#generating-isasource-correlation)), or only need the trace itself, you can build the decoder without one via `-DRCV_FETCH_TRACE_DECODER_WITH_DISASSEMBLY=OFF`.

When a backend is enabled (the default), two are supported:

* **amd_comgr** (default): comes from a ROCm install. Point CMake at it with `CMAKE_PREFIX_PATH` / `ROCM_PATH`; no separate LLVM package is required.
* **LLVM-C**: a system LLVM development package (with the AMDGPU target — standard distro builds qualify). The RCV fetch path always uses amd_comgr, so this applies only to a pre-built decoder you configure yourself with `-DUSE_LLVM_DISASM=ON`. Install it with:

```bash
# Ubuntu
sudo apt install -y llvm-dev libclang-dev

# Fedora / RHEL
sudo dnf install -y llvm-devel clang-devel
```

On Windows, install a full LLVM dev package (e.g. via the official installer or `choco install llvm`) and pass `-DLLVM_DIR=<path-to>/lib/cmake/llvm` at configure time.

##### CMake options

| Variable | Default | Effect |
|---|---|---|
| `RCV_FETCH_TRACE_DECODER` | `ON` (`OFF` on macOS) | Fetch and build the trace decoder automatically when `TRACE_DECODER_ROOT` is not set. |
| `RCV_FETCH_TRACE_DECODER_WITH_DISASSEMBLY` | `ON` | Build the fetched decoder with the amd_comgr disassembly backend. |
| `TRACE_DECODER_ROOT` | *(unset)* | Use a **pre-built** decoder tree (build dir or install prefix) instead of fetching. Takes precedence over `RCV_FETCH_TRACE_DECODER`. |
| `RCV_TRACE_DECODER_REPO` | rocm-systems upstream | Git URL to fetch from. |
| `RCV_TRACE_DECODER_TAG` | tracked branch | Branch / tag / commit to check out. Use `develop` for the latest decoder. |
| `RCV_TRACE_DECODER_FETCH_DIR` | `${CMAKE_SOURCE_DIR}/external/rocm-systems` | Where to place the sparse checkout. Lives outside `build/` so a clean rebuild does not re-download the monorepo. |

The configure step prints one of:

```
-- Trace-decoder enabled: .../source
-- Trace-decoder disabled
```

### Windows WSL

* Requires Ubuntu 24+
* Follow the same instructions for Linux.
* Recommended to use Qt6.4

### Windows Native

To build on Windows, use QT Tools with QT-6.8+:
* https://wiki.qt.io/Quick_Start:_Installing_Qt_on_Windows

### Disabling OpenGL widgets

```bash
cmake .. -DRCV_DISABLE_OPENGL=On
```

## Viewing traces from the rocprofiler-sdk API

rocprofv3 normally converts thread trace into a UI output directory (JSON). If instead you capture trace by calling the **rocprofiler-sdk API from your own application**, you get a directory of raw `.att`/`.out` files. RCV can open these directly — they are parsed via the trace decoder, so rocprofv3 doesn't need to convert them to JSON first. This requires a decoder-enabled build (see [Trace-decoder support](#trace-decoder-support)).

### Loading raw `.att`/`.out`

* **GUI**: **Menu -> Import -> ATT Trace Files...**, then select the `.att`/`.out` files.
* **CLI**: pass the directory; the format is auto-detected.

```bash
# Open raw .att/.out files directly (requires trace-decoder build)
./rocprof-compute-viewer <dir_with_att_and_out_files>

# With optional code.json and/or snapshots.json (auto-detected by filename, any order)
./rocprof-compute-viewer <dir_with_att_and_out_files> /path/to/code.json /path/to/snapshots.json
```

`code.json` and `snapshots.json` supply ISA disassembly and source-file snapshots respectively. rocprofv3 emits them automatically, but the SDK API does not — so generate them as described below.

### Generating ISA/source correlation

**When:** you captured the trace via the rocprofiler-sdk API, so the raw `.att`/`.out` output has the trace samples but no `code.json`/`snapshots.json`.

**Why:** without this metadata the viewer can still draw the trace, but it cannot map SQTT tokens to ISA instructions or to source lines (the Instructions view and source pane stay empty). `scripts/generate_snapshot.py` recreates that correlation from the kernel code objects so the SDK-API workflow matches the CLI experience.

**How:** point the script at the kernel ELF code objects (`.hsaco`, `.out`, or `.o`):

```bash
# Explicit code objects.
python3 scripts/generate_snapshot.py kernel_code_object_id_1.out kernel_code_object_id_2.out

# Or, with no arguments, every *.hsaco and *.out in the current directory.
python3 scripts/generate_snapshot.py
```

It writes `code.json`, `snapshots.json`, and copies of the referenced source files into the current directory, which you then pass to the viewer as shown under [Loading raw `.att`/`.out`](#loading-raw-attout) above.

Notes:
* Each code object is tagged with the **code object id** the trace references, parsed from the trailing number in the filename (e.g. `..._code_object_id_1.out` → `1`, `codeobj_42.out` → `42`). Only `.hsaco` files may use id `0`; a `.out` without a parseable id, or an id that collides with another input, is skipped with a warning.
* Build the code objects with debug info (`-g`) for source correlation; without it you still get ISA but no source mapping.
* Requires `llvm-objdump` (from a ROCm/LLVM install or on `PATH`) and the `pyelftools` Python package (`pip install pyelftools`).

## Hidden Latency

Hidden latency runs automatically for gfx10+ thread traces. It can also be run manually from menu Analyze -> Hidden Latency.

Hidden latency estimates cycles hidden by other busy pipes. A wave's idle or stalled cycles are hidden when another pipe is busy; issuing/executing cycles are hidden only by a higher-priority busy pipe.

Current pipe priority is: WMMA > VALU > VMEM/LDS/FLAT > SMEM/SALU > Others. Others include IMMED, MSG, branches, and similar token types; they never hide latency. This priority order is a first approximation.

Total latency includes hidden latency. Nonhidden Latency subtracts it. After analysis runs, the Instructions view, source hotspots, and Flamegraph view can display or weight by total or nonhidden latency.

Marker flamegraphs have one limitation: nonhidden marker widths distribute hidden latency from per-ISA-line totals. If the same instruction line appears under multiple marker scopes, or hidden work crosses marker boundaries, marker-level nonhidden widths are approximate. Total-latency marker flamegraphs are unaffected.
