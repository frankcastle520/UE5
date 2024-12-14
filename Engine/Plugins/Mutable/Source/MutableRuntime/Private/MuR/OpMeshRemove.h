// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuR/Ptr.h"
#include "HAL/Platform.h"

#include "Containers/BitArray.h"

namespace mu
{
	class Mesh;

	/** Remove a list of vertices and related faces from a mesh. The list of vertices is stored in a specially formatted Mask mesh. 
	 * If bRemoveIfAllVerticesCulled is true, a face is remove if all its vertices have the bit set in VerticesToCull.
	 * If bRemoveIfAllVerticesCulled is false, remove a face if at least one vertex is removed.
	 */
	extern void MeshRemoveMask(Mesh* Result, const Mesh* Source, const Mesh* Mask, bool bRemoveIfAllVerticesCulled, bool& bOutSuccess);

	/** 
	 * Remove a set of vertices and related faces from a mesh in-place. VertexToCull is a bitset where if bit i-th is set, 
	 * the vertex i-th will be removed if all faces referencing this vertex need to be removed. 
	 * If bRemoveIfAllVerticesCulled is true, a face is remove if all its vertices have the bit set in VerticesToCull. 
	 * If bRemoveIfAllVerticesCulled is false, remove a face if at least one vertex is removed.
	 */
	extern void MeshRemoveVerticesWithCullSet(Mesh* Result, const TBitArray<>& VerticesToCull, bool bRemoveIfAllVerticesCulled);

	/**
	 * Recreates the Surface and Surfaces Submeshes given a set of vertices and faces remaining after mesh removal.
	 */
	extern void MeshRemoveRecreateSurface(Mesh* Result, const TBitArray<>& UsedVertices, const TBitArray<>& UsedFaces);
}
