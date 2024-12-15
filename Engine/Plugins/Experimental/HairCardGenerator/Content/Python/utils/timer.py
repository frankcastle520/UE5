# -*- coding: utf-8 -*-
"""
Timer

@author: Pablo Garrido, Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import time


class Timer():
    """
    Class Timer allows the user to record elapsed time between events.
    """

    def __init__(self, unit='s'):
        """ Initialize the Timer class.

        Parameters (optional):
            unit (str) -- Time unit [s | ms | us | ns].
        """

        self._unit = unit
        self._state = 0
        self._start = None
        self._stop = None

    @property
    def unit(self):
        """Getter function for unit"""
        return self._unit

    @unit.setter
    def unit(self, unit: str):
        """Setter function for unit"""

        self._unit = 's'
        if unit in ('m', 'ms', 'us', 'ns'):
            self._unit = unit

    def start(self):
        """Start the clock"""

        self._start = time.time()
        self._state = 1

    def stop(self):
        """Stop the clock"""

        if self._state == 1:
            self._stop = time.time()

    def show(self) -> float:
        """Show the elapsed time"""

        diff = self._stop - self._start

        # Return elapsed time in user-defined unit.
        if self._unit == 'm':
            return diff/60.0
        if self._unit == 'ms':
            return diff*1e3
        if self._unit == 'us':
            return diff*1e6
        if self._unit == 'ns':
            return diff*1e9

        return diff
