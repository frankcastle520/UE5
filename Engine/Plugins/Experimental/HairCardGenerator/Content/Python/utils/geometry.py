# -*- coding: utf-8 -*-
"""
Geometry utilities

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from typing import Union

import numpy as np
import torch
from scipy.spatial.distance import cdist, euclidean

from . import math as umath


def get_triangle_normal(points: np.array) -> np.array:
    """Compute normal from 3 points

    Args:
        points (numpy.array): points

    Returns:
        numpy.array: normal
    """

    assert len(points) == 3

    a = umath.normalize(points[1] - points[0])
    b = umath.normalize(points[2] - points[0])
    return umath.normalize(np.cross(a, b))


def compute_vertex_normals(vertices: np.array, faces: np.array) -> np.array:
    """Compute vertex normals

    Args:
        vertices (np.array): vertices
        faces (np.array): triangles

    Returns:
        np.array: vertex normals
    """

    # get vertices for faces
    mv = vertices[faces]
    # calculate face cross products (normals)
    # TODO: Use angle-weighted normals
    fn = np.cross(mv[:, 1] - mv[:, 0], mv[:, 2] - mv[:, 0])

    normals = np.zeros_like(vertices)
    np.add.at(normals, faces[:, 0], fn)
    np.add.at(normals, faces[:, 1], fn)
    np.add.at(normals, faces[:, 2], fn)
    normals /= np.linalg.norm(normals, axis=1)[:, np.newaxis]

    assert not np.any(np.isnan(normals)), "some normals are nan"
    return normals


def geometric_median(data_points, eps=1e-5):
    """Compute geometric median of a n dimensional set of p points.
    See https://www.pnas.org/content/pnas/97/4/1423.full.pdf
    Vardi and Zhang. The multivariate L1-median and associated data depth. 1999.
    Args:
        data_points (np.ndarray): Set of n dimensional points.
    Return
        n dimensional median.
    """
    # Initialize median as the mean.
    median_point = np.mean(data_points, 0)

    while True:
        # Compute distance to all points from the current median.
        D = cdist(data_points, [median_point])
        nonzeros = (D != 0)[:, 0]

        Dinv = 1 / D[nonzeros]
        Dinvs = np.sum(Dinv)
        W = Dinv / Dinvs
        T = np.sum(W * data_points[nonzeros], 0)

        num_zeros = len(data_points) - np.sum(nonzeros)
        if num_zeros == 0:
            cur_median_point = T
        elif num_zeros == len(data_points):
            return median_point
        else:
            R = (T - median_point) * Dinvs
            r = np.linalg.norm(R)
            if r == 0:
                rinv = 0
            else:
                rinv = num_zeros / r
            cur_median_point = max(0, 1 - rinv) * T + min(1,
                                                          rinv) * median_point

        if euclidean(median_point, cur_median_point) < eps:
            return cur_median_point

        median_point = cur_median_point


def get_R_aligning_vectors(a: np.array, b: np.array) -> np.array:
    """Get the rotation matrix to align vector a to vector b

    Args:
        a (np.array): vector
        b (np.array): vector

    Returns:
        np.array: rotation matrix
    """

    # check that are 1d vectors of n dimensions
    assert a.ndim == 1
    assert b.ndim == 1

    a = umath.unit_vector(a)
    b = umath.unit_vector(b)

    # get ab plane normal
    n = np.cross(a, b)

    # angle between vectors
    angle_ab = np.arccos(np.dot(a, b))

    R = umath.rotation_matrix(angle_ab, n)[:3, :3]
    return R


def project_vector_onto_plane(a: np.array, b: np.array,
                              v: np.array) -> np.array:
    """Project vector v onto plane defined by vectors a and b

    Args:
        a (np.array): vecotor a on the plane
        b (np.array): vector b on the plane
        v (np.array): vector to be projected

    Returns:
        np.array: projected vector onto the plane
    """

    # find normal to the plane
    n = umath.unit_vector(np.cross(a, b))

    # component of v based on normal n
    v_n = np.dot(v, n) * n

    # get v onto plane
    v_plane = v - v_n

    return v_plane


def normal_vector_point_cloud(p_cloud: np.array, p: np.array = None, n: int = None) -> np.array:
    """Given a cloud of points and a point p, return the tangent of the plain
       best fitting the n elements from the cloud closest to p

    Args:
        p_cloud (np.array): full point cloud
        p (np.array): reference point
        n (int): number of point of the cloud used to compute the tangent

    Returns:
        np.array: tangent vector
    """

    if p is None or n > len(p_cloud) - 1:
        n = len(p_cloud) - 1

    distances = np.linalg.norm(p_cloud[:, None] - p, axis=-1)

    close_points = p_cloud[np.argpartition(distances, n, axis = 0)[:n]]
    close_points = np.moveaxis(close_points, 0, 2)

    tan_vector = np.linalg.svd(close_points - np.mean(close_points, axis=-1, keepdims=True))[0][:, :, -1]

    return tan_vector

def get_R(vec, c):
    """Get 3D rotation axis.

    Args:
        vec (np.array): rotation axis
        c (float): cosine of rotation angle

    Returns:
        np.array: rotation matrix
    """

    C = 1 - c
    s = np.sqrt(1-c**2)

    return np.array([[vec[0]**2*C+c, vec[0]*vec[1]*C-vec[2]*s, vec[0]*vec[2]*C+vec[1]*s],
                     [vec[1]*vec[0]*C+vec[2]*s, vec[1]**2*C+c, vec[1]*vec[2]*C-vec[0]*s],
                     [vec[2]*vec[0]*C-vec[1]*s, vec[2]*vec[1]*C+vec[0]*s, vec[2]**2*C+c]])

def linear_interp_between_points(a: np.array, b: np.array,
                                 t: Union[float, list]) -> np.array:
    """Linearly interpolate between two n-dimensional points A and B based on the
    values contained in t.

    Assumption: t is ordered from small to larger values;

    Args:
        a (np.array): point A
        b (np.array): point B
        t (Union[float, list]): describe the single or multiple interpolation factors;
                                Defined in [0, 1], where 0 returns point A, and 1.0
                                retuns value of point B.
                                If a list is given, multiple interpolations will be executed,
                                one for each element in t.

    Returns:
        np.array: array of size (len(t) X n-dim) where element in position 0
                  is the result of the interpolation with t[0].
    """

    # check interpolation factors
    if isinstance(t, list):
        assert len(t) > 1, "list with one element only"
        assert t[0] < t[-1]
    else:
        t = [t]

    t = np.array(t, dtype=float)
    assert np.min(t) >= 0.0, "Interpolation factor < 0 is not allowed"
    assert np.max(t) <= 1.0, "Interpolation factor > 1.0 is not allowed"

    # check points
    assert a.ndim == 1, "Point A should have 1 dimension"
    assert b.ndim == 1, "Point B should have 1 dimension"
    assert a.shape == b.shape, "A and B shapes are not matching"

    # interpolate
    diff = b - a
    values = np.zeros([len(t), len(a)])
    for idx, int_factor in enumerate(t):
        values[idx] = a + int_factor * diff

    return values


def get_tris_from_vertex_grid(n_vertical, n_horizontal) -> np.array:
    """Get faces for a grid of size (n_vertical x n_horizontal).
    The order of the vertices is left to right, top to bottom and we
    follow an counter-clock wise direction in building the faces.

    Args:
        n_vertical (int): number of vertices vertically
        n_horizontal (int): number of vertices horizontally

    Returns:
        np.array: faces
    """

    assert n_vertical >= 2, "at least 2 verts required"
    assert n_horizontal >= 2, "at least 2 verts required"

    faces = []
    for v_id in range(n_vertical - 1):
        for h_id in range(n_horizontal - 1):
            idx = v_id * n_horizontal + h_id

            faces.append([idx, idx + n_horizontal, idx + n_horizontal + 1])
            faces.append([idx + n_horizontal + 1, idx + 1, idx])

            # if v_id % 2:
            #     faces.append([idx, idx + n_horizontal, idx + n_horizontal + 1])
            #     faces.append([idx + n_horizontal + 1, idx + 1, idx])
            #
            # else:
            #     faces.append([idx + n_horizontal, idx + n_horizontal + 1, idx + 1])
            #     faces.append([idx + 1, idx, idx + n_horizontal])
    return np.array(faces, dtype=np.int64)


def get_uvs_from_vertex_grid(n_vertical: int, n_horizontal: int) -> np.array:
    """Get uvs for a grid of size (n_vertical x n_horizontal).
    The order of the vertices is left to right, top to bottom and we
    follow an counter-clock wise direction in building the faces.

    Args:
        n_vertical (int): number of vertices vertically
        n_horizontal (int): number of vertices horizontally

    Returns:
        np.array: uv coordinates
    """

    assert n_vertical >= 2, "at least 2 verts required"
    assert n_horizontal >= 2, "at least 2 verts required"

    uu, vv = np.meshgrid(np.arange(n_horizontal), np.arange(n_vertical))
    uu = uu / (n_horizontal - 1)
    vv = vv / (n_vertical - 1)

    uv = np.stack([uu.flatten(), vv.flatten()], axis=1)
    return uv


def get_uvs(vert_seg_lengths: np.array, width: float) -> np.array:
    """Get uv coordinates, with the card scale

    Args:
        vert_seg_lengths (np.array): cumulative segment vert_seg_lengths
        width (float): card width

    Returns:
        np.array: uv coordinates
    """

    uu, vv = np.meshgrid([0, width], vert_seg_lengths)
    uv = np.stack([uu.flatten(), vv.flatten()], axis=1)

    return uv

def get_R_to_match_x(vector: np.array) -> np.array:
    """Get rotation matrix that applied to the input vector v ensures
    that the largest dimension of v is aligned with the x axis.

    Args:
        vector (np.array): input vector

    Returns:
        np.array: rotation matrix
    """
    def axis_id(t):
        return np.argmax(np.abs(t))

    assert vector.ndim == 1, "should be a 1D vector"
    target_pos = axis_id(vector)

    if target_pos == 0:
        # x-axis is already at the right place. No
        # transformation is needed.
        return np.eye(3)

    if target_pos == 1:
        # expected x-axis is aligned with the y-axis.
        # let's rotate accordingly without being sure about the other axes
        # rotating around z by 90 moved x in the right direction
        R = umath.euler_matrix(0.0, 0.0, np.pi / 2)[:3, :3]

        rotated = np.dot(R, vector)
        assert axis_id(rotated) == 0, "Should have solved that"

        return R

    # expected x-aix is aligned with the z-axis
    # rotating around z by 90 moved x in the right direction
    R = umath.euler_matrix(0.0, np.pi / 2, 0.0)[:3, :3]

    rotated = np.dot(R, vector)
    assert axis_id(rotated) == 0, "Should have solved that"

    return R


def get_R_to_match_z(approx_normal: np.array) -> np.array:
    """Get rotation matrix that applied to the input approximated
    normal aligns it with the z axis.

    Args:
        approx_normal (np.array): approximated normal of a card

    Returns:
        np.array: rotation matrix
    """

    # choose the axis among y,z closest to the normal
    theta_y_pos = umath.angle_between_vectors(approx_normal,
                                              np.array([0, 1, 0]),
                                              directed=True)
    theta_z_pos = umath.angle_between_vectors(approx_normal,
                                              np.array([0, 0, 1]),
                                              directed=True)
    theta_y_neg = umath.angle_between_vectors(approx_normal,
                                              np.array([0, -1, 0]),
                                              directed=True)
    theta_z_neg = umath.angle_between_vectors(approx_normal,
                                              np.array([0, 0, -1]),
                                              directed=True)

    all_thetas = [theta_y_pos, theta_z_pos, theta_y_neg, theta_z_neg]
    min_theta = np.argmin(all_thetas)

    if min_theta == 0:
        # y axis is what we expected as z axis. Rotate around
        # the x axis for +90
        return umath.euler_matrix(np.pi / 2, 0.0, 0.0)[:3, :3]

    if min_theta == 1:
        # z is already the axis aligned with the normal as desired
        return np.eye(3)

    if min_theta == 2:
        # -y axis is what we expected as z axis. Rotate around
        # the x axis for -90
        return umath.euler_matrix(-np.pi / 2, 0.0, 0.0)[:3, :3]

    # -z axis is what we expected as z axis. Rotate around
    # the x axis for +180
    return umath.euler_matrix(np.pi, 0.0, 0.0)[:3, :3]


def get_mesh_laplacian(verts: torch.Tensor,
                       faces: torch.Tensor) -> torch.Tensor:
    """Get Mesh Laplacian

    Args:
        verts (torch.Tensor): mesh vertices
        faces (torch.Tensor): mesh faces

    Returns:
        torch.Tensor: Laplacian
    """

    # get vertices for faces
    mv = verts[faces]

    # get all edges
    edges = torch.stack(
        [mv[:, 1] - mv[:, 0], mv[:, 2] - mv[:, 1], mv[:, 0] - mv[:, 2]], dim=1)

    # calculate area
    area = (edges[:, 0].cross(-edges[:, 2])).norm(dim=1) * 0.5

    # calculate angle dot product
    edges /= edges.norm(dim=2).unsqueeze(2)
    angle = torch.stack([(-edges[:, 2] * edges[:, 0]).sum(dim=1),
                         (-edges[:, 0] * edges[:, 1]).sum(dim=1),
                         (-edges[:, 1] * edges[:, 2]).sum(dim=1)],
                        dim=1)

    # calculate cotan of all
    cotan = angle / torch.sqrt(1.0 - angle * angle)

    # create sparse matrix with cotan entries
    n_vertices = len(verts)
    indices = torch.stack([faces[:, [1, 2, 0]], faces[:, [2, 0, 1]]],
                          dim=0).view(2, -1)
    L = torch.sparse_coo_tensor(indices, cotan.view(-1),
                                (n_vertices, n_vertices))
    L += L.t()

    # add diagonal to matrix
    d = torch.sparse.sum(L, dim=0).to_dense()
    d_indices = torch.stack([
        torch.arange(0, n_vertices, dtype=torch.int64),
        torch.arange(0, n_vertices, dtype=torch.int64)
    ], dim=0).view(2, -1)
    L -= torch.sparse_coo_tensor(d_indices, d, (n_vertices, n_vertices))
    L = L.coalesce()

    # multiply with the inverse area matrix
    vertex_area = torch.zeros(n_vertices)
    vertex_area[faces[:, 0]] += area
    vertex_area[faces[:, 1]] += area
    vertex_area[faces[:, 2]] += area
    inv_area = 3.0 / vertex_area

    # multiply L's rows by the inverse area and return L
    return torch.sparse.FloatTensor(L.indices(),
                                    L.values() * inv_area[L.indices()[0]],
                                    (n_vertices, n_vertices)).coalesce()
