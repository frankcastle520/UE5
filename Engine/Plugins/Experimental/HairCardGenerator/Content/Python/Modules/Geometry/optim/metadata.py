# -*- coding: utf-8 -*-
"""
Metadata class

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from dataclasses import dataclass
import numpy as np


@dataclass
class CardMeta:
    """Card meta class"""

    length: float
    width: float
    lr_ratio: float
    top_vec: np.array
    texture_group: int
