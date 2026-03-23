# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.1.7

### Added

* Heuristic GPU Utilization metric in derived counters. Create a new file to refresh.
* Flamegraph (replaced Explorer)

### Changed

* LDS, VMEM and Flat utilization are multiplied by 2 relative to previous versions.

### Fixed

* Hardcoded target_cu for latency analysis
