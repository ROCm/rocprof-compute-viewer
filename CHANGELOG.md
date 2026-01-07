# ROCprof Compute Viewer Release Notes

ROCprof Compute Viewer (RCV) is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.

## Release 0.1.6

### Bug Fixes
* Fixed minor display issue where some tokens would have darker lines separating them with display scaling

### Added
* CDNA: Added option to have a separate LDS Pipe in utilization view. Options -> Display options -> "Separate LDS pipe" (SWDEV-564105)
* RDNA: LDS, Raytrace (BVH), BRANCH and WMMA have their own separate pipes in Utilization View.
* CDNA and RDNA: Added separate message bus pipe (MSG) in Utilization view.
  * Note: As with IMMED, MSG bus utilization is approximate.
* Code object ID and offset/vaddr to instruction view (SWDEV-564105)
* Clicking on instruction now updates "Search" box to contain the instruction text.
 * This allows for quicker search of the same instruction, as well as Copying text from the Viewer (SWDEV-564105)

### Changed
* Renamed "OTHER" track in utilization view to "IMMED" as only IMMED and TRAP are part of it now.
