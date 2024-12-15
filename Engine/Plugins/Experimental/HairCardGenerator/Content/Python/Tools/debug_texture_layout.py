# -*- coding: utf-8 -*-
"""
Debug Texture Layout

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import argparse

import numpy as np

from utils.data import Groom
from utils import Metadata
from Modules.Texture import TextureCompressionOptimizer
from Modules.Export import VariableTxtAtlas

from debug_helpers import load_settings

from logger.progress_iterator import ScopedProgressTracker


def debug_texture_layout(groom_name: str, output_name: str, root_path: str):
    metadata_path = os.path.join(root_path, 'Metadata')
    if output_name is None:
        output_name = groom_name + '_LOD0'

    settings = load_settings(output_name, metadata_path)
    if ( settings is None ):
        print('Unable to load settings')
        return
    
    cards_path = os.path.join(root_path, 'Card')
    
    groom_path = os.path.join(root_path, 'CachedGrooms')
    groom_file = os.path.join(groom_path, groom_name + '.cached')
    if ( not os.path.exists(groom_file) ):
        print('Cached groom does not exist, rerun hair card generation with groom caching on')
        return
    
    groom = Groom.load(groom_file)
    with ScopedProgressTracker(2) as progress_tracker:
        card_sizes = np.empty((0, 2), dtype=np.float64)
        compression_factors = np.empty((0), dtype=np.float64)
        gcidslist: list[np.ndarray] = []
        for gid,group_settings in enumerate(settings.Groups):
            group_output_name = f'{output_name}_GIDX{gid}'
            group_metadata = Metadata.open(metadata_path, group_output_name)
            quantization_filename = 'quantization_mapping.npy'
            quantization = group_metadata.load(quantization_filename, allow_pickle=True).item()
            id_centers = [k['center'] for k in quantization.values()]

            cardsmeta_filename = 'config_cards_info.npy'
            config_cards = group_metadata.load(cardsmeta_filename, allow_pickle=True)

            group_card_sizes = np.array([[c.length, c.width] for c in config_cards[id_centers]])
            card_sizes = np.append(card_sizes, group_card_sizes, axis=0)

            # Build per-card compression factors (if compression_factor < 0)
            if group_settings.UseOptimizedCompressionFactor:
                compressor = TextureCompressionOptimizer(asset_name=group_output_name,
                                                        groom=groom,
                                                        id_centers=id_centers,
                                                        metadata=group_metadata,
                                                        obj_path=cards_path)
                group_compression_factors = compressor.get_compression_factors()
            else:
                group_compression_factors = np.full((len(group_card_sizes)), 1.0)

            compression_factors = np.append(compression_factors, group_compression_factors)
            gcids = np.stack((np.full(len(id_centers), gid), np.array(id_centers)), axis=-1)
            gcidslist.append(gcids)

        next(progress_tracker)
        # HACK: Place the file in settings group 0 folder to keep the metadata together
        g0_name = f'{output_name}_GIDX0'
        g0_metadata = Metadata.open(metadata_path, g0_name)

        card_gcids = np.concatenate(gcidslist, axis=0)
        quant_atlas_manager = VariableTxtAtlas(g0_metadata, settings.AtlasSize, 0.3)
        quant_atlas_manager.generate_coordinates(card_gcids, card_sizes, compression_factors)

        layout_filename = 'texture_layout.npz'
        quant_atlas_manager.save_to_file(layout_filename)


if __name__ == '__main__':
    parser = argparse.ArgumentParser('debug_texture_layout', description='Run texure atlas layout engine')
    parser.add_argument('name', help='Name of hair card asset to layout')
    parser.add_argument('haircard_path', help='Path to base directory for hair card files (e.g. <ProjectDir>/Intermediate/GroomHairCardGen/)')
    parser.add_argument('--basename', default=None, help='Generated name (default: <GroomName>_LOD0)')
    args = parser.parse_args()
    debug_texture_layout(args.name, args.basename, args.haircard_path)

