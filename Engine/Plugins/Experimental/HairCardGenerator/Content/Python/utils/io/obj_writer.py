# -*- coding: utf-8 -*-
"""
Obj writer

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

from __future__ import annotations

from typing import Optional, Union
from io import IOBase
import numpy as np


class ObjWriter:
    """ObjWriter"""

    @staticmethod
    def _save(f: IOBase,
              verts: np.array,
              faces: np.array,
              uvs: Optional[np.array],
              normals: Optional[np.array],
              decimal_places: Optional[int] = None) -> None:
        """Save"""

        if decimal_places is None:
            float_str = "%f"
        else:
            float_str = "%" + ".%df" % decimal_places

        # write vertex positions
        lines = ""
        if len(verts) > 0:
            V, D = verts.shape
            for i in range(V):
                vert = [float_str % verts[i, j] for j in range(D)]
                lines += "v %s\n" % " ".join(vert)

            if np.any(faces >= verts.shape[0]) or np.any(faces < 0):
                raise Exception("Faces have invalid indices")

        if (uvs is not None) or (normals is not None) or len(faces) > 0:
            if decimal_places is None:
                float_str = "%f"
            else:
                float_str = "%" + ".%df" % decimal_places

            # UV coordinates
            if uvs is not None:
                U, D = uvs.shape
                for i in range(U):
                    vts = [float_str % uvs[i, j] for j in range(D)]
                    lines += "vt %s\n" % " ".join(vts)

            # vertex normals
            if normals is not None:
                N, D = normals.shape
                for i in range(N):
                    vts = [float_str % normals[i, j] for j in range(D)]
                    lines += "vn %s\n" % " ".join(vts)

            # faces
            F, P = faces.shape
            for i in range(F):
                f_line = []
                for j in range(P):
                    # vertex id
                    vid = faces[i, j]
                    face = '{:d}'.format(vid + 1)
                    if uvs is not None:
                        face += '/{:d}'.format(vid + 1)
                        # face += '/{:d}'.format((i * P) + j + 1)
                    if normals is not None:
                        if uvs is None:
                            face += '/'
                        face += '/{:d}'.format(vid + 1)
                        # face += '/{:d}'.format((i * P) + j + 1)
                    f_line.append(face)

                if i + 1 < F:
                    lines += "f %s\n" % " ".join(f_line)
                elif i + 1 == F:
                    # No newline at the end of the file.
                    lines += "f %s" % " ".join(f_line)

        f.write(lines)

    @staticmethod
    def save_mesh(f: Union[str, IOBase],
                  verts: np.array,
                  faces: np.array,
                  uv_coordinates: Optional[np.array] = None,
                  vertex_normals: Optional[np.array] = None,
                  decimal_places: Optional[int] = None) -> None:
        """
        Save a mesh to an obj file.
        Args:
            f (str or IOBase): file to which the mesh should be written.
            verts (np.array): FloatTensor of shape (V, 3) giving vertex coordinates.
            faces (np.array): LongTensor of shape (F, 3) giving faces.
            uvs (np.array, optional): UV coordinates of shape (V, 3).
            vertex_normals (np.array, optional): vertex normals of shape (V, 3).
            decimal_places (int, optional): Number of decimal places for saving.
        """

        if len(verts) > 0 and not (verts.ndim == 2 and verts.shape[1] == 3):
            message = "Argument 'verts' should either be empty or of shape (num_verts, 3)."
            raise ValueError(message)

        if len(faces) > 0 and not (faces.ndim == 2 and faces.shape[1] == 3):
            message = "Argument 'faces' should either be empty or of shape (num_faces, 3)."
            raise ValueError(message)

        if uv_coordinates is not None:
            assert uv_coordinates.ndim == 2
            if not (uv_coordinates.shape[1] == 2 and
                    uv_coordinates.shape[0] == verts.shape[0]):
                message = "Argument 'uvs' should either be empty or of shape (num_verts, 2)."
                raise ValueError(message)

        if vertex_normals is not None:
            assert vertex_normals.ndim == 2
            if not (vertex_normals.shape[1] == 3 and
                    vertex_normals.shape[0] == verts.shape[0]):
                message = "Argument 'vertex_normals' should either be empty or of shape (vertex_normals, 3)."
                raise ValueError(message)

        # check if f is already a file, not a file path
        if isinstance(f, IOBase):
            return ObjWriter._save(f, verts, faces, uv_coordinates, vertex_normals,
                                   decimal_places)

        with open(f, "w") as save_file:
            return ObjWriter._save(save_file, verts, faces, uv_coordinates,
                                   vertex_normals, decimal_places)
