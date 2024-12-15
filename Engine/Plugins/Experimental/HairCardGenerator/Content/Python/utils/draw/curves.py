# -*- coding: utf-8 -*-
"""
Draw curves

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import open3d as o3d
import numpy as np


def get_line_set_from_points(points: np.array, seg_lengths: np.array) -> o3d.geometry.LineSet:
    """Get line_set from a set of curve points

    Args:
        points (np.array): curve points
        seg_lengths (np.array): number of elements per curve

    Returns:
        o3d.geometry.LineSet: LineSet object
    """

    # let's build lines
    assert points.shape[0] == np.sum(seg_lengths)

    lines = []
    start_id = 0
    for seg in seg_lengths:
        ids = list(range(start_id, start_id + seg))
        line_segments = np.array([ids[:-1], ids[1:]])
        lines.extend(line_segments.T.tolist())
        start_id += seg

    line_set = o3d.geometry.LineSet()
    line_set.points = o3d.utility.Vector3dVector(points)
    line_set.lines = o3d.utility.Vector2iVector(lines)

    return line_set
