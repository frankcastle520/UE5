# -*- coding: utf-8 -*-
"""
Framework class to be used to extend all
the other classes

@author: Denis Tome'

"""

__version__ = "0.1.0"

from logger.console_logger import ConsoleLogger


class FrameworkClass:
    """Framework Class"""

    def __init__(self):
        self._logger = ConsoleLogger(self.__class__.__name__)
