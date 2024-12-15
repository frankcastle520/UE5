# -*- coding: utf-8 -*-
"""
Base texture atlas class

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations

from abc import abstractmethod
import numpy as np
from typing import Tuple
from utils import Metadata

from .template import FrameworkClass

class TxtAtlas(FrameworkClass):
    """Base texture atlas generator class"""

    def __init__(self, metadata: Metadata, squared_atlas_size: int) -> None:
        """Initialize

        Args:
            squared_atlas_size (int): atlas size in pixels
        """

        super().__init__()

        self._atlas_size = squared_atlas_size
        self._metadata = metadata
        self._clear_mask = np.ones((self._atlas_size,self._atlas_size), dtype=np.bool8)

        # computed
        self._card_ids: np.ndarray = []
        self._id_map: dict[Tuple[int,int],int] = {}
        self._txt_size: np.ndarray = []
        self._txt_coords: np.ndarray = []
        self._card_size: np.ndarray = []

    def _map_from_ids(self, ids: np.ndarray) -> dict[Tuple[int,int],int]:
        return dict({(int(k[0]),int(k[1])):i for i,k in enumerate(ids)})

    def load_from_file(self, file_name: str) -> None:
        # TODO: Potentially pass metadata to txt atlas creation instead
        filedata = self._metadata.load(file_name)
        self._txt_size = filedata['txt_size']
        self._txt_coords = filedata['txt_coords']
        self._card_size = filedata['card_size']
        self._card_ids = filedata['card_ids']
        self._clear_mask = filedata['clear_mask']
        self._id_map = self._map_from_ids(self._card_ids)

    def save_to_file(self, file_name: str) -> None:
        self._metadata.savez(file_name, 
                             txt_size=self._txt_size,
                             txt_coords=self._txt_coords,
                             card_ids=self._card_ids,
                             card_size=self._card_size,
                             clear_mask=self._clear_mask)

    @abstractmethod
    def generate_coordinates(self) -> None:
        """Generate coordinates"""
        raise NotImplementedError()

    def __len__(self) -> int:
        """Number of textures"""
        if len(self._txt_coords) > 0:
            return len(self._txt_coords)
        return 0
    
    def get_clear_mask(self) -> np.ndarray:
        return self._clear_mask

    def get_texture_size(self, grp_id: int, card_id: int) -> Tuple[int,int]:
        """Get texture size

        Args:
            txt_id (int): texture id

        Returns:
            tuple: texture size [height, width]
        """
        txt_id = (int(grp_id),int(card_id))
        assert len(self._card_size) > 0, "sizes not initialized"
        assert txt_id in self._id_map, "ID not valid"

        idx = self._id_map[txt_id]
        return tuple(self._txt_size[idx])

    def get_texture_coordinate(self, grp_id: int, card_id: int) -> Tuple[int,int]:
        """Get texture position in pixel coordinates of
        texture with txt_id in the atlas image

        Args:
            grp_id (int): Card Group Id
            card_id (int): Card (centroid) Id

        Returns:
            tuple: row and column coordinate
        """
        txt_id = (int(grp_id),int(card_id))
        assert len(self._card_size) > 0, "sizes not initialized"
        assert txt_id in self._id_map, "ID not valid"

        idx = self._id_map[txt_id]
        return tuple(self._txt_coords[idx])
    
    def get_physical_size(self, grp_id: int, card_id: int) -> Tuple[int,int]:
        """Get physical size texture will be mapped to (in cm/uu)

        Args:
            grp_id (int): Card Group Id
            card_id (int): Card (centroid) Id

        Returns:
            tuple: card size in cm [height, width]
        """
        txt_id = (int(grp_id),int(card_id))
        assert len(self._card_size) > 0, "sizes not initialized"
        assert txt_id in self._id_map, "ID not valid"

        idx = self._id_map[txt_id]
        return tuple(self._card_size[idx])
