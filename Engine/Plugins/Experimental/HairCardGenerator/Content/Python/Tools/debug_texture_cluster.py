# -*- coding: utf-8 -*-
"""
Debug Rasterize Cards

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import argparse

import numpy as np
from Modules.Texture.data.helper import CardData, OptData

from utils.data import Groom
from utils import Metadata

from Modules.Quantization.quantize import Quantization

from debug_helpers import load_settings

from logger.progress_iterator import ScopedProgressTracker


def debug_texture_cluster(groom_name: str, output_name: str, root_path: str):
    metadata_path = os.path.join(root_path, 'Metadata')
    if output_name is None:
        output_name = groom_name + '_LOD0'

    settings = load_settings(output_name, metadata_path)
    if ( settings is None ):
        print('Unable to load settings')
        return
    
    cards_path = os.path.join(root_path, 'Card')
    output_path = os.path.join(root_path, 'Output', output_name)
    
    groom_path = os.path.join(root_path, 'CachedGrooms')
    groom_file = os.path.join(groom_path, groom_name + '.cached')
    if ( not os.path.exists(groom_file) ):
        print('Cached groom does not exist, rerun hair card generation with groom caching on')
        return
    
    groom = Groom.load(groom_file)
    with ScopedProgressTracker(1) as progress_tracker:
        for gid,group_settings in enumerate(settings.Groups):
            group_output_name = f'{output_name}_GIDX{gid}'
            group_metadata = Metadata.open(metadata_path, group_output_name)
            num_quant_textures = group_settings.NumberOfTexturesInAtlas

            model_path = os.path.abspath(os.path.join(os.path.dirname(
                os.path.abspath(__file__)), os.pardir, 'torch_models', 'texture_features.pt'))

            quantize = Quantization(name=group_output_name,
                                metadata=group_metadata,
                                obj_path=cards_path,
                                num_points_per_curve=groom.num_points_per_curve,
                                num_workers=0,
                                groom=groom)
            quantize.run(num_quant_textures, model_path)


if __name__ == '__main__':
    parser = argparse.ArgumentParser('debug_texture_cluster', description='Cluster card textures into most appropriate groups')
    parser.add_argument('name', help='Name of hair card asset to rasterize')
    parser.add_argument('haircard_path', help='Path to base directory for hair card files (e.g. <ProjectDir>/Intermediate/GroomHairCardGen/)')
    parser.add_argument('--basename', default=None, help='Generated name (default: <GroomName>_LOD0)')
    args = parser.parse_args()
    debug_texture_cluster(args.name, args.basename, args.haircard_path)
