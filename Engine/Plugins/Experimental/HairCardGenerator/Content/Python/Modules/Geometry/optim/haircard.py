# -*- coding: utf-8 -*-
"""
HairCard class to store every haircard's info

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations

import copy
import numpy as np

from base import FrameworkClass
from utils import math as umath


class HairCard(FrameworkClass):
    """Hair card"""

    verts: np.array
    faces: np.array
    uvs: np.array
    length: float = -1
    width: float = -1
    width_lr_ratio: float = -1
    top_vec: np.array
    texture_group = 0

    def __init__(self) -> None:
        """Initialize"""

        super().__init__()
        self.loc_normal = np.array([0, 0, 1])

        # transformations
        self.R = np.eye(3, dtype=float)
        self.t = np.zeros(3, dtype=float)

    @property
    def normal(self) -> np.array:
        """Get world card normal"""
        return np.dot(self.R, self.loc_normal)

    @property
    def area(self) -> float:
        """Get card area"""

        area = 0.0
        for tri in self.faces:
            # counter clock-wise
            p0 = self.verts[tri[0]]
            p1 = self.verts[tri[1]]
            p2 = self.verts[tri[2]]

            l1 = p1 - p0
            l2 = p2 - p0
            area += umath.norm2(np.cross(l1, l2)) * 0.5

        return area

    def rotate(self, R: np.array) -> None:
        """Rotate vertices

        Args:
            R (np.array): rotation matrix
        """

        self.verts = np.dot(R, self.verts.T).T

        self.R = R

    def translate(self, t: np.array):
        """Translate vertices

        Args:
            center (np.array): translation
        """

        if isinstance(t, list):
            t = np.array(t)

        self.verts += t

        self.t = t

    def copy(self):
        """Copy content

        Returns:
            HairCard: copied haircard
        """

        new_haircard = HairCard()

        new_haircard.verts = copy.deepcopy(self.verts)
        new_haircard.faces = copy.deepcopy(self.faces)
        new_haircard.uvs = copy.deepcopy(self.uvs)

        new_haircard.R = copy.deepcopy(self.R)
        new_haircard.t = copy.deepcopy(self.t)

        return new_haircard
