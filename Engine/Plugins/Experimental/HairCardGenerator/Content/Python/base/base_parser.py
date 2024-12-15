# -*- coding: utf-8 -*-
"""
Base Parser

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from abc import abstractmethod
import argparse


class BaseParser:
    """Base parser"""

    def __init__(self, description):
        """Initialization"""
        super().__init__()

        self._parser = argparse.ArgumentParser(description=description)

        self.set_arguments()

    @abstractmethod
    def set_arguments(self):
        """Add parser arguments

        E.g.
        self._parser.add_argument(
            '-i',
            '--input_dir',
            type=str,
            help='dir path with files')

        """
        raise NotImplementedError()

    def get_arguments(self):
        """Get arguments"""
        return self._parser.parse_args()
