# -*- coding: utf-8 -*-
"""
Cards optimizer class

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations

import os
import copy
import numpy as np

from base import BaseOptimizer
from utils import io as uio
from utils import data
from utils import Metadata
from logger.progress_iterator import log_progress
from .card_optimizer import CardOptimizer
from .metadata import CardMeta

_INT_DEBUG = False
if _INT_DEBUG:
    from Modules.Geometry.debug.groom_debugger import GroomDebugger

__all__ = ['GeometryOptimizer']


class GeometryOptimizer(BaseOptimizer):
    """Optimizer"""

    def __init__(self, groom: data.Groom, obj_path: str,
                 metadata: Metadata, group_index: int,
                 use_multicard_clumps: bool,
                 **kwargs) -> None:
        """Init

        Args:
            groom (Groom): groom asset
            obj_path (str): obj dir path where to save objs
            metadata (str): metadata object
        """

        super().__init__(**kwargs)

        self.groom = groom
        self.obj_path = uio.safe_dir(obj_path)
        self.metadata = metadata
        self._group_index = group_index
        self._use_multicard_clumps = use_multicard_clumps

        # ------------------- transformations ------------------- #

        self._R = np.eye(3)
        self._s = np.eye(3)
        # self._s[0, 0] *= -1

    @property
    def applied_R(self) -> np.array:
        """Get applied rotation"""
        return self._R

    def reset_transformations(self) -> None:
        """Reset rotation"""
        self.rotate_groom(np.eye(3), centered=True)

    def rotate_groom(self, R: np.array, centered: bool = True) -> None:
        """Apply an absolute rotation to the asset wrt the Identity rotation.

        Args:
            R (np.ndarray): Rotation matrix
            centered (bool, optional): apply rotation around groom center.
                                       Defaults to False.
        """

        self.groom.scale(self._s)
        self.groom.rotate(R, centered)
        self._R = R.copy()

    def _align_card(self, cluster_id: int, labels: np.array) -> CardOptimizer:
        """Optimize plane over hair-strand point cloud

        Args:
            cluster_id (int): cluster id
            labels (np.array): set of labels

        Returns:
            CardOptimizer: optimizer
        """

        curve_ids = np.where(labels == cluster_id)[0]

        # optimize
        card_opt = CardOptimizer(name=self._name,
                                 curves=self.groom.points[curve_ids],
                                 curve_ids=curve_ids,
                                 use_multicard_clumps=self._use_multicard_clumps,
                                 groom_center_pos=self.groom.center,
                                 all_root_points = self.groom.root_points)

        return card_opt

    def _save_single_card(self, card, card_id: int):
        """Save single card information

        Args:
            card (HairCard): card
            card_id (int): card id
        """

        card_out_path = uio.safe_dir(os.path.join(self.obj_path, self._name))
        out_path = os.path.join(card_out_path, '{:06d}.obj'.format(card_id))
        uio.ObjWriter.save_mesh(out_path, card.verts, card.faces, card.uvs)

    @staticmethod
    def _get_subd_area(card_sizes: list, v_subd: int, h_subd: int) -> tuple:
        """Get subdivision area factor to determin the subdivision level
        for each of the cards

        Args:
            card_sizes (list): card sizes

        Returns:
            tuple: vertical and horizontal area factor
        """

        card_sizes = np.array(card_sizes)
        max_size = card_sizes.max(axis=0)

        v_segment_area = max_size[0] / v_subd
        h_segment_area = max_size[1] / h_subd

        return (v_segment_area, h_segment_area)

    def generate_optimizations(self, max_flyaways: int):
        """Generate optimizations for each card

        """

        # ------------------- define cards from strands ------------------- #
        clump_labels_filename = 'clumps_strand_labels.npy'
        max_main_clump_filename = 'max_main_clump.npy'

        labels = self.metadata.load(clump_labels_filename, allow_pickle=True)
        max_main_clump = self.metadata.load(max_main_clump_filename)

        clusters = np.unique(labels[(labels >= 0) & (labels <= (max_main_clump + max_flyaways))])

        # ------------------- align cards ------------------- #
        self._logger.info('Cards alignment from hair-strands...')
        self.optimizations: list[CardOptimizer] = []
        for cluster_id in log_progress(clusters, 'Fitting cards to strand groups'):
            opt = self._align_card(cluster_id, labels)
            self.optimizations.append(opt)

    def run(self):
        """Run optimization

        """

        self._logger.info('Optimizing cards...')

        if _INT_DEBUG:
            debugger = GroomDebugger()
            debugger.add_points(self.groom.points)

        # ------------------- card optimization ------------------- #
        self._logger.info('Cards generation...')
        cards_meta = []
        labels = np.full(self.groom.points.shape[0], -1, dtype=np.int32)
        
        phys_grp_max_err = 0.0
        phys_grp_total_err = 0.0
        card_phys_grps: list[int] = []

        cid_abs = 0
        for opt in log_progress(self.optimizations, 'Generating card geometry'):
            for cid, curve_ids in enumerate(opt.curve_ids):
                min_success, card = opt.optimize(cid)
                self._save_single_card(card, cid_abs)

                if not min_success:
                    self._logger.warning("Card {} minimization failed".format(cid_abs))

                if _INT_DEBUG:
                    debugger.add_haircard(card)

                for curve_id in curve_ids:
                    labels[curve_id] = cid_abs
                cid_abs += 1

                # Majority strand vote for phyics group per-card
                uq_phys, uq_counts = np.unique(self.groom._phys_group_id[curve_ids], return_counts=True)
                majority_idx = np.argmax(uq_counts)
                card_phys_grps.append(uq_phys[majority_idx])
                # TODO: Probably remove this if we start weighting clusting by physics groups
                phys_err = (np.sum(uq_counts) - uq_counts[majority_idx]) / np.sum(uq_counts)
                phys_grp_max_err = max(phys_grp_max_err, phys_err)
                phys_grp_total_err = phys_grp_total_err + phys_err

                # metadata
                cards_meta.append(CardMeta(card.length, card.width, card.width_lr_ratio,
                                           card.top_vec, card.texture_group))

        # save metadata
        self._logger.info('Save card information...')
        self.metadata.save('config_cards_info.npy', cards_meta)
        self.metadata.save('cards_strand_labels.npy', labels)
        self.metadata.save('cards_physgrp_labels.npy', np.array(card_phys_grps, dtype=np.uint8))

        # self._logger.info(f'Physics group assignment error: max={100*phys_grp_max_err:.2f}%, avg={100*phys_grp_total_err/cid_abs:.2f}%')

        if _INT_DEBUG:
            debugger.view()

        self._logger.info('Done.')
