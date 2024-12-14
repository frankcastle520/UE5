// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Logging/LogMacros.h"

struct FManagedArrayCollection;
struct FMeshBuildSettings;
struct FMeshDescription;
struct FDataflowNode;
class FSkeletalMeshLODModel;
class FString;
class IPropertyHandle;

namespace UE::Chaos::ClothAsset
{
	/**
	* Tools shared by cloth dataflow nodes
	*/
	struct FClothDataflowTools
	{
		static void AddRenderPatternFromSkeletalMeshSection(const TSharedRef<FManagedArrayCollection>& ClothCollection, const FSkeletalMeshLODModel& SkeletalMeshModel, const int32 SectionIndex, const FString& RenderMaterialPathName);

		static void AddSimPatternsFromSkeletalMeshSection(const TSharedRef<FManagedArrayCollection>& ClothCollection, const FSkeletalMeshLODModel& SkeletalMeshModel, const int32 SectionIndex, const int32 UVChannelIndex, const FVector2f& UVScale, bool bImportNormals = false);

		static void LogAndToastWarning(const FDataflowNode& DataflowNode, const FText& Headline, const FText& Details);

		/**
		 * Turn a string into a valid collection group or attribute name.
		 * The resulting name won't contains spaces and any other special characters as listed in
		 * INVALID_OBJECTNAME_CHARACTERS (currently "',/.:|&!~\n\r\t@#(){}[]=;^%$`).
		 * It will also have all leading underscore removed, as these names are reserved for internal use.
		 * @param InOutString The string to turn into a valid collection name.
		 * @return Whether the InOutString was already a valid collection name.
		 */
		static bool MakeCollectionName(FString& InOutString);

		static bool BuildSkeletalMeshModelFromMeshDescription(const FMeshDescription* const InMeshDescription, const FMeshBuildSettings& InBuildSettings, FSkeletalMeshLODModel& SkeletalMeshModel);

		/** Return the Dataflow node owning by this property, and cast it to the desired node type. */
		template<typename T = FDataflowNode>
		static T* GetPropertyOwnerDataflowNode(const TSharedPtr<IPropertyHandle>& PropertyHandle)
		{
			return static_cast<FDataflowNode*>(GetPropertyOwnerDataflowNode(PropertyHandle, T::StaticStruct()));
		}

		static bool RemoveDegenerateTriangles(
			const TArray<FIntVector3>& TriangleToVertexIndex,
			const TArray<FVector2f>& RestPositions2D,
			const TArray<FVector3f>& DrapedPositions3D,
			TArray<FIntVector3>& OutTriangleToVertexIndex,
			TArray<FVector2f>& OutRestPositions2D,
			TArray<FVector3f>& OutDrapedPositions3D,
			TArray<int32>& OutIndices);  // Old to new vertices lookup
		static bool RemoveDuplicateTriangles(TArray<FIntVector3>& TriangleToVertexIndex);
		static bool RemoveDuplicateStitches(TArray<TArray<FIntVector2>>& SeamStitches);

	private:
		/** Return the Dataflow node owning by this property. */
		static FDataflowNode* GetPropertyOwnerDataflowNode(const TSharedPtr<IPropertyHandle>& PropertyHandle, const UStruct* DataflowNodeStruct);
	};
}  // End namespace UE::Chaos::ClothAsset

DECLARE_LOG_CATEGORY_EXTERN(LogChaosClothAssetDataflowNodes, Log, All);
