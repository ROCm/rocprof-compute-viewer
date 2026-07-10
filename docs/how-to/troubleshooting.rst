.. meta::
  :description: Troubleshooting common issues with ROCprof Compute Viewer.
  :keywords: ROCprof Compute Viewer troubleshooting, RCV issues, occupancy only, empty trace

.. _rcv-troubleshooting:

***************
Troubleshooting
***************

**Issue:** RCV doesn't display anything except "Occupancy" and the `stats_*.csv <https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/how-to/using-thread-trace.html#stats-csv>`_ file is empty.

**Solution:** Thread trace receives detailed information from the ``target_cu``. If the application doesn't populate the ``target_cu``, then nothing will be traced.
For possible solutions, see `thread trace documentation <https://rocm.docs.amd.com/projects/rocprofiler-sdk/en/latest/how-to/using-thread-trace.html#troubleshooting>`_.
