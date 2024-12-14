// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BoneIndices.h"
#include "Containers/Array.h"
#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Logging/LogMacros.h"
#include "MeshTypes.h"
#include "StaticMeshOperations.h"

struct FMeshDescription;


DECLARE_LOG_CATEGORY_EXTERN(LogSkeletalMeshOperations, Log, All);



class FSkeletalMeshOperations : public FStaticMeshOperations
{
public:
	struct FSkeletalMeshAppendSettings
	{
		FSkeletalMeshAppendSettings()
			: SourceVertexIDOffset(0)
		{}

		int32 SourceVertexIDOffset;
		TArray<FBoneIndexType> SourceRemapBoneIndex;
		bool bAppendVertexAttributes = false;
	};
	
	static SKELETALMESHDESCRIPTION_API void AppendSkinWeight(const FMeshDescription& SourceMesh, FMeshDescription& TargetMesh, FSkeletalMeshAppendSettings& AppendSettings);

	/** Copies skin weight attribute from one mesh to another. Assumes the two geometries are identical or near-identical.
	 *  Uses closest triangle on the target mesh to interpolate skin weights to each of the points on the target mesh.
	 *  Attributes for the given profiles on both meshes should exist in order for this function to succeed. 
	 *  @param InSourceMesh The mesh to copy skin weights from.
	 *  @param InTargetMesh The mesh to copy skin weights to.
	 *  @param InSourceProfile The name of the skin weight profile on the source mesh to read from.
	 *  @param InTargetProfile The name of the skin weight profile on the target mesh to write to.
	 *  @param SourceBoneIndexToTargetBoneIndexMap An optional mapping table to map bone indexes on the source mesh to the target mesh.
	 *     The table needs to be complete for all the source bone indexes to valid target bone indexes, otherwise the behavior
	 *     is undefined. If the table is not given, the bone indexes on the source and target meshes are assumed to be the same.
	 */
	static SKELETALMESHDESCRIPTION_API bool CopySkinWeightAttributeFromMesh(
		const FMeshDescription& InSourceMesh,
		FMeshDescription& InTargetMesh,
		const FName InSourceProfile,
		const FName InTargetProfile,
		const TMap<int32, int32>* SourceBoneIndexToTargetBoneIndexMap
		);

	/** Remaps the bone indices on all skin weight attributes from one index to another. The array view should contain a full mapping of all the
	 *  bone indices contained in the skin weights. The array is indexed by the current bone index and the value at that index is the new bone index.
	 *  If the mapping is incomplete or if two entries map to the same bone, the result is undefined. No prior checking is performed.
	 *  @param InMesh The mesh on which to modify all skin weight attributes to remap their bone indices.
	 *  @param InBoneIndexMapping The mapping from one bone index to another. The old bone index is used to index into the array, the value at that
	 *     position is the new bone index.
	 *  @return true if the operation was successful. If the mapping array was incomplete then this will return false. If there are no skin weight
	 *     attributes on the mesh, then the operation is still deemed successful.
	 */  
	static SKELETALMESHDESCRIPTION_API bool RemapBoneIndicesOnSkinWeightAttribute(
		FMeshDescription& InMesh,
		TConstArrayView<int32> InBoneIndexMapping
		);

	/** Returns a mesh in the pose given by the component-space transforms passed in. The list of transforms should match exactly
	 *  the list of bones stored on the mesh. If not, the function fails and returns \c false.
	 *  If there are no skin weights on the mesh, or the named skin weight profile doesn't exist, the function also returns \c false.
	 *  The resulting bones on the mesh will have their bone-space transforms updated so that the same mesh can be re-posed as needed.
	 *  @param InSourceMesh The mesh to deform.
	 *  @param OutTargetMesh The deformed mesh result.
	 *  @param InComponentSpaceTransforms The component space transforms used to move the mesh joints for deforming, using linear-blend skinning.
	 *  @param InSkinWeightProfile The skin weight profile to use as the source of skin weights for the deformation.
	 *  @param InMorphTargetWeights Optional morph target weights to apply. Any morph target that doesn't exist is ignored.
	 *  @return \c true if the operation succeeded.
	 */
	static SKELETALMESHDESCRIPTION_API bool GetPosedMesh(
		const FMeshDescription& InSourceMesh,
		FMeshDescription& OutTargetMesh,
		TConstArrayView<FTransform> InComponentSpaceTransforms,
		const FName InSkinWeightProfile = NAME_None,
		const TMap<FName, float>& InMorphTargetWeights = {}
		);

	/** Returns a mesh in the pose given by the bone-space transforms passed in. The transforms simply replace the matching ref pose transforms
	 *  stored in the bone data on the mesh. Any named transform, that does not match a bone on the mesh, is ignored.  
	 *  If there are no skin weights on the mesh, or the named skin weight profile doesn't exist, the function also returns \c false.
	 *  The resulting bones on the mesh will have their bone-space transforms updated so that the same mesh can be re-posed as needed.
	 *  
	 *  @param InSourceMesh The mesh to deform.
	 *  @param OutTargetMesh The deformed mesh result.
	 *  @param InBoneSpaceTransforms A map of named bone-space transforms.
	 *  @param InSkinWeightProfile The skin weight profile to use as the source of skin weights for the deformation.
	 *  @param InMorphTargetWeights Optional morph target weights to apply. Any morph target that doesn't exist is ignored.
	 *  @return \c true if the operation succeeded.
	 */
	static SKELETALMESHDESCRIPTION_API bool GetPosedMesh(
		const FMeshDescription& InSourceMesh,
		FMeshDescription& OutTargetMesh,
		const TMap<FName, FTransform>& InBoneSpaceTransforms, 
		const FName InSkinWeightProfile = NAME_None,
		const TMap<FName, float>& InMorphTargetWeights = {}
		);
};
