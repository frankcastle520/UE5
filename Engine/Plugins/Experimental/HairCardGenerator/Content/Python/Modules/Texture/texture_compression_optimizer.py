# -*- coding: utf-8 -*-
"""
Texture compression optimizer

@author: Erica Alcusa Saez'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import numpy as np
import os
import torch

from utils.data import Groom
from utils import Metadata
from logger.progress_iterator import log_progress
from Modules.Texture.geometry import Mesh


class TextureCompressionOptimizer():
    """Texture compression optimizer"""

    def __init__(self,
                 asset_name: str,
                 groom: Groom,
                 id_centers: list,
                 metadata: Metadata,
                 obj_path: str,
                 max_slope: float = 0.5,
                 max_compression_factor: float = 0.1) -> None:
        """Init

        Args:
            asset_name (str): asset name
            groom (Groom): groom asset
            id_centers (list): centroid ids
            metadata (str): path to dir containing metadata
            obj_path (str): path to dir containing objs
            max_slope (float): maximum allowed slope for compressing
            max_compression_factor: maximum compression factor
        """

        self._asset_name = asset_name
        self._groom = groom
        self._id_centers = id_centers
        self._metadata = metadata
        self._obj_path = obj_path
        self._max_slope = max_slope
        self._max_compression_factor = max_compression_factor

    def get_compression_factors(self):
        """Get compression factor for every centoid card

        Returns:
            np.array with the centroid cards stretch factors
        """

        compression_factors = []

        # load clustering info
        k_name = 'cards_strand_labels.npy'
        labels = self._metadata.load(k_name, allow_pickle=True)

        cards_confg = self._metadata.load('config_cards_info.npy', allow_pickle=True)

        for index in log_progress(self._id_centers, 'Calculating texture compression'):
            obj_path = os.path.join(self._obj_path, self._asset_name, '{:06d}.obj'.format(index))
            lr_ratio = cards_confg[index].lr_ratio
            mesh = Mesh()
            mesh.load_obj(obj_path)

            curve_ids = np.where(labels == index)[0]

            projected_points = mesh.project_csplines(torch.from_numpy(
                self._groom.points[curve_ids]).float().view(-1, 3), lr_ratio)
            projected_points = projected_points.view(-1, self._groom.num_points_per_curve, 2)
            dif_vector = projected_points[:, 1:] - projected_points[:, :-1]

            eps = 1e-12
            slopes = torch.abs((dif_vector[:, :, 0]) / (dif_vector[:, :, 1] + eps)).flatten()

            compression_factors.append(min(torch.sum(torch.sqrt(slopes)) / len(slopes), 1))

        return compression_factors
