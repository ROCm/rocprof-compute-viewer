.. meta::
  :description: A high-level overview of ROCprof Compute Viewer: the pipeline it fits into, its input formats, and which view answers which performance question.
  :keywords: ROCprof Compute Viewer overview, RCV overview, RCV at a glance, ATT, thread trace visualization, wavefront execution timeline

.. _rcv-at-a-glance:

**********************************
ROCprof Compute Viewer at a glance
**********************************

ROCprof Compute Viewer (RCV) is the visualization front end for :doc:`Advanced Thread Trace (ATT) <rocprofiler-sdk:how-to/using-thread-trace>` data: it renders decoded, instruction-level wavefront execution traces so you can see exactly which instruction stalled, for how long, and why. Use RCV once a coarser-grained tool (kernel-level counters or API traces) has identified which kernel is slow; RCV narrows the analysis down to the specific instruction responsible.

This topic orients you to how RCV fits into the profiling pipeline and which view to open for a given question. For step-by-step instructions, follow the links to the relevant how-to and reference topics.

How it works
=============

RCV sits at the end of a three-stage pipeline:

.. image:: /data/how_rcv_works.png
    :alt: How RCV works
    :align: center
    :width: 50%

This output path is independent of the general ``--output-format`` setting used for other ``rocprofv3`` tracing. For how to collect the trace itself, see :doc:`rocprofiler-sdk:how-to/using-thread-trace`.

Input formats at a glance
===========================

RCV auto-detects which of the following you're pointing it at; you don't need to specify the format.

.. list-table::
   :header-rows: 1
   :widths: 25 45 30

   * - Format
     - When to use it
     - Details
   * - ``rocprofv3`` UI output (JSON)
     - Standard path — ``rocprofv3 --att`` already decoded the trace for you. Identified by a ``filenames.json`` file in the directory.
     - :ref:`Import a rocprofv3 UI output directory <importing-rocprofv3-ui-output>`
   * - Raw ``.att``/``.out``
     - Trace captured directly through the rocprofiler-sdk API; requires a decoder-enabled build.
     - :ref:`Import raw .att and .out files <loading-raw-att-out>`

Views at a glance
===================

Each view answers a different performance question. Full descriptions and controls for every view are in :ref:`rcv-views-reference`.

.. list-table::
   :header-rows: 1
   :widths: 25 45 30

   * - View
     - Question it answers
     - Reference
   * - Compute Unit
     - Which wavefronts ran, stalled, or idled, and when?
     - :ref:`compute-unit-and-utilization-view`
   * - Utilization
     - How busy was each hardware pipe (VALU, MFMA, VMEM, LDS)?
     - :ref:`compute-unit-and-utilization-view`
   * - Instructions (ISA)
     - Which instruction cost the most cycles, and is it waiting on memory?
     - :ref:`instructions-view`
   * - Hotspot
     - What are the most expensive instructions, without scrolling the full ISA list?
     - :ref:`hotspot-view`
   * - Flamegraph
     - Which code path dominates accumulated latency?
     - :ref:`flamegraph-view`
   * - Global view
     - How were waves distributed across all CUs and selected shader engines over time?
     - :ref:`global-view`
   * - Occupancy / Kernel dispatch
     - How many waves were resident, per shader engine or per kernel?
     - :ref:`occupancy-and-kernel-dispatch-views`
   * - Counters
     - How did a specific SQ hardware counter trend over the run?
     - :ref:`counters-view`
   * - Summary *(MI200/MI300 only)*
     - What's the aggregate utilization and instruction cost across the whole trace?
     - :ref:`summary-view`

Getting started
=================

RCV depends on AQLprofile, ROCprofiler-SDK, and Qt. The ROCprof Trace Decoder is an additional dependency only when importing raw ``.att``/``.out`` files. Prebuilt binaries are available in the `RCV releases <https://github.com/ROCm/rocprof-compute-viewer/releases>`_, so a build from source isn't required to get started.

For supported versions and build instructions, see :ref:`install-viewer`. For rocprofv3's own prerequisites (AQLprofile, ROCprofiler-SDK, and the decoder it bundles), see the :ref:`Requirements <using-compute-viewer-requirements>` section.

Navigation
===========

RCV's mouse and keyboard controls (zoom, pan, measure cycle ranges, jump between a token and its ISA line) are listed in :ref:`rcv-views-reference`.

See also
=========

- :ref:`install-viewer` — building RCV from source
- :ref:`using-compute-viewer` — importing traces and generating ISA/source correlation
- :ref:`rcv-views-reference` — every view, its controls, and its shortcuts
- :ref:`hidden-latency-concept` — how RCV separates hidden from non-hidden latency
- :ref:`rcv-troubleshooting` — common issues
