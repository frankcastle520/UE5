# -*- coding: utf-8 -*-
"""
Class to identify the grouping of the strands to form cards.

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations
from typing import Union

import numpy as np
from sklearn.cluster import MiniBatchKMeans, DBSCAN
from sklearn.metrics import pairwise_distances
from threadpoolctl import threadpool_limits

from base import FrameworkClass
from utils.data import Groom
from utils import parameters
from logger.progress_iterator import log_progress


class StrandCardAllocator(FrameworkClass):
    """Hair Card allocator"""

    MIN_STRANDS_PER_CARD = parameters.geometry.min_strands_per_card

    def __init__(self, asset: Groom, strand_ids: list = None) -> None:
        """Initialize class

        Args:
            asset (Groom): groom asset
        """

        super().__init__()

        self._asset = asset

        if strand_ids is not None:
            self._strand_ids = strand_ids
        else:
            self._strand_ids = range(self._asset.points.shape[0])

    def _get_features(self) -> np.array:
        """Generate features

        Returns:
            np.array: point features, format (N_curves x N_Feat)
        """

        red_factor = 16

        target_n_points = int(self._asset.num_points_per_curve // red_factor)
        reduced_indices = np.linspace(0, self._asset.num_points_per_curve - 1, target_n_points, dtype=np.int32)

        return self._asset.points[self._strand_ids][:, reduced_indices].reshape((-1, target_n_points * 3))

    def _cleanup_labels(self, labels, num_elements):
        """Cleanup labels

        Args:
            labels (np.array): clustering labels
            num_elements (int): number of clustering elements

        Returns:
            np.array: clean clustering labels, with -1 for elements without cluster
            np.array: unique labels
        """

        unique_labels = np.unique(labels)
        if unique_labels[-1] != (len(unique_labels) - 1):
            # re-name labels to have them consecutively from 1 to N
            # these labels are sorted
            for idx in range(len(unique_labels)):
                card_strand_ids = np.where(labels == unique_labels[idx])[0]
                labels[card_strand_ids] = idx

        labels_clean = np.empty((num_elements), dtype=np.int32)
        labels_clean.fill(-1)
        labels_clean[np.arange(num_elements)] = labels

        return labels_clean, unique_labels

    def _run_kmeans(self, n_clumps, random_seed, features):
        """Run kmeans clustering

        Args:
            n_clumps (int): total number of clumps
            random_seed (int): random seed
            features (np.array): clustering features

        Returns:
            np.array: clustering labels
        """

        kmeans = MiniBatchKMeans(n_clusters=n_clumps,
                                 random_state=random_seed,
                                 n_init=5,
                                 max_iter=300,
                                 init_size=max(2, len(self._strand_ids) // 10),
                                 verbose=1,
                                 compute_labels=False).fit(features)

        # Compute labels in chunks
        chunk_size = 500
        labels = np.empty((features.shape[0]), dtype=np.integer)
        for idxstart in log_progress(range(0, features.shape[0],chunk_size), 'Computing strand labels'):
            cidxs = range(idxstart,min(idxstart+chunk_size, features.shape[0]))
            labels[cidxs] = kmeans.predict(features[cidxs,:])

        return self._cleanup_labels(labels, features.shape[0])

    def allocate(self,
                 number_clumps: int,
                 random_seed: int,
                 max_flyaways: int) -> Union[np.array, tuple]:
        """Allocate strands to clumps

        Args:
            number_clumps (int): total number of clumps
            random_seed (int): random seed for kmeans clustering
            max_flyaways (int): maximum number of flyaways

        Returns:
            np.array: strand card id they belong to
        """

        self._logger.info('Feature generation...')

        point_features = self._get_features()

        # ------------------- cluster features ------------------- #

        self._logger.info('Clustering features...')

        # Skip DBSCAN outlier detection if max_flyaways is 0
        if max_flyaways > 0:
            # Do a first DBSCAN clustering to isolate flyaways
            avg_root_dist_to_center = np.linalg.norm(self._asset.root_points - self._asset.root_points.mean(axis = 0), axis=-1).mean()
            with threadpool_limits(16, user_api='openmp'):
                labels = DBSCAN(eps = 0.06 * avg_root_dist_to_center * np.sqrt(point_features.shape[1])).fit_predict(point_features)
            mask = labels==-1
        else:
            mask = np.zeros((point_features.shape[0],), dtype=bool)

        # If there are more detected flyaways than the maximum allowed, turn the remaining ones into normal strands
        if mask.sum() > max_flyaways:
            # Get the indices of the closest no-flyaway strand to each flyaway
            ind_closest_strand = pairwise_distances(point_features[mask], point_features[np.logical_not(mask)]).argmin(axis=-1)

            flyaway_strands = self._asset.points[np.where(mask)[0]]
            closest_strands = self._asset.points[np.where(np.logical_not(mask))[0][ind_closest_strand]]

            # Get the vectors tangent to the closest strands
            closest_tan_vecs = closest_strands[:, 1:] - closest_strands[:, :-1]
            closest_tan_vecs /= np.linalg.norm(closest_tan_vecs, axis=-1, keepdims=True)

            # For each point of the closest stands, get the normal vector, defined as
            # the vector from the point to the line defined by the tangent vector and the groom center
            normal = closest_strands[:, 1:] - self._asset.center - \
                ((closest_strands[:, 1:] - self._asset.center) * closest_tan_vecs).sum(axis=-1)[:, :, None] * closest_tan_vecs
            normal /= np.linalg.norm(normal, axis=-1, keepdims=True)

            # For each point of the flyaway strands, get the indices of the closest point in the closest strand
            ind_closest_point = np.linalg.norm(flyaway_strands[:, :, None] - closest_strands[:, None, 1:], axis=-1).argmin(axis=-1)

            # For each flyaway, get the value of the maximum dot product between the vector from the flyaway point to the
            # closest point in the closest strand and the normal vector. This is an estimation of the maximum displacement
            # of the flyaway in the direction outside the groom
            ind_closest_aux = np.arange(ind_closest_point.shape[0])[:, None], ind_closest_point
            max_out_displacement = (normal[ind_closest_aux] * (flyaway_strands - closest_strands[:, 1:][ind_closest_aux]))\
                .sum(axis=-1).max(axis=-1)

            # Update the flyaway mask to exclude surplus flyaways
            mask[np.where(mask)[0][max_out_displacement.argsort()[:-max_flyaways]]] = False

        # Cluster the main strands with kmeans
        if number_clumps < np.logical_not(mask).sum():
            labels_main, _ = self._run_kmeans(number_clumps, random_seed, point_features[np.logical_not(mask)])
        else:
            labels_main = np.arange(np.logical_not(mask).sum())

        max_main_clump = labels_main.max() if len(labels_main) > 0 else -1

        labels_all = np.full(point_features.shape[0], -1, dtype=np.integer)
        labels_all[np.logical_not(mask)] = labels_main
        labels_all[mask] = np.arange(mask.sum()) + 1 + max_main_clump

        return labels_all, max_main_clump