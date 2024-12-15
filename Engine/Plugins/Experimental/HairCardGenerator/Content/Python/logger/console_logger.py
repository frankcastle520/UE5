# -*- coding: utf-8 -*-
"""
ConsoleLogger class to print data in
the console.

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

__version__ = "0.2.1"
__all__ = ['ConsoleLogger']

try:
    import unreal
except ImportError:
    WITH_UE = False
else:
    WITH_UE = True

import logging

class CustomFormatter(logging.Formatter):
    """Custom formatter"""

    DATE = '\033[94m'
    GREEN = '\033[92m'
    WHITE = '\033[0m'
    WARNING = '\033[93m'
    RED = '\033[91m'

    def __init__(self):
        """initializer"""

        orig_fmt = "%(name)s: %(message)s"
        datefmt = "%H:%M:%S"

        super().__init__(orig_fmt, datefmt)

    def format(self, record):
        """format message"""

        color = self.WHITE
        if record.levelno == logging.INFO:
            color = self.GREEN

        if record.levelno == logging.WARN:
            color = self.WARNING

        if record.levelno == logging.ERROR:
            color = self.RED

        self._style._fmt = "{}%(asctime)s {}[%(levelname)s]{} %(name)s{}: %(message)s".format(
            self.DATE, color, self.DATE, self.WHITE)

        return logging.Formatter.format(self, record)


class ConsoleLogger():
    """Console logger"""
    loggers = {}

    def __init__(self, name='main'):
        super().__init__()

        if WITH_UE:
            self._name = name
        else:
            if self.loggers.get(name):
                self._logger = self.loggers.get(name)
                return

            self._logger = logging.getLogger(name)
            self._logger.setLevel(logging.INFO)
            self._logger.propagate = 0

            formatter = CustomFormatter()
            console_log = logging.StreamHandler()
            console_log.setLevel(logging.INFO)
            console_log.setFormatter(formatter)

            self._logger.addHandler(console_log)
            self.loggers[name] = self._logger

    def info(self, *args, **kwargs):
        """info"""
        if WITH_UE:
            msg = format(*args, **kwargs)
            unreal.log('[{}] {}'.format(self._name, msg))
        else:
            self._logger.info(*args, **kwargs)

    def warning(self, *args, **kwargs):
        """warning"""
        if WITH_UE:
            msg = format(*args, **kwargs)
            unreal.log_warning('[{}] {}'.format(self._name, msg))
        else:
            self._logger.warning(*args, **kwargs)

    def error(self, *args, **kwargs):
        """error"""
        if WITH_UE:
            msg = format(*args, **kwargs)
            unreal.log_error('[{}] {}'.format(self._name, msg))
        else:
            self._logger.error(*args, **kwargs)
