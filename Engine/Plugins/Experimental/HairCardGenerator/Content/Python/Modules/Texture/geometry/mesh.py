# -*- coding: utf-8 -*-
"""
Mesh class

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations

import torch
from torchcubicspline import natural_cubic_spline_coeffs, NaturalCubicSpline
from utils.io.obj_reader import ObjReader
from utils.io.obj_writer import ObjWriter


class Mesh:
    """ Mesh class, stores vertices, faces, and skinweights """

    def __init__(self,
                 vertices: torch.Tensor = None,
                 faces: torch.Tensor = None,
                 uvs: torch.Tensor = None,
                 txt_idx: torch.Tensor = None,
                 aspect_threshold: float = 1.5,
                 edge_threshold: float = 0.25) -> None:
        """Create a new mesh with the given vertices, faces.

        Aspect_threshold and edge_threshold are parameters used
        to control the mesh subdivision."""

        # ------------------- subdivision params ------------------- #

        self.aspect_threshold = aspect_threshold
        self.edge_threshold = edge_threshold

        # ------------------- mesh data ------------------- #

        if vertices is None:
            self.vertices = torch.empty((0, 3),
                                        dtype=torch.float32,
                                        device=torch.device("cpu"))
        else:
            self.vertices = vertices

        if faces is None:
            self.faces = torch.empty((0, 3), dtype=torch.int64, device=torch.device("cpu"))
        else:
            self.faces = faces

        if vertices is None:
            self.uvs = torch.empty((0, 2), dtype=torch.float32, device=torch.device("cpu"))
        else:
            self.uvs = uvs

        if txt_idx is None:
            self.txt_idx = torch.empty((0, 3), dtype=torch.float32, device=torch.device("cpu"))
        else:
            self.txt_idx = txt_idx

        # ------------------- properties ------------------- #

        self.normals = None
        self.tangents = None
        self.face_normals = None

        if vertices is not None:
            if faces is not None:
                self.estimate_tangent_space()

        # set types
        self.dtype = self.vertices.dtype
        self.device = self.vertices.device

        # sanity check
        assert self.vertices.shape[-1] == 3
        assert self.faces.shape[-1] == 3
        assert self.vertices.device == self.faces.device

    def to(self, dtype_or_device) -> Mesh:
        """Convert class to different dtype or change storage device"""
        # casts structure to type or returns dtype without parameters
        result = Mesh()

        for k in vars(self):
            if isinstance(getattr(self, k), torch.Tensor):
                if isinstance(dtype_or_device, torch.dtype) and getattr(
                        self, k).dtype == torch.int64:
                    setattr(result, k, getattr(self, k).clone())
                else:
                    setattr(result, k, getattr(self, k).to(dtype_or_device))
            else:
                setattr(result, k, getattr(self, k))

        result.dtype = result.vertices.dtype
        result.device = result.vertices.device

        return result

    def clone(self) -> Mesh:
        """Return a deep copy of this"""
        # create a clone of the structure
        result = Mesh()

        for k in vars(self):
            if isinstance(getattr(self, k), torch.Tensor):
                setattr(result, k, getattr(self, k).clone())
            else:
                setattr(result, k, getattr(self, k))

        result.dtype = result.vertices.dtype
        result.device = result.vertices.device

        return result

    def estimate_tangent_space(self):
        """Update the vertex normals and tangents using area-weighting"""
        # get vertices for faces
        mv = self.vertices[self.faces]

        # Get tri-edges (physical space)
        e1 = mv[:, [1]] - mv[:, [0]]
        e2 = mv[:, [2]] - mv[:, [0]]
        e3 = mv[:, [2]] - mv[:, [1]]

        # Compute face-angles per vertex
        en1 = e1 / e1.norm(dim=2).unsqueeze(2)
        en2 = e2 / e2.norm(dim=2).unsqueeze(2)
        en3 = e3 / e3.norm(dim=2).unsqueeze(2)

        e = torch.cat([en1,en2,en3], dim=1)
        ep = torch.cat([en2,en3,-en1], dim=1)

        cosangle = torch.clamp(torch.sum(e*ep, dim=2), -1.0,1.0)
        angle = torch.acos(cosangle)

        # Normalize face-normal
        fn = torch.linalg.cross(e1,e2, dim=2)
        fn = fn / fn.norm(dim=2).unsqueeze(2)

        # Use angle-weighted vertex normals
        weighted_fn = fn * angle.unsqueeze(2)

        # sum up vertex normals
        self.normals = torch.zeros_like(self.vertices)
        self.normals.index_add_(0, self.faces[:, 0], weighted_fn[:,0])
        self.normals.index_add_(0, self.faces[:, 1], weighted_fn[:,1])
        self.normals.index_add_(0, self.faces[:, 2], weighted_fn[:,2])
        self.normals /= self.normals.norm(dim=1).unsqueeze(1)

        # Get uvs and normals per-face
        nv = self.normals[self.faces]
        uv = self.uvs[self.faces]

        # Get tri-edges (uv space)
        u1 = (uv[:,1] - uv[:,0]).unsqueeze(1)
        u2 = (uv[:,2] - uv[:,0]).unsqueeze(1)

        ft = u2[:,:,1].unsqueeze(2)*e1 - u1[:,:,1].unsqueeze(2)*e2

        # Project each tangent into vertex-normal's tangent plane
        ftn = nv*torch.sum(ft*nv, dim=2).unsqueeze(2)
        ftp = ft - ftn
        ftp /= ftp.norm(dim=2).unsqueeze(2)

        weighted_ftp = ftp * angle.unsqueeze(2)

        # Sum up vertex tangents
        self.tangents = torch.zeros_like(self.vertices)
        self.tangents.index_add_(0, self.faces[:, 0], weighted_ftp[:,0])
        self.tangents.index_add_(0, self.faces[:, 1], weighted_ftp[:,1])
        self.tangents.index_add_(0, self.faces[:, 2], weighted_ftp[:,2])
        self.tangents /= self.tangents.norm(dim=1).unsqueeze(1)

    def load_obj(self, path: str) -> None:
        """Load obj"""

        verts, face_info, aux = ObjReader.load(path)

        self.vertices = verts
        self.faces = face_info.verts_idx
        self.uvs = aux.verts_uvs
        self.txt_idx = face_info.textures_idx

        self.estimate_tangent_space()

    def save_to_obj(self, path: str) -> None:
        """Save mesh to obj"""

        verts = self.vertices.detach().numpy()
        faces = self.faces.detach().numpy()
        uvs = self.uvs.detach().numpy()

        ObjWriter.save_mesh(path, verts, faces, uvs)

    def project_csplines(self, points: torch.Tensor, lr_ratio: float, ignore_depth: bool = True):
        """Project points onto card using csplines method"""

        # make t parameter array using the v coordinates of the vertices,
        # expanding the projection area to the back to cover all points
        t = torch.cat((-self.uvs[2, 1][None], self.uvs[::2, 1])).contiguous()

        p_prev = 2 * self.vertices[:2] - self.vertices[2:4]
        vertices_exp = torch.cat((p_prev, self.vertices), dim=0)

        # create cubic splines for left and right card borders
        cs_left = NaturalCubicSpline(natural_cubic_spline_coeffs(t, vertices_exp[::2]))
        cs_right = NaturalCubicSpline(natural_cubic_spline_coeffs(t, vertices_exp[1::2]))
        cs_mean = NaturalCubicSpline(natural_cubic_spline_coeffs(t, (1 - lr_ratio) *
                                                                 vertices_exp[::2] +
                                                                 lr_ratio *
                                                                 vertices_exp[1::2]))

        # create the reference mean points
        n_mean_points = 100
        t_mean = torch.linspace(t[0], t[-1], n_mean_points)
        pts_mean = torch.Tensor(cs_mean.evaluate(t_mean))

        # make points-to-mean square distance array and get the minimum
        d2 = torch.sum((points[:, None] - pts_mean[None])**2, axis=-1)
        argmin = torch.argmin(d2, dim=1).clip(1, n_mean_points-2)

        # from the minimum point and it's two adjacents, do a quadratic interpolation to get v
        idx = torch.arange(argmin.size()[0])
        d2_3min = torch.stack((d2[idx, argmin-1], d2[idx, argmin], d2[idx, argmin+1]), dim=0)
        t_3min = torch.stack((t_mean[argmin-1], t_mean[argmin], t_mean[argmin+1]), dim=0)

        denom = (t_3min[0] - t_3min[1]) * (t_3min[0] - t_3min[2]) * (t_3min[1] - t_3min[2])
        A = (t_3min[2] * (d2_3min[1] - d2_3min[0]) + t_3min[1] *
             (d2_3min[0] - d2_3min[2]) + t_3min[0] * (d2_3min[2] - d2_3min[1])) / denom
        B = (t_3min[2]*t_3min[2] * (d2_3min[0] - d2_3min[1]) + t_3min[1]*t_3min[1] *
             (d2_3min[2] - d2_3min[0]) + t_3min[0]*t_3min[0] * (d2_3min[1] - d2_3min[2])) / denom
        v = (-B / (2 * A)).clip(t[0], t[-1])

        # get u from the distance to left and right border
        p_left = cs_left.evaluate(v)
        p_right = cs_right.evaluate(v)
        d2_left = torch.sum((p_left - points)**2, axis=-1)
        d2_right = torch.sum((p_right - points)**2, axis=-1)
        u = (self.uvs[1, 0]**2 + d2_left - d2_right) / (2 * self.uvs[1, 0])

        # if there are points with v coordinate smaller than 0, make them be 0 by applying
        # a smooth transformation
        if v.min() < 0:
            v -= v.min() * torch.exp(2.0 * (v - v.min()) / t[0])

        uv = torch.stack((u, v), dim=-1)

        if ignore_depth:
            return uv

        # calculate depth
        u_norm = u / self.uvs[1, 0]
        projected_points = u_norm[:, None] * p_right + (1 - u_norm[:, None]) * p_left

        vec_disp = points - projected_points
        vec_plane = torch.cross(cs_left.evaluate(v + 1e-1) - p_left, p_right - p_left, dim=-1)

        depth = torch.linalg.vector_norm(vec_disp, axis=-1) * torch.sign(torch.sum(vec_disp * vec_plane, axis=-1))

        return uv, depth

    def validate(self) -> bool:
        """Validate mesh"""

        if torch.any(torch.isnan(self.vertices)):
            return False

        if torch.any(torch.isnan(self.faces)):
            return False

        return True
