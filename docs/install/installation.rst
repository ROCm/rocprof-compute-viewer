.. meta::
  :description: ROCprof Compute Viewer is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.
  :keywords: Using ROCprof compute viewer, Using RCV, RCV user manual, ATT, ROCprof compute viewer user manual, ROCprof compute viewer user guide, RCV user guide, Use ROCprof compute viewer, Use RCV

.. _install-viewer:

********************************************
Building ROCprof Compute Viewer from source
********************************************

This topic explains how to build ROCprof Compute Viewer (RCV) from source on various Operating Systems.

For prebuild binaries, see the `RCV release <https://github.com/ROCm/rocprof-compute-viewer/releases>`_.

The following table lists the supported operating systems, recommended Qt versions, and support level:

.. list-table::
   :header-rows: 1
   :widths: 20 30 20

   * - OS
     - Recommended Qt version
     - Support
   * - Windows 11
     - 6.8+
     - Full
   * - macOS (ARM)
     - 6.4+
     - Full
   * - macOS (x86)
     - 6.4+
     - Partial
   * - WSL2
     - 6.4+
     - Full
   * - Ubuntu 24.04
     - 6.4+
     - Full
   * - Ubuntu 22.04
     - 5.15
     - Partial

By default, the project builds with Qt 6.8. To select a different version, pass the matching CMake flags:

.. code-block:: shell

    -DQT_VERSION_MAJOR=5                       # Qt 5 (any minor)
    -DQT_VERSION_MAJOR=6 -DQT_VERSION_MINOR=4  # Qt 6.4
    -DQT_VERSION_MAJOR=6 -DQT_VERSION_MINOR=8  # Qt 6.8 (default)

Building on macOS Homebrew
---------------------------

1. Install Qt 5 or Qt 6:

   .. code-block:: shell

    brew install qt@5
    brew install qt@6

2. Configure CMake and build:

   .. code-block:: shell

    mkdir build
    cd build
    cmake .. -DCMAKE_PREFIX_PATH=$(brew --prefix qt@6)
    # or
    cmake .. -DQT_VERSION_MAJOR=5 -DCMAKE_PREFIX_PATH=$(brew --prefix qt@5)
    make -j

.. _linux-build:

Building on Linux
------------------

1. Install Qt 5 or Qt 6:

   - For Ubuntu 22.04

     .. code-block:: shell

        sudo apt install -y qtbase5-dev qt5-qmake cmake build-essential

   - For Ubuntu 24.04

     .. code-block:: shell

        sudo apt install -y libgl1 qt6-base-dev qmake6 build-essential

   - For other distributions, see `Getting Started with Qt <https://doc.qt.io/qt-5/gettingstarted.html>`_.

2. Configure CMake and build:

   .. code-block:: shell

    mkdir build
    cd build
    # Ubuntu 22.04 (Qt 5)
    cmake .. -DQT_VERSION_MAJOR=5
    # Ubuntu 24.04 (Qt 6.4+; omit for the project default of Qt 6.8)
    # cmake .. -DQT_VERSION_MINOR=4
    make -j

Building on Windows Subsystem for Linux (WSL)
----------------------------------------------

- Requires Ubuntu 22+

- Follow the same instructions as given for :ref:`Linux <linux-build>`

- Recommended to use Qt 5.15

Building on Windows Native
---------------------------

To build on Windows, use Qt Tools with Qt 6.8 (or later). See `Installing Qt on Windows <https://wiki.qt.io/Quick_Start:_Installing_Qt_on_Windows>`_ for details.

Trace-decoder support
----------------------

Trace-decoder support lets RCV open directories of raw ``.att``/``.out`` files captured directly through the rocprofiler-sdk API, without needing ``rocprofv3`` to convert them to JSON first; it is not required for the JSON path. This is RCV's own link to the decoder and is independent of the decoder bundled with ``rocprofv3`` since ROCm 7.13. It requires the decoder's V2 API (``SOVERSION 0.2``, built with ``VERSION_MINOR=2``).

By default, CMake fetches and builds the decoder automatically when ``TRACE_DECODER_ROOT`` is not provided, except on macOS where fetching is disabled by default. To build a JSON-only viewer without the decoder, pass ``-DRCV_FETCH_TRACE_DECODER=OFF``. To use a prebuilt decoder instead, pass ``-DTRACE_DECODER_ROOT=<path>``.

After CMake runs, the output confirms whether the decoder was found:

- ``-- Trace-decoder enabled: .../source`` — decoder is available and the path to its source is shown.
- ``-- Trace-decoder disabled`` — no decoder; only rocprofv3 JSON input is supported.

Disassembly backend (optional)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

A disassembly backend lets the decoder produce ISA for raw ``.att``/``.out`` inputs. It is only needed if you want built-in disassembly. If you supply ``code.json`` via :ref:`Generating ISA and source correlation <generating-isa-source-correlation>` or only need the raw trace, you can build without one via ``-DRCV_FETCH_TRACE_DECODER_WITH_DISASSEMBLY=OFF``.

Two backends are supported:

- ``amd_comgr`` (default): the ``amd_comgr`` library shipped with ROCm. Point CMake at it with ``CMAKE_PREFIX_PATH`` or ``ROCM_PATH``; no separate LLVM package is required.

- **LLVM-C**: a system LLVM development package with the AMDGPU target. The RCV fetch path always uses ``amd_comgr``, so this applies only to a prebuilt decoder configured with ``-DUSE_LLVM_DISASM=ON``.

  To install the LLVM dev package:

  .. code-block:: shell

      # Ubuntu
      sudo apt install -y llvm-dev libclang-dev

      # Fedora / RHEL
      sudo dnf install -y llvm-devel clang-devel

  On Windows, install a full LLVM dev package (for example via the official installer or ``choco install llvm``) and pass ``-DLLVM_DIR=<path-to>/lib/cmake/llvm`` when running CMake.

CMake options
^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 40 20 40

   * - Variable
     - Default
     - Effect
   * - ``RCV_FETCH_TRACE_DECODER``
     - ``ON`` (``OFF`` on macOS)
     - Fetch and build the trace decoder automatically when ``TRACE_DECODER_ROOT`` is not set.
   * - ``RCV_FETCH_TRACE_DECODER_WITH_DISASSEMBLY``
     - ``ON``
     - Build the fetched decoder with the ``amd_comgr`` disassembly backend.
   * - ``TRACE_DECODER_ROOT``
     - *(unset)*
     - Use a prebuilt decoder tree (build dir or install prefix) instead of fetching. Takes precedence over ``RCV_FETCH_TRACE_DECODER``.
   * - ``RCV_TRACE_DECODER_REPO``
     - rocm-systems upstream
     - Git URL to fetch from.
   * - ``RCV_TRACE_DECODER_TAG``
     - tracked branch
     - Branch, tag, or commit to check out. Use ``develop`` for the latest decoder.
   * - ``RCV_TRACE_DECODER_FETCH_DIR``
     - ``${CMAKE_SOURCE_DIR}/external/rocm-systems``
     - Local directory where the decoder source is downloaded. Placing it outside ``build/`` means a clean rebuild does not redownload the monorepo.

Disabling OpenGL widgets
-------------------------

To disable OpenGL widgets, use:

.. code-block:: shell

  cmake .. -DRCV_DISABLE_OPENGL=On
