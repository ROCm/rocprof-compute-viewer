.. meta::
  :description: Collect and load SPM counters in ROCprof Compute Viewer.
  :keywords: SPM, streaming performance monitor, SQ_CYCLES, ROCprof Compute Viewer

.. _using-spm-counters:

*****************************
Collect and view SPM counters
*****************************

RCV can load rocprofv3 Streaming Performance Monitor (SPM) JSON output either
by itself or alongside a matching SQTT trace.

Collect SPM
===========

Always include ``SQ_CYCLES``. RCV uses it to:

- convert each XCC's SPM timestamps into shader-clock cycles;
- follow shader-clock frequency changes;
- identify and reconstruct accumulated missing sample windows.

For example:

.. code-block:: bash

  rocprofv3 --spm-beta-enabled \
    --spm SQ_CYCLES SQ_WAVES TCP_TOTAL_CACHE_ACCESSES \
    --spm-sample-interval-unit sclk_cycles \
    --spm-sample-interval 4096 \
    --output-format json -- ./application

The supported counters and sample-interval range depend on the GPU and ROCm
version. Use ``rocprofv3-avail list --spm-config`` and
``rocprofv3-avail list --spm`` to inspect them.

Attach SPM to SQTT
==================

1. Load the matching rocprofv3 UI output directory or raw ``.att`` trace.
2. Select **Import > SPM JSON...**.
3. Select the rocprofv3 results JSON containing the SPM collection.

The SQTT input must contain realtime records (``realtime.json`` or decoder
``REALTIME`` records). RCV aligns every XCC independently using the first SQTT
realtime anchor and ``SQ_CYCLES``.

If the realtime ranges do not overlap, RCV displays a warning but still
performs the alignment.

Open standalone SPM
===================

Use **Import > SPM JSON...**, or pass the JSON file on the command line:

.. code-block:: bash

  ./rocprof-compute-viewer results.json

Without SQTT realtime anchors, the plot uses timestamps relative to the first
SPM timestamp rather than aligned shader-clock cycles.

Plot behavior
=============

- XCC, shader engine, and hardware instance are tensor dimensions.
- SE and instance dimensions are summed for plotting.
- XCC clocks remain independent. Combined curves update whenever any XCC
  advances and keep the last value from the other XCCs.
- Missing hardware windows accumulated into a later sample are divided evenly
  across reconstructed windows.
- Derived counters are listed before basic counters in the Plot tab; each group
  is alphabetical.

Derived counters
================

SPM definitions can reference:

- the raw SPM counter names;
- ``SCLOCK``: the aligned per-XCC shader-clock sample endpoints;
- ``SPM_CLOCK``: the original per-XCC timestamp deltas.

For example:

.. code-block:: text

  WAVES_PER_CYCLE := SQ_WAVES / SQ_CYCLES
  XCC2_WAVES := select[SQ_WAVES, 2, axis=XCC]

SE and CU/instance dimensions are automatically summed when a result is
plotted. Keep XCC as a dimension unless intentionally selecting one XCC; XCC
reduction clock semantics are still being finalized.

Current limitations
===================

- JSON loading currently assumes one GPU agent and one relevant
  dispatch/stream.
- Loading SPM from ROCpd is not implemented yet.
- Attach the SPM capture to the SQTT capture from the same profiling run and
  dispatch.
