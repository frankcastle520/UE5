# -*- coding: utf-8 -*-
"""
Copyright Epic Games, Inc. All Rights Reserved.
"""

from typing import Tuple

import torch
from torch import Tensor
from torch.utils.data import Dataset

class CornersDataset(Dataset):
    def __init__(
        self,
        pixels: Tensor,  # (n_frames, n_pts, 2)
        frame_inds: Tensor,  # (n_frames,)
        resolution_wh: Tuple[int, int]
    ):
        # This dataset keeps the following tensors
        # pixels: (n_valid_pts, 2)
        # frame_inds: (n_valid_pts,)
        # pt_inds: (n_valid_pts,)

        # Ensure these are indeed 2D pixels
        assert len(pixels.shape) == 3 and pixels.shape[-1] == 2

        n_frames, n_pts, _ = pixels.shape

        # Ensure that the number of frame indices match the actual number of frames
        assert frame_inds.shape[0] == n_frames

        # Compute each points' frame indices
        frame_inds = frame_inds.reshape(n_frames, 1).repeat(1, n_pts)

        # Compute each points' point indices
        pt_inds = torch.arange(n_pts).unsqueeze(0).repeat(n_frames, 1)

        # We remove out-of-frame pixels.
        pixels = pixels.reshape(-1, 2)
        valid_mask = (
            (pixels[..., 0] >= 0)
            .logical_and(pixels[..., 0] <= resolution_wh[0] - 1)
            .logical_and(pixels[..., 1] >= 0)
            .logical_and(pixels[..., 1] <= resolution_wh[1] - 1)
        ).flatten()  # (n_im_train * n_pts)

        self.pixels = pixels[valid_mask, :]  # (n_valid_pts, 3)

        self.frame_inds = frame_inds.flatten()[valid_mask]
        self.pt_inds = pt_inds.flatten()[valid_mask]

    def __len__(self):
        return self.pixels.shape[0]

    def __getitem__(self, idx):
        return (self.pixels[idx, :], self.frame_inds[idx], self.pt_inds[idx])
