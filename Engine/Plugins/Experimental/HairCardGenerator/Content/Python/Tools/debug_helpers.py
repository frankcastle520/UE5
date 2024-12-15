# -*- coding: utf-8 -*-
"""
Debug Texture Layout

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os

import json
from addict import Dict


def load_settings(name: str, metadata_path: str) -> Dict:
    # TODO: Better load/parse of all settings (possibly unified class for settings from UE)
    atlas_map = {'AtlasSize256':256,
                 'AtlasSize512':512,
                 'AtlasSize1024':1024,
                 'AtlasSize2048':2048,
                 'AtlasSize4096':4096,
                 'AtlasSize8192':8192}
    config_file = os.path.join(metadata_path, name + '.json')
    with open(config_file, 'rt') as f:
        settings = Dict(json.load(f))
        settings.AtlasSize = atlas_map[settings.AtlasSize]
        return settings
    
