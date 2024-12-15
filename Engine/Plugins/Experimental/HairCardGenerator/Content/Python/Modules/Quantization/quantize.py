# -*- coding: utf-8 -*-
"""
Cards quantization class

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import torch
import numpy as np
import math
from sklearn.cluster import MiniBatchKMeans

from base import BaseOptimizer
from utils.config import parameters
from Modules.Texture.data import CardsDataset
from Modules.Texture.data import CardsDataLoader
from utils.data import Groom
from utils import Metadata

from logger.progress_iterator import log_progress


class Quantization(BaseOptimizer):
    """Cards Quantization VGG class. It quantizes card textures using VGG features"""

    def __init__(self,
                 name: str,
                 metadata: Metadata,
                 obj_path: str,
                 num_points_per_curve: int,
                 num_workers: int,
                 groom: Groom):
        """Init

        Args:
            metadata (str): path to dir containing metadata
        """

        super().__init__(name)

        self._metadata = metadata
        self._norm_size = parameters.quantization.norm_size
        self._strand_len = num_points_per_curve

        # number of bins for histograms
        self._bins = 32
        self._bin_ind = torch.arange(0, self._bins)

        # ------------------- dataset ------------------- #

        self._logger.info('Generating data loader...')

        self._dataset = CardsDataset(groom,
                                     self._name,
                                     obj_path,
                                     metadata)

        self._data_loader = CardsDataLoader(self._dataset,
                                            parameters.quantization.batch_size,
                                            shuffle=False,
                                            num_workers=num_workers)

        # ------------------- card sizes ------------------- #

        self._logger.info('Loading cards metadata...')
        data = metadata.load('config_cards_info.npy', allow_pickle=True)
        self._card_sizes = torch.Tensor([[d.length, d.width] for d in data])
        self._txt_groups = np.array([d.texture_group for d in data], dtype=np.integer)

    @staticmethod
    def get_n_gpus() -> int:
        """Get number of GPUs

        Returns:
            int: number of GPUs
        """

        return torch.cuda.device_count()

    def has_gpu(self) -> bool:
        """Check if machine has GPU

        Returns:
            bool: True if GPU is available
        """

        return self.get_n_gpus() > 0

    def _save_results(self, feat: np.array, labels: np.array, centers: np.array,
                      feat_raw: np.array, feat_flipped: np.array) -> None:
        """Save results of the quantization step

        Args:
            feat (np.array): features
            labels (np.array): labels
            centers (np.array): feat of centroids
            feat_raw (np.array): raw features (without normalizing, weighting
                                 or adding card dimensions)
            feat_flipped (np.array): raw features of the flipped textures
        """

        # identify the discrete centroid by approximation of its features
        # the element with the smallest distance to its centroid, becomes
        # the centroid representative
        labels_id = np.unique(labels)
        card_id = np.arange(0, len(labels))
        quant_map = dict()

        for i in range(len(labels_id)):
            label_id = labels_id[i]
            ids = card_id[labels == label_id]
            k_feat = feat[labels == label_id]

            id_center = np.argmin(np.linalg.norm(k_feat - centers[i, None], axis=1))

            error_raw = np.linalg.norm(feat_raw[ids] - feat_raw[ids[id_center], None], axis = -1)
            error_flipped = np.linalg.norm(feat_raw[ids] - feat_flipped[ids[id_center], None], axis = -1)

            flipped = error_raw > error_flipped

            quant_map[i] = {'center': int(ids[id_center]), 'cards': ids.tolist(), 'flipped': flipped.tolist()}

        self._metadata.save('quantization_mapping.npy', quant_map)

    def _get_histogram(self, x: torch.Tensor, mask: torch.Tensor,
                       min: float, max: float) -> torch.Tensor:
        """Get histogram.

        Takes a property and generates the boolean histogram

        Args:
            x (torch.Tensor): feature tensor
            mask (torch.tensor): mask to select elements in _bins
            min (float): value of the first bin
            max (float): value of the last bin

        Returns:
            tensor of the boolean histogram
        """

        t = torch.linspace(min, max, self._bins+1)
        x_masked_avg = torch.sum(torch.where(mask, x[:, :, None], torch.FloatTensor([0])), axis=1) /\
            mask.float().sum(axis=1)
        x_mask = (x_masked_avg[:, :, None] > t[None, self._bin_ind]) &\
            (x_masked_avg[:, :, None] < t[None, self._bin_ind+1])

        return x_mask.any(axis=0)

    def _generate_histograms(self) -> torch.Tensor:
        """Generate histograms to create features.

        Generates boolean histogram of properties to create features for clustering:

        Returns:
            tensor of histograms
        """

        self._logger.info('Generating features...')

        histograms = torch.empty((len(self._dataset), 2, self._bins, self._bins))
        density = torch.empty((len(self._dataset), 2))

        # smoothing function
        hamming = torch.hamming_window(31)
        conv = torch.nn.Conv1d(1, 1, hamming.size(0), padding=hamming.size(0)//2,
                               padding_mode='replicate', bias=False)
        conv.weight.data = (hamming / hamming.sum())[None, None]

        num_iters = math.ceil(len(self._dataset) / parameters.quantization.batch_size)
        for _, opt_data in log_progress(self._data_loader,
                                            task_desc='Generating Texture Features',
                                            num_iters=num_iters):
            # BUG: Contrary to documentation (torch v2.4.0) .unique doesn't seem to work properly for large lists unless dim is specified
            # unique_cid = torch.unique(opt_data.cid, dim=0)
            # NOTE: Sped up using unique_consecutive since the card ids are always consecutive from the loader
            unique_cid = torch.unique_consecutive(opt_data.cid)

            # smooth point coordinates to allow for generating 1st and 2nd derivatives
            points_all = opt_data.b_uv.view([-1, self._strand_len, 2])
            points_all[:, :, 0] = conv(points_all[:, None, :, 0]).view([-1, self._strand_len])
            points_all[:, :, 1] = conv(points_all[:, None, :, 1]).view([-1, self._strand_len])

            for card_id in unique_cid:
                card_sid = torch.nonzero(opt_data.cid == card_id).flatten()
                points = points_all.view([-1, 2])[card_sid].view([-1, self._strand_len, 2])
                widths = opt_data.widths[card_sid].view([-1, self._strand_len, 1])

                density[card_id, 0] = widths.size(0)
                density[card_id, 1] = widths[:, 0].mean()

                # convert to square space
                size = self._card_sizes[card_id]
                points[:, :, 0] /= size[1]
                points[:, :, 1] /= size[0]

                t_bins = torch.linspace(0, 1, self._bins+1)
                mask = (points[:, :, 1, None] > t_bins[None, self._bin_ind]) &\
                    (points[:, :, 1, None] < t_bins[None, self._bin_ind+1])

                pos = points[:, :, 0]
                deriv = (pos[:, 1:] - pos[:, :-1]) /\
                    (points[:, 1:, 1] - points[:, :-1, 1])

                hist_pos = self._get_histogram(pos, mask, 0, 1)
                hist_deriv = self._get_histogram(deriv, mask[:, :-1], -5, 5)

                histograms[card_id] = torch.stack((hist_pos, hist_deriv))

        return histograms, density

    def save_histograms(self, path: str) -> None:
        """Save histograms.

        Saves the histograms.

        Args:
            path (str): the path in which to save the histograms
        """
        histograms, _ = self._generate_histograms()
        np.save(path, histograms)

    def run(self, target_n_textures: int,
            model_path: str = 'torch_models/texture_features.pt') -> dict:
        """Cluster textures.

        Saves a dictionary containing the mapping between centroids and cards:
            centroids and cards. i.e.
            {
                '0': {'centroid': CARD_ID_C, 'cards': [27, 1, 39, ...]},
                ...
                '63': {'centroid': CARD_ID_C, 'cards': [7, 2, 9, ...]}
            }
        """
        histograms, density = self._generate_histograms()
        assert len(histograms) == len(self._dataset)

        model = torch.jit.load(model_path)
        model.eval()
        features = model(histograms)
        features_flipped = model(histograms.flip(3))

        # combine features with card sizes
        combined_feat = torch.cat((self._card_sizes, density, features), axis=1)
        np_feat = combined_feat.detach().numpy()

        # make the artificial features go between 0 and a value depending on the difference
        # between the ratio between maximum and minimum, and then apply a multiplier
        norm_idx = np.array([0, 1, 2, 3])

        np_feat[:, norm_idx] = (np_feat[:, norm_idx] - np.min(np_feat[:, norm_idx], axis=0)) /\
            (1e-10 + np.max(np_feat[:, norm_idx], axis=0))

        np_feat[:, [0, 1]] *= 0.4 * len(features)
        np_feat[:, 2] *= 0.2 * len(features)
        np_feat[:, 3] *= 0.2 * len(features)

        # cluster features based on target num of cards
        self._logger.info('Clustering features...')
        random_seed_name = 'random_seed.npy'
        random_seed = self._metadata.load(random_seed_name, allow_pickle=True)[0]

        # hard coded number of flyaway textures, probably needs to be in a config file
        # but not exposed in the UI, same as the max number of flyaways
        flyaway_textures = 3
        target_n_textures_list = [target_n_textures - flyaway_textures, flyaway_textures]

        labels = np.empty((np_feat.shape[0]))
        cluster_centers = np.empty((0))

        for txt_group in np.unique(self._txt_groups):
            card_ids = np.argwhere(self._txt_groups == txt_group).flatten()

            if target_n_textures_list[txt_group] < card_ids.size:
                kmeans = MiniBatchKMeans(n_clusters=target_n_textures_list[txt_group],
                                         random_state=random_seed,
                                         n_init=5,
                                         max_iter=300,
                                         init_size=len(np_feat[card_ids]),
                                         verbose=0).fit(np_feat[card_ids])

                labels[card_ids] = kmeans.labels_ + cluster_centers.shape[0]
                cluster_centers = np.append(cluster_centers, kmeans.cluster_centers_)
            else:
                labels[card_ids] = np.arange(card_ids.size) + cluster_centers.shape[0]
                cluster_centers = np.append(cluster_centers, card_ids)

        self._logger.info('Exporting results...')
        self._save_results(np_feat, labels, cluster_centers, features.detach().numpy(), features_flipped.detach().numpy())
        self._logger.info('Done.')
