# -*- coding: utf-8 -*-
"""
Different texture sizes in atlas class

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations

from dataclasses import dataclass
import numpy as np
import math

from copy import deepcopy
from typing import Tuple

from base import TxtAtlas
from utils import Metadata
from logger.progress_iterator import log_progress
from .packing import RectangleBin


class VariableTxtAtlas(TxtAtlas):
    """Variable texture sizes in atlas"""

    @dataclass
    class TxtInfo:
        """Txt info"""

        # (x, y) positions as col and row in the image
        positions: np.ndarray = None
        completed: bool = False
        density: float = 0.0
        coverage: float = 0.0
        binpack: RectangleBin = None

    def __init__(self, metadata: Metadata, squared_atlas_size: int = 0, reserve_pct: float = 0.0) -> None:
        """Init"""
        super().__init__(metadata, squared_atlas_size)
        self._binpack_filename = 'texture_binpack.npy'
        self._binpack: RectangleBin = None
        self._reserve_pct = reserve_pct

    def _load_parent_binpack(self, derived: Metadata = None):
        if derived is None:
            return
        bin_tree = derived.load(self._binpack_filename, allow_pickle=True)
        if bin_tree is None:
            return
        self._binpack = RectangleBin.load_dfs_layout(bin_tree)

    def _fit_rectangles(self, root_atlas: RectangleBin, rectangles: np.ndarray, randomize: bool = False) -> TxtInfo:
        """Run packer

        Args:
            rectangles (np.ndarray): [width, height] since we are considering (x,y) coordinates.
            randomize (bool, optional): randomize order of rectangles. Defaults to False.

        Returns:
            TxtInfo: results
        """

        # let's sort based on diagonal of the rectangles
        order_idx = np.argsort(np.sum(rectangles**2, axis=1))[::-1]
        if randomize:
            # max index from which to randomly shuffle
            max_id = int(np.random.random() * len(rectangles)) - 1
            order_idx[:max_id] = np.random.permutation(order_idx[:max_id])

        # ------------------- add textures ------------------- #
        # Need to deepcopy the bin tree in order to reset for each scale
        atlas = deepcopy(root_atlas)

        area = 0.0
        completed = True
        offsets = np.zeros([len(rectangles), 2], dtype=int)

        for idx in order_idx:
            rec_offset = atlas.insert(rectangles[idx])
            if rec_offset is None:
                completed = False
                break

            # save stats
            area += np.prod(rectangles[idx])
            offsets[idx] = rec_offset

        if not completed:
            return self.TxtInfo()

        # ------------------- build otuput ------------------- #

        used_area = np.max(offsets + rectangles, axis=0)
        # of the area we are using, how efficient are we in using it
        density = area / np.product(used_area)

        # of the entire area, how much are we using
        # coverage = area / root_atlas.get_free_area()

        return self.TxtInfo(positions=offsets,
                            completed=True,
                            density=density,
                            coverage=0,
                            binpack=atlas)
    
    def _binsearch_scales(self,
                          atlas: RectangleBin,
                          rectangles: np.ndarray,
                          scale_tweak: float = 1,
                          randomize: bool = False) -> Tuple[TxtInfo, float]:
        """Binary search for scale that efficiently packs all textures
        """
        max_edge = np.max(rectangles)
        ar = rectangles[:,1] / rectangles[:,0]
        total_area = np.sum(np.prod(rectangles, axis=1))

        # Guess the worst-case scale factor limits using ratio of biggest/smallest aspect ratios
        ar_worst = np.max(ar) / np.min(ar)
        scale_factor = scale_tweak * max(min(10.0, ar_worst), 2.0)

        atlas_area = atlas.get_free_area()
        up_scale = math.ceil(math.sqrt(atlas_area / total_area) * scale_factor)
        low_scale = math.floor(math.sqrt(atlas_area / total_area) / scale_factor)

        # Number of partitions that can (approximately) make a difference in coverage
        min_step = 0.5 / max_edge
        num_scales = math.ceil((up_scale - low_scale) / min_step)

        # Binary search the space 
        num_bins = 2 * math.ceil(math.log2(num_scales))
        best_txt_info = self.TxtInfo()
        best_scale = low_scale
        for idx in log_progress(range(num_bins), "Analyzing scales"):
            delta = up_scale - low_scale
            scale = 0.5 * delta + low_scale

            scaled_card_sizes = (rectangles * scale).astype(np.int32)
            res = self._fit_rectangles(atlas, scaled_card_sizes, randomize)

            if not res.completed:
                up_scale = scale
            else:
                low_scale = scale
                best_txt_info = res
                best_scale = scale

            if ( abs(delta) < min_step ):
                break

        return best_txt_info, best_scale
    
    def _compute_reserved_atlas(self) -> RectangleBin:
        """Compute atlas space for current run with reserved space for later LODs (if requested)
        """
        if self._binpack is None:
            atlas = RectangleBin(size=(self._atlas_size, self._atlas_size))
            reserved = atlas
            prev_res_size = reserved.get_size()
        else:
            # TODO: Use all space, not just reserved?
            atlas = self._binpack.get_reserved()
            reserved = atlas
            prev_res_size = reserved.get_size()
            # Can't make layout if area is 0
            if np.prod(prev_res_size) < 1:
                self._logger.error('Cannot create texture layout, no area reserved in previous LOD')
                return
        
        # Prepare to reserve subrect for later LODs
        atlas.reset_reserved()

        # Heuristics to decide reasonable reserve orientations to try
        prev_res_area = np.prod(prev_res_size)
        small_side = np.min(prev_res_size)

        # Leave this as float and ceil in edge calcs
        res_area = prev_res_area*self._reserve_pct

        # Don't reserve any space for next LOD if < 1%
        min_reserve = 0.01
        if self._reserve_pct < min_reserve:
            return atlas

        # Check if square will make a side too thin (<= 0.4 right now)
        sq_size = math.ceil(math.sqrt(res_area))
        square_rat = sq_size / small_side
        thin_size = small_side - sq_size

        # Size of horizontal/vertical bar
        h_size = np.array([prev_res_size[0], math.ceil(res_area / prev_res_size[0])], dtype=np.int32)
        v_size = np.array([math.ceil(res_area / prev_res_size[1]), prev_res_size[1]], dtype=np.int32)

        # Comparison ratios 1<ar<Inf
        h_ar = np.max(h_size) / np.min(h_size)
        v_ar = np.max(v_size) / np.min(v_size)

        # If less than 40% of short-side will be used reserve a square (also limit abs pixels)
        if square_rat <= 0.6 and thin_size >= 64:
            reserved.reserve(size=(sq_size,sq_size))
        # Otherwise use the more-square (closer to 1) aspect ratio
        elif h_ar < v_ar:
            reserved.reserve(size=h_size)
        else:
            reserved.reserve(size=v_size)
        return atlas
    
    def _compute_clear_mask(self, atlas: RectangleBin):
        """Create mask for clearing active layout region (for multi-LOD textures)
        """
        # TODO: Compute this on texture gen instead!
        self._clear_mask = np.zeros((self._atlas_size,self._atlas_size), dtype=np.bool8)
        b_cl = atlas.get_bounds()
        self._clear_mask[b_cl[1]:b_cl[3], b_cl[0]:b_cl[2]] = True
        # TODO: Subregion clearing creates some confusing artifacts, handle this better
        # reserved = atlas.get_reserved()
        # if reserved is not None:
        #     b_rs = reserved.get_bounds()
        #     self._clear_mask[b_rs[1]:b_rs[3], b_rs[0]:b_rs[2]] = False
        return self._clear_mask

    def set_parent_layout(self, derived_meta: Metadata = None):
        """Set the parent metadata to use to derive reserved layout space
            NOTE: Must have reserved space on previous LOD
        """
        self._load_parent_binpack(derived_meta)

    def generate_coordinates(self, card_ids: np.ndarray, card_sizes: np.ndarray, compression_factors: np.ndarray = None) -> None:
        """Generate texture coordinates

        Args:
            card_ids (np.ndarray): card group/card id pairs [[gid,cid]...]
            card_sizes (np.ndarray): card sizes [[h, w]...]
            compression_factors (np.ndarray): height compression factor per-card (or None)
        """

        self._logger.info('Analyzing scales...')

        self._card_ids = np.array(card_ids)
        self._id_map = self._map_from_ids(self._card_ids)

        compressed_sizes = np.copy(card_sizes)
        if compression_factors is not None:
            compressed_sizes[:, 0] = card_sizes[:, 0] * compression_factors

        # Get atlas (or partial atlas) with reserved space for next LOD
        atlas = self._compute_reserved_atlas()
        self._compute_clear_mask(atlas)

        # This shouldn't ever run more than once, but loop just in case
        for scale_tweak in range(1,5):
            res, scale = self._binsearch_scales(atlas, compressed_sizes[:, [1, 0]], scale_tweak)
            if res is None or res.completed:
                break

        tree = res.binpack.save_dfs_layout()
        self._metadata.save(self._binpack_filename, tree)

        txt_sizes = (compressed_sizes * scale).astype(np.int32)
        # hight and width
        self._txt_size = txt_sizes
        # bring it to [row, column] format
        self._txt_coords = res.positions[:, [1, 0]]
        # physical height/width
        self._card_size = np.copy(card_sizes)
