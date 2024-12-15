# -*- coding: utf-8 -*-
"""
Argument parser

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import argparse
from os.path import abspath, join, exists, dirname
import yaml
from addict import Dict
from .template import FrameworkClass

_CURR_DIR = dirname(abspath(__file__))
ROOT_DIR = join(_CURR_DIR, '../')


class ConfigParser(FrameworkClass):
    """Card parser"""

    def __init__(self, description):
        """Initialization"""
        super().__init__()

        self.parser = argparse.ArgumentParser(description=description)

        self.add_input_path()

    def add_input_path(self):
        """Add config file"""

        self.parser.add_argument('config_file_path', type=str, help='input config file path')

    # ------------------------------------------------------
    # ------------------- Get Arguments --------------------
    # ------------------------------------------------------

    def get_arguments(self):
        """Get arguments"""

        args = vars(self.parser.parse_args())
        config_path = args['config_file_path']

        # ------------------- check file -------------------

        if not exists(config_path):
            self._logger.error('Config file at {} does not exist!'.format(config_path))
            raise FileExistsError()

        if 'yml' not in config_path:
            self._logger.error('Config file {} has wrong format!'.format(config_path))
            raise RuntimeError()

        # ------------------- load param -------------------

        # parameters need to be retrieved from config file
        with open(config_path) as fin:
            config_data = Dict(yaml.safe_load(fin))

        # ------------------- versioning -------------------

        # update content based on version
        if config_data.meta.version < 4.4:
            self._logger.error('Config file version is outdated. Please update.')
            raise RuntimeError()

        # ------------------- placeholders -------------------

        # replace placeholders
        config_data = config_data.args
        for k, v in config_data.items():
            if isinstance(v, str):
                if '${workspaceFolder}' in v:
                    rel_path = v.replace('${workspaceFolder}/', '')
                    config_data[k] = abspath(join(ROOT_DIR, rel_path))

        return config_data
