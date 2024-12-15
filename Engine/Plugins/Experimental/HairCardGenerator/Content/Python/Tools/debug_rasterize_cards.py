# -*- coding: utf-8 -*-
"""
Debug Rasterize Cards

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import argparse

import numpy as np
from Modules.Texture.data.helper import CardData, OptData
from base.base_txt_atlas import TxtAtlas

from utils.data import Groom
from utils import Metadata
from Modules.Texture import TextureGenerator
from Modules.Export import VariableTxtAtlas

from debug_helpers import load_settings

from logger.progress_iterator import ScopedProgressTracker


class DebugTextureGenerator(TextureGenerator):
    """ Texture generator that hooks into renderdoc if available for card rasterization
    """
    def __init__(self,
                 name: str,
                 groom: Groom,
                 obj_path: str,
                 output_path: str,
                 num_points_per_curve: int,
                 atlas_manager: TxtAtlas,
                 group_data: dict,
                 channel_layout: int = 0,
                 atlas_size: int = 4096,
                 batch_size: int = 25,
                 random_seed: int = 0,
                 depth_min_range: float = None,
                 depth_max_range: float = None,
                 debug: bool = False,
                 num_workers: int = 25) -> None:
        super().__init__(name, groom, obj_path, output_path, num_points_per_curve, atlas_manager, group_data, channel_layout, atlas_size, batch_size, random_seed, depth_min_range, depth_max_range, debug, num_workers)

    def debug_load_renderdoc_api(self, renderdoc_api):
        self._rdoc_api = renderdoc_api

    def _debug_renderdoc_startframe(self, frame_title: str = None):
        if self._rdoc_api is not None:
            self._rdoc_api.start_capture(frame_title)

    def _debug_renderdoc_endframe(self):
        if self._rdoc_api is not None:
            self._rdoc_api.end_capture()

    def rasterize_card(self, gid: int, card_id: int, cards: CardData, opt_data: OptData):
        self._debug_renderdoc_startframe(f'Tx_{gid}_{card_id}')

        super().rasterize_card(gid, card_id, cards, opt_data)

        self._debug_renderdoc_endframe()



def debug_init_renderdoc():
    # TODO: Internal see https://github.ol.epicgames.net/mark-winter/PyRenderdoc to download the renderdoc wrapper
    try:
        import PyRenderdoc
        PyRenderdoc.init('C:/Program Files/RenderDoc/')
        return PyRenderdoc
    except ImportError:
        pass
    return None


def debug_rasterize_cards(groom_name: str, output_name: str, root_path: str):
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
        group_data = []
        for gid,group_settings in enumerate(settings.Groups):
            group_output_name = f'{output_name}_GIDX{gid}'
            group_metadata = Metadata.open(metadata_path, group_output_name)
            group_data.append({})
            group_data[-1]['name'] = group_output_name
            group_data[-1]['strand_width_scale'] = group_settings.StrandWidthScalingFactor

            quantization_filename = 'quantization_mapping.npy'
            quantization = group_metadata.load(quantization_filename, allow_pickle=True).item()
            group_data[-1]['dataset_card_filter'] = [k['center'] for k in quantization.values()]
            group_data[-1]['metadata'] = group_metadata

        layout_filename = 'texture_layout.npz'
        g0_name = f'{output_name}_GIDX0'
        g0_metadata = Metadata.open(metadata_path, g0_name)
        quant_atlas_manager = VariableTxtAtlas(g0_metadata, settings.AtlasSize, 0.0)
        quant_atlas_manager.load_from_file(layout_filename)

        # NOTE: RenderdocUI must be attached before the GL context is initialized
        PyRenderdoc = debug_init_renderdoc()

        batch_size = 25
        quant_texture_generator = DebugTextureGenerator(
                name=output_name,
                groom=groom,
                obj_path=cards_path,
                output_path=output_path,
                num_points_per_curve=groom.num_points_per_curve,
                atlas_manager=quant_atlas_manager,
                group_data=group_data,
                atlas_size=settings.AtlasSize,
                batch_size=batch_size,
                depth_min_range=settings.DepthMinimum,
                depth_max_range=settings.DepthMaximum,
                # NOTE: subprocesses currently unsupported so num_workers must be 0
                num_workers=0)
        
        if PyRenderdoc is not None:
            quant_texture_generator.debug_load_renderdoc_api(PyRenderdoc)

        quant_texture_generator.generate()
        quant_texture_generator.dilate_atlas(dilation=16)
        quant_texture_generator.save_atlas()



if __name__ == '__main__':
    parser = argparse.ArgumentParser('debug_rasterize_cards', description='Run texure atlas rasterization with renderdoc attached')
    parser.add_argument('name', help='Name of hair card asset to rasterize')
    parser.add_argument('haircard_path', help='Path to base directory for hair card files (e.g. <ProjectDir>/Intermediate/GroomHairCardGen/)')
    parser.add_argument('--basename', default=None, help='Generated name (default: <GroomName>_LOD0)')
    args = parser.parse_args()
    debug_rasterize_cards(args.name, args.basename, args.haircard_path)
