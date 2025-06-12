# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.1.1

### Bug Fixes
* Fixed an issue where some plots would not update when loading empty traces
* gfx12's v_swmmac_ instructions were not properly categorized as MATRIX type
* Fixed an issue where VALU instructions would sometimes be shown as IMMED in hotspots tab
* Fixed some tickmark issues in Hotspot tab
* Fixed windows installer not creating a shortcut
* Token history now clears when switching to a different trace
* Plot value info now clears properly when switching to a different trace
* Fix windows installer including the package version in install directory
* Fix icons not displaying on windows

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
### Bug Fixes
*
*
-->

<!--
### Performance Improvements
*
*
-->

### Other Changes
* Added Help menu
* Swap amd.png to amd.ico
