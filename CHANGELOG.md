# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.2.0

### Added

* Open raw `.att`/`.out` directories (extracted from rocprofiler-sdk thread-trace output) directly, without first converting to JSON (requires a trace-decoder build; see README).
* `scripts/generate_snapshot.py` generates ISA/source correlation (`code.json` / `snapshots.json`) from kernel code objects, so traces captured via the rocprofiler-sdk API directly can be viewed with full ISA and source mapping.
* Flamegraph view (replaces the Explorer view): per-target-CU/SIMD source/ISA stack rollup, plus a global marker flamegraph when SQTT instrumentation is present.
* Hidden latency analysis for gfx10+/Navi thread traces, with Total latency and Nonhidden Latency views for instructions, source hotspots and flamegraphs.
* SQTT instrumentation marker visualization from `.sqtt_funcmap` ELF sections, including marker tracks in Global View.
* Decoder event and dispatch visualization in Global View for both raw trace-decoder input and JSON input.
* Event filters in Global View for dispatches, flush events, code-object events, SQTT events, GC rinse events and other decoder events.
* Heuristic GPU Utilization metric in derived counters. Create a new file to refresh.
* Shift + Mousewheel to scroll the Compute Unit and Utilization timelines horizontally.

### Changed

* LDS, VMEM and Flat utilization are multiplied by 2 relative to previous versions.
* The standalone Wave States tab has been temporarily removed; its functionality is expected to move into the Counters view.
* Global View event and marker colors have been tuned for readability.
* Updated the AMD application icon.
* Updated build documentation, including macOS instructions and trace-decoder build options.

### Fixed

* Hardcoded target_cu for latency analysis.
* Incorrect scaling of the clock counter in the wave slot widgets.
* Global offset handling and wave JSON fallback.
* Realtime alignment for JSON input and raw `.att` input.
* Realtime alignment for counters.
* Global View event rendering performance.
* Global View misalignment when events or dispatches occur outside the occupancy sample range.
* Missing user-facing reporting for malformed marker sequences and trace-decoder/input errors.
* Utilization computation now includes other-SIMD activity where applicable.

### Build and CI

* Added GitHub Actions build, release and CodeQL workflows.
* Release workflow validation and package verification were improved.
* Tests now prefer a local GoogleTest installation when available.
