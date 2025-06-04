.. meta::
  :description: ROCprof Compute Viewer is a tool for visualizing and analyzing GPU thread trace data collected with rocprofv3.
  :keywords: Using ROCprof compute viewer, Using RCV, RCV user manual, ATT, ROCprof compute viewer user manual, ROCprof compute viewer user guide, RCV user guide, Use ROCprof compute viewer, Use RCV

.. _install-viewer:

************************************
ROCprof Compute Viewer installation
************************************

This topic explains how to build ROCprof Compute Viewer (RCV) from source on various Operating Systems.

For prebuild binaries, see the `RCV release <https://github.com/ROCm/rocprof-compute-viewer/releases>`_.

Building from source
=====================

By default, the project builds with Qt 6.8. To build with Qt 5, use:

.. code-block:: shell

    cmake -DQT_VERSION_MAJOR=5 ..

For Qt 6.4, use:

.. code-block:: shell

    cmake -DQT_VERSION_MINOR=4 ..

Build on macOS Homebrew
------------------------

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

Build on Linux
---------------

1. Install Qt 5 or Qt 6:

   - For Ubuntu 22.04

     .. code-block:: shell

        sudo apt install -y qtbase5-dev qt5-qmake cmake build-essential

   - For Ubuntu 24.04

     .. code-block:: shell

        sudo apt install -y libgl1 qtbase5-dev qt5-qmake cmake build-essential

   - For other distributions, see `Getting Started with Qt <https://doc.qt.io/qt-5/gettingstarted.html>`_.

2. Configure CMake and build:

   .. code-block:: shell

    mkdir build
    cd build
    cmake .. -DQT_VERSION_MAJOR=5
    # for qt6.4, use
    # cmake .. -DQT_VERSION_MAJOR=6 -DQT_VERSION_MINOR=4
    make -j

Build on Windows Subsystem for Linux (WSL)
-------------------------------------------

- Requires Ubuntu 22+

- Follow the same instructions as given for :ref:`Linux <linux-build>`

- Recommended to use Qt 5.15

Build on Windows Native
------------------------

To build on Windows, use Qt Tools with Qt 6.8 (or later). See `Installing Qt on Windows <https://wiki.qt.io/Quick_Start:_Installing_Qt_on_Windows>`_ for details.

Disabling OpenGL widgets
-------------------------

To disable OpenGL widgets, use:

.. code-block:: shell

  cmake .. -DRCV_DISABLE_OPENGL=On
