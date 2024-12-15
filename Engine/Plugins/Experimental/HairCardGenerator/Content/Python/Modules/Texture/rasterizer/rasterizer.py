# -*- coding: utf-8 -*-
"""
Rasterizer

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import pathlib

from pyrr import Matrix44
import numpy as np
import moderngl
from moderngl import Context
from OpenGL import GL
from .shader_utils import ShaderHandler


class TextureRasterizer:
    """Texture rasterizer"""

    def __init__(
            self, ctx: Context,
            card_size_cm: tuple = (1, 1),
            texture_size_px: tuple = (800, 800),
            anti_aliasing_px: tuple = (2.0, 0.1)
            ) -> None:
        """Init texture rasterizer

        Args:
            ctx (Context): context
            card_size_cm (tuple): size of the card in cm (this is the space of the strand projections)
            texture_size_px (tuple): resolution of the texture
            anti_aliasing_px (tuple, optional): anti-aliasing applied to each dimension.
        """

        self.ctx = ctx

        # Set depth bounds
        self._depth_eps = 0.001

        # ------------------- shaders ------------------- #

        dir_path = pathlib.Path(__file__).parent.resolve()
        self._shloader = ShaderHandler(self.ctx, os.path.join(dir_path, '../shaders'))
        # self._shloader.set_debug_generate(True)

        self._prog_lines_cov = self._shloader.build_program(vertex_shader='lines_cov_vs.glsl',
                                                            geometry_shader='lines_cov_gs.glsl',
                                                            fragment_shader='lines_cov_fs.glsl')

        self._prog_lines_depth = self._shloader.build_program(vertex_shader='lines_depth_vs.glsl',
                                                              geometry_shader='lines_depth_gs.glsl',
                                                              fragment_shader='lines_depth_fs.glsl')
        
        self._prog_card_tan = self._shloader.build_program(vertex_shader='cards_tan_vs.glsl',
                                                           fragment_shader='cards_tan_fs.glsl')

        # ------------------- frame buffer ------------------- #
        # Create framebuffer object w/ current texture size
        # Also sets viewport_size uniform
        self.set_texture_size(texture_size_px)

        # ------------------- uniforms ------------------- #
        # Sets up projection matrix and card size uniform
        self.set_card_size(card_size_cm)

        self.update_dilation(1.0)
        self._prog_lines_cov['u_aa_radius_px'].value = anti_aliasing_px
        self._prog_lines_depth['u_aa_radius_px'].value = anti_aliasing_px
        # Set texture bind points for card tangent shader
        self._prog_card_tan['u_coverage_tx'] = 0
        self._prog_card_tan['u_wtan_tx'] = 1

        # Set up blend mode (premultiplied alpha blend)
        self.ctx.enable(moderngl.BLEND)
        self.ctx.blend_equation = moderngl.FUNC_ADD
        self.ctx.blend_func = moderngl.ONE, moderngl.ONE_MINUS_SRC_ALPHA

        # ------------------- init ------------------- #

        self._vao_cov: moderngl.VertexArray = None
        self._vao_depth: moderngl.VertexArray = None
        self._vbo_curves: moderngl.Buffer = None
        self._ibo_curves_adj: moderngl.Buffer = None

        self._vao_card: moderngl.VertexArray = None
        self._vbo_cards: moderngl.Buffer = None
        self._ibo_card: moderngl.Buffer = None

    def set_card_size(self, card_size_cm: tuple) -> None:
        """Set card size (changes ortho projection)"""
        # Pad near/far clip planes since tris can end up right on them
        ncp = -self._depth_eps
        fcp = 1.0 + self._depth_eps
        p = Matrix44.orthogonal_projection(0, card_size_cm[0], 0, card_size_cm[1], ncp, fcp, dtype='f4')
        self._prog_lines_cov['u_projection'].write(p)
        self._prog_lines_cov['u_card_size_cm'].value = card_size_cm
        self._prog_lines_depth['u_projection'].write(p)
        self._prog_lines_depth['u_card_size_cm'].value = card_size_cm
        self._prog_card_tan['u_projection'].write(p)
        self._prog_card_tan['u_card_size_cm'].value = card_size_cm

    def set_texture_size(self, texture_size_px: tuple) -> None:
        """Set new window size"""
        # Coverage texture (rendered to alpha channel)
        self._cov_tx = self.ctx.texture(texture_size_px, components=4)
        # World-space tangents rendered to RGB (with full f32 precision)
        self._wtan_tx = self.ctx.texture(texture_size_px, components=3, dtype='f4')
        # NOTE: Using RGBA textures for each single-byte texture in case texture dims are odd
        lcltan_attachment = [
            # Tangent-space tangents output
            self.ctx.renderbuffer(texture_size_px, components=4),
        ]
        attrib_attachments = [
            # Depth texture
            self.ctx.renderbuffer(texture_size_px, components=4),
            # U-Coord texture
            self.ctx.renderbuffer(texture_size_px, components=4),
            # Seed texture
            self.ctx.renderbuffer(texture_size_px, components=4),
            # World-space strand tangents
            self._wtan_tx
        ]
        depth_buffer = self.ctx.depth_renderbuffer(texture_size_px)

        self.fbo_cov = self.ctx.framebuffer([self._cov_tx])
        self.fbo_depth = self.ctx.framebuffer(attrib_attachments, depth_buffer)
        self.fbo_tan = self.ctx.framebuffer(lcltan_attachment)

        self._prog_lines_cov['u_viewport_size_px'].value = texture_size_px
        self._prog_lines_depth['u_viewport_size_px'].value = texture_size_px

    def load_card_curve_data(self, index_adjancency: np.ndarray, data: np.ndarray) -> None:
        """Load data"""

        index_bytes = index_adjancency.astype('u4').tobytes()
        data_bytes = data.astype('f4').tobytes()

        if self._vbo_curves is not None:
            self._ibo_curves_adj.release()
            self._vbo_curves.release()
            self._vao_cov.release()
            self._vao_depth.release()

        self._ibo_curves_adj = self.ctx.buffer(index_bytes, dynamic=False)
        self._vbo_curves = self.ctx.buffer(data_bytes, dynamic=False)

        self._vao_cov = self.ctx.vertex_array(self._prog_lines_cov,
            [(self._vbo_curves, '2f 4f 3x4', 'in_position', 'in_info')],
            index_buffer = self._ibo_curves_adj,
            index_element_size = 4
        )

        self._vao_depth = self.ctx.vertex_array(self._prog_lines_depth,
            [(self._vbo_curves, '2f 4f 3f', 'in_position', 'in_info', 'in_tan')], 
            index_buffer = self._ibo_curves_adj,
            index_element_size = 4
        )

    def load_card_vert_buffer(self, vert_data: np.ndarray) -> None:
        vert_bytes = vert_data.astype('f4').tobytes()

        if self._vbo_cards is not None:
            self._vbo_cards.release()

        self._vbo_cards = self.ctx.buffer(vert_bytes, dynamic=False)

    def load_card_index_buffer(self, face_data: np.ndarray) -> None:
        face_bytes = face_data.astype('u4').tobytes()

        if self._ibo_card is not None:
            self._ibo_card.release()

        self._ibo_card = self.ctx.buffer(face_bytes, dynamic=False)
        self._vao_card = self.ctx.vertex_array(self._prog_card_tan,
            [(self._vbo_cards, '2f 3f 3f', 'in_pos', 'in_norm', 'in_tan')],
            index_buffer = self._ibo_card,
            index_element_size=4
        )

    def update_dilation(self, value: float) -> None:
        """Update width multiplier"""
        self._prog_lines_cov['u_dilation'].value = value
        self._prog_lines_depth['u_dilation'].value = value

    def render_coverage(self) -> None:
        """Render alpha-blended coverage"""
        self.fbo_cov.use()

        # Disable depth and enable blending for coverage
        self.ctx.enable(moderngl.BLEND)
        self.ctx.disable(moderngl.DEPTH_TEST)

        # Clear all frame-buffer attachments and depth
        self.fbo_cov.clear(0.0, 0.0, 0.0, 0.0, 1.0)

        if self._vao_cov is None:
            print('Coverage VAO is None!!')
            return
        
        self._vao_cov.render(mode=moderngl.LINES_ADJACENCY)

    def render_depth_test(self) -> None:
        """Render depth tested attributes (no blend)"""
        self.fbo_depth.use()
        # Enable depth test (and don't blend, keeps consistent tangents)
        self.ctx.disable(moderngl.BLEND)
        self.ctx.enable(moderngl.DEPTH_TEST)

        # Clear all frame-buffer attachments and depth
        self.fbo_depth.clear(0.0, 0.0, 0.0, 0.0, 1.0)
        # Set alpha of depth to 1 everywhere
        GL.glClearBufferfv(GL.GL_COLOR, 0, [0.0, 0.0, 0.0, 1.0])
            
        if self._vao_depth is None:
            print('Depth VAO is None!')
            return

        self._vao_depth.render(mode=moderngl.LINES_ADJACENCY)

    def render_card_tangents(self) -> None:
        """Load world-space tangent texture values and render to local interpolated tangent-space texture"""
        self.fbo_tan.use()
        # Don't bother with depth or blend since we render in uv space
        self.ctx.disable(moderngl.BLEND)
        self.ctx.disable(moderngl.DEPTH_TEST)
        self.ctx.disable(moderngl.CULL_FACE)

        # Bind coverage/world-tangent textures for rendering
        self._cov_tx.use(0)
        self._wtan_tx.use(1)

        # Clear tangent color
        self.fbo_tan.clear(0.0,0.0,0.0,0.0, 1.0)
        GL.glClearBufferfv(GL.GL_COLOR, 0, [0.5, 1.0, 0.5, 1.0])

        if self._vao_card is None:
            print('Cards VAO is None!')
            return
        
        self._vao_card.render(mode=moderngl.TRIANGLES)
