# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

import numpy as np
from datetime import datetime

debug_level = 0
PRINT_TIMING_METRICS = False

def log_timing_metrics(prev_time, context, label):
    if (PRINT_TIMING_METRICS == False):
        return

    global debug_level
    current_time = datetime.now()
    diff = current_time - prev_time
    debug_str = "Timing Metrics\t\t"
    for i in range(debug_level):
        debug_str += "\t"

    debug_str += context + "\t\t" + label + ": " + str(diff.total_seconds()) + " seconds"
    print(debug_str)
    return current_time

def start_timing():
    global debug_level
    debug_level += 1
    return datetime.now()

def stop_timing():
    global debug_level
    debug_level -= 1

def ue_to_np(ue_vectors):
    tuples = []

    for vector in ue_vectors:
        tuples.append(vector.to_tuple())

    return np.array(tuples, dtype=np.float32)
