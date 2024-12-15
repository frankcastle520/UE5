# -*- coding: utf-8 -*-
"""
Data loader

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations
import os

import numpy as np
import torch
from torch.utils.data import Dataset

from Modules.Texture.geometry import Mesh
from utils.data import Groom
from utils import parameters
from utils import Metadata

from .helper import CardData, OptData

from typing import Tuple

class CardsDataset(Dataset):
    """Cards dataset"""

    def __init__(self,
                 groom: Groom,
                 asset_name: str,
                 obj_path: str,
                 metadata: Metadata,
                 dataset_card_filter: list = None,
                 strand_width_scale: float = parameters.rendering.default_width_scale,
                 ignore_depth: bool = True) -> None:
        """Initialize cards loader

        Args:
            groom (Groom): groom asset with strands data
            asset_name (str): asset name
            obj_path (str): path to dir containing objs
            metadata (str): path to dir containing metadata
            strand_width_scale (float, optional): strand width scale factor.
        """

        super().__init__()

        # attributes
        self._groom = groom
        self._name = asset_name
        self._w_scale = strand_width_scale

        self._obj_dir = os.path.join(obj_path, self._name)
        self._metadata = metadata

        self.ignore_depth = ignore_depth

        # check that data_dir has the right processed information
        assert os.path.exists(self._obj_dir), f"Dir doesn't exist: {self._obj_dir}"

        # load clustering info
        k_name = 'cards_strand_labels.npy'
        self._labels = self._metadata.load(k_name, allow_pickle=True)

        if dataset_card_filter is None:
            self._card_ids = np.unique(self._labels[self._labels >= 0])
        else:
            # only use provided card id
            self._card_ids = np.array(dataset_card_filter)

    def __len__(self) -> int:
        return len(self._card_ids)

    def __getitem__(self, idx: int) -> Tuple[CardData,OptData]:
        """Get card with idx among list of cards in the groom"""

        index = self._card_ids[idx]
        curve_ids = np.where(self._labels == index)[0]
        pts = self._groom.points[curve_ids].reshape(-1, 3)
        widths = self._groom.widths[curve_ids].flatten()

        pts = np.ascontiguousarray(pts, dtype=np.float32)

        pts = torch.from_numpy(pts)
        widths = torch.from_numpy(widths) * self._w_scale

        # ------------------- obj ------------------- #

        obj_path = os.path.join(self._obj_dir, '{:06d}.obj'.format(index))
        mesh = Mesh()
        mesh.load_obj(obj_path)

        # ------------------- point mesh projection ------------------- #

        config_info_name = 'config_cards_info.npy'
        lr_ratio = self._metadata.load(config_info_name, allow_pickle=True)[index].lr_ratio

        if self.ignore_depth:
            depth = None
            b_uv = mesh.project_csplines(pts, lr_ratio)
        else:
            b_uv, depth = mesh.project_csplines(pts, lr_ratio, ignore_depth=False)

        # ------------------- tangents ------------------- #
        seg_pts = pts.view((-1, self._groom.num_points_per_curve, 3))
        seg_tan = seg_pts[:, 1:] - seg_pts[:, :-1]
        seg_tan_avg = 0.5 * (seg_tan[:,1:] + seg_tan[:,:-1])

        # Use vertex tangents (average segment tangents for each internal curve vertex)
        # NOTE: Use world-space tangents and convert to tangent space in a second rasterization pass
        tan = torch.cat([seg_tan[:,0].unsqueeze(1), seg_tan_avg, seg_tan[:,-1].unsqueeze(1)], dim=1)
        tan /= tan.norm(dim=2).unsqueeze(2)
        tan = tan.view([-1, 3])

        # u coordinates
        u_coord = torch.linspace(0.0, 1.0, steps=self._groom.num_points_per_curve)
        u_coord = u_coord.unsqueeze(0).repeat([len(widths), 1]).flatten()

        # seeds -> random value per strand
        seeds = torch.rand([len(widths), 1]).repeat([1, self._groom.num_points_per_curve]).flatten()

        # ------------------- pack data ------------------- #

        groom_data = CardData(mesh.vertices, mesh.faces, mesh.normals, mesh.tangents,
                              mesh.uvs, mesh.txt_idx, index)
        opt_data = OptData(pts, widths, b_uv, depth, tan, u_coord, seeds, index)

        return groom_data, opt_data
