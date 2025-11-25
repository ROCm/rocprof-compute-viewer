# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.1.5

### Bug Fixes
* Fixed counters not properly loading with discontiguous SE numbers
* Fixed relative paths for source files

### Added
* Display Shader Array ID in global view
* Option to view branch targets [Issue #7](https://github.com/ROCm/rocprof-compute-viewer/issues/7)
* Option to view latency per instruction
* Ability to display multiple tabs at the same time
  * e.g. Compute Unit + Utilization
  * Ctrl + Click to open a tab without closing the previous one
* Hovering over a token will soft highlight the ISA line

### Changed
* Order of Prev/Next in search bar: [Issue #9](https://github.com/ROCm/rocprof-compute-viewer/issues/9)
* Hitcount label will show total OR per wave depending on latency selection
