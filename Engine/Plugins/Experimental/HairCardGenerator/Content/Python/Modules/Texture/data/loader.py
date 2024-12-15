# -*- coding: utf-8 -*-
"""
Custom data loader

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import torch
from torch.utils.data import DataLoader

from Modules.Texture.data import CardsDataset
from .helper import CardData, OptData

from typing import Tuple

class CardsDataLoader(DataLoader):
    """Cards DataLoader"""

    def __init__(self,
                 dataset: CardsDataset,
                 batch_size: int = 1,
                 shuffle: bool = False,
                 num_workers: int = 0):
        super().__init__(dataset,
                         batch_size=batch_size,
                         shuffle=shuffle,
                         num_workers=num_workers,
                         collate_fn=self._custom_collate)

        self._ignore_depth = dataset.ignore_depth

    def _custom_collate(self, batch: Tuple[CardData,OptData]) -> Tuple[CardData,OptData]:
        """Custom collate function that combines the data
        coming from the different batches

        Notice:
            len(batch) == batch_size
            GroomData, OptData = batch[0]
        """

        verts = torch.cat([d[0].verts for d in batch])
        normals = torch.cat([d[0].normals for d in batch])
        tangents = torch.cat([d[0].tangents for d in batch])
        uvs = torch.cat([d[0].uvs for d in batch])
        b_uv = torch.cat([d[1].b_uv for d in batch])
        tan = torch.cat([d[1].tan for d in batch])
        u_coord = torch.cat([d[1].s_u for d in batch])
        seeds = torch.cat([d[1].seeds for d in batch])
        pts = torch.cat([d[1].pts for d in batch])
        widths = torch.cat([d[1].widths for d in batch])
        depth = None if self._ignore_depth else torch.cat([d[1].depth for d in batch])

        faces = []
        txt = []
        cid = []
        cfid = []

        max_vid = 0

        card_data: CardData
        opt_data: OptData
        for card_data, opt_data in batch:
            # NOTE: Datasets just send the card id in both CardData.cfid and OptData.cid
            index = opt_data.cid
            faces.append(card_data.faces + max_vid)
            txt.append(card_data.txt + max_vid)
            cid.append(torch.ones(opt_data.pts.size(0), dtype=torch.int) * index)
            cfid.append(torch.ones(card_data.faces.size(0), dtype=torch.int) * index)
            max_vid += card_data.faces.max() + 1

        faces = torch.cat(faces)
        txt = torch.cat(txt)
        cid = torch.cat(cid)
        cfid = torch.cat(cfid)

        groom_data = CardData(verts, faces, normals, tangents, uvs, txt, cfid)
        opt_data = OptData(pts, widths, b_uv, depth, tan, u_coord, seeds, cid)
        return groom_data, opt_data
