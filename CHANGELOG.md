# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.1.2

### Bug Fixes
* Fixed s_endpgm showing in utilization bar
* Potential thread safety fixes
* Fixed an issue where the order of which wave was the main wave changed how the cycles were attributed to source

### Changes
* Renamed "CTXSW" -> "TRAP" to indicate time spent in the trap handler

### Added
* Proper handling of function inlining information (requires corresponding rocprofiler SDK fix)

<!-- Release Description -->

<!-- Uncomment only the parts that are needed for this release -->
<!--
### Upgrade Steps
* [ACTION REQUIRED]
*
-->

<!--
### Breaking Changes
*
*
-->

<!--
### New Features
*
*
-->

<!--
### Performance Improvements
*
*
-->

<!--
### Other Changes
*
*
-->
