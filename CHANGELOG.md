# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.1.4

### Bug Fixes
* Fixed display of idle time in utilziation view
  * Improved display of zoomed out view
* Fixed zooming in/out in Compute Unit and Utilization tabs using mouse wheel
* Fixed spacing of compute unit and utilization tabs for some screens

### Added
* Added "OTHER" in utilization tab for MSG/IMMED/TRAP types.
  * It'll only display the last issue (quad)cycle to avoid clutter.
