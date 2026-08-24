.. meta::
  :description: ROCprof Compute Viewer is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.
  :keywords: Using ROCprof compute viewer, Using RCV, RCV user manual, ATT, ROCprof compute viewer user manual, ROCprof compute viewer user guide, RCV user guide, Use ROCprof compute viewer, Use RCV

.. _using-compute-viewer:

********************************************
Visualize and analyze GPU thread trace data
********************************************

ROCprof Compute Viewer (RCV) interprets the output of `ui_output_agent_{agent_id}_dispatch_{dispatch_id} <https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/how-to/using-thread-trace.html#rocprofv3-output-files>`_ directories for visualization. The views available in RCV include:

- Source visualization (Trace -> ISA)
- Hotspot analysis
- Memory ops to ``waitcnt`` dependency
- Occupancy visualization
- Flamegraph view (per-target-CU/SIMD source and ISA stack rollup, plus a global marker flamegraph when SQTT instrumentation is present)
- SQTT instrumentation marker visualization (from the ``.sqtt_funcmap`` ELF section emitted by the LLVM pass)

For a description of each view and its controls, see the :ref:`Views and controls <rcv-views-reference>` topic.

.. _using-compute-viewer-requirements:

Requirements
=============

To ensure that ``rocprofv3`` generates the thread trace data correctly, install the following components:

* AQLprofile:

  * Available with ROCm 7.0 or later, or `build from source <https://github.com/ROCm/rocm-systems/tree/develop/projects/aqlprofile>`_.

* ROCprofiler-SDK:

  * Available with ROCm 7.0 or later, or `build from source <https://github.com/ROCm/rocm-systems/tree/develop/projects/rocprofiler-sdk>`_.

* ROCprof Trace Decoder:

  * Bundled with ``rocprofv3`` since ROCm 7.13, so no extra install is needed. On ROCm versions earlier than 7.13, `build from source <https://github.com/ROCm/rocm-systems/tree/develop/projects/rocprof-trace-decoder>`_.

For instructions on how to run ``rocprofv3`` to collect thread trace data, see `using rocprofv3 to collect thread trace <https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/how-to/using-thread-trace.html>`_.

Input formats
==============

RCV accepts two kinds of input:

- A ``rocprofv3`` UI output directory (JSON), produced when ``rocprofv3`` converts the thread trace for you.
- A directory of raw ``.att`` and ``.out`` thread-trace files captured directly through the rocprofiler-sdk API. These require a decoder-enabled build.

.. _importing-rocprofv3-ui-output:

Import a rocprofv3 UI output directory
----------------------------------------

To import a ``rocprofv3`` UI output directory into RCV, use any of the following methods:

- Go to **Menu > Import > Rocprofv3 UI Output**.
- Provide the full path in the **UI path** field.
- Pass the directory on the command line:

.. code-block:: bash

    ./rocprof-compute-viewer <dir_to_ui_folder>

.. _loading-raw-att-out:

Import raw .att and .out files
--------------------------------

To import raw ``.att`` and ``.out`` files into RCV (requires a decoder-enabled build):

- Go to **Menu > Import > ATT Trace Files...** and select the files.
- Or pass the directory on the command line:

.. code-block:: bash

    ./rocprof-compute-viewer <dir_with_att_out_files>

Raw traces captured via the rocprofiler-sdk API don't normally include
``code.json`` or ``snapshots.json``. They can still be imported, but the
Instructions view and source pane have no ISA/source correlation unless that
metadata is supplied by the capture workflow.
