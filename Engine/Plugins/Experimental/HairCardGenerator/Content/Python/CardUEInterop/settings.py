# -*- coding: utf-8 -*-
"""
Settings

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import numpy as np

class GroupSettings:
    pass


class Settings:
    """Settings"""

    def __init__(self) -> None:
        """Init"""

        self.bg_color = [0.0, 0.0, 0.0, 1.0]

        # all cards
        self.num_clumps = 300
        self.geo_margin = 7.5
        self.uv_margin = 2.0
        self.LOD = 0
        self.run_group = 0

        self.accurate_card_geometry = False

        # individual cards
        self.vertical_subd = 5
        self.horizontal_subd = 1

        # texture
        self.dilation_size = 16
        self.allowed_dilation_sizes = np.array([0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40],
                                               dtype=int)

        # atlas
        self.atlas_size = 1024 * 2
        self.allowed_atlas_sizes = np.array([2**10, 2**11, 2**12, 2**13, 2**14],
                                            dtype=int)
        self.num_quant_textures = 150

        # strand
        self.n_cards_sharing_strands = 2
        self.strand_width = 1.0
        self.strand_sampling = 2
        self.allowed_strand_sampling = np.array([1, 2, 4, 6, 8, 10, 12, 14, 16],
                                                dtype=int)
