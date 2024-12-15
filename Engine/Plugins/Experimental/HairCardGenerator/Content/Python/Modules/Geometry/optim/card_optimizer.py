# -*- coding: utf-8 -*-
"""
Card optimizer

@author: Denis Tome'

Assumption: in a card, the x-axis is the largest dimension, the z-axis
            is aligned with the normal of the card, and the y-axis is
            the cross product of the two.

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations

import numpy as np
import torch
from scipy.optimize import minimize
from scipy.interpolate import UnivariateSpline

from base import BaseOptimizer
from utils import geometry
from utils import math as umath
from utils import parameters
from .haircard import HairCard

_DEBUG = -1

__all__ = ['CardOptimizer']

_X = np.array([1, 0, 0])
_Y = np.array([0, 1, 0])
# average curve linear sampling controller T
_T = 32
_SAMPLING = np.array([[(_T - n) / _T for n in range(_T)], [n / _T for n in range(_T)]])


class CardOptimizer(BaseOptimizer):
    """Card Optimizer"""

    _EPS = 1e-5

    def __init__(self, curves: np.array, curve_ids: np.array,
                 use_multicard_clumps: bool, groom_center_pos: np.array,
                 all_root_points: np.array, **kwargs) -> None:
        """Initialize card optimizer

        Args:
            curves (np.array): array of curve points
            curve_ids (np.array): array of curve ids
            use_multicard_clumps (bool): whether to use multi-card clumps
            groom_center_pos (np.array): goom center of mass
            all_root_points (np.array): strand root points
        """

        super().__init__(**kwargs)

        # ------------------- parameters ------------------- #

        self._v_subd = None
        self._groom_center = groom_center_pos
        self._margin = parameters.geometry.margin

        self._curves = curves

        self._root_points = self._get_root_points_from_curves()
        self._mean_root_point = np.mean(self._root_points, axis = 0)
        self._normal = geometry.normal_vector_point_cloud(all_root_points, self._mean_root_point, 100)[0]

        if self._normal[2] < 1e-10:
            self._tangent = np.array([0, 0, 1])

        else:
            self._tangent = np.array([1, 1, -1 * (self._normal[0] + self._normal[1]) / self._normal[2]])
            self._tangent /= np.linalg.norm(self._tangent)
        self._binormal = np.cross(self._normal, self._tangent)

        if np.dot(self._mean_root_point - self._groom_center, self._normal) > 0:
            self._normal = -self._normal

        if use_multicard_clumps and len(self._root_points) > 10:
            self._use_multicard_clumps = True
        else:
            self._use_multicard_clumps = False

        self.curve_ids = []
        top_vectors, self._curve_indices = self._generate_curve_indices(len(curves))
        self.haircards = []
        self.avg_curves = []
        self._subd_vectors = []
        self._avg_curve_points = []
        for i, indices in enumerate(self._curve_indices):
            if len(indices) > 0:
                self.haircards.append(HairCard())
                if len(indices) == 1:
                    self.haircards[-1].texture_group = 1
                if(self._use_multicard_clumps):
                    self.haircards[-1].top_vec = top_vectors[i]
                self._subd_vectors.append([])
                self._avg_curve_points.append([])
                self.curve_ids.append(curve_ids[indices])

        # ------------------- transformations ------------------- #

        self._R = np.eye(3)
        self._card_center = np.zeros(3)

        for i in range(len(self.haircards)):
            self.avg_curves.append(self._get_average_curve(i))

    def _get_rotated_tangent(self, angles: np.array) -> np.array:
        """Get rotated tangent.

        Args:
            angles (np.array): rotation angles

        Returns:
            Vectors resulting from rotating the tangent vector around the normal the "angles" ammount
        """

        return  self._tangent[None] * np.cos(angles[:, None]) + self._binormal[None] * np.sin(angles[:, None])

    def _split_strands_angle(self):
        """Split strands into groups.

        Returns:
            Top vector and curve indices for each card
        """

        # Calculate the card tangent at the top
        full_avg_curve = self._get_average_curve()
        card_tan = full_avg_curve[max(1, full_avg_curve.shape[0] // 5)] - full_avg_curve[0]
        card_tan /= np.linalg.norm(card_tan, axis=-1, keepdims=True)

        root_points = self._root_points - self._mean_root_point

        # Get root points in the space of the card tangent
        goal_norm = np.array([0, 0, 1])
        axis = np.cross(card_tan, goal_norm)
        axis /= np.linalg.norm(axis)
        cos_ang = np.dot(card_tan, goal_norm)

        R = geometry.get_R(axis, cos_ang)

        root_points_rot = np.sum(root_points[:, None] * R, axis = -1)[:, :2]

        # Get the direction where there are more root points
        tan_vector = np.linalg.svd(root_points_rot[:, :2].T)[0][1]
        ang_ellipse = -np.arctan2(tan_vector[1], tan_vector[0])
        R2 = [[np.cos(ang_ellipse), -np.sin(ang_ellipse)],
              [np.sin(ang_ellipse), np.cos(ang_ellipse)]]
        R2_i = [[np.cos(-ang_ellipse), -np.sin(-ang_ellipse), 0],
                [np.sin(-ang_ellipse), np.cos(-ang_ellipse), 0],
                [0, 0, 1]]

        points_rot = np.sum(root_points_rot[:, None, :2] * R2, axis=-1)

        R_i = np.dot(geometry.get_R(-axis, cos_ang), R2_i)

        div_angs = np.linspace(0, 2*np.pi, 3, endpoint=False)
        div_vecs = np.array([np.mean(np.abs(points_rot[:, 0])) * np.cos(div_angs),
                         np.mean(np.abs(points_rot[:, 1])) * np.sin(div_angs),
                         np.zeros(len(div_angs))]).T

        div_angs = np.arctan2(div_vecs[:, 1], div_vecs[:, 0])
        div_angs[div_angs < 0] += 2 * np.pi
        card_angs = np.arctan2(points_rot[:, 1], points_rot[:, 0])
        card_angs[card_angs < 0] += 2 * np.pi

        # Get indiced of the strands corresponding to each card
        point_index1 = np.where((card_angs >= div_angs[0]) & (card_angs < div_angs[1]))[0]
        point_index2 = np.where((card_angs >= div_angs[1]) & (card_angs < div_angs[2]))[0]
        index_mask = np.zeros(card_angs.shape, dtype=bool)
        index_mask[np.concatenate((point_index1, point_index2))] = True
        point_index3 = np.arange(len(card_angs))[~index_mask]
        point_index = [point_index1, point_index2, point_index3]
        
        point_index_valid = []
        card_index = []
        for i in range(len(point_index)):
            if len(point_index[i]) > 0:
                card_index.append(i)
                point_index_valid.append(point_index[i])

        ellipticity = np.mean(np.abs(points_rot[:, 0])) / np.mean(np.abs(points_rot[:, 1]))

        card_vecs = []
        if ellipticity > 0.5:
            vecs = np.array([div_vecs[1] - div_vecs[0], div_vecs[2] - div_vecs[1], div_vecs[0] - div_vecs[2]])
            for index in card_index:
                card_vecs.append(vecs[index])
        else:
            for indices in point_index_valid:
                vec = np.linalg.svd((points_rot[indices] - np.mean(points_rot[indices], axis=0)).T)[0][0]
                card_vecs.append([vec[0], vec[1], 0])

        card_vecs = np.array(card_vecs)

        # Rotate card top vectors to groom space
        card_vecs_3D = np.sum(card_vecs[:, None] * R_i, axis=-1)

        return card_vecs_3D, point_index_valid

    def _generate_curve_indices(self, n: int):
        """Generate curve indices

        Args:
            n (int): total number of curves

        Returns:
            np.array: curve indices
        """

        if self._use_multicard_clumps:
            return self._split_strands_angle()
        return [], [np.arange(n)]

    def _get_root_points_from_curves(self, cid: int = None) -> np.array:
        """Get root points from curves

        Args:
            cid (int): card index

        Returns:
            np.array: root points
        """

        if cid is None:
            return self._curves[:, 0]
        else:
            return self._curves[self._curve_indices[cid], 0]

    def _get_tip_points_from_curves(self, cid: int = None) -> np.array:
        """Get tip points from curves

        Args:
            cid (int): card index

        Returns:
            np.array: root points
        """

        if cid is None:
            return self._curves[:, -1]
        else:
            return self._curves[self._curve_indices[cid]]

    @staticmethod
    def _get_R_align_x(start_point: np.array, end_point) -> np.array:
        """Get rotation matrix R to align a segment with the x-axis without
        carying about the other dimensions.

        Args:
            start_point (np.array): segment start 3d position
            end_point (np.array): segment end 3d position
        """

        direction = end_point - start_point
        tan = umath.unit_vector(direction)
        return geometry.get_R_aligning_vectors(tan, _X)

    def _get_average_curve(self, cid: int = None) -> np.array:
        """Get average curve from which to extrude the card

        Args:
            cid (int): card index

        Returns:
            Average curve
        """

        if cid is None:
            curves = self._curves
        else:
            curves = self._curves[self._curve_indices[cid]]

        if len(curves) == 1:
            return curves[0]

        points_per_curve = curves.shape[1]

        # Get the raw average curve
        avg_curve_points = np.mean(curves, axis=0)
        weights = 1 / (np.std(curves, axis=0) + 1e-10)

        # Extend the average curve one point to cover all tips
        v0 = avg_curve_points[-1] - avg_curve_points[-2]
        v0 /= np.linalg.norm(v0)
        tip_distance = np.dot(curves[:, -1] - avg_curve_points[-1], v0)
        extension_point = avg_curve_points[-1] + (tip_distance.max() + self._margin) * v0
        avg_curve_points = np.concatenate((avg_curve_points, extension_point[None]), axis=0)
        weights = np.concatenate((weights, weights[-1][None]), axis=0)

        # Calculate a t parameter for each point of the average curve,
        # which is the distance from the root
        t_param = np.concatenate((np.zeros(1), np.cumsum(np.linalg.norm(
            avg_curve_points[1:] - avg_curve_points[:-1], axis=-1))))

        # To have a smooth and equally-spaced curve, interpolate with a spline,
        # and, since the t parameter used to create the spline is the distance from
        # the root, the resulting points will be equally spaced
        t_param_interp = np.linspace(0, t_param.max(), points_per_curve)
        interp_points = np.empty((points_per_curve, 3))

        s = 1e-2 * weights.shape[0]

        for i in range(3):
            spline = UnivariateSpline(t_param, avg_curve_points[:, i], w=weights[:, i], s=s)
            interp_points[:, i] = spline(t_param_interp)

        return interp_points

    def _get_corrected_top_vec(self, cid: int):
        """Get corrected top vector.

        Args:
            cid (int): card index

        Returns:
            New top vector, which is within an error range
        """

        # Maximum acceptable error
        max_error = 0.2
        sample_vecs = self._get_rotated_tangent(np.linspace(0, np.pi, 50, endpoint=False))
        errors = np.empty(len(sample_vecs))
        for i, vec in enumerate(sample_vecs):
            error, _ = self._get_vecs_multicard(self._avg_curve_points[cid], vec)
            errors[i] = error

        # Get the vectors with acceptable errors
        valid_vecs = sample_vecs[np.where(errors < max_error)]

        # If there are no vectors with acceptable errors, return the one with the minimum error
        if valid_vecs.size < 1:
            return None

        # If there are vectors with acceptable errors, return the one closest to the initial vector
        dot_prod = np.abs(np.dot(valid_vecs, self.haircards[cid].top_vec[:, None])).flatten()

        return valid_vecs[np.argmax(dot_prod)]

    def _set_subdivision_vectors(self, cid: int, top_vec: np.array = None) -> bool:
        """Calculate and set the subdivision vectors.

        Args:
            cid (int): card index
            vec_opt (np.array): card top vector

        Returns:
            If the minimization was successful
        """

        success = True

        if top_vec is None:
            top_vec = np.cross(self._avg_curve_points[cid][1] - self._avg_curve_points[cid][0], self._normal)
            top_vec /= np.linalg.norm(top_vec)

            if len(self._avg_curve_points[cid]) > 2:
                min = minimize(self._get_vec_error, [1, 1], args=(self._avg_curve_points[cid]))

                success = min.success
                if success:
                    top_vec = self._cartesian_from_spherical(min.x[0], min.x[1])

                else:
                    min = minimize(self._get_vec_error, min.x, method='Nelder-Mead',
                               args=(self._avg_curve_points[cid]))

                    success = min.success
                    if success:
                        top_vec = self._cartesian_from_spherical(min.x[0], min.x[1])

                    else:
                        min = minimize(self._get_vec_error, min.x, method='Powell',
                                   args=(self._avg_curve_points[cid]))

                        success = min.success
                        if success:
                            top_vec = self._cartesian_from_spherical(min.x[0], min.x[1])

        _, self._subd_vectors[cid] = self._get_vecs(self._avg_curve_points[cid], top_vec)
        self._orientate_subd_vecs(cid)

        self.haircards[cid].top_vec = self._subd_vectors[cid][0]

        return success

    def _displace_card_top(self, cid: int):
        """Displace card top to lay on the head while keeping projection safe

        Args:
            cid (int): card index
        """

        # get vector tangent to head in the displacement direction
        first_seg_vec = self._avg_curve_points[cid][1] - self._avg_curve_points[cid][0]
        first_seg_vec /= np.linalg.norm(first_seg_vec)
        disp_vec = np.dot(first_seg_vec, self._normal) * self._normal - first_seg_vec
        disp_vec /= np.linalg.norm(disp_vec)

        # get line-plane intersection point
        l0 = self._avg_curve_points[cid][0]
        p_int = l0 + first_seg_vec * (np.dot((self._mean_root_point - l0), self._normal) / np.dot(first_seg_vec,self._normal))

        # get distances from the root points to the average point in the direction of the
        # displacement vector, to calculate the safe displacement distance
        root_points = self._get_root_points_from_curves(cid)
        dists = np.sum(disp_vec[None] * root_points, axis = 1) - np.dot(disp_vec, p_int)[None]

        # the more tangent the card to the head, the more we displace the top
        disp_dist = -dists.max() * np.dot(disp_vec, first_seg_vec)

        # displace the card top
        self._avg_curve_points[cid][0] = p_int + disp_dist * disp_vec

    def _get_verts_and_uvs(self, widths: np.array, cid: int) -> tuple[np.array, np.array]:
        """Get vertices and uvs

        Args:
            widths (np.array): distance from the average curve to each edge
            cid (int): card index

        Returns:
            Vectors of vertices and uvs
        """

        seg_vecs = self._avg_curve_points[cid][1:] - self._avg_curve_points[cid][:-1]
        seg_lengths = np.linalg.norm(seg_vecs, axis=1)

        uvs = geometry.get_uvs(np.concatenate((np.array([0]), np.cumsum(seg_lengths))),
                               widths[0] + widths[1])

        verts = np.empty((2 * len(self._avg_curve_points[cid]), 3))
        for i in range(len(self._avg_curve_points[cid])):
            verts[2*i] = self._avg_curve_points[cid][i] - widths[0] * self._subd_vectors[cid][i]
            verts[2*i+1] = self._avg_curve_points[cid][i] + widths[1] * self._subd_vectors[cid][i]

        return verts, uvs

    def _get_next_vec(self, v0: np.array, p0: np.array, p1: np.array, p2: np.array) -> np.array:
        """Get vector for the next subdivision

        Args:
            v0 (np.array): vector of previous subdivision
            p0 (np.array): previous subdivision point
            p1 (np.array): current subdivision point
            p2 (np.array): next subdivision point

        Returns:
            Vector of next subdivision
        """

        v10 = (p0-p1) / np.linalg.norm(p0-p1)
        v12 = (p2-p1) / np.linalg.norm(p2-p1)

        A = -(((p1[0]-p0[0])*v0[2]-v0[0]*p1[2]+v0[0]*p0[2])*v12[2]+((p0[0]-p1[0])*v0[2]+v0[0]*p1[2]-v0[0]*p0[2])*v10[2]+((p1[0]-p0[0])*v0[1]-v0[0]*p1[1]+v0[0]*p0[1])*v12[1]+((p0[0]-p1[0])*v0[1]+v0[0]*p1[1]-v0[0]*p0[1])*v10[1])
        B= -(((p1[1]-p0[1])*v0[2]-v0[1]*p1[2]+v0[1]*p0[2])*v12[2]+((p0[1]-p1[1])*v0[2]+v0[1]*p1[2]-v0[1]*p0[2])*v10[2]+((p0[0]-p1[0])*v12[0]+(p1[0]-p0[0])*v10[0])*v0[1]+(v0[0]*v12[0]-v0[0]*v10[0])*p1[1]+(v0[0]*v10[0]-v0[0]*v12[0])*p0[1])
        C = ((p1[1]-p0[1])*v12[1]+(p0[1]-p1[1])*v10[1]+(p1[0]-p0[0])*v12[0]+(p0[0]-p1[0])*v10[0])*v0[2]+(-v0[1]*v12[1]+v0[1]*v10[1]-v0[0]*v12[0]+v0[0]*v10[0])*p1[2]+(v0[1]*v12[1]-v0[1]*v10[1]+v0[0]*v12[0]-v0[0]*v10[0])*p0[2]

        vr = [A, B, C]

        vr = vr / np.linalg.norm(vr)

        if np.dot(v0, vr) > 0:
            return vr
        return -vr

    def _cartesian_from_spherical(self, theta: float, phi: float) -> np.array:
        """Get cartesian representation of a unitary vector from the spherical angles

        Args:
            theta (float): spherical theta angle
            phi (float): spherical phi angle

        Returns:
            x, y and z cartesian representation of unitary angle
        """

        return np.array([np.sin(theta) * np.cos(phi), np.sin(theta) * np.sin(phi), np.cos(theta)])

    def _get_vecs(self, avg_points: np.array, vec0: np.array) -> tuple[float, np.array]:
        """Get subdivision vectors from initial vector

        Args:
            vec0 (np.array): first subdivision vector
            avg_points (np.array): sudbivision points

        Returns:
            subdivision vectors and total error to minimize
        """

        vecs = [vec0 / np.linalg.norm(vec0)]
        errors = []

        for i in range(1, len(avg_points)-1):
            vecs.append(self._get_next_vec(vecs[-1], avg_points[i-1],
                                           avg_points[i], avg_points[i+1]))
            errors.append((1 - np.dot(vecs[-1], vecs[-2])) /
                          (100 * np.linalg.norm(avg_points[-1] - avg_points[-2])))
        vecs.append(vecs[-1])

        if len(errors) > 0:
            errors = np.array(errors)
            smoothmax_vec = np.exp(2 * errors)
            smoothmax = 100 * (smoothmax_vec * errors).sum() / smoothmax_vec.sum()
        else:
            smoothmax = 0

        error = (1 - np.linalg.norm(np.cross(vecs[0], self._normal)))**2 + 7 * smoothmax

        return error, np.array(vecs)

    def _get_vecs_multicard(self, avg_points: np.array, vec0: np.array) -> tuple[float, np.array]:
        vecs = [vec0 / np.linalg.norm(vec0)]
        errors = []

        for i in range(1, len(avg_points)-1):
            vecs.append(self._get_next_vec(vecs[-1], avg_points[i-1],
                                           avg_points[i], avg_points[i+1]))
            vec_lin = avg_points[i-1] - avg_points[i]
            vec_lin /= np.linalg.norm(vec_lin)
            errors.append([1 - np.abs(np.dot(vecs[-1], vecs[-2])),
                          np.abs(np.dot(vecs[-1], vec_lin))])
        vecs.append(vecs[-1])
        vec_lin = avg_points[-1] - avg_points[-2]
        vec_lin /= np.linalg.norm(vec_lin)
        errors.append([0, np.abs(np.dot(vecs[-1], vec_lin))])

        return np.max(errors), np.array(vecs)

    def _orientate_subd_vecs(self, cid: int):
        """Decide what is the front and the back side of the card

        Args:
            cid (int): card index
        """

        face_normals = np.cross(self._subd_vectors[cid][:-1], self._avg_curve_points[cid][1:] -
                                self._avg_curve_points[cid][:-1])

        if np.sum(np.dot(face_normals, self._normal)) < 0:
            self._subd_vectors[cid] = -self._subd_vectors[cid]

    def _get_vec_error(self, sph_angles: np.array, avg_points: np.array) -> float:
        """Get error from initial subdivision angle, for minimization

        Args:
            sph_angles (np.array): spherical theta and phi angles
            avg_points (np.array): subdivision points

        Returns:
            error for minimization
        """
        res, _ = self._get_vecs(avg_points, self._cartesian_from_spherical(sph_angles[0], sph_angles[1]))
        return res

    def optimize(self, cid: int) -> tuple[bool, HairCard]:
        """Optimize card from points

        Args:
            root_points (np.array): root points of all strands
            cid (int): card index
        """

        self._displace_card_top(cid)

        self.haircards[cid].faces = geometry.get_tris_from_vertex_grid(len(self._avg_curve_points[cid]), 2)

        max_dist = 0.01

        self.haircards[cid].top_vec = self._get_corrected_top_vec(cid) if self._use_multicard_clumps else None
        min_success = self._set_subdivision_vectors(cid, self.haircards[cid].top_vec)

        verts, uvs = self._get_verts_and_uvs([max_dist, max_dist], cid)

        # create a mesh to project the points and determine the width of the card
        from Modules.Texture.geometry import Mesh
        mesh = Mesh(torch.from_numpy(verts).float(),
                    torch.from_numpy(self.haircards[cid].faces).long(),
                    torch.from_numpy(uvs).float())
        projected_points = mesh.project_csplines(torch.from_numpy(
            self._curves[self._curve_indices[cid]]).float().view(-1, 3), 0.5)

        x_values = projected_points[:, 0]
        x_edges = np.array([np.nanmin(x_values), np.nanmax(x_values)])
        widths = np.array([self._margin + max_dist - x_edges[0], self._margin + x_edges[1] - max_dist])

        self.haircards[cid].verts, self.haircards[cid].uvs = self._get_verts_and_uvs(widths, cid)
        self.haircards[cid].width = widths.sum()
        self.haircards[cid].length = self.haircards[cid].uvs[-1, 1] - self.haircards[cid].uvs[0, 1]
        self.haircards[cid].width_lr_ratio = widths[0] / self.haircards[cid].width

        # Smooth vertices in case of pinching (subdivision sides are contra-parallel)
        n_side_verts = len(self.haircards[cid].verts) // 2

        if n_side_verts > 3:
            t_param = self.haircards[cid].uvs[::2, 1]

            if n_side_verts > 4:
                k = 3
            else:
                k = 2

            window_side = 2
            hamming = np.hamming(window_side * 2 + 1)
            hamming /= hamming.sum()

            def smooth_vertices(vertices, mask):
                mask[np.where(mask)[0] - 1] = True
                w = 1. - np.sqrt(np.convolve(mask.astype(float), hamming)[window_side:-window_side])
                s = 0.0001 * n_side_verts * self.haircards[cid].width**2
    
                for i in range(vertices.shape[-1]):
                    spline = UnivariateSpline(t_param, vertices[:, i], k=k, s=s, w=w)
                    vertices[:, i] = spline(t_param)

            vec_left = self.haircards[cid].verts[2:-2:2] - self.haircards[cid].verts[:-4:2]
            vec_right = self.haircards[cid].verts[3:-1:2] - self.haircards[cid].verts[1:-3:2]
            length_left = np.linalg.norm(vec_left, axis=-1)
            length_right = np.linalg.norm(vec_right, axis=-1)

            dp_sign = np.sign(np.sum(vec_left * vec_right, axis=-1))

            mask_left = np.concatenate(([False], (length_left < length_right) & (dp_sign < 0), [False]))
            mask_right = np.concatenate(([False], (length_left > length_right) & (dp_sign < 0), [False]))

            if mask_left.any():
                smooth_vertices(self.haircards[cid].verts[::2], mask_left)
            if mask_right.any():
                smooth_vertices(self.haircards[cid].verts[1::2], mask_right)

        return min_success, self.haircards[cid]
