# -*- coding: utf-8 -*-
"""
Groom debugger

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import os
import open3d as o3d
import open3d.visualization.gui as gui
import open3d.visualization.rendering as rendering
import numpy as np
import matplotlib.pyplot as plt
from utils.config import config
from base import FrameworkClass
from Modules.Geometry.optim.haircard import HairCard

__all__ = ["GroomDebugger"]


class Settings:
    """Settings"""
    CMAP = np.array(['winter', 'jet', 'uniform'])
    CARD_CMAP = np.array(['uniform', 'different'])
    AXES = np.array(['z', 'y', 'x'])
    SHOW_POINTS = True
    SHOW_ROOT = False
    SHOW_CARDS = False
    SHOW_VERT_NORMS = False
    SHOW_AXES = True
    SHOW_WIRE_FRAME = True
    PTS_SIZE = 1.0
    CARD_NORMAL_SIZE = 1.0
    COORD_SIZE = 3.0
    ROTATE = [np.pi / 2, 0.0, np.pi]


class GroomDebugger(FrameworkClass):
    """Debuger for hair-cards generation"""

    def __init__(self, w: int = 1920, h: int = 1080):
        """Init"""

        super().__init__()

        self._points = None
        self._hl_points = None
        self._root_pts = None
        self._root_labels = None

        self._hair_cards = []
        self._hl_hair_cards = []
        self._num_cards = 0
        self._hl_num_cards = 0

        # -------------------------------------------------------
        # ------------------- visualizer part -------------------
        # -------------------------------------------------------

        gui.Application.instance.initialize()
        self._window = gui.Application.instance.create_window("Debug", w, h)
        self._scene = gui.SceneWidget()
        self._scene.scene = rendering.Open3DScene(self._window.renderer)

        self._elems = {}
        self._build_panel()

        self._window.set_on_layout(self._on_layout)
        self._window.add_child(self._scene)
        self._window.add_child(self._elems['settings_panel'])

        self._material = rendering.Material()
        self._material.base_color = [0.8, 0.8, 0.8, 1.0]
        self._material.shader = "defaultUnlit"

        self._bg_color = [0.0, 0.0, 0.0, 1.0]
        self._pts_cmaps = [
            plt.get_cmap("winter"),
            plt.get_cmap("jet"), self._uniform_color_pts
        ]
        self._cards_cmap = plt.get_cmap("tab20")

        # ------------------------------------------------------
        # ----------------------- cache ------------------------
        # ------------------------------------------------------

        self._obj = {}
        self._exp_img_idx = 0
        self._T = []

    def _apply_settings(self):
        """Apply settings"""

        self._scene.scene.set_background(self._bg_color)
        self._material.point_size = int(self._elems['point_size'].double_value)
        self._scene.scene.update_material(self._material)
        self._elems['sel_card_id'].set_limits(-1, self._num_cards - 1)

    def _build_panel(self):
        """Build settings panel to interact with options"""

        em = self._window.theme.font_size
        separation_height = int(round(0.5 * em))

        # margins defined as left, top, right, bottom
        self._elems['settings_panel'] = gui.Vert(
            0, gui.Margins(0.5 * em, 0.25 * em, 0.25 * em, 0.5 * em))

        view_ctrls = gui.CollapsableVert("Control", 0.25 * em, gui.Margins(em, 0, 0, 0))
        self._elems['show_points'] = gui.Checkbox("Show points")
        self._elems['show_points'].checked = Settings.SHOW_POINTS
        self._elems['show_points'].set_on_checked(self._on_event)
        view_ctrls.add_child(self._elems['show_points'])

        self._elems['show_cards'] = gui.Checkbox("Show cards")
        self._elems['show_cards'].checked = Settings.SHOW_CARDS
        self._elems['show_cards'].set_on_checked(self._on_event)
        view_ctrls.add_child(self._elems['show_cards'])

        self._elems['show_axes'] = gui.Checkbox("Show axes")
        self._elems['show_axes'].checked = Settings.SHOW_AXES
        self._elems['show_axes'].set_on_checked(self._on_event)
        view_ctrls.add_child(self._elems['show_axes'])

        view_app = gui.CollapsableVert("Appearance", 0.25 * em, gui.Margins(em, 0, 0, 0))

        self._elems['wire_edges'] = gui.Checkbox("Wire edges")
        self._elems['wire_edges'].checked = Settings.SHOW_WIRE_FRAME
        self._elems['wire_edges'].set_on_checked(self._on_event)
        view_app.add_child(self._elems['wire_edges'])

        self._elems['point_size'] = gui.Slider(gui.Slider.INT)
        self._elems['point_size'].set_limits(1, 10)
        self._elems['point_size'].double_value = Settings.PTS_SIZE
        self._elems['point_size'].set_on_value_changed(self._on_event)

        self._elems['coord_size'] = gui.Slider(gui.Slider.INT)
        self._elems['coord_size'].set_limits(1, 10)
        self._elems['coord_size'].double_value = Settings.COORD_SIZE
        self._elems['coord_size'].set_on_value_changed(self._on_event)

        self._elems['c_map'] = gui.Combobox()
        self._elems['c_map'].add_item(Settings.CMAP[0])
        self._elems['c_map'].add_item(Settings.CMAP[1])
        self._elems['c_map'].add_item(Settings.CMAP[2])
        self._elems['c_map'].set_on_selection_changed(self._on_cmap)

        self._elems['c_map_axis'] = gui.Combobox()
        self._elems['c_map_axis'].add_item(Settings.AXES[0])
        self._elems['c_map_axis'].add_item(Settings.AXES[1])
        self._elems['c_map_axis'].add_item(Settings.AXES[2])
        self._elems['c_map_axis'].set_on_selection_changed(self._on_cmap)

        self._elems['card_c_map'] = gui.Combobox()
        self._elems['card_c_map'].add_item(Settings.CARD_CMAP[0])
        self._elems['card_c_map'].add_item(Settings.CARD_CMAP[1])
        self._elems['card_c_map'].set_on_selection_changed(self._on_cmap)

        self._elems['pts_color'] = gui.ColorEdit()
        self._elems['pts_color'].set_on_value_changed(self._on_cmap)
        self._elems['pts_color'].color_value = gui.Color(0.0, 0.0, 1.0)

        self._elems['cards_color'] = gui.ColorEdit()
        self._elems['cards_color'].set_on_value_changed(self._on_cmap)
        self._elems['cards_color'].color_value = gui.Color(1.0, 0.0, 0.0)

        self._elems['sel_card_id'] = gui.Slider(gui.Slider.INT)
        self._elems['sel_card_id'].set_limits(-1, -1)
        self._elems['sel_card_id'].double_value = -1
        self._elems['sel_card_id'].set_on_value_changed(self._on_event)

        grid = gui.VGrid(2, 0.25 * em)
        grid.add_child(gui.Label("Point size"))
        grid.add_child(self._elems['point_size'])
        grid.add_child(gui.Label("Coordinate frame size"))
        grid.add_child(self._elems['coord_size'])
        grid.add_child(gui.Label("Point color map"))
        grid.add_child(self._elems['c_map'])
        grid.add_child(gui.Label("Color map axis"))
        grid.add_child(self._elems['c_map_axis'])
        grid.add_child(gui.Label("Color map hair-cards"))
        grid.add_child(self._elems['card_c_map'])
        grid.add_child(gui.Label("Points uniform color"))
        grid.add_child(self._elems['pts_color'])
        grid.add_child(gui.Label("Cards uniform color"))
        grid.add_child(self._elems['cards_color'])
        grid.add_child(gui.Label("Select card id"))
        grid.add_child(self._elems['sel_card_id'])
        view_app.add_child(grid)

        self._elems['next_card'] = gui.Button("Next card")
        self._elems['next_card'].horizontal_padding_em = 0.5
        self._elems['next_card'].vertical_padding_em = 0
        self._elems['next_card'].set_on_clicked(self._next_card)
        view_app.add_child(self._elems['next_card'])

        self._elems['prev_card'] = gui.Button("Previous card")
        self._elems['prev_card'].horizontal_padding_em = 0.5
        self._elems['prev_card'].vertical_padding_em = 0
        self._elems['prev_card'].set_on_clicked(self._prev_card)
        view_app.add_child(self._elems['prev_card'])

        self._elems['all_cards'] = gui.Button("All cards")
        self._elems['all_cards'].horizontal_padding_em = 0.5
        self._elems['all_cards'].vertical_padding_em = 0
        self._elems['all_cards'].set_on_clicked(self._all_cards)
        view_app.add_child(self._elems['all_cards'])

        opt_app = gui.CollapsableVert("Options", 0.25 * em, gui.Margins(em, 0, 0, 0))
        self._elems['export_img'] = gui.Button("Save image")
        self._elems['export_img'].horizontal_padding_em = 0.5
        self._elems['export_img'].vertical_padding_em = 0
        self._elems['export_img'].set_on_clicked(self._export_img)
        opt_app.add_child(self._elems['export_img'])

        self._elems['settings_panel'].add_child(view_ctrls)
        self._elems['settings_panel'].add_fixed(separation_height)
        self._elems['settings_panel'].add_fixed(separation_height)
        self._elems['settings_panel'].add_child(view_app)
        self._elems['settings_panel'].add_fixed(separation_height)
        self._elems['settings_panel'].add_child(opt_app)
        self._elems['settings_panel'].add_fixed(separation_height)

    def _on_layout(self, theme):
        """Layout for menu

        Args:
            theme (Theme): theme
        """

        r = self._window.content_rect
        self._scene.frame = r
        width = 17 * theme.font_size
        height = min(r.height,
                     self._elems['settings_panel'].calc_preferred_size(theme).height)

        self._elems['settings_panel'].frame = gui.Rect(r.get_right() - width, r.y, width,
                                                       height)

    def _on_cmap(self, *args, **kwargs):
        color = self._color_map_points(self._points)
        self._obj['points'].colors = o3d.utility.Vector3dVector(color)
        self._render()

    def _on_event(self, visible):
        self._render()
        print('Event: {}'.format(visible))

    def _color_map_points(self, vertices: np.array) -> np.array:
        """Apply color map to points

        Args:
            vertices (np.array): vertex positions

        Returns:
            np.array: colors
        """

        cmap_id = np.where(self._elems['c_map'].selected_text == Settings.CMAP)[0][0]
        hair_cmap = self._pts_cmaps[cmap_id]
        axis = np.where(self._elems['c_map_axis'].selected_text == Settings.AXES)[0][0]

        vals = vertices[:, axis]
        min_val = vals.min()
        max_val = vals.max()

        vals = (vals - min_val) / (max_val - min_val)
        colors = hair_cmap(vals)[:, :3]

        return colors

    def _uniform_color_pts(self, points) -> np.array:
        """Uniform color for point cloud"""

        colors = np.ones([len(points), 3], dtype=np.float)
        b_col = self._uniform_color('pts_color')
        colors *= b_col
        return colors

    def _uniform_color(self, col_name):
        """Uniform color"""

        b_col = np.array([
            self._elems[col_name].color_value.red,
            self._elems[col_name].color_value.green,
            self._elems[col_name].color_value.blue
        ])
        return b_col

    def _next_card(self):
        """View next card"""

        idx = int(self._elems['sel_card_id'].double_value)
        if (idx + 1) < self._num_cards:
            self._elems['sel_card_id'].double_value = float(idx + 1)
        self._render()

    def _prev_card(self):
        """View next card"""

        idx = int(self._elems['sel_card_id'].double_value)
        if (idx - 1) >= 0:
            self._elems['sel_card_id'].double_value = float(idx - 1)
        self._render()

    def _all_cards(self):
        """View next card"""

        self._elems['sel_card_id'].double_value = float(-1)
        self._render()

    def _export_img(self):
        """Save image"""

        path = os.path.join(config.dirs.root,
                            'exported_img_{}.png'.format(self._exp_img_idx))

        def on_image(image):

            quality = 9  # png
            o3d.io.write_image(path, image, quality)

        self._scene.scene.scene.render_to_image(on_image)
        self._exp_img_idx += 1

    def add_points(self, points: np.array):
        """Add hairstrand points"""
        self._points = points
        self._T = np.mean(points, axis=0)

    def add_haircard(self, haircard: HairCard) -> None:
        """Add haircard"""
        self._hair_cards.append(haircard)
        self._num_cards += 1

    def _get_haircards_mesh(self):
        """Generate hair-card mesh to be displayed"""

        all_verts = []
        all_tris = []
        max_vid = 0

        for c in self._hair_cards:
            all_verts.append(c.verts)
            all_tris.append(c.faces + max_vid)
            max_vid += c.faces.max() + 1

        verts = np.concatenate(all_verts, axis=0)
        tris = np.concatenate(all_tris, axis=0)
        mesh = o3d.geometry.TriangleMesh(o3d.utility.Vector3dVector(verts),
                                         o3d.utility.Vector3iVector(tris))
        mesh.compute_vertex_normals()

        return mesh

    def _get_haircards_diff_colors(self) -> np.array:
        """Get hair-cards colors based on card id

        Returns:
            np.array: vertex colors
        """

        colors = []

        for cid, c in enumerate(self._hair_cards):
            color_id = cid / len(self._hair_cards)
            color = np.array(self._cards_cmap(color_id)[:3]).reshape([-1, 3])
            colors.append(color.repeat(len(c.verts), axis=0))

        return np.concatenate(colors, axis=0)

    def _render(self):
        """Render"""

        self._scene.scene.clear_geometry()
        self._apply_settings()

        # ------------------------------------------------------ #
        # ------------------- plot everything ------------------ #
        # ------------------------------------------------------ #

        # ------------------- points ------------------- #

        if self._elems['show_points'].checked:
            self._scene.scene.add_geometry('Point cloud', self._obj['points'],
                                           self._material)

        # ------------------- cards ------------------- #

        if self._elems['show_cards'].checked:
            if int(self._elems['sel_card_id'].double_value) == -1:
                mesh = self._get_haircards_mesh()
            else:
                idx = int(self._elems['sel_card_id'].double_value)

                mesh = o3d.geometry.TriangleMesh(
                    o3d.utility.Vector3dVector(self._hair_cards[idx].verts),
                    o3d.utility.Vector3iVector(self._hair_cards[idx].faces))
                mesh.compute_vertex_normals()

            if self._elems['card_c_map'].selected_text == Settings.CARD_CMAP[0]:
                mesh.paint_uniform_color(self._uniform_color('cards_color'))
            else:
                if int(self._elems['sel_card_id'].double_value) > -1:
                    mesh.paint_uniform_color(self._uniform_color('cards_color'))
                else:
                    colors = self._get_haircards_diff_colors()
                    mesh.vertex_colors = o3d.utility.Vector3dVector(colors)

            self._scene.scene.add_geometry('Cards', mesh, self._material)
            if self._elems['wire_edges'].checked:
                lines = o3d.geometry.LineSet.create_from_triangle_mesh(mesh)
                self._scene.scene.add_geometry('Wire', lines, self._material)

        # ------------------- ref system -------------------

        if self._elems['show_axes'].checked:
            coordinates = o3d.geometry.TriangleMesh.create_coordinate_frame(
                size=int(self._elems['coord_size'].double_value),
                origin=self._obj['points'].get_center())
            self._scene.scene.add_geometry('axes', coordinates, self._material)

    def view(self):
        """view data"""

        # ------------------- points ------------------- #

        pcd = o3d.geometry.PointCloud()
        pcd.points = o3d.utility.Vector3dVector(self._points)
        pcd.colors = o3d.utility.Vector3dVector(self._color_map_points(self._points))
        self._obj['points'] = pcd

        # ------------------- generic ------------------- #

        bounds = self._obj['points'].get_axis_aligned_bounding_box()
        self._scene.setup_camera(60, bounds, bounds.get_center())

        self._render()
        gui.Application.instance.run()
