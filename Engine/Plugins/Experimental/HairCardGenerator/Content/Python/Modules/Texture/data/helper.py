# -*- coding: utf-8 -*-
"""
Helper

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from dataclasses import dataclass

import torch


@dataclass
class CardData:
    """Card dataclass"""
    verts: torch.Tensor
    faces: torch.Tensor
    normals: torch.Tensor
    tangents: torch.Tensor
    uvs: torch.Tensor
    txt: torch.Tensor
    cfid: torch.Tensor


@dataclass
class OptData:
    """Opt dataclass"""
    pts: torch.Tensor
    widths: torch.Tensor
    b_uv: torch.Tensor
    depth: torch.Tensor
    tan: torch.Tensor
    s_u: torch.Tensor
    seeds: torch.Tensor
    cid: torch.Tensor
