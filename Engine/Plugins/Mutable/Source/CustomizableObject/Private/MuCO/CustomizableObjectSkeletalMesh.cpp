// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCO/CustomizableObjectSkeletalMesh.h"

#include "MuCO/CustomizableObjectSystem.h"
#include "MuCO/CustomizableObjectSystemPrivate.h"

#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectMeshUpdate.h"

FName GenerateUniqueNameFromCOInstance(const UCustomizableObjectInstance& Instance)
{
	FString BaseName = FString("MutableSkMesh-") + Instance.GetCustomizableObject()->GetName();
	FName SkeletalMeshName = MakeUniqueObjectName(GetTransientPackage(), USkeletalMesh::StaticClass(), FName(BaseName), 
							                      EUniqueObjectNameOptions::GloballyUnique);

	return SkeletalMeshName;
}


UCustomizableObjectSkeletalMesh* UCustomizableObjectSkeletalMesh::CreateSkeletalMesh(const TSharedRef<FUpdateContextPrivate>& OperationData, 
	const UCustomizableObjectInstance& Instance, UCustomizableObject& CustomizableObject, const int32 InstanceComponentIndex)
{
	FName SkeletalMeshName = GenerateUniqueNameFromCOInstance(Instance);
	UCustomizableObjectSkeletalMesh* OutSkeletalMesh = NewObject<UCustomizableObjectSkeletalMesh>(GetTransientPackage(), SkeletalMeshName, RF_Transient);
	
	// Debug info
	OutSkeletalMesh->CustomizableObjectPathName = GetNameSafe(&CustomizableObject);
	OutSkeletalMesh->InstancePathName = Instance.GetName();


	// Init properties
	OutSkeletalMesh->Model = CustomizableObject.GetPrivate()->GetModel();

	OutSkeletalMesh->Parameters = OperationData->Parameters;
	OutSkeletalMesh->State = OperationData->GetCapturedDescriptor().GetState();
	
	OutSkeletalMesh->MeshIDs.Init(MAX_uint64, MAX_MESH_LOD_COUNT);
	
	const FInstanceUpdateData::FComponent& Component = OperationData->InstanceUpdateData.Components[InstanceComponentIndex];

	for (int32 LODIndex = OperationData->FirstLODAvailable; LODIndex < Component.LODCount; ++LODIndex)
	{
		const FInstanceUpdateData::FLOD& LOD = OperationData->InstanceUpdateData.LODs[Component.FirstLOD + LODIndex];
		OutSkeletalMesh->MeshIDs[LODIndex] = LOD.MeshID;
	}

	return OutSkeletalMesh;
}


bool UCustomizableObjectSkeletalMesh::StreamIn(int32 NewMipCount, bool bHighPrio)
{
	check(IsInGameThread());
	if (!HasPendingInitOrStreaming() && CachedSRRState.StreamIn(NewMipCount))
	{
		FRenderAssetUpdate::EThreadType CreateResourcesThread = GRHISupportsAsyncTextureCreation
			? FRenderAssetUpdate::TT_Async
			: FRenderAssetUpdate::TT_Render;

		PendingUpdate = new FCustomizableObjectMeshStreamIn(this, bHighPrio, CreateResourcesThread);

		return !PendingUpdate->IsCancelled();
	}
	return false;
}
