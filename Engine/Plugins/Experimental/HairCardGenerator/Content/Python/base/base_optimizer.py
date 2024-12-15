# -*- coding: utf-8 -*-
"""
Base Optimizer

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

__version__ = "0.4.0"

from base.template import FrameworkClass


class BaseOptimizer(FrameworkClass):
    """Base optimizer"""

    def __init__(self, name: str, debug: bool = False) -> None:
        """Init

        Args:
            name (str): name
            debug (bool, optional): debug. Defaults to False.
        """

        super().__init__()

        self._name = name
        self._debug = debug

    @property
    def name(self):
        """Get name"""
        return self._name

    @name.setter
    def name(self, name):
        self.__name = name
