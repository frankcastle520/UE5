# -*- coding: utf-8 -*-
"""
Clump generator class

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import numpy as np

from utils import data
from utils import Metadata
from utils import io as uio
from base.template import FrameworkClass

from .strand_card_allocation import StrandCardAllocator

__all__ = ['ClumpGenerator']

class ClumpGenerator(FrameworkClass):
    def __init__(self,
                 groom: data.Groom,
                 metadata: Metadata,
                 group_index: int,
                 name: str) -> None:

        super().__init__()
        
        self.groom = groom
        self._name = name
        self._group_index = group_index
        self.metadata = metadata

    def cluster_strands(self, target_num_clumps: int,
                        max_flyaways: int,
                        use_multi_card_clumps: bool) -> np.array:
        """Cluster strands according to clustering features
        and the target number of cards

        Args:
            target_num_clumps (int): number of clumps
            max_flyaways (int): maximum number of flyaways
            use_multi_card_clumps (bool): use multi-card clumps

        Returns:
            np.array: labels
        """

        clump_labels_filename = 'clumps_strand_labels.npy'
        max_main_clump_filename = 'max_main_clump.npy'

        # let's generate labels -> identify which curve is assigned to which clump
        strand_ids = self.groom.get_strand_ids_for_group_index(self._group_index)
        self._strand_allocator = StrandCardAllocator(self.groom, strand_ids)

        if use_multi_card_clumps and len(strand_ids) / target_num_clumps > 10:
            target_num_clumps = target_num_clumps // 3

        random_seed = self.metadata.load('random_seed.npy')[0]

        labels_local, max_main_clump = self._strand_allocator.allocate(target_num_clumps,
                                                                      random_seed,
                                                                      max_flyaways)

        labels = np.full(self.groom.points.shape[0], -1, dtype=np.int32)
        labels[strand_ids] = labels_local

        self.metadata.save(clump_labels_filename, labels)
        self.metadata.save(max_main_clump_filename, max_main_clump)

        return labels, max_main_clump
