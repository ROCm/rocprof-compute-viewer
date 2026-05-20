# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.1.7

### Added

* Open raw `.att`/`.out` directories (extracted from rocprofiler-sdk thread-trace output) directly, without first converting to JSON (requires a trace-decoder build; see README).
* `scripts/generate_snapshot.py` generates ISA/source correlation (`code.json` / `snapshots.json`) from kernel code objects, so traces captured via the rocprofiler-sdk API directly can be viewed with full ISA and source mapping.
* Flamegraph view (replaces the Explorer view): per-target-CU/SIMD source/ISA stack rollup, plus a global marker flamegraph when SQTT instrumentation is present.
* Heuristic GPU Utilization metric in derived counters. Create a new file to refresh.
* Shift + Mousewheel to scroll the Compute Unit and Utilization timelines horizontally.

### Changed

* LDS, VMEM and Flat utilization are multiplied by 2 relative to previous versions.

### Fixed

* Hardcoded target_cu for latency analysis.
* Incorrect scaling of the clock counter in the wave slot widgets.
* Global offset handling and wave JSON fallback.
