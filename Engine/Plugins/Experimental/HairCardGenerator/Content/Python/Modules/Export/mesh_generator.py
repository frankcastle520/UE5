# -*- coding: utf-8 -*-
"""
MeshGenerator

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import numpy as np

from base import FrameworkClass
from base import TxtAtlas
from utils import geometry
from utils.io.obj_reader import ObjReader
from logger.progress_iterator import log_progress


class MeshGenerator(FrameworkClass):
    """Mesh generator"""

    def __init__(self,
                 obj_path: str,
                 group_data: list[dict],
                 phys_group: int,
                 name: str,
                 atlas_size: int,
                 atlas_manager: TxtAtlas,
                 from_opt_objs: bool = False) -> None:
        """Init

        Args:
            obj_path (str): path to dir containing objs
            group data (dict): group parameters
            name (str): name of the asset
            atlas_size (int): atlas size
            atlas_manager (TxtAtlas): atlas manager
            from_opt_objs (bool, optional): if True use information from post-quantization optimization.
                                            Defaults to False.
        """

        super().__init__()

        for group in group_data:
            assert 'name' in group, "no group name provided"

        self._obj_path = obj_path
        self._name = name
        self._atlas_size = atlas_size
        self._atlas_manager = atlas_manager
        self._opt_objs = from_opt_objs
        self._output_name = name
        self._group_data = group_data
        self._phys_group = phys_group


    def extract_data_from_cards(self, R: np.ndarray):
        """Generate mesh data vertices, triangles, normals, uv_coordinates, groups for export

        Args:
            R (np.array): transformation matrix to apply to the cards
        """

        # mesh data
        groups = []
        vertices = []
        triangles = []
        uv_coordinates = []

        abs_kid = 0
        max_vid = 0

        for gid,group in log_progress(enumerate(self._group_data), 'Generating mesh'):
            if self._opt_objs:
                obj_path = os.path.join(self._obj_path, group['name'], 'opt_objs')
            else:
                obj_path = os.path.join(self._obj_path, group['name'])
        
            quantization = group['metadata'].load('quantization_mapping.npy', allow_pickle=True).item()
            phys_group_ids = group['metadata'].load('cards_physgrp_labels.npy')

            for cluster_data in quantization.values():
                # ------------------- place in atlas ------------------- #

                center_id = cluster_data['center']
                s = self._atlas_manager.get_texture_size(gid,center_id)
                r, c = self._atlas_manager.get_texture_coordinate(gid,center_id)

                # load cards
                cards_in_k = cluster_data['cards']
                flipped = cluster_data['flipped']
                for i, card_id in enumerate(cards_in_k):
                    # Ignore card physics groups if set to output all groups
                    if self._phys_group >= 0:
                        # Skip cards not in the current output physics group
                        if card_id >= len(phys_group_ids) or self._phys_group != phys_group_ids[card_id]:
                            continue

                    file_path = os.path.join(obj_path, '{:06d}.obj'.format(card_id))

                    # Ignore cards that don't exist. It can happen when reducing from a previous LOD
                    # that has a higher number of max flyaways (extra flyaway cards won't exist)
                    if not os.path.isfile(file_path):
                        continue

                    verts, faces, aux = ObjReader.load(file_path)

                    # convert Tensors to numpy arrays
                    verts = verts.numpy()
                    faces = faces.verts_idx.numpy()

                    # copy the uv coordinates from the centroid
                    card_uvs = aux.verts_uvs.numpy()
                    atlas_uvs_orig = np.empty_like(card_uvs)
                    atlas_uvs_orig[:, 0] = (card_uvs[:, 0] / card_uvs[-1, 0] * s[1] + c) / self._atlas_size
                    atlas_uvs_orig[:, 1] = (card_uvs[:, 1] / card_uvs[-1, 1] * s[0] + r) / self._atlas_size

                    if flipped[i]:
                        atlas_uvs = np.empty_like(atlas_uvs_orig)
                        atlas_uvs[::2] = atlas_uvs_orig[1::2]
                        atlas_uvs[1::2] = atlas_uvs_orig[::2]
                    else:
                        atlas_uvs = atlas_uvs_orig

                    vertices.append(verts)
                    triangles.append(faces + max_vid)
                    max_vid += verts.shape[0]

                    uv_coordinates.append(atlas_uvs)
                    groups.append(np.full((verts.shape[0],1), card_id, dtype=np.uint32))

                abs_kid += 1

        # ------------------- generate mesh -------------------

        vertices = np.concatenate(vertices, axis=0)
        triangles = np.concatenate(triangles, axis=0)
        uvs = np.concatenate(uv_coordinates, axis=0)
        groups = np.concatenate(groups, axis=0)

        # ------------------- transform -------------------

        rotated_verts = np.dot(R, vertices.T).T

        # get vertex normals
        vertex_normals = geometry.compute_vertex_normals(rotated_verts, triangles)

        return rotated_verts, triangles, vertex_normals, uvs, groups
