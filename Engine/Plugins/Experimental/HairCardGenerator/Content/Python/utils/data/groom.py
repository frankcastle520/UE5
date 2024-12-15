# -*- coding: utf-8 -*-
"""
Hair descriptor

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations
from typing import Union

import pickle
import numpy as np

from base import FrameworkClass

from logger.progress_iterator import log_progress

VERSION = 1.0


class Groom(FrameworkClass):
    """Groom descriptor"""

    def __init__(self, num_points_per_curve: int = None) -> None:
        """Init"""

        super().__init__()

        self.center = 0

        self.num_points_per_curve = num_points_per_curve

        # Cached map from settings groups to strands
        self._groupidx_curves_map = {}

        # Physics group IDs from UE Asset editor
        self._num_phys_groups = 0
        self._phys_group_id: np.ndarray = []
        self._phys_group_width_multipliers: np.ndarray = []

        # applied transformations
        self._R = np.eye(3)
        self._s = np.eye(3)

        # cached data
        self._curve_pts = None
        self._curve_pts_width = None
        self._root_pts = None
        self._max_width = None

    @property
    def points(self) -> np.array:
        """Get points on the curves based on the interpolation factor"""
        return self._curve_pts

    @property
    def root_points(self) -> np.array:
        """Gets the root point of every strand"""
        if self._root_pts is None:
            self._root_pts = self._curve_pts[:, 0]

        return self._root_pts

    @property
    def widths(self) -> np.array:
        """Get point widths"""
        if len(self._phys_group_width_multipliers) == self._num_phys_groups:
            vertex_multipliers = np.linspace(self._phys_group_width_multipliers[self._phys_group_id, 0],
                                             self._phys_group_width_multipliers[self._phys_group_id, 1],
                                             self.num_points_per_curve, axis=-1)

            return self._curve_pts_width * vertex_multipliers
        else:
            return self._curve_pts_width

    @property
    def max_width(self) -> np.array:
        """Get maximum width"""
        return self._max_width

    def update_width_multipliers(self, hair_widths: np.array, root_scales: np.array, tip_scales: np.array):
        if len(hair_widths) == self._num_phys_groups and len(root_scales) == self._num_phys_groups and len(tip_scales) == self._num_phys_groups:
            self._phys_group_width_multipliers = np.ones((hair_widths.size, 2))

            override_mask = hair_widths > 0
            if np.any(override_mask):
                self._phys_group_width_multipliers[override_mask,:] = np.repeat(hair_widths[override_mask,None], 2, axis=1) / self._max_width
                self._phys_group_width_multipliers[:, 0] *= root_scales
                self._phys_group_width_multipliers[:, 1] *= tip_scales

            return True
        else:
            return False

    def add_curve_to_filter_group(self, curve_id: int, curve_group_index: int) -> None:
        if curve_group_index in self._groupidx_curves_map:
            self._groupidx_curves_map[curve_group_index].append(curve_id)
        else:
            self._groupidx_curves_map[curve_group_index] = [curve_id]

    def set_strands_phys_group(self, phys_group_ids: np.ndarray):
        """Set the physics group ids for all strands
        """
        self._num_phys_groups = np.size(np.unique(phys_group_ids))
        self._phys_group_id = np.array(phys_group_ids)

    def get_strand_ids_for_group_index(self, group_index: int) -> np.array:
        """ Get the strand ids of the strand belonging to the requested group_index
            NOTE: Uses internal array built on groom load for fast access
        """
        return self._groupidx_curves_map[group_index]

    def scale(self, s: np.array):
        """Apply absolute scale to groom. E.g. if we apply scale 0.5 and later
        1.5 the resulting operation is like applying only scale 1.5 to the
        original groom.

        Args:
            s (np.array): scale transformation
        """

        if np.array_equal(self._s, s):
            return

        s_ = np.dot(s, self._s.T)

        # apply scale to all points
        self._curve_pts = np.dot(s_, self._curve_pts.T).T
        self._s = s

    def rotate(self, R: np.array, centered: bool) -> None:
        """Apply absolute rotation to asset. E.g. if we apply rotation x and later
        rotation y the resulting operation is like applying only rotation y the
        original groom.

        Args:
            R (np.array): rotation matrix
            centered (bool): centered around groom
        """

        if np.array_equal(self._R, R):
            return

        R_ = np.dot(R, self._R.T)
        center = np.zeros(3)
        if centered:
            center = self.center

        # apply transformation to all points
        points = self._curve_pts - center
        rotated = np.dot(R_, points.T).T

        self._curve_pts = rotated + center
        self._R = R

    def translate(self, t: np.array) -> None:
        """Apply a translation to the asset.

        Args:
            t (np.array): translation
        """

        if isinstance(t, list):
            t = np.array(t)

        assert t.ndim == 1, "unexpected number of dimensions"
        assert t.shape[0] == 3, "unexpected size"

        # apply translation to all points
        self._curve_pts = self._curve_pts + t

    def reset_curve_filter_groups(self, ue_curve_group_map):
        if ( len(ue_curve_group_map) != len(self._curve_pts) ):
            return False

        self._groupidx_curves_map = {}
        for i,grp_idx in enumerate(ue_curve_group_map):
            self.add_curve_to_filter_group(i, grp_idx)

        return True

    def unmarshal_groom_data(self, ue_groom_data):
        """Unmarshal Groom Data"""

        phys_groups = np.zeros((len(ue_groom_data.strands),), dtype=np.uint8)
        for strand_id, strand in enumerate(ue_groom_data.strands):
            phys_groups[strand_id] = strand.group_id if strand.group_id > 0 else 0

        self.set_strands_phys_group(phys_groups)
        self._curve_pts = np.array(ue_groom_data.vertex_positions).reshape((-1, self.num_points_per_curve, 3))
        self.center = np.mean(self._curve_pts, axis=(0, 1))

        width_epsilon = 1e-10
        widths = np.array(ue_groom_data.vertex_widths).reshape((-1, self.num_points_per_curve))
        max_width = widths.max()
        if max_width < width_epsilon:
            self._curve_pts_width = np.full((widths.shape), width_epsilon)
            self._max_width = width_epsilon
        else:
            self._curve_pts_width = widths
            self._max_width = max_width

    def dump(self, file_name):
        with open(file_name, "wb") as f:
            pickle.dump(self, f)

    @staticmethod
    def load(file_name):
        with open(file_name, "rb") as f:
            return pickle.load(f)