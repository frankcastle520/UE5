# -*- coding: utf-8 -*-
"""
Class to extract texture coordinates from an atlas where
each texture is squared.

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from math import floor, sqrt, ceil
from base import TxtAtlas


class SquaredTxtAtlas(TxtAtlas):
    """UV helper class"""

    def generate_coordinates(self, n_textures: int) -> None:
        """Compute texture coordinates

        Args:
            n_textures (int): number of squared textures in atlas
        """

        # texture size -> same for all textures
        atlas_ndim = ceil(sqrt(n_textures))
        margin_px = self._margin * 2 * atlas_ndim
        card_px_size = floor((self._atlas_size - margin_px) / atlas_ndim)
        self._txt_size = [(card_px_size, card_px_size)] * n_textures

        # find each texture position
        for tid in range(n_textures):
            grid_c = tid % atlas_ndim
            grid_r = tid // atlas_ndim

            # pixel coordinates in the atlas
            # (0,0) is BL corner instead of TL
            c = (card_px_size + self._margin * 2) * grid_c + self._margin
            r = self._atlas_size - card_px_size * (
                grid_r + 1) - 2 * self._margin * grid_r - self._margin
            self._txt_coords.append((r, c))
