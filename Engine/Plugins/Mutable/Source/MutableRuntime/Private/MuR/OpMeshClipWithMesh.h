// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Platform.h"

namespace mu
{
	class Mesh;
	class Image;
	class Layout;

    /** Generate a classification list for which vertex of pBase is fully contained in pClipMesh */
	extern void MeshClipMeshClassifyVertices(TBitArray<>& VertexInClipMesh, const Mesh* pBase, const Mesh* pClipMesh);

    /**  */
	extern void MeshClipWithMesh(Mesh* Result, const Mesh* pBase, const Mesh* pClipMesh, bool& bOutSuccess);

    /** Generate a mask mesh with the faces of the base mesh inside the clip mesh. */
	extern void MeshMaskClipMesh(Mesh* Result, const Mesh* pBase, const Mesh* pClipMesh, bool& bOutSuccess);

	/** Generate a mask mesh with the faces of the base mesh that have all 3 vertices marked in the given mask. */
	extern void MakeMeshMaskFromUVMask(Mesh* Result, const Mesh* Base, const Mesh* BaseForUVs, const Image* Mask, uint8 LayoutIndex, bool& bOutSuccess);

    /** Generate a mask mesh with the faces of the base mesh matching the fragment. */
	extern void MeshMaskDiff(Mesh* Result, const Mesh* pBase, const Mesh* pFragment, bool& bOutSuccess);

	/** Generate a mask mesh with the faces of the base mesh that have all 3 vertices inside any block of the given layout. */
	extern void MakeMeshMaskFromLayout(Mesh* Result, const Mesh* Base, const Mesh* BaseForUVs, const Layout* Mask, uint8 LayoutIndex, bool& bOutSuccess);

}
