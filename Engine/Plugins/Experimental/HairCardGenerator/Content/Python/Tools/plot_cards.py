# -*- coding: utf-8 -*-
"""
Card plotter

@author: Erica Alcusa Saez'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import numpy as np
import os, sys
import matplotlib as mpl
import matplotlib.pyplot as plt
import matplotlib.cm as cm
from matplotlib import gridspec

from utils.data import Groom
from utils import Metadata
from utils import geometry
from Modules.Texture.data import CardsDataset, CardsDataLoader
from Modules.Geometry.optim import GeometryOptimizer
from Modules.Geometry.optim.card_optimizer import CardOptimizer

def main(base_folder, asset_name, cached_groom_name):
    obj_path = os.path.join(base_folder, 'Card')
    metadata_path = os.path.join(base_folder, 'Metadata')
    groom_path = os.path.join(base_folder, 'CachedGrooms', cached_groom_name)

    groom = Groom.load(groom_path)

    metadata = Metadata.open(metadata_path, asset_name)
    quantization = metadata.load('quantization_mapping.npy', allow_pickle=True).item()
    id_centers = [k['center'] for k in quantization.values()]

    dataset = CardsDataset(groom,
                           asset_name,
                           obj_path,
                           metadata,
                           dataset_card_filter=id_centers)

    k_name = 'cards_strand_labels.npy'
    labels = metadata.load(k_name, allow_pickle=True)

    data_loader = CardsDataLoader(dataset)

    for cid, (cards, opt_data) in enumerate(data_loader):
        curve_ids = np.where(labels == id_centers[cid])[0]
        curves = []
        for curve_id in curve_ids:
            c = groom.get_curve(curve_id)
            curves.append(c)

        card_opt = CardOptimizer(name=asset_name,
                                 curves=curves,
                                 curve_ids=curve_ids,
                                 use_multicard_clumps=False,
                                 groom_center_pos=groom.center,
                                 all_root_points=groom.root_points)

        mean_curve = card_opt.avg_curve.control_points

        # get the coordinates of the projected points in 3D space
        ind = 2*opt_data.ind.flatten()
        ug = (opt_data.b_uv[:, 0] / cards.uvs[ind+1, 0])[:, None]
        vg = ((opt_data.b_uv[:, 1] - cards.uvs[ind, 1]) / (cards.uvs[ind+2, 1] - cards.uvs[ind, 1]))[:, None]
        projected_points = (1-ug) * (1-vg) * cards.verts[ind] + ug * (1-vg) * cards.verts[ind+1] + (1-ug) * vg * cards.verts[ind+2] + ug * vg * cards.verts[ind+3]
        projected_points = projected_points.view(-1, groom.num_points_per_curve, 3).numpy()

        # get depth and color for colormap
        depth = cards.face_normals[opt_data.ind, 0] * opt_data.pts[:, 0] +\
                cards.face_normals[opt_data.ind, 1] * opt_data.pts[:, 1] +\
                cards.face_normals[opt_data.ind, 2] * opt_data.pts[:, 2] +\
                cards.face_normals[opt_data.ind, 3]
        depth =  depth.view(-1, groom.num_points_per_curve).numpy()

        norm = mpl.colors.Normalize(vmin=depth.min(), vmax=depth.max())
        m = cm.ScalarMappable(norm=norm, cmap=cm.winter)
        depth_norm = m.to_rgba(depth)

        # get compression_factors
        compression_factors = metadata.load('compression_factors.npy')

        # get angles between subdivisions
        vertices = cards.verts.numpy()
        vectors = vertices[2::2] - vertices[:-2:2]
        vectors /= np.linalg.norm(vectors, axis=1)[:, None]

        angles = np.arccos(np.sum(vectors[1:] * vectors[:-1], axis=1))

        if np.any(angles > 0.0 * np.pi):
            fig = plt.figure()

            spec = gridspec.GridSpec(ncols=2, nrows=1,
                                     width_ratios=[5, 1])

            ax = fig.add_subplot(spec[1])

            # plot depth colormap in another figure
            b_uv = opt_data.b_uv.view(-1, groom.num_points_per_curve, 2).numpy()
            for i, strand in enumerate(b_uv):
                ax.scatter(strand[:, 0], -strand[:, 1] * compression_factors[cid], c=depth_norm[i])

            ax.axis('equal')

            ax = fig.add_subplot(spec[0], projection='3d')

            # plot card borders
            for i in range(1, len(vertices)//2):
                index = np.array([2*(i-1), 2*i, 2*i+1, 2*(i-1)+1])
                ax.plot(vertices[index, 0], vertices[index, 1], vertices[index, 2], lw=0.5, color='orange')

            # plot original strands
            for strand in opt_data.pts.view(-1, groom.num_points_per_curve, 3).numpy():
                ax.plot(strand[:, 0], strand[:, 1], strand[:, 2], lw=0.5, color='red')

            ax.plot(mean_curve[:, 0], mean_curve[:, 1], mean_curve[:, 2], color='blue')
            # plot projected strands
            for strand in projected_points:
                ax.plot(strand[:, 0], strand[:, 1], strand[:, 2], lw=0.1, color='yellow')

            # keep axis proportional
            world_limits = ax.get_w_lims()
            ax.set_box_aspect((world_limits[1]-world_limits[0], world_limits[3]-world_limits[2],
                               world_limits[5]-world_limits[4]))

            plt.show()

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print('Need base folder path, run name and cached groom name')
    else:
        base_folder = sys.argv[1]
        asset_name = sys.argv[2]
        cached_groom_name = sys.argv[3]

        main(base_folder, asset_name, cached_groom_name)
