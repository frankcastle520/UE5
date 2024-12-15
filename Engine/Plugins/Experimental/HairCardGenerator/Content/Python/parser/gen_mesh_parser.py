# -*- coding: utf-8 -*-
"""
Generate mesh parser

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from base import BaseParser


class MeshParser(BaseParser):
    """Mesh parser"""

    def set_arguments(self):
        """Add parser arguments"""

        self._parser.add_argument('-i',
                                  '--input_dir',
                                  type=str,
                                  help='dir path with files')

        self._parser.add_argument('--name', type=str, help='asset name')

        self._parser.add_argument('-o',
                                  '--output_file',
                                  type=str,
                                  help='output file path')
