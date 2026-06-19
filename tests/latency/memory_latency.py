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
Command-line version of memory_latency.py with option to save results for comparison with C++.
"""
import numpy as np
import json
import glob
import sys
import os
import argparse

def parse_shader_engines(value):
    """Parse shader engines from comma-separated format."""
    value = value.strip('[]')
    if ',' in value:
        return [int(x.strip()) for x in value.split(',')]
    return [int(value)]

parser = argparse.ArgumentParser(description='Latency Analysis Tool')
parser.add_argument('uidir', help='Path to UI output directory')
parser.add_argument('--counter', '-c', default='SQ_INST_LEVEL_VMEM',
                    choices=['SQ_INST_LEVEL_VMEM', 'SQ_INST_LEVEL_SMEM', 'SQ_INST_LEVEL_LDS'],
                    help='Performance counter to analyze')
parser.add_argument('--target-cu', '-t', type=int, default=1,
                    help='--att-target-cu used in rocprofv3 (default 1)')
parser.add_argument('--perf-interval', '-p', type=int, default=40,
                    help='Performance interval (40 for MI300, 36 for MI350)')
parser.add_argument('--shader-engines', '-s', type=parse_shader_engines, default=[0],
                    help='Shader engines to analyze, e.g. 0,16 (default: 0)')
parser.add_argument('--output', '-o', type=str, default=None,
                    help='Save results to JSON file instead of plotting')
parser.add_argument('--no-plot', action='store_true',
                    help='Disable plotting')

args = parser.parse_args()

COUNTER_NAME = args.counter
ATT_TARGET_CU = args.target_cu
PERF_INTERVAL = args.perf_interval
uidir = args.uidir
SHADER_ENGINES = args.shader_engines

is_level_counter = {
    'SQ_INST_LEVEL_VMEM': lambda x: x == 3 or x == 4,
    'SQ_INST_LEVEL_SMEM': lambda x: x == 1,
    'SQ_INST_LEVEL_LDS' : lambda x: x == 5
}[COUNTER_NAME]

open_json = lambda x: json.load(open(os.path.join(uidir, x), 'r'))

filenames = open_json('filenames.json')

# index of level counter
LEVEL = -1
for idx, name in enumerate(filenames["counter_names"]):
    if name == COUNTER_NAME:
        LEVEL = idx + 1

assert(LEVEL != -1), f"Counter {COUNTER_NAME} not found"

used_index = {}
insts = []

code = {int(c[2]): str(c[3]).split('/')[-1] + ": " + str(c[0]) for c in open_json('code.json')["code"]}

for SE in SHADER_ENGINES:
    perf = open_json('se'+str(SE)+'_perfcounter.json')["data"]
    perf = [p[:5] for p in perf if p[5] == ATT_TARGET_CU and p[6] == 0]

    if not perf: continue

    # We should use SQ_BUSY_CU_CYCLES instead
    for k in range(1, len(perf)):
        if perf[k][0] - perf[k-1][0] > PERF_INTERVAL:
            perf.append([perf[k-1][0] + PERF_INTERVAL, 0, 0, 0, 0, 0])

    perf = sorted(perf, key=lambda x: x[0])
    perf.append([perf[-1][0]+PERF_INTERVAL, 0, 0, 0, 0, 0])

    def get_wave(name):
        simd = int(name.split('_sm')[1][0])
        wave = json.load(open(name, 'r'))["wave"]["instructions"]
        return [[simd, int(c[0]) + int(c[2]) + simd, int(c[4])] for c in wave if is_level_counter(int(c[1]))]

    waves = []
    for file in glob.glob(uidir+"/se"+str(SE)+"_sm*.json"):
        waves.extend(get_wave(file))

    waves = sorted(waves, key=lambda x: x[1])

    for wave in waves:
        if wave[2] not in used_index:
            used_index[wave[2]] = len(insts)
            insts.append([code[wave[2]], []])

        wave[2] = used_index[wave[2]]

    perf_time = [p[0] for p in perf]
    active = [p[LEVEL] for p in perf]
    completed = [0 for p in perf]

    piter = 0
    it = 0

    for N, w in enumerate(waves):
        while piter < len(perf) and perf_time[piter] < w[1]:
            completed[piter] = N - active[piter] # completed = started - level
            while it < len(waves) and it < completed[piter]:
                delta = perf_time[piter] - waves[it][1]
                # TODO: This isn't quite right. Latencies are supposed to be biased towards the higher end, not midpoint
                if delta > PERF_INTERVAL:
                    delta -= PERF_INTERVAL/2 # Correct for counter sample delay
                else:
                    delta /= 2 # If we are in first sample, estimate ~ 1/2 actual
                insts[waves[it][2]][1].append(delta)
                it += 1
            piter += 1

    while piter < len(perf):
        completed[piter] = len(waves) - active[piter]
        while it < len(waves) and it < completed[piter]:
            it += 1
        piter += 1

# Print results
for lds in insts:
    print()
    print(f"\033[94m{lds[0]}\033[00m")
    
    unbias = 0.1
    if len(lds[1]) > 1:
        unbias = np.sqrt(len(lds[1]) - 1.5)

    lat_std = np.std(lds[1], ddof=1) if len(lds[1]) > 1 else 0.0
    lat_error = np.sqrt(lat_std**2+(PERF_INTERVAL)**2/3)/unbias

    print(f"\t Mean latency  : \033[92m{np.mean(lds[1]):.1f}\033[00m  (\033[91m{lat_error:.1f}\033[00m) +- \033[93m{lat_std:.1f}\033[00m")

get_source = lambda x: x.split(':')[0] + ':' + x.split(':')[1]

per_source = {}
for lds in insts:
    source = get_source(lds[0])
    if source not in per_source:
        per_source[source] = [len(per_source.keys()), True, []]
    per_source[source][2].extend(lds[1])

print()
for source, result in per_source.items():
    unbias = 0.1
    if len(result[2]) > 1:
        unbias = np.sqrt(len(result[2]) - 1.5)

    lat_std = np.std(result[2], ddof=1) if len(result[2]) > 1 else 0.0
    lat_error = np.sqrt(lat_std**2+(PERF_INTERVAL)**2/3)/unbias

    print(f"\033[94m{source}\033[00m - Mean Latency: \033[92m{np.mean(result[2]):.1f}\033[00m  (\033[91m{lat_error:.1f}\033[00m) +- \033[93m{lat_std:.1f}\033[00m")

# Save results to file if requested
if args.output:
    def compute_stats(latencies):
        if len(latencies) == 0:
            return {"mean": 0.0, "stddev": 0.0, "error": 0.0, "count": 0}
        unbias = 0.1
        if len(latencies) > 1:
            unbias = np.sqrt(len(latencies) - 1.5)
        lat_std = float(np.std(latencies, ddof=1)) if len(latencies) > 1 else 0.0
        lat_error = np.sqrt(lat_std**2 + (PERF_INTERVAL)**2/3) / unbias
        return {
            "mean": float(np.mean(latencies)),
            "stddev": lat_std,
            "error": float(lat_error),
            "count": len(latencies)
        }

    output_data = {
        "instructions": [
            {"code": lds[0], "stats": compute_stats(lds[1])} for lds in insts
        ]
    }
    with open(args.output, 'w') as f:
        json.dump(output_data, f, indent=2)
    print(f"\nResults saved to {args.output}")

# Plot if not disabled and no output file
if not args.no_plot and not args.output:
    import matplotlib.pyplot as plt
    
    plt.title('Each plot line is an assembly instruction.\nFor distinction, global_stores are represented by negative values.')
    for lds in insts:
        if len(lds[1]) < 32: continue
        a, b = np.histogram(lds[1], bins=32)

        b = 0.5*(b[1:] + b[:-1])
        if 'store' in lds[0]: a *= -1

        source = per_source[get_source(lds[0])]
        color = ['C7', 'C6', 'C0', 'C5', 'C2', 'C1', 'C4', 'C3'][source[0]]
        if source[1]:
            source[1] = False
            plt.plot(b, a, color, label=get_source(lds[0]))
        else:
            plt.plot(b, a, color)
    plt.legend()
    plt.ylabel('Count')
    plt.xlabel('Latency (cycles)')
    plt.show()
