# -*- coding: utf-8 -*-
"""
Texture generator

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import math
import time
import torch
import moderngl
from PIL import Image
import numpy as np
import scipy.ndimage as spim
from typing import Tuple

from base import BaseOptimizer
from base import TxtAtlas
from utils.data import Groom
from utils import io as uio
from utils import parameters
from logger.progress_iterator import log_progress

from Modules.Texture.data import CardsDataset
from Modules.Texture.data import CardsDataLoader
from Modules.Texture.rasterizer import TextureRasterizer

from .data.helper import OptData, CardData

class TextureSlotMap:
    """ Helper class for creating textures from rasterized elements using chan_map layout
    """
    def __init__(self, num_channels: int, chan_map: dict[int,str]) -> None:
        """ Initialize the slot map using num_channels and chan_map
            num_channels: Number of channels in output texture slot
            chan_map: Maps start channel index to atlas name from Texture Generator
        """
        self._mode_map = {
            1: 'L',
            3: 'RGB',
            4: 'RGBA'
        }
        self._nchan = num_channels
        self._chan = chan_map

    def get_shape(self, generator: 'TextureGenerator') -> Tuple[int,int,int]:
        """ Return the size of the output texture using the first chan_map element
        """
        selector = next(iter(self._chan.values()))
        if not hasattr(generator, selector):
            return None
        size = getattr(generator, selector).shape[0:2]
        return (size[0], size[1], self._nchan)
    
    def create_texture(self, generator: 'TextureGenerator') -> Image.Image:
        """ Merge mapped atlas elements to form the texture slot
        """
        tx_shape = self.get_shape(generator)
        if tx_shape is None:
            print(f'WARNING: Unable to compute shape for texture')
            return None
    
        slot_tx: np.ndarray = np.zeros(tx_shape, dtype=np.uint8)
        for start_chan,selector in self._chan.items():
            if not hasattr(generator, selector):
                print(f'WARNING: Invalid component specified for texture slot: {selector}, skipped')
                continue

            txp: np.ndarray = np.atleast_3d(getattr(generator, selector))
            if slot_tx.shape[2] < start_chan+txp.shape[2]:
                print(f'WARNING: Texture too big for slot: {selector}, ({slot_tx.shape[2]} < {start_chan+txp.shape[2]}), skipped')
                continue

            set_chans = range(start_chan, start_chan+txp.shape[2])
            slot_tx[:,:,set_chans] = txp
        if slot_tx.shape[2] == 1:
            slot_tx = slot_tx.squeeze(axis=2)
        mode = self._mode_map[self._nchan]
        return Image.fromarray(slot_tx, mode=mode)
    
    def load_texture(self, generator: 'TextureGenerator', tx_slot: Image.Image) -> bool:
        """ Initialize atlas elements in tx_generator from an input texture slot using this layout
        """
        tx_shape = self.get_shape(generator)
        if tx_shape is None:
            print(f'WARNING: Unable to compute shape for texture')
            return False
        
        slot_tx = np.atleast_3d(np.asarray(tx_slot))
        if not np.all(slot_tx.shape == tx_shape):
            print(f'WARNING: Texture slot shape does not match expected: {slot_tx.shape} != {tx_shape}, ignoring')
            return False
        
        for start_chan,selector in self._chan.items():
            if not hasattr(generator, selector):
                print(f'WARNING: Invalid component specified for texture slot: {selector}, skipped')
                continue

            txp_size = np.atleast_3d(getattr(generator, selector)).shape

            get_chans = range(start_chan, start_chan+txp_size[2])
            set_tx_val = slot_tx[:,:,get_chans]
            if set_tx_val.shape[2] == 1:
                set_tx_val = set_tx_val.squeeze(axis=2)
            setattr(generator, selector, set_tx_val)
        return True



class TextureGenerator(BaseOptimizer):
    """Texture generator"""

    def __init__(self,
                 name: str,
                 groom: Groom,
                 obj_path: str,
                 output_path: str,
                 num_points_per_curve: int,
                 atlas_manager: TxtAtlas,
                 group_data: dict,
                 channel_layout: int = 0,
                 atlas_size: int = 4096,
                 batch_size: int = 25,
                 random_seed: int = 0,
                 depth_min_range: float = None,
                 depth_max_range: float = None,
                 debug: bool = False,
                 num_workers: int = 25) -> None:
        """Init

        Args:
            name (str): asset name
            groom (Groom): groom asset
            obj_path (str): path to dir containing objs
            texture_path (str): path to dir where to save the textures
            num_points_per_curve (int): number of points sampled per strand in the groom.
            atlas_manager (TxtAtlas): object that contains the information of which texture goes where.
            group_data (dict): group parameters
            atlas_size (int): atlas size (squared image).
            batch_size (int): batch size.
            depth_max_range (float, optional) define max value of depth range.
                                              If None, no limit applied. Defaults to None.
            depth_min_range (float, optional): define min value of depth range.
                                               If None, no limit applied. Defaults to None.
            num_workers (int, optional): number of processors for data loader.
                                         Defaults to 1.
        """

        super().__init__(name, debug)

        self._groom = groom
        self._batch_size = batch_size
        self._strand_len = num_points_per_curve
        self._verbose = False
        self._atlas_size = atlas_size
        self._random_seed = random_seed
        self._depth_min = depth_min_range
        self._depth_max = depth_max_range
        self._rdoc_api = None

        self._channel_layout = channel_layout
        self._num_slots: int = 0
        self._slot_map: dict[int,TextureSlotMap] = {}
        self._init_layout_map()

        # ------------------- initialize data loading ------------------- #

        self._datasets = list[CardsDataset]()
        self._data_loaders = list[CardsDataLoader]()

        for group in group_data:
            assert 'name' in group, "no group name provided"

            self._datasets.append(
                CardsDataset(groom,
                             group['name'],
                             obj_path,
                             group['metadata'],
                             group['dataset_card_filter']\
                                if 'dataset_card_filter' in group else None,
                             group['strand_width_scale']\
                                if 'strand_width_scale' in group\
                                else parameters.rendering.default_width_scale,
                                ignore_depth=False))

            self._data_loaders.append(
                CardsDataLoader(self._datasets[-1],
                                batch_size=self._batch_size,
                                shuffle=False,
                                num_workers=num_workers))

        # ------------------- atlas data ------------------- #

        self._atlas = None
        self._atlas_manager = atlas_manager
        assert len(self._atlas_manager) > 0, "Must generate atlas layout before texture rasterization"

        # ------------------- initialize rasterizer ------------------- #

        self._ctx = moderngl.create_standalone_context()
        self._txt_raster = TextureRasterizer(self._ctx, texture_size_px=(32,32))

        self._txt_path = uio.safe_dir(output_path)

        # all passes
        self._atlas_op = np.zeros((self._atlas_size, self._atlas_size), dtype=np.uint8)
        self._atlas_dp = np.zeros((self._atlas_size, self._atlas_size), dtype=np.uint8)
        self._atlas_tan = np.zeros((self._atlas_size, self._atlas_size,3), dtype=np.uint8)
        self._atlas_seed = np.zeros((self._atlas_size, self._atlas_size), dtype=np.uint8)
        self._atlas_ucoord = np.zeros((self._atlas_size, self._atlas_size), dtype=np.uint8)

        self._cards_atlas_pos = dict()

    def _init_layout_map(self):
        if self._channel_layout == 0 or self._channel_layout == 1:
            self._num_slots = 6
            self._slot_map = {
                # Depth
                0: TextureSlotMap(1, {0: '_atlas_dp'}),
                # Coverage
                1: TextureSlotMap(1, {0: '_atlas_op'}),
                # Tangents
                2: TextureSlotMap(3, {0: '_atlas_tan'}),
                # Attributes (RootUV | CoordU | Seed)
                3: TextureSlotMap(4, {2: '_atlas_ucoord', 3: '_atlas_seed'}),
            }
        elif self._channel_layout == 2:
            self._num_slots = 3
            self._slot_map = {
                # Tangents | CoordU
                0: TextureSlotMap(4, {0: '_atlas_tan', 3: '_atlas_ucoord'}),
                # Coverage | Depth | Seed
                1: TextureSlotMap(3, {0: '_atlas_op', 1: '_atlas_dp', 2: '_atlas_seed'}),
            }
        elif self._channel_layout == 3:
            self._num_slots = 4
            self._slot_map = {
                # Tangents
                0: TextureSlotMap(3, {0: '_atlas_tan'}),
                # RootUV | CoordU | GroupID
                1: TextureSlotMap(4, {1: '_atlas_ucoord'}),
                # Coverage | Depth | Seed
                2: TextureSlotMap(3, {0: '_atlas_op', 1: '_atlas_dp', 2: '_atlas_seed'}),
            }


    def _prepare_gl_line_data(self, card_id: int, opt_data: OptData) -> Tuple[np.ndarray,np.ndarray]:
        """Prepara data to load on the GPU

        Args:
            card_id (int): card if
            opt_data (OptData): optimized data

        Returns:
            np.array: data
        """

        # get strands info
        card_sid = torch.nonzero(opt_data.cid == card_id).flatten()

        card_uv = opt_data.b_uv[card_sid].view([-1, self._strand_len, 2])
        card_widths = opt_data.widths[card_sid].view([-1, self._strand_len, 1])
        card_seeds = opt_data.seeds[card_sid].view([-1, self._strand_len, 1])
        card_strands_u = opt_data.s_u[card_sid].view([-1, self._strand_len, 1])
        card_depth = opt_data.depth[card_sid].view([-1, self._strand_len, 1])
        card_tan = opt_data.tan[card_sid].view([-1, self._strand_len, 3])

        # smooth uvs, depth and tangent
        window_size = 21
        hamming = torch.hamming_window(window_size)[None, None]
        padding = window_size//2

        conv = torch.nn.Conv1d(1, 1, hamming.size(0), bias=False)
        conv.weight.data = hamming / hamming.sum()

        def smooth_points(points, keep_edges = False):
            if keep_edges:
                expansion_start = 2 * points[:, 0, None] - points[:, :padding].flip(1)
                expansion_end = 2 * points[:, -1, None] - points[:, -padding:].flip(1)
            else:
                expansion_start = points[:, 0, None].repeat(1, padding, 1)
                expansion_end = points[:, -1, None].repeat(1, padding, 1)

            expanded = torch.cat([expansion_start, points, expansion_end], dim=1)
            for dim in range(points.shape[-1]):
                points[:, :, dim] = conv(expanded[:, None, :, dim])[:, 0]

        smooth_points(card_uv, True)
        smooth_points(card_depth)
        smooth_points(card_tan)

        # convert depth to range [0, 1] for the selected card, where 0 -> min, 1 -> max
        # distance of the strands to the card
        if (self._depth_min is not None) and (self._depth_max is not None):
            card_depth = (card_depth - self._depth_min) / (self._depth_max - self._depth_min)
            card_depth = torch.clip(card_depth, 0, 1)

        # build up seg vector
        batch = torch.cat(
            [card_uv, card_widths, card_depth, card_seeds, card_strands_u, card_tan], dim=2)
        
        batch_index = np.reshape(np.arange(0, batch.shape[0]*batch.shape[1], dtype='u4'), (batch.shape[0], batch.shape[1]))
        batch_preadj_idx = np.hstack((batch_index[:,[0]],batch_index[:,:-2]))
        batch_postadj_idx = np.hstack((batch_index[:,2:],batch_index[:,[-1]]))
        batch_adj_idx = np.stack((batch_preadj_idx,batch_index[:,:-1],batch_index[:,1:],batch_postadj_idx), axis=2)

        # arrange data per line -> start-end segment data
        gl_vert_data = batch.flatten().detach().numpy()
        gl_index_data = batch_adj_idx.flatten()
        return gl_vert_data, gl_index_data
    
    def _prepare_gl_tri_vbuf(self, card_data: CardData) -> np.ndarray:
        """ Set up vertex buffer for all triangle info (all cards in batch)
            Rendering will select a single card using a separate index buffer
        """
        vbuf = torch.cat([card_data.uvs, card_data.normals, card_data.tangents], dim=1)
        return vbuf.flatten().numpy()
    
    def _prepare_gl_tri_ibuf(self, card_id: int, card_data: CardData) -> np.ndarray:
        card_fid = torch.nonzero(card_data.cfid == card_id)

        card_faces = card_data.faces[card_fid]
        return card_faces.flatten().numpy()

    def _extract_atlas_data(self, grp_id: int, card_id: int):
        """Extract the card information to build the atlas. Dilation is applied
        to all these passes.

        NOTE: OpenGL has (0, 0) in the BL corner, so when we read the image instead
              of having the strands going bottom to top, they are going in top-to-bottom.
              We are not going to change this as this is what we want anyway.


        Args:
            card_id (int): card number
        """

        fbo = self._txt_raster.fbo_depth
        depth_img = Image.frombytes('RGBA', fbo.size, fbo.read(components=4, attachment=0))
        np_depth_img = np.asarray(depth_img)
        ucoord_img = Image.frombytes('RGBA', fbo.size, fbo.read(components=4, attachment=1))
        np_ucoord_img = np.asarray(ucoord_img)
        seed_img = Image.frombytes('RGBA', fbo.size, fbo.read(components=4, attachment=2))
        np_seed_img = np.asarray(seed_img)

        s = self._atlas_manager.get_texture_size(grp_id, card_id)
        r, c = self._atlas_manager.get_texture_coordinate(grp_id, card_id)

        # add texture to the atlas in the right place, for all passes
        self._atlas_dp[r:r + s[0], c:c + s[1]] = np_depth_img[:,:,0]
        self._atlas_ucoord[r:r + s[0], c:c + s[1]] = np_ucoord_img[:,:,0]
        self._atlas_seed[r:r + s[0], c:c + s[1]] = np_seed_img[:,:,0]

    def _extract_atlas_opacity(self, grp_id: int, card_id: int):
        """Extract the card opacity to build the atlas. No dilation here.

        NOTE: OpenGL has (0, 0) in the BL corner, so when we read the image instead
              of having the strands going bottom to top, they are going in top-to-bottom.
              We are not going to change this as this is what we want anyway.


        Args:
            card_id (int): card number
        """

        tx = self._txt_raster._cov_tx
        opacity_img = Image.frombytes('RGBA', tx.size, tx.read())
        np_opacity_img = np.asarray(opacity_img)

        s = self._atlas_manager.get_texture_size(grp_id, card_id)
        r, c = self._atlas_manager.get_texture_coordinate(grp_id, card_id)
        # assert max(s) == np_opacity_img.shape[0], "Window size not matching"

        # add texture to the atlas in the right place, for all passes
        # NOTE: Coverage only uses .r so just move alpha to r
        self._atlas_op[r:(r + s[0]), c:(c + s[1])] = np_opacity_img[:,:,-1]

    def _extract_atlas_tan(self, grp_id: int, card_id: int) -> None:
        fbo = self._txt_raster.fbo_tan
        tan_img = Image.frombytes('RGBA', fbo.size, fbo.read(components=4, attachment=0))
        np_tan_img = np.asarray(tan_img)

        s = self._atlas_manager.get_texture_size(grp_id, card_id)
        r, c = self._atlas_manager.get_texture_coordinate(grp_id, card_id)

        self._atlas_tan[r:r + s[0], c:c + s[1]] = np_tan_img[:,:,0:3]

    def _clear_masked(self, im: np.ndarray, im_op: np.ndarray) -> np.ndarray:
        """ Clear using atlas mask, but also clear all opaque non-masked regions
            NOTE: This assumes opacity is the non-dilated mask for all textures
        """
        clear_mask = self._atlas_manager.get_clear_mask()
        clear_dilated = (clear_mask | (im_op==0))

        out_im = im.copy()
        if out_im.ndim < 3:
            out_im[clear_dilated] = 0
        else:
            out_im[clear_dilated,:] = 0
        return out_im

    def init_atlas(self):
        """ Load atlas files (if exist) and clear masked region for writing
        """
        for slot_id,tsmap in self._slot_map.items():
            tx_filepath = os.path.join(self._txt_path, f'{self._name}_TS{slot_id}.tga')
            # Load existing texture slot images if they exist
            if not os.path.exists(tx_filepath):
                continue
            slot_im = Image.open(tx_filepath)
            if slot_im is None:
                # TODO: Check LOD level and error
                continue
            tsmap.load_texture(self, slot_im)
        
        self._clear_masked(self._atlas_dp, self._atlas_op)
        self._clear_masked(self._atlas_op, self._atlas_op)
        self._clear_masked(self._atlas_tan, self._atlas_op)
        self._clear_masked(self._atlas_seed, self._atlas_op)
        self._clear_masked(self._atlas_ucoord, self._atlas_op)

    def dilate_atlas(self, dilation: int = 16):
        """Dilate all strands by `dilation` to produce better mip levels
        
        Args:
            dilation (int, optional): number of pixels to dilate
        """
        # TODO: Use upsampled image to reduce dilation aliasing
        inv_bin_array = np.zeros_like(self._atlas_op, dtype=np.uint8)
        inv_bin_array[self._atlas_op == 0] = 1

        # Use Euclidean distance transform O(n) regardless of dilation amount
        d_im,idx_im = spim.distance_transform_edt(inv_bin_array, return_distances=True, return_indices=True)

        # Find all pixels with distance within dilation amount
        b_dlt = np.logical_and(d_im > 0, d_im <= dilation)
        # Get matrix indices for the
        xi = idx_im[0,b_dlt]
        yi = idx_im[1,b_dlt]

        self._atlas_dp[b_dlt] = self._atlas_dp[xi,yi]
        self._atlas_seed[b_dlt] = self._atlas_seed[xi,yi]
        self._atlas_ucoord[b_dlt] = self._atlas_ucoord[xi,yi]
        self._atlas_tan[b_dlt,:] = self._atlas_tan[xi,yi,:]

    def save_atlas(self, group: str = None):
        """Save atlas data

        Args:
            file_name (str, optional): file name. Defaults to 'full_atlas.png'.
        """
        for slot_id,tsmap in self._slot_map.items():
            slot_im = tsmap.create_texture(self)
            if slot_im is None:
                continue
            slot_im.save(os.path.join(self._txt_path, f'{self._name}_TS{slot_id}.tga'))

    def rasterize_card(self, gid: int, card_id: int, cards: CardData, opt_data: OptData):
        # resize based on card specific size
        tex_size = self._atlas_manager.get_texture_size(gid,card_id)
        card_size = self._atlas_manager.get_physical_size(gid,card_id)

        self._txt_raster.set_texture_size((tex_size[1], tex_size[0]))
        self._txt_raster.set_card_size((card_size[1], card_size[0]))

        gl_line_data, gl_lineidx_data = self._prepare_gl_line_data(card_id, opt_data)
        self._txt_raster.load_card_curve_data(gl_lineidx_data, gl_line_data)
        self._txt_raster.render_coverage()
        self._txt_raster.render_depth_test()

        gl_triidx_data = self._prepare_gl_tri_ibuf(card_id, cards)
        self._txt_raster.load_card_index_buffer(gl_triidx_data)
        self._txt_raster.render_card_tangents()

        self._extract_atlas_opacity(gid, card_id)
        self._extract_atlas_tan(gid, card_id)
        self._extract_atlas_data(gid, card_id)

    def generate(self):
        """Generate textures
        """

        total_cards = 0
        for dataset in self._datasets:
            total_cards += len(dataset)
        print('Num cards to rasterize: {}'.format(total_cards))
        t = time.time()

        torch.manual_seed(self._random_seed)

        abs_card_id = 0
        for gid in range(len(self._datasets)):
            num_iters = math.ceil(len(self._datasets[gid]) / self._batch_size)
            for (cards, opt_data) in log_progress(self._data_loaders[gid], task_desc='Optimizing/rasterizing card batches (quantized) of group {} of {}'.format(gid+1, len(self._datasets)), num_iters=num_iters):

                s = time.time()

                # ------------------- render cards ------------------- #
                gl_tri_data = self._prepare_gl_tri_vbuf(cards)
                self._txt_raster.load_card_vert_buffer(gl_tri_data)

                # NOTE: Using unique_consecutive since the card ids are always consecutive from the loader
                unique_cid = torch.unique_consecutive(opt_data.cid)
                for card_id in unique_cid:
                    self.rasterize_card(gid, card_id, cards, opt_data)
                    abs_card_id += 1

                print('Batch opt time: {:.03f}'.format(time.time() - s))

        print('Total loading time: {:.03f}'.format(time.time() - t))
        print('Done.')
