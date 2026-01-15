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

"""
Compare latency output from C++ and Python implementations.
"""
import json
import argparse
import sys

def load_results(path):
    with open(path, 'r') as f:
        data = json.load(f)
    # Build dict by code for easy lookup
    return {instr['code']: instr['stats'] for instr in data['instructions']}

def compare_results(cpp_path, py_path, tolerance=1e-6):
    cpp_results = load_results(cpp_path)
    py_results = load_results(py_path)
    
    cpp_codes = set(cpp_results.keys())
    py_codes = set(py_results.keys())
    
    all_match = True
    
    # Check for missing codes
    only_cpp = cpp_codes - py_codes
    only_py = py_codes - cpp_codes
    
    if only_cpp:
        print(f"\033[91mCodes only in C++:\033[0m")
        for code in sorted(only_cpp):
            print(f"  {code}")
        all_match = False
    
    if only_py:
        print(f"\033[91mCodes only in Python:\033[0m")
        for code in sorted(only_py):
            print(f"  {code}")
        all_match = False
    
    # Compare common codes
    common = cpp_codes & py_codes
    print(f"\nComparing {len(common)} common instructions...")
    
    mismatches = []
    for code in sorted(common):
        cpp_stats = cpp_results[code]
        py_stats = py_results[code]
        
        diffs = {}
        for key in ['mean', 'stddev', 'error', 'count']:
            cpp_val = cpp_stats.get(key, 0)
            py_val = py_stats.get(key, 0)
            
            if key == 'count':
                if cpp_val != py_val:
                    diffs[key] = (cpp_val, py_val)
            else:
                if abs(cpp_val - py_val)/max(1, abs(cpp_val+py_val)) > tolerance:
                    diffs[key] = (cpp_val, py_val)
        
        if diffs:
            mismatches.append((code, diffs))
    
    if mismatches:
        print(f"\n\033[91m{len(mismatches)} mismatches found:\033[0m")
        for code, diffs in mismatches:
            print(f"\n\033[94m{code}\033[0m")
            for key, (cpp_val, py_val) in diffs.items():
                diff = cpp_val - py_val if isinstance(cpp_val, float) else None
                if diff is not None:
                    print(f"  {key}: C++={cpp_val:.4f}, Py={py_val:.4f}, diff={diff:.4f}")
                else:
                    print(f"  {key}: C++={cpp_val}, Py={py_val}")
        all_match = False
    else:
        print(f"\033[92mAll {len(common)} instructions match!\033[0m")
    
    return all_match

def main():
    parser = argparse.ArgumentParser(description='Compare C++ and Python latency outputs')
    parser.add_argument('cpp_json', help='Path to C++ output JSON')
    parser.add_argument('py_json', help='Path to Python output JSON')
    parser.add_argument('--tolerance', '-t', type=float, default=1e-5,
                        help='Tolerance for floating point comparison (default: 1e-6)')
    
    args = parser.parse_args()
    
    match = compare_results(args.cpp_json, args.py_json, args.tolerance)
    sys.exit(0 if match else 1)

if __name__ == '__main__':
    main()
