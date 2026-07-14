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

For a description of each view and its controls, see the :ref:`Views and shortcuts <rcv-views-reference>` topic.

Requirements
=============

To ensure that ``rocprofv3`` generates the thread trace data correctly, install the following components:

* AQL profile:

  * Available with ROCm 7.0 or later, or `build from source <https://github.com/ROCm/rocm-systems/tree/develop/projects/aqlprofile>`_.

  * If ``rocprofv3`` throws INVALID_SHADER_DATA error, the AQL profile and Trace Decoder versions are incompatible.

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

Import a rocprofv3 UI output directory
----------------------------------------

To import a ``rocprofv3`` UI output directory into the Compute Viewer, use any of the following methods:

- Go to **Menu > Import > Rocprofv3 UI Output**.
- Provide the full path in the **UI path** field.
- Pass the directory on the command line:

.. code-block:: bash

    ./rcviewer <dir_to_ui_folder>

.. _loading-raw-att-out:

Import raw .att and .out files
--------------------------------

To import raw ``.att`` and ``.out`` files into the Compute Viewer (requires a decoder-enabled build):

- Go to **Menu > Import > ATT Trace Files...** and select the files.
- Or pass the directory on the command line:

.. code-block:: bash

    ./rcviewer <dir_with_att_out_files>

Raw traces captured via the rocprofiler-sdk API don't include ``code.json`` or ``snapshots.json``, so the Instructions view and source pane are empty by default. To enable them, generate the ISA and source correlation before importing.

.. _generating-isa-source-correlation:

Generating ISA and source correlation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Use ``scripts/generate_snapshot.py`` to recreate that correlation from the kernel code objects:

.. code-block:: bash

    # Explicit code objects
    python3 scripts/generate_snapshot.py kernel_code_object_id_1.out kernel_code_object_id_2.out

    # With no arguments, scans every *.hsaco and *.out in the current directory
    python3 scripts/generate_snapshot.py

This writes ``code.json``, ``snapshots.json``, and copies of the referenced source files into the current directory. Once generated, import the directory as described above.

Key considerations when using the script:

- **Code object IDs:** Each code object is tagged with the ID the trace references, parsed from the trailing number in the filename (for example, ``..._code_object_id_1.out`` → ``1``, ``codeobj_42.out`` → ``42``). Only ``.hsaco`` files might use ID ``0``; a ``.out`` without a parseable ID, or an ID that collides with another input, is skipped with a warning.

- **Debug symbols:** Build the code objects with debug info (``-g``) to enable source-line mapping. Without it, the script still produces ISA output but the source pane stays empty.

- **Dependencies:** The script requires ``llvm-objdump`` to disassemble code objects and the ``pyelftools`` Python package to parse ELF metadata. Install ``pyelftools`` with ``pip install pyelftools``. ``llvm-objdump`` is available from a ROCm or LLVM install, or can be added to ``PATH`` separately.
