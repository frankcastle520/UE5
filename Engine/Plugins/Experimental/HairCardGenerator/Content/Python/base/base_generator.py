# -*- coding: utf-8 -*-
"""
Base Generator

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

__version__ = "0.4.0"
__all__ = ['BaseGenerator']

from abc import abstractmethod
from .template import FrameworkClass


class BaseGenerator(FrameworkClass):
    """Base optimizer"""

    def __init__(self, output_path: str, name: str, debug: bool = False):
        """Init

        Args:
            output_path (str): path where files are saved
            name (str): asset name
            debug (bool, optional): debug. Defaults to False.
        """

        super().__init__()
        self._output_path = output_path
        self._name = name
        self._debug = debug

    @abstractmethod
    def run(self, params: dict) -> dict:
        """Run generation"""

        raise NotImplementedError()
