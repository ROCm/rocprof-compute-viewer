.. meta::
  :description: Explains hidden latency analysis in ROCprof Compute Viewer, including pipe priority and how total and non-hidden latency are defined.
  :keywords: hidden latency, RCV, ROCprof Compute Viewer, pipe priority, non-hidden latency, total latency

.. _hidden-latency-concept:

***************
Hidden latency
***************

Hidden latency runs automatically for gfx10+ thread traces. To run it manually, go to **Analyze > Hidden Latency**.

Hidden latency estimates cycles that are hidden by other busy pipes. A wave's idle or stalled cycles are hidden when another pipe is busy. Issuing or executing cycles are hidden only by a higher-priority busy pipe.

The current pipe priority order, from highest to lowest, is WMMA > VALU > VMEM/LDS/FLAT > SMEM/SALU > Others. Instructions classified as Others (IMMED, MSG, branches, and similar token types) never hide latency. This priority order is a first approximation.

After the analysis runs, the instruction latency dropdown in the Instructions view, the source hotspots, and the Flamegraph view can each be set to display either of the following:

- **Total latency**: All cycles, including those hidden by concurrent pipe activity.
- **Non-hidden latency**: Latency not hidden by other pipes, calculated as total latency minus the hidden portion.

.. _flamegraph:

Flamegraph and marker interaction
===================================

After hidden latency analysis runs, the flamegraph can be weighted by **Total latency** or **Non-hidden latency**. Tooltips show the total, non-hidden, and hidden cycle breakdown for each frame. Marker flamegraphs can also use either weighting option.

.. note::

  Marker flamegraphs have one limitation: non-hidden marker widths distribute hidden latency from per-ISA-line totals. If the same instruction line appears under multiple marker scopes, or if hidden work crosses marker boundaries, marker-level non-hidden widths are approximate. Total-latency marker flamegraphs are unaffected.
