# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.2.1

### Added

* SPM JSON counter visualization, including realtime clock alignment and support for derived counters over SPM data.
* The Wave States plot for eligible single-SE JSON inputs, with per-trace-family defaults under Options → Graph Options.
* Plot alignment options for keeping timeline plots locked to either the Compute Unit/Utilization detail range or the Global View range.

### Changed

* Replaced the plot LOD enable/disable setting with a signed LOD bias for finer resolution control.
* The Options tab now scrolls vertically when the window is too short to show every section.
* Optimized SQTT marker rendering and updated marker colors.
* Corrected the notation used for other-SIMD utilization rows.
* Reorganized and expanded the installation, troubleshooting, hidden-latency and view documentation.
* Removed the standalone `scripts/generate_snapshot.py` workflow. Raw SDK captures without metadata remain viewable but do not provide ISA/source correlation.

### Fixed

* Graph Options no longer compresses vertically when the window is resized.
* Plot alignment accounts for the label columns in plots, Compute Unit/Utilization and Global View at every zoom level.
* Qt 5 builds with the flamegraph view enabled.
* Trace-decoder record emission and ATT loader coverage.

### Build and CI

* Added trace-decoder builds and ATT loader tests to CI.
