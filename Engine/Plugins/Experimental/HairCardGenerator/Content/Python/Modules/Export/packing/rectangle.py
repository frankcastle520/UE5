# -*- coding: utf-8 -*-
"""
Rectangle

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import copy
import numpy as np

from typing import Tuple, Callable

# TODO: Simplify DFS traversal with a decorator
# def traverse_dfs() -> Callable:
#     def internal_traverse_dfs(func: Callable) -> Callable:
#         def wrap_dfs(node: 'RectangleBin', *args, **kwargs):
#             for child in node.children:
#                 if child is not None:
#                     return wrap_dfs(child, *args, **kwargs)
#             return func(node, *args, **kwargs)
#         return wrap_dfs
#     return internal_traverse_dfs

class RectangleBin:
    """
    Implementation of solution http://www.blackpawn.com/texts/lightmaps/
    """

    _EPS = 1e-12

    def __init__(self, bounds: list = None, size: list = None, margin_px: int = 1) -> None:
        """Init

        Args:
            bounds (list, optional): [min_x, min_y, max_x, max_y]. Defaults to None.
            size (list, optional): [x_size, y_size]. Defaults to None.
        """

        self.margin_px = margin_px
        self.children: list[RectangleBin] = [None, None]
        self.full = False
        self.reserved = False

        if bounds is not None:
            self.bounds = np.array(bounds, dtype=np.int32)
        elif size is not None:
            self.bounds = np.array([-margin_px, -margin_px, size[0], size[1]], dtype=np.int32)
        else:
            raise ValueError('No size or bounds')

    def _split(self, length: int, is_vertical: bool = True) -> np.ndarray:
        """Split

        Args:
            length (int): length of the split
            is_vertical (bool, optional): if True split the area vertically.
                                          Defaults to True.

        Returns:
            np.array: array of size [2, 4] defining the bounds
                      of the split areas, using the same convention
                      [min_x, min_y, max_x, max_y].
        """

        l, b, r, t = self.bounds
        if is_vertical:
            return np.array([[l, b, l + length, t], [l + length, b, r, t]], dtype=np.int32)
        return np.array([[l, b, r, b + length], [l, b + length, r, t]], dtype=np.int32)
    
    def _insert_bin(self, rectangle: np.ndarray) -> 'RectangleBin':
        for child in self.children:
            if child is not None:
                # let's add this in the child subtree
                inserted_bin = child._insert_bin(rectangle)
                if inserted_bin is not None:
                    return inserted_bin

        # if current area is full, this subtree is extinguished
        if self.full or self.reserved:
            return None
        
        offset_rect = rectangle + self.margin_px

        # if it's not full, let's check if we can fit it
        # bounds = [min_x, min_y, max_x, max_y]
        check = (self.bounds[2:] - self.bounds[:2]) - offset_rect
        if any(check < 0):
            # too big
            return None

        # check if it fits without splitting this area up
        if all(check < 1):
            return self

        # rectangle fits, but spare area is left. Let's split this then
        is_vertical_split = check[0] > check[1]
        if is_vertical_split:
            # get width
            length = offset_rect[0]
        else:
            # get height
            length = offset_rect[1]

        children_bounds = self._split(length, is_vertical_split)

        # Bin is full if it's got children
        self.full = True

        # no longer a leaf node
        self.children = [None,None]
        self.children[0] = RectangleBin(bounds=children_bounds[0], margin_px=self.margin_px)
        self.children[1] = RectangleBin(bounds=children_bounds[1], margin_px=self.margin_px)

        # by default add left
        return self.children[0]._insert_bin(rectangle)
    
    def get_size(self) -> np.ndarray:
        # FIXME: This underestimates size if margin > 0 for nonroot bins
        return ((self.bounds[2:4]-self.bounds[0:2]) - self.margin_px)
    
    def get_bounds(self) -> np.ndarray:
        return self.bounds + np.array([self.margin_px,self.margin_px,0,0])
    
    def get_free_area(self) -> int:
        area = 0
        for child in self.children:
            if child is not None:
                area = area + child.get_free_area()
    
        if self.full or self.reserved:
            return area
        return np.prod(self.get_size())

    def insert(self, rectangle: np.ndarray) -> np.ndarray:
        """Insert rectangle in the subtree

        Args:
            rectangle (np.array): [width, height]

        Returns:
            np.array: position of the rectangle. If None it failed
        """
        new_bin = self._insert_bin(rectangle)
        if new_bin is None:
            return None
        
        # Returned bin should fit exactly and is full
        new_bin.full = True
        return new_bin.bounds[:2]+self.margin_px
        
    def get_reserved(self) -> 'RectangleBin':
        """ Return reserved space (must always be a single reserved node)
        """
        if self.reserved:
            return self

        for child in self.children:
            if child is not None:
                res = child.get_reserved()
                if res is not None:
                    return res

        return None
    
    def reserve(self, size: Tuple[int,int]) -> bool:
        """ Reserve a rectangle of space for future use in texture
            NOTE: For now this only works for reserving rectangles or squares, reserving non-rect regions 
                doesn't work recursively with this layout system
        """
        new_bin = self._insert_bin(np.array(size, dtype=np.int32))
        if new_bin is None:
            return False
        
        new_bin.reserved = True
        return True
    
    def reset_reserved(self) -> None:
        self.reserved = False
        for child in self.children:
            if child is not None:
                child.reset_reserved()
    
    def _write_dfs(self, start_idx: int = 0) -> list[dict]:
        cids = list[int]()
        tree = list[np.ndarray]()
        idx = start_idx
        for child in self.children:
            if child is not None:
                idx, subtree = child._write_dfs(idx)
                cids.append(idx-1)
                tree.extend(subtree)
        dict_node = vars(copy.copy(self))
        dict_node['children'] = cids
        tree.append(dict_node)
        idx = idx + 1
        return idx, tree

    def save_dfs_layout(self) -> np.ndarray:
        num_bins, tree = self._write_dfs()
        tree = np.array(tree)
        return tree

    @classmethod
    def _make_from_dict(cls, in_dict: dict) -> 'RectangleBin':
        new_bin = RectangleBin(bounds=[0,0,0,0])
        for k,v in in_dict.items():
            if hasattr(new_bin, k) and k != 'children':
                setattr(new_bin, k, v)
        return new_bin

    @classmethod
    def load_dfs_layout(cls, tree: np.ndarray) -> 'RectangleBin':
        nodes = list[RectangleBin]()
        for r in tree:
            nodes.append(cls._make_from_dict(r))
        nodes = np.array(nodes)
        for node,r in zip(nodes,tree):
            cids = [nodes[cid] for cid in r['children']]
            node.children = cids if len(cids) > 0 else [None,None]
        return nodes[-1]
