.. meta::
  :description: ROCprof Compute Viewer is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.
  :keywords: Using ROCprof compute viewer, Using RCV, RCV user manual, ATT, ROCprof compute viewer user manual, ROCprof compute viewer user guide, RCV user guide, Use ROCprof compute viewer, Use RCV

.. _using-compute-viewer:

*******************************************
Visualization using ROCprof Compute Viewer
*******************************************

The Compute Viewer interprets the output of ui_output_agent_{agent_id}_dispatch_{dispatch_id} file for visualization. The visualization includes:

- Trace -> ISA -> source visualization
- Hotspot analysis
- Memory ops to ``waitcnt`` dependency
- Occupancy visualization

To launch the Compute Viewer, use *any* of the following methods:

- Go to Menu -> Import -> rocprofv3 UI
- Provide the full path to "UI path"
- Use command line:

.. code-block:: bash

    ./rcviewer <dir_to_ui_folder>

The various views available as tabs on the top are described in the following sections.

Hotspot view
==============

.. image:: /data/hotspot.png
    :alt: Hotspot view

The Hotspot tab displays a histogram of instruction costs.

- Vertical axis (Cycles): Total accumulated latency cycles for each bin based on the bin's center value.

- To adjust the number of bins and histogram range, go to Edit -> Hotspot Options. Clicking a bin highlights the first and last ISA lines present in the bin.

- The hotspot is computed over all waves within the "WaveView Clock Range".

- IMMED instructions such as ``s_nop``, ``s_waitcnt``, and ``s_barrier`` might appear to over-represent cycles since waves in a SIMD often wait concurrently.

- Idle time is not computed into hotspot, only execute and stall.

Compute unit and Utilization view
==================================

The "Compute Unit" tab displays the trace aggregated per wave. The trace is separated per SIMD-Slot such as "2-6". Here is a sample Compute unit view:

.. image:: /data/cu.png
    :alt: Compute unit

The "Utilization" tab displays the trace aggregated per SIMD. The trace is separated per instruction type such as VALU, VMEM, SCALAR, and OTHER. This view hides IMMED type tokens as multiple waves could be executing them in parallel. Utilization view displays only issue for gfx or execution for gfx10+ and hides stalled time. This view can be used to identify bubbles. Note that in case of overlapping tokens from different waves slots, only one token is displayed.



The

Instructions view
==================

.. image:: /data/isaview.png
    :alt: Instructions view

The ISA view contains a list of instructions along with their "Hitcount" and "Latency" cost. If debug symbols are present, ``rocprofv3`` snapshots the related source files, as shown on the right side of the image.

- The cost can be calculated as a mean or sum of the selected wave, mean or sum of all waves, or display a particular loop iteration.

- Arrows link memory operations to the ``s_waitcnt`` waiting on them. This instruction view is per wave.Another wave that takes a different execution path might present a different set of arrows or links.

- Left or right on the right side of the instruction takes the trace bar to the SQTT token executing that instruction. This is true for Utilization and Compute Unit tabs as well.
Left click on a token highlights (in green) the ISA line corresponding to that instruction.
Hover or Click on an ISA line to highlight the corresponding source line. The opposite way is also possible.
Clicking on a source line permamently highlights the ISA lines until the user clicks on the same or another line.

Wave states view
=================

.. image:: /data/wavestate.png
    :alt: Wave states

The "Wave States" tab presents a vertical slice of the "Compute Unit" tab looking at the wave states. The "Wave States" tab is applicable only for the ``target_cu``.

Occupany view
==============

.. image:: /data/occupancy.png
    :alt: Occupancy

The "Occupancy" tab shows occupancy per Shader Engine (SE) as number of waves.

Kernel dispatch view
=====================

.. image:: /data/dispatch.png
    :alt: Kernel dispatch

The "Kernel Dispatches" tab shows occupancy per kernel. This tab is usually relevant when there are multiple kernels running on different streams.

Settings
=========

The left panel consists of the settings to help the user configure the Compute Viewer.

.. image:: /data/left.png
    :alt: Left side panel

The settings as shown in the preceding image
