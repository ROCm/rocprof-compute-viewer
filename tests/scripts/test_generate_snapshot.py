#!/usr/bin/env python3

# MIT License
#
# Copyright (c) 2024-2026 Advanced Micro Devices, Inc. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

"""Tests for scripts/generate_snapshot.py.

Pure unit tests for the code-object-id parsing always run. The integration
tests run generate_snapshot.py over the ``.out`` code objects bundled in the
trace-decoder datasets and check that (1) every emitted code.json row carries
the code object id taken from its source filename, and (2) DWARF-bearing
datasets produce file:line source correlation. They are skipped when the
decoder datasets (RCV_ATT_TEST_ROOT) or llvm-objdump are unavailable.
"""

import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parents[2] / "scripts"
SCRIPT = SCRIPTS_DIR / "generate_snapshot.py"


def _load_module():
    spec = importlib.util.spec_from_file_location("generate_snapshot", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


gs = _load_module()

# Column indices in a code.json row (see HEADER in the script).
COMMENT_COL = 3
CODEOBJ_COL = 4

# Datasets known to ship code objects with DWARF line tables. Used by the
# source-correlation test; absent datasets are skipped, not failed.
DWARF_DATASETS = ("navi4_float", "mi300_shaderdata")


def _have_objdump() -> bool:
    try:
        gs.llvm_objdump()
        return True
    except FileNotFoundError:
        return False


class CodeobjIdFromPath(unittest.TestCase):
    def test_codeobj_out(self):
        self.assertEqual(gs.codeobj_id_from_path("codeobj_12345.out"), 12345)

    def test_path_prefix(self):
        self.assertEqual(gs.codeobj_id_from_path("/tmp/data/codeobj_7.out"), 7)

    def test_no_underscore(self):
        self.assertEqual(gs.codeobj_id_from_path("kernel.hsaco"), 0)

    def test_non_numeric_suffix(self):
        self.assertEqual(gs.codeobj_id_from_path("my_kernel.hsaco"), 0)

    def test_multiple_underscores(self):
        self.assertEqual(gs.codeobj_id_from_path("a_b_c_42.out"), 42)


class GenerateSnapshotIntegration(unittest.TestCase):
    def setUp(self):
        root = os.getenv("RCV_ATT_TEST_ROOT")
        if not root:
            self.skipTest("RCV_ATT_TEST_ROOT not set")
        if not _have_objdump():
            self.skipTest("llvm-objdump not available")
        self.root = Path(root)
        if not self.root.is_dir():
            self.skipTest(f"dataset root not found: {self.root}")

    def _collect_codeobjs(self):
        """Map dataset name -> {codeobj_id: out_path} for every dataset.

        The id is derived the same way the script does (trailing number of the
        stem), so this tracks whatever the decoder names its .out files, e.g.
        ``..._code_object_id_1.out`` -> 1. Files whose id collides within a
        dataset are dropped to match the script's collision rule."""
        datasets = {}
        for ds in sorted(self.root.iterdir()):
            if not ds.is_dir():
                continue
            ids = {}
            collisions = set()
            for out in sorted(ds.glob("*.out")):
                cid = gs.codeobj_id_from_path(str(out))
                if cid in ids:
                    collisions.add(cid)
                ids[cid] = out
            for cid in collisions:
                ids.pop(cid, None)
            if ids:
                datasets[ds.name] = ids
        return datasets

    def _run_script(self, paths, label):
        """Run generate_snapshot.py on paths in a temp dir; return code.json."""
        with tempfile.TemporaryDirectory() as tmp:
            cmd = [sys.executable, str(SCRIPT)] + [str(p) for p in paths]
            res = subprocess.run(cmd, cwd=tmp, capture_output=True, text=True)
            self.assertEqual(
                res.returncode, 0, msg=f"[{label}] script failed:\n{res.stderr}")
            code_json = Path(tmp) / "code.json"
            self.assertTrue(code_json.is_file(), f"[{label}] no code.json")
            return json.loads(code_json.read_text())

    def test_codeobj_ids_in_code_json(self):
        datasets = self._collect_codeobjs()
        if not datasets:
            self.skipTest("no datasets with .out code objects found")

        any_checked = False
        for name, ids in datasets.items():
            # Only valid (non-zero) ids make it into the output.
            valid = {cid: p for cid, p in ids.items() if cid != 0}
            if not valid:
                continue
            doc = self._run_script(valid.values(), name)
            rows = doc.get("code", [])
            self.assertTrue(rows, f"[{name}] code.json has no rows")

            emitted = {row[CODEOBJ_COL] for row in rows}
            for cid in valid:
                self.assertIn(
                    cid, emitted,
                    msg=f"[{name}] code object id {cid} missing from code.json")
            # No stray ids: every row must belong to a requested codeobj.
            self.assertTrue(
                emitted.issubset(set(valid)),
                msg=f"[{name}] unexpected codeobj ids {emitted - set(valid)}")
            any_checked = True

        if not any_checked:
            self.skipTest("no datasets with non-zero codeobj ids found")

    def test_dwarf_source_correlation(self):
        """DWARF-bearing datasets must yield file:line source comments."""
        any_checked = False
        for name in DWARF_DATASETS:
            ds = self.root / name
            if not ds.is_dir():
                continue
            valid = [p for p in sorted(ds.glob("*.out"))
                     if gs.codeobj_id_from_path(str(p)) != 0]
            if not valid:
                continue
            doc = self._run_script(valid, name)
            n_dwarf = sum(
                1 for row in doc.get("code", [])
                if isinstance(row[COMMENT_COL], str)
                and gs._RE_PATH_LINE.search(row[COMMENT_COL]))
            self.assertGreater(
                n_dwarf, 0,
                msg=f"[{name}] expected DWARF file:line comments, found none")
            any_checked = True

        if not any_checked:
            self.skipTest(
                "no DWARF reference datasets present: " + ", ".join(DWARF_DATASETS))


if __name__ == "__main__":
    unittest.main()
