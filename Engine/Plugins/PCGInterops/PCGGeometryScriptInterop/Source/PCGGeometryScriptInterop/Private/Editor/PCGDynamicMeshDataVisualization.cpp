// Copyright Epic Games, Inc. All Rights Reserved.

#include "Editor/PCGDynamicMeshDataVisualization.h"

#if WITH_EDITOR

#include "Data/PCGDynamicMeshData.h"
#include "Helpers/PCGHelpers.h"
#include "Resources/PCGDynamicMeshManagedComponent.h"

#include "UDynamicMesh.h"
#include "Components/DynamicMeshComponent.h"

#define LOCTEXT_NAMESPACE "PCGDynamicMeshDataVisualization"

void FPCGDynamicMeshDataVisualization::ExecuteDebugDisplay(FPCGContext* Context, const UPCGSettingsInterface* SettingsInterface, const UPCGData* Data, AActor* TargetActor) const
{
	const UPCGDynamicMeshData* DynMeshData = CastChecked<UPCGDynamicMeshData>(Data);
	if (!DynMeshData)
	{
		return;
	}

	// We force debug resources to be transient.
	UPCGDynamicMeshManagedComponent* ManagedComponent = PCGDynamicMeshManagedComponent::GetOrCreateDynamicMeshManagedComponent(Context, SettingsInterface, DynMeshData, TargetActor, EPCGEditorDirtyMode::Preview);
	UDynamicMeshComponent* Component = ManagedComponent ? ManagedComponent->GetComponent() : nullptr;
	
	if (Component)
	{
		DynMeshData->InitializeDynamicMeshComponentFromData(Component);
	}
}

#undef LOCTEXT_NAMESPACE

#endif // WITH_EDITOR