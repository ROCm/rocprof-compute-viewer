.. meta::
  :description: Collect and load SPM counters in ROCprof Compute Viewer.
  :keywords: SPM, streaming performance monitor, ROCprof Compute Viewer

.. _using-spm-counters:

*****************************
Collect and view SPM counters
*****************************

ROCprof Compute Viewer (RCV) can load rocprofv3 Streaming Performance Monitor (SPM) JSON output either
by itself or alongside a matching SQTT trace.

Collect SPM
===========

Include ``SQ_CYCLES`` when available. RCV uses it to:

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

Navi4 (gfx12) collections can be loaded without ``SQ_CYCLES``. For example:

.. code-block:: bash

  rocprofv3 --spm-beta-enabled 1 \
    --spm SQ_WAVES SQ_INSTS_VALU SQ_INSTS_SALU TA_TA_BUSY \
    --spm-sample-interval-unit refclk_cycles \
    --spm-sample-interval 1024 \
    --output-format json -- ./application

RCV currently loads one sampled GPU, dispatch, and stream per SPM JSON file.
Multiple callback batches for that capture are combined.

Attach SPM to SQTT
==================

1. Load the matching rocprofv3 UI output directory or raw ``.att`` trace.
2. Go to **Import > SPM JSON...**.
3. Select the rocprofv3 results JSON containing the SPM collection.

For clock alignment, the SQTT input must contain realtime records
(``realtime.json`` or decoder ``REALTIME`` records). RCV aligns every XCC
independently using the first SQTT realtime anchor and ``SQ_CYCLES``.

If the realtime ranges do not overlap, RCV displays a warning. With
``SQ_CYCLES``, it still performs the alignment.

Without ``SQ_CYCLES``, alignment requires two realtime anchors from the same
SE and overlapping SPM and trace timestamp ranges. If alignment is unavailable,
SPM still loads alongside the trace using timestamps relative to its first
sample. Its plot then uses an independent time scale.

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
- RCV sums the SE and instance dimensions for plotting.
- XCC clocks remain independent. Combined curves update whenever any XCC
  advances and keep the last value from the other XCCs.
- Missing hardware windows accumulated into a later sample are divided evenly
  across reconstructed windows. This reconstruction is disabled for Navi4 and
  refclk-cycle sampling.

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
plotted.

When clock alignment is unavailable, ``SCLOCK`` contains relative SPM
timestamps instead of shader-clock cycles.

Navi4 selections
----------------

Both shader arrays in each SE contribute to raw counter totals. For counters
with an SA dimension, use ``axis=SA`` with index ``2 * SE + SA``: SE=0/SA=1 is index 1,
and SE=1/SA=0 is index 2. Counters with only an SE dimension keep their original
SE indices. Existing ``axis=SE`` expressions continue to work for gfx9/MI300.

``CU``, ``WGP``, and ``INSTANCE`` name the same selection axis. SQ and SQC have
one entry per WGP. TA has two instances per WGP, indexed as
``2 * WGP + INSTANCE``. Both TA units remain separately selectable. Counters
without a WGP dimension keep their original INSTANCE indices.

For example, select SQ waves from SE=0/SA=1/WGP=1, both TA units from
SE=0/SA=0/WGP=1, or just the first of those TA units:

.. code-block:: text

  WAVES_SE0_SA1_WGP1 := select[select[SQ_WAVES, 1, axis=SA], 1, axis=WGP]
  TA_SE0_SA0_WGP1 := select[select[TA_TA_BUSY, 0, axis=SA], 2:4, axis=INSTANCE]
  TA_SE0_SA0_WGP1_UNIT0 := select[select[TA_TA_BUSY, 0, axis=SA], 2, axis=INSTANCE]

The range ``2:4`` selects indices 2 and 3, whose values are summed for plotting.
Navi4 has no XCC dimension and uses XCC=0. XCC selection and independent XCC
clocks remain supported on devices that provide that dimension.

.. note::

  Loading SPM data from ROCpd is not implemented yet.
