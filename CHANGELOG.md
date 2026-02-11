# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.1.6

### Bug Fixes

* Fixed minor display issue where some tokens would have darker lines separating them with display scaling
* Fixed display issue in Qt5

### Added

* Shaderdata record visualization in Global Wave View
  * Shaderdata records are displayed as black rectangles on matching wave tracks (SE/CU/SIMD/slot).
  * Hover tooltip shows time, value, wave ID, and flags.

* Shaderdata record visualization in WaveView.
  * Shaderdata is usually delayed with respect to the s_ttracedata instruction.
  * Shows up as dedicated, small tracks for this record.
  * This display method is temporary.

* Vertical zoom for Global Wave View
  * Shift+Mousewheel scales track height (1–20 pixels).

* CDNA: Added option to have a separate LDS Pipe in utilization view. Options -> Display options -> "Separate LDS pipe" (SWDEV-564105)

* RDNA: LDS, Raytrace (BVH), BRANCH and WMMA have their own separate pipes in Utilization View.

* CDNA and RDNA: Added separate message bus pipe (MSG) in Utilization view.
  * Note: As with IMMED, MSG bus utilization is approximate.

* Code object ID and offset/vaddr to instruction view (SWDEV-564105)

* Clicking on instruction now updates "Search" box to contain the instruction text.
  * This allows for quicker search of the same instruction, as well as Copying text from the Viewer (SWDEV-564105)

* CDNA: Added option to define derived counters: Go to Edit -> Derived Counters
  * Save/Delete/Edit several derived counter definitions.
  * Help button for syntax.
  * On editor Save + Close, the "Counters" plot tab will update with list of derived counters.
  * Individual counters can be enabled or disabled in "Plots" tab in the left panel.

* CDNA: Barebones stochastic PC sampling support
  * Added "Samples", "Issued" and "Stalls" categories in Instruction view.
  * Added toggle for these categories, as below.

* CDNA: Added memory latency analysis
  * Go to Analyze -> Memory Latency
  * Requires collecting ATT with:
    * rocprofv3 --att-perfcounter-ctrl 1 --att-perfcounters "SQ_INST_LEVEL_VMEM SQ_INST_LEVEL_LDS"
    * If available, include --att-perfcounter-target-only True
  * Displays memory latency per assembly line.
    * Correctness requires ordered memory operations (no FLAT read/writes)
    * Latency calculation is an estimate and results may be unreliable.

* Added a "Instruction Columns" frame under the "Options" tab to toggle which columns to be displayed.

* Toogle for which counters to display in Options tab.

### Changed

* Renamed "OTHER" track in utilization view to "IMMED" as only IMMED and TRAP are part of it now.
