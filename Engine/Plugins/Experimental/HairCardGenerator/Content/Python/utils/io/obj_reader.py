# -*- coding: utf-8 -*-
"""
Obj reader

@author: Denis Tome'

Copyright Epic Games, Inc. All Rights Reserved.

"""

import warnings
from typing import Optional
from collections import namedtuple

import torch

# Faces & Aux type returned from load_obj function.
_Faces = namedtuple("Faces", "verts_idx normals_idx textures_idx")
_Aux = namedtuple("Properties", "normals verts_uvs")


class ObjReader:
    """ObjReader"""

    @staticmethod
    def _format_faces_indices(faces_indices, max_index, device, pad_value=None):
        """
        Format indices and check for invalid values. Indices can refer to
        values in one of the face properties: vertices, textures or normals.
        See comments of the load_obj function for more details.

        Args:
            faces_indices: List of ints of indices.
            max_index: Max index for the face property.
            pad_value: if any of the face_indices are padded, specify
                the value of the padding (e.g. -1). This is only used
                for texture indices indices where there might
                not be texture information for all the faces.

        Returns:
            faces_indices: List of ints of indices.

        Raises:
            ValueError if indices are not in a valid range.
        """
        faces_indices = ObjReader._make_tensor(faces_indices,
                                               cols=3,
                                               dtype=torch.int64,
                                               device=device)

        if pad_value is not None:
            mask = faces_indices.eq(pad_value).all(dim=-1)

        # Change to 0 based indexing.
        faces_indices[(faces_indices > 0)] -= 1

        # Negative indexing counts from the end.
        faces_indices[(faces_indices < 0)] += max_index

        if pad_value is not None:
            faces_indices[mask] = pad_value

        return ObjReader._check_faces_indices(faces_indices, max_index, pad_value)

    @staticmethod
    def _check_faces_indices(faces_indices: torch.Tensor,
                             max_index: int,
                             pad_value: Optional[int] = None) -> torch.Tensor:
        if pad_value is None:
            mask = torch.ones(faces_indices.shape[:-1]).bool()  # Keep all faces
        else:
            # pyre-fixme[16]: `torch.ByteTensor` has no attribute `any`
            mask = faces_indices.ne(pad_value).any(dim=-1)
        if torch.any(faces_indices[mask] >= max_index) or torch.any(faces_indices[mask] < 0):
            warnings.warn("Faces have invalid indices")
        return faces_indices

    @staticmethod
    def _make_tensor(data, cols: int, dtype: torch.dtype, device: str = "cpu") -> torch.Tensor:
        """
        Return a 2D tensor with the specified cols and dtype filled with data,
        even when data is empty.
        """

        if not len(data):
            return torch.zeros((0, cols), dtype=dtype, device=device)

        return torch.tensor(data, dtype=dtype, device=device)

    @staticmethod
    def _parse_face(line, tokens, faces_verts_idx, faces_normals_idx, faces_textures_idx):

        face = tokens[1:]
        face_list = [f.split("/") for f in face]
        face_verts = []
        face_normals = []
        face_textures = []

        for vert_props in face_list:
            # Vertex index.
            face_verts.append(int(vert_props[0]))
            if len(vert_props) > 1:
                if vert_props[1] != "":
                    # Texture index is present e.g. f 4/1/1.
                    face_textures.append(int(vert_props[1]))
                if len(vert_props) > 2:
                    # Normal index present e.g. 4/1/1 or 4//1.
                    face_normals.append(int(vert_props[2]))
                if len(vert_props) > 3:
                    raise ValueError("Face vertices can ony have 3 properties. \
                                    Face vert %s, Line: %s" % (str(vert_props), str(line)))

        if len(face_normals) > 0:
            if not (len(face_verts) == len(face_normals)):
                raise ValueError("Face %s is an illegal statement. \
                            Vertex properties are inconsistent. Line: %s" %
                                 (str(face), str(line)))
        else:
            face_normals = [-1] * len(face_verts)  # Fill with -1
        if len(face_textures) > 0:
            if not (len(face_verts) == len(face_textures)):
                raise ValueError("Face %s is an illegal statement. \
                            Vertex properties are inconsistent. Line: %s" %
                                 (str(face), str(line)))
        else:
            face_textures = [-1] * len(face_verts)  # Fill with -1

        # Subdivide faces with more than 3 vertices.
        # See comments of the load_obj function for more details.
        for i in range(len(face_verts) - 2):
            faces_verts_idx.append((face_verts[0], face_verts[i + 1], face_verts[i + 2]))
            faces_normals_idx.append(
                (face_normals[0], face_normals[i + 1], face_normals[i + 2]))
            faces_textures_idx.append(
                (face_textures[0], face_textures[i + 1], face_textures[i + 2]))

    @staticmethod
    def load(file_path: str, device="cpu"):
        """Load obj

        Args:
            file_path (str): file path
        """
        verts, normals, verts_uvs = [], [], []
        faces_verts_idx, faces_normals_idx, faces_textures_idx = [], [], []

        with open(file_path, 'r') as f:
            lines = [line.strip() for line in f]

        # startswith expects each line to be a string. If the file is read in as
        # bytes then first decode to strings.
        if lines and isinstance(lines[0], bytes):
            lines = [el.decode("utf-8") for el in lines]

        for line in lines:
            tokens = line.strip().split()

            if line.startswith("v "):  # Line is a vertex.
                vert = [float(x) for x in tokens[1:4]]
                if len(vert) != 3:
                    msg = "Vertex %s does not have 3 values. Line: %s"
                    raise ValueError(msg % (str(vert), str(line)))
                verts.append(vert)
            elif line.startswith("vt "):  # Line is a texture.
                tx = [float(x) for x in tokens[1:3]]
                if len(tx) != 2:
                    raise ValueError("Texture %s does not have 2 values. Line: %s" %
                                     (str(tx), str(line)))
                verts_uvs.append(tx)
            elif line.startswith("vn "):  # Line is a normal.
                norm = [float(x) for x in tokens[1:4]]
                if len(norm) != 3:
                    msg = "Normal %s does not have 3 values. Line: %s"
                    raise ValueError(msg % (str(norm), str(line)))
                normals.append(norm)
            elif line.startswith("f "):  # Line is a face.
                ObjReader._parse_face(
                    line,
                    tokens,
                    faces_verts_idx,
                    faces_normals_idx,
                    faces_textures_idx,
                )

        verts = ObjReader._make_tensor(verts, cols=3, dtype=torch.float32, device=device)
        normals = ObjReader._make_tensor(normals, cols=3, dtype=torch.float32, device=device)
        verts_uvs = ObjReader._make_tensor(verts_uvs,
                                           cols=2,
                                           dtype=torch.float32,
                                           device=device)
        faces_verts_idx = ObjReader._format_faces_indices(faces_verts_idx,
                                                          verts.shape[0],
                                                          device=device)

        # Repeat for normals and textures if present.
        if len(faces_normals_idx):
            faces_normals_idx = ObjReader._format_faces_indices(faces_normals_idx,
                                                                normals.shape[0],
                                                                device=device,
                                                                pad_value=-1)
        if len(faces_textures_idx):
            faces_textures_idx = ObjReader._format_faces_indices(faces_textures_idx,
                                                                 verts_uvs.shape[0],
                                                                 device=device,
                                                                 pad_value=-1)

        faces = _Faces(verts_idx=faces_verts_idx,
                       normals_idx=faces_normals_idx,
                       textures_idx=faces_textures_idx)
        aux = _Aux(normals=normals if len(normals) else None,
                   verts_uvs=verts_uvs if len(verts_uvs) else None)

        return verts, faces, aux
