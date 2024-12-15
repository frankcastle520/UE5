# -*- coding: utf-8 -*-
"""
Configurations used throughout the project

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

__all__ = ['config', 'parameters']

from os.path import abspath, join, exists
import yaml
from addict import Dict
from .io import safe_dir, ROOT_DIR


def load_config() -> dict:
    """Load configuration file

    Returns:
        dict: dictionary with project configuration information
    """

    # path with project configs
    config_path = join(ROOT_DIR, 'config/general.yml')
    if not exists(config_path):
        raise Exception('File {} does not exist!'.format(config_path))

    with open(config_path) as fin:
        config_data = Dict(yaml.safe_load(fin))

    # fix paths wrt project root dir path
    for key, val in config_data.dirs.items():
        config_data.dirs[key] = safe_dir(abspath(join(ROOT_DIR, val)))

    return config_data


def load_parameters() -> dict:
    """Load parameters from file

    Returns:
        dict: dictionary with parameters
    """

    # path with project configs
    file_path = join(ROOT_DIR, 'config/parameters.yml')
    if not exists(file_path):
        raise Exception('File {} does not exist!'.format(file_path))

    with open(file_path) as fin:
        params = Dict(yaml.safe_load(fin))

    return params


# ------------------------------------------------------
# --------------------- Load parts ---------------------

config = load_config()
parameters = load_parameters()
