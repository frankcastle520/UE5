// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDGeometryDataComponent.h"

#include "Settings/ChaosVDCoreSettings.h"
#include "ChaosVDGeometryBuilder.h"
#include "ChaosVDModule.h"
#include "ChaosVDParticleActor.h"
#include "ChaosVDSettingsManager.h"
#include "Components/MeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Visualizers/ChaosVDParticleDataComponentVisualizer.h"


FChaosVDMeshDataInstanceHandle::FChaosVDMeshDataInstanceHandle(int32 InInstanceIndex, UMeshComponent* InMeshComponent, int32 InParticleID, int32 InSolverID)
{
	InstanceState.MeshComponent = InMeshComponent;
	InstanceState.MeshInstanceIndex = InInstanceIndex;
	InstanceState.OwningParticleID = InParticleID;
	InstanceState.OwningSolverID = InSolverID;

	if (Cast<UInstancedStaticMeshComponent>(InstanceState.MeshComponent))
	{
		InstanceState.MeshComponentType = EChaosVDMeshComponent::InstancedStatic;
	}
	else if (Cast<UStaticMeshComponent>(InstanceState.MeshComponent))
	{
		InstanceState.MeshComponentType = EChaosVDMeshComponent::Static;
	}
	else
	{
		InstanceState.MeshComponentType = EChaosVDMeshComponent::Dynamic;
	}
}

void FChaosVDMeshDataInstanceHandle::SetWorldTransform(const FTransform& InTransform)
{
	if (!ExtractedGeometryHandle)
	{
		UE_LOG(LogChaosVDEditor, Error, TEXT("[%s] Attempted to update the world transform without a valid geometry handle"), ANSI_TO_TCHAR(__FUNCTION__));
		return;
	}

	const FTransform ExtractedRelativeTransform = ExtractedGeometryHandle->GetRelativeTransform();

	InstanceState.CurrentWorldTransform.SetLocation(InTransform.TransformPosition(ExtractedRelativeTransform.GetLocation()));
	InstanceState.CurrentWorldTransform.SetRotation(InTransform.TransformRotation(ExtractedRelativeTransform.GetRotation()));
	InstanceState.CurrentWorldTransform.SetScale3D(ExtractedRelativeTransform.GetScale3D());
	
	if (IChaosVDGeometryComponent* CVDGeometryComponent = Cast<IChaosVDGeometryComponent>(GetMeshComponent()))
	{
		CVDGeometryComponent->UpdateInstanceWorldTransform(AsShared(), InstanceState.CurrentWorldTransform);
	}
}

void FChaosVDMeshDataInstanceHandle::SetGeometryHandle(const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& InHandle)
{
	ExtractedGeometryHandle = InHandle;

	using namespace Chaos;
	if (ExtractedGeometryHandle)
	{
		InstanceState.ImplicitObjectInfo.bIsRootObject = ExtractedGeometryHandle->GetRootImplicitObject() == ExtractedGeometryHandle->GetImplicitObject();
		InstanceState.ImplicitObjectInfo.ShapeInstanceIndex = ExtractedGeometryHandle->GetShapeInstanceIndex();
		InstanceState.ImplicitObjectInfo.ImplicitObjectType = ExtractedGeometryHandle->GetTypeName();
		InstanceState.ImplicitObjectInfo.RelativeTransform = ExtractedGeometryHandle->GetRelativeTransform();
	}
	else
	{
		InstanceState.ImplicitObjectInfo = FChaosVDImplicitObjectBasicView();
	}
}

void FChaosVDMeshDataInstanceHandle::SetInstanceColor(const FLinearColor& NewColor)
{
	if (InstanceState.CurrentGeometryColor != NewColor)
	{
		if (IChaosVDGeometryComponent* CVDGeometryComponent = Cast<IChaosVDGeometryComponent>(GetMeshComponent()))
		{
			CVDGeometryComponent->UpdateInstanceColor(AsShared(), NewColor);
			InstanceState.CurrentGeometryColor = NewColor;
		}
	}
}

void FChaosVDMeshDataInstanceHandle::UpdateMeshComponentForCollisionData(const FChaosVDShapeCollisionData& InCollisionData)
{
	if (InCollisionData.bIsValid && InstanceState.CollisionData != InCollisionData)
	{
		if (const TSharedPtr<FChaosVDGeometryBuilder> GeometryBuilderPtr = GeometryBuilderInstance.Pin())
		{
			EChaosVDMeshAttributesFlags RequiredMeshAttributes = EChaosVDMeshAttributesFlags::None;

			// If this is a query only type of geometry, we need a translucent mesh
			if (InCollisionData.bQueryCollision && !InCollisionData.bSimCollision)
			{
				EnumAddFlags(RequiredMeshAttributes, EChaosVDMeshAttributesFlags::TranslucentGeometry);
			}

			// Mirrored geometry needs to be on a instanced mesh component with reversed culling
			if (GeometryBuilderPtr->HasNegativeScale(ExtractedGeometryHandle->GetRelativeTransform()))
			{
				EnumAddFlags(RequiredMeshAttributes, EChaosVDMeshAttributesFlags::MirroredGeometry);
			}

			// If the current mesh component does not meet the required mesh attributes, we need to move to a new mesh component that it does
			bool bMeshComponentWasUpdated = false;
			if (IChaosVDGeometryComponent* CVDOldGeometryComponent = Cast<IChaosVDGeometryComponent>(GetMeshComponent()))
			{
				if (RequiredMeshAttributes != CVDOldGeometryComponent->GetMeshComponentAttributeFlags())
				{
					if (InstanceState.bIsSelected)
					{
						CVDOldGeometryComponent->SetIsSelected(AsShared(), false);
					}

					CVDOldGeometryComponent->RemoveMeshInstance(AsShared());

					GeometryBuilderPtr->UpdateMeshDataInstance<UChaosVDInstancedStaticMeshComponent>(AsShared(), RequiredMeshAttributes);

					bMeshComponentWasUpdated = true;
				}
			}

			if (bMeshComponentWasUpdated)
			{
				if (IChaosVDGeometryComponent* CVDNewGeometryComponent = Cast<IChaosVDGeometryComponent>(GetMeshComponent()))
				{
					// Reset the color so it is updated in the next Update color calls (which always happens after updating the shape instance data)
					InstanceState.CurrentGeometryColor = FLinearColor(ForceInitToZero);
		
					CVDNewGeometryComponent->UpdateInstanceVisibility(AsShared(), InstanceState.bIsVisible);
					CVDNewGeometryComponent->SetIsSelected(AsShared(), InstanceState.bIsSelected);
				}
			}
		}
	}
}

void FChaosVDMeshDataInstanceHandle::SetGeometryCollisionData(const FChaosVDShapeCollisionData& InCollisionData)
{
	// If this is a static mesh component, we can't just update change the material. We need to remove this instance from the current component and move it to a
	// component that has the correct translucent mesh
	if (GetMeshComponentType() == EChaosVDMeshComponent::InstancedStatic)
	{
		UpdateMeshComponentForCollisionData(InCollisionData);
	}

	InstanceState.CollisionData = InCollisionData;
}

void FChaosVDMeshDataInstanceHandle::SetIsSelected(bool bInIsSelected)
{
	if (IChaosVDGeometryComponent* CVDGeometryComponent = Cast<IChaosVDGeometryComponent>(GetMeshComponent()))
	{
		CVDGeometryComponent->SetIsSelected(AsShared(), bInIsSelected);
	}

	InstanceState.bIsSelected = bInIsSelected;
}

void FChaosVDMeshDataInstanceHandle::SetVisibility(bool bInIsVisible)
{
	if (InstanceState.bIsVisible != bInIsVisible)
	{
		if (IChaosVDGeometryComponent* CVDGeometryComponent = Cast<IChaosVDGeometryComponent>(GetMeshComponent()))
		{
			CVDGeometryComponent->UpdateInstanceVisibility(AsShared(), bInIsVisible);
		}

		InstanceState.bIsVisible = bInIsVisible;
	}
}

void FChaosVDMeshDataInstanceHandle::HandleInstanceIndexUpdated(TArrayView<const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData> InIndexUpdates)
{
	// When an index changes, we receive an array with all the indexes that were modified. We need to only act upon the update of for the index we are tracking in this handle
	for (const FInstancedStaticMeshDelegates::FInstanceIndexUpdateData& IndexUpdateData : InIndexUpdates)
	{
		switch (IndexUpdateData.Type)
		{
			case FInstancedStaticMeshDelegates::EInstanceIndexUpdateType::Added:
				break; // We don't need to process 'Added' updates as they can't affect existing IDs
			case FInstancedStaticMeshDelegates::EInstanceIndexUpdateType::Relocated:
				{
					if (InstanceState.MeshInstanceIndex == IndexUpdateData.OldIndex)
					{
						InstanceState.MeshInstanceIndex = IndexUpdateData.Index;
					}
					break;
				}

			case FInstancedStaticMeshDelegates::EInstanceIndexUpdateType::Removed:
			case FInstancedStaticMeshDelegates::EInstanceIndexUpdateType::Cleared:
			case FInstancedStaticMeshDelegates::EInstanceIndexUpdateType::Destroyed:
				{
					if (InstanceState.MeshInstanceIndex == IndexUpdateData.Index)
					{
						InstanceState.MeshInstanceIndex = INDEX_NONE;
					}
					break;
				}
			default:
				break;
		}
	}
}

void FChaosVDGeometryComponentUtils::UpdateCollisionDataFromShapeArray(const TArray<FChaosVDShapeCollisionData>& InShapeArray, const TSharedRef<FChaosVDMeshDataInstanceHandle>& InInstanceHandle)
{
	if (InShapeArray.IsEmpty())
	{
		return;
	}

	const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& ExtractedGeometryHandle = InInstanceHandle->GetGeometryHandle();

	if (!ExtractedGeometryHandle)
	{
		return;
	}

	const int32 ShapeInstanceIndex = ExtractedGeometryHandle->GetShapeInstanceIndex();
	if (!InShapeArray.IsValidIndex(ShapeInstanceIndex))
	{
		const FName ImplicitObjectTypeName = InInstanceHandle->GetState().ImplicitObjectInfo.ImplicitObjectType;
		const Chaos::FImplicitObject* RootImplicitObject = ExtractedGeometryHandle->GetRootImplicitObject();
		const FName RootImplicitObjectTypeName = !InInstanceHandle->GetState().ImplicitObjectInfo.bIsRootObject && RootImplicitObject ? Chaos::GetImplicitObjectTypeName(Chaos::GetInnerType(RootImplicitObject->GetType())) : TEXT("None");
		
		FString ErrorMessage = FString::Printf(TEXT("[%s] Failed to find shape instance data at Index [%d] | Particle ID[%d] | Available Shape instance Data Num [%d] | Implicit Type [%s] - Root Implicit Type [%s] | This geometry will be hidden..."), ANSI_TO_TCHAR(__FUNCTION__), ShapeInstanceIndex, InInstanceHandle->GetOwningParticleID(), InShapeArray.Num(), *ImplicitObjectTypeName.ToString(), *RootImplicitObjectTypeName.ToString());
		
		UE_LOG(LogChaosVDEditor, Verbose, TEXT("[%s]"), *ErrorMessage);

		ensureMsgf(false, TEXT("[%s]"), *ErrorMessage);

		InInstanceHandle->bFailedToUpdateShapeInstanceData = true;
		return;
	}
	else if (InInstanceHandle->bFailedToUpdateShapeInstanceData)
	{
		InInstanceHandle->bFailedToUpdateShapeInstanceData = false;
		UE_LOG(LogChaosVDEditor, Verbose, TEXT("[%s] Recovered from failing to find shape instance data at Index [%d] | Particle ID[%d] | Available Shape instance Data Num [%d] | This geometry will be shown again..."), ANSI_TO_TCHAR(__FUNCTION__), ShapeInstanceIndex, InInstanceHandle->GetOwningParticleID(), InShapeArray.Num());
	}

	FChaosVDShapeCollisionData CollisionDataToUpdate = InShapeArray[ShapeInstanceIndex];
	CollisionDataToUpdate.bIsComplex = FChaosVDGeometryBuilder::DoesImplicitContainType(ExtractedGeometryHandle->GetImplicitObject(), Chaos::ImplicitObjectType::HeightField) || FChaosVDGeometryBuilder::DoesImplicitContainType(ExtractedGeometryHandle->GetImplicitObject(), Chaos::ImplicitObjectType::TriangleMesh);
	CollisionDataToUpdate.bIsValid = true;

	InInstanceHandle->SetGeometryCollisionData(CollisionDataToUpdate);
}

void FChaosVDGeometryComponentUtils::UpdateMeshColor(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, const FChaosVDParticleDataWrapper& InParticleData, bool bIsServer)
{
	const FChaosVDShapeCollisionData& ShapeData = InInstanceHandle->GetGeometryCollisionData();
	const bool bIsQueryOnly = ShapeData.bQueryCollision && !ShapeData.bSimCollision;

	if (ShapeData.bIsValid)
	{
		FLinearColor ColorToApply = GetGeometryParticleColor(InInstanceHandle->GetGeometryHandle(), InParticleData, bIsServer);

		constexpr float QueryOnlyShapeOpacity = 0.6f;
		ColorToApply.A = bIsQueryOnly ? QueryOnlyShapeOpacity : 1.0f;

		InInstanceHandle->SetInstanceColor(ColorToApply);
	}
}

void FChaosVDGeometryComponentUtils::UpdateMeshVisibility(const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InInstanceHandle, const FChaosVDParticleDataWrapper& InParticleData, bool bIsActive)
{
	if (!InInstanceHandle || !InInstanceHandle->GetGeometryHandle())
	{
		return;
	}

	if (!bIsActive)
	{
		InInstanceHandle->SetVisibility(bIsActive);
		return;
	}

	if (const UChaosVDParticleVisualizationSettings* ParticleVisualizationSettings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDParticleVisualizationSettings>())
	{
		const EChaosVDGeometryVisibilityFlags CurrentVisibilityFlags = ParticleVisualizationSettings->GetGeometryVisualizationFlags();
		
		bool bShouldGeometryBeVisible = false;

		if (!EnumHasAnyFlags(CurrentVisibilityFlags, EChaosVDGeometryVisibilityFlags::ShowDisabledParticles))
		{
			if (InParticleData.ParticleDynamicsMisc.HasValidData() && InParticleData.ParticleDynamicsMisc.bDisabled)
			{
				InInstanceHandle->SetVisibility(bShouldGeometryBeVisible);
				return;
			}
		}

		// TODO: Re-visit the way we determine visibility of the meshes.
		// Now that the options have grown and they will continue to do so, these checks are becoming hard to read and extend

		const bool bIsHeightfield = InInstanceHandle->GetGeometryHandle()->GetImplicitObject() && Chaos::GetInnerType(InInstanceHandle->GetGeometryHandle()->GetImplicitObject()->GetType()) == Chaos::ImplicitObjectType::HeightField;

		if (bIsHeightfield && EnumHasAnyFlags(CurrentVisibilityFlags, EChaosVDGeometryVisibilityFlags::ShowHeightfields))
		{
			bShouldGeometryBeVisible = true;
		}
		else
		{
			const FChaosVDShapeCollisionData& InstanceShapeData = InInstanceHandle->GetGeometryCollisionData();

			if (InstanceShapeData.bIsValid)
			{
				// Complex vs Simple takes priority although this is subject to change
				const bool bShouldBeVisibleIfComplex = InstanceShapeData.bIsComplex && EnumHasAnyFlags(CurrentVisibilityFlags, EChaosVDGeometryVisibilityFlags::Complex);
				const bool bShouldBeVisibleIfSimple = !InstanceShapeData.bIsComplex && EnumHasAnyFlags(CurrentVisibilityFlags, EChaosVDGeometryVisibilityFlags::Simple);
		
				if (bShouldBeVisibleIfComplex || bShouldBeVisibleIfSimple)
				{
					bShouldGeometryBeVisible = (InstanceShapeData.bSimCollision && EnumHasAnyFlags(CurrentVisibilityFlags, EChaosVDGeometryVisibilityFlags::Simulated))
					|| (InstanceShapeData.bQueryCollision && EnumHasAnyFlags(CurrentVisibilityFlags, EChaosVDGeometryVisibilityFlags::Query));
				}
			}
		}

		InInstanceHandle->SetVisibility(bShouldGeometryBeVisible);
	}
}

FLinearColor FChaosVDGeometryComponentUtils::GetGeometryParticleColor(const TSharedPtr<FChaosVDExtractedGeometryDataHandle>& InGeometryHandle, const FChaosVDParticleDataWrapper& InParticleData, bool bIsServer)
{
	constexpr FLinearColor DefaultColor(0.088542f, 0.088542f, 0.088542f);
	FLinearColor ColorToApply = DefaultColor;

	if (!InGeometryHandle)
	{
		return ColorToApply;
	}

	const UChaosVDParticleVisualizationColorSettings* VisualizationSettings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDParticleVisualizationColorSettings>();
	if (!VisualizationSettings)
	{
		return ColorToApply;
	}

	switch (VisualizationSettings->ParticleColorMode)
	{
	case EChaosVDParticleDebugColorMode::ShapeType:
		{
			ColorToApply = InGeometryHandle->GetImplicitObject() ? VisualizationSettings->ColorsByShapeType.GetColorFromShapeType(Chaos::GetInnerType(InGeometryHandle->GetImplicitObject()->GetType())) : DefaultColor;
			break;
		}
	case EChaosVDParticleDebugColorMode::State:
		{
			if (InParticleData.Type == EChaosVDParticleType::Static)
			{
				ColorToApply = VisualizationSettings->ColorsByParticleState.GetColorFromState(EChaosVDObjectStateType::Static);
			}
			else
			{
				ColorToApply = VisualizationSettings->ColorsByParticleState.GetColorFromState(InParticleData.ParticleDynamicsMisc.MObjectState);
			}
			break;
		}
	case EChaosVDParticleDebugColorMode::ClientServer:
		{
			if (InParticleData.Type == EChaosVDParticleType::Static)
			{
				ColorToApply = VisualizationSettings->ColorsByClientServer.GetColorFromState(bIsServer, EChaosVDObjectStateType::Static);
			}
			else
			{
				ColorToApply = VisualizationSettings->ColorsByClientServer.GetColorFromState(bIsServer, InParticleData.ParticleDynamicsMisc.MObjectState);
			}
			break;
		}

	case EChaosVDParticleDebugColorMode::None:
	default:
		// Nothing to do here. Color to apply is already set to the default
		break;
	}

	return ColorToApply;
}

UMaterialInterface* FChaosVDGeometryComponentUtils::GetBaseMaterialForType(EChaosVDMaterialType Type)
{
	const UChaosVDCoreSettings* EditorSettings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCoreSettings>();
	if (!EditorSettings)
	{
		return nullptr;
	}

	switch(Type)
	{
		case EChaosVDMaterialType::SMTranslucent:
				return EditorSettings->QueryOnlyMeshesMaterial.Get();
		case EChaosVDMaterialType::SMOpaque:
				return EditorSettings->SimOnlyMeshesMaterial.Get();
		case EChaosVDMaterialType::ISMCOpaque:
				return EditorSettings->InstancedMeshesMaterial.Get();
		case EChaosVDMaterialType::ISMCTranslucent:
				return EditorSettings->InstancedMeshesQueryOnlyMaterial.Get();
		default:
			return nullptr;
	}	
}

void Chaos::VisualDebugger::SelectParticleWithGeometryInstance(const TSharedRef<FChaosVDScene>& InScene, IChaosVDGeometryOwnerInterface* GeometryOwner, const TSharedPtr<FChaosVDMeshDataInstanceHandle>& InMeshDataHandle)
{
	InScene->SetSelectedObject(nullptr);

	if(GeometryOwner)
	{
		GeometryOwner->SetSelectedMeshInstance(InMeshDataHandle);
		InScene->SetSelectedObject(Cast<UObject>(GeometryOwner));	
	}
}
