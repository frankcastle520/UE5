// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDParticleActorCustomization.h"

#include "ChaosVDEngine.h"
#include "ChaosVDModule.h"
#include "ChaosVDParticleActor.h"
#include "ChaosVDScene.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IStructureDetailsView.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"
#include "DetailsCustomizations/ChaosVDDetailsCustomizationUtils.h"
#include "Widgets/SChaosVDMainTab.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

FChaosVDParticleActorCustomization::FChaosVDParticleActorCustomization(const TWeakPtr<SChaosVDMainTab>& InMainTab)
{
	AllowedCategories.Add(FChaosVDParticleActorCustomization::ParticleDataCategoryName);
	AllowedCategories.Add(FChaosVDParticleActorCustomization::GeometryCategoryName);

	MainTabWeakPtr = InMainTab;

	ResetCachedView();
}

FChaosVDParticleActorCustomization::~FChaosVDParticleActorCustomization()
{
	RegisterCVDScene(nullptr);
}

TSharedRef<IDetailCustomization> FChaosVDParticleActorCustomization::MakeInstance(TWeakPtr<SChaosVDMainTab> InMainTab)
{
	return MakeShared<FChaosVDParticleActorCustomization>(InMainTab);
}

void FChaosVDParticleActorCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	FChaosVDDetailsCustomizationUtils::HideAllCategories(DetailBuilder, AllowedCategories);

	TSharedPtr<SChaosVDMainTab> MainTabPtr =  MainTabWeakPtr.Pin(); 
	TSharedPtr<FChaosVDScene> Scene = MainTabPtr ? MainTabPtr->GetChaosVDEngineInstance()->GetCurrentScene() : nullptr;

	RegisterCVDScene(Scene);

	if (!Scene)
	{
		ResetCachedView();
		return;
	}

	// We keep the particle data we need to visualize as a shared ptr because copying it each frame we advance/rewind to to an struct that lives in the particle actor it is not cheap.
	// Having a struct details view to which we set that pointer data each time the data in the particle is updated (meaning we assigned another ptr from the recording)
	// seems to be more expensive because it has to rebuild the entire layout from scratch.
	// So a middle ground I found is to have a Particle Data struct in this customization instance, which we add as external property. Then each time the particle data is updated we copy the data over.
	// This allows us to only perform the copy just for the particle that is being inspected and not every particle updated in that frame.

	TArray<TWeakObjectPtr<UObject>> SelectedObjects;
	DetailBuilder.GetObjectsBeingCustomized(SelectedObjects);
	if (SelectedObjects.Num() > 0)
	{
		//TODO: Add support for multi-selection.
		if (!ensure(SelectedObjects.Num() == 1))
		{
			UE_LOG(LogChaosVDEditor, Warning, TEXT("[%s] [%d] objects were selected but this customization panel only support single object selection."), ANSI_TO_TCHAR(__FUNCTION__), SelectedObjects.Num())
		}
		
		AChaosVDParticleActor* CurrentActor = CurrentObservedActor.Get();
		AChaosVDParticleActor* SelectedActor = Cast<AChaosVDParticleActor>(SelectedObjects[0]);

		if (CurrentActor && CurrentActor != SelectedActor)
		{
			ResetCachedView();
		}
		
		if (SelectedActor)
		{
			CurrentObservedActor = SelectedActor;

			HandleSceneUpdated();

			TSharedPtr<IPropertyHandle> InspectedDataPropertyHandlePtr;

			if (TSharedPtr<FChaosVDMeshDataInstanceHandle> SelectedGeometryInstance = SelectedActor->GetSelectedMeshInstance().Pin())
			{
				InspectedDataPropertyHandlePtr = AddExternalStructure(CachedGeometryDataInstanceCopy, DetailBuilder, GeometryCategoryName, LOCTEXT("GeometryShapeDataStructName", "Geometry Shape Data"));
			}
			else
			{
				InspectedDataPropertyHandlePtr = AddExternalStructure(CachedParticleData, DetailBuilder, ParticleDataCategoryName, LOCTEXT("ParticleDataStructName", "Particle Data"));
			}

			if (InspectedDataPropertyHandlePtr)
			{
				TSharedRef<IPropertyHandle> InspectedDataPropertyHandleRef = InspectedDataPropertyHandlePtr.ToSharedRef();
				FChaosVDDetailsCustomizationUtils::HideInvalidCVDDataWrapperProperties({&InspectedDataPropertyHandleRef, 1}, DetailBuilder);
			}
		}
	}
	else
	{
		ResetCachedView();
	}
}

void FChaosVDParticleActorCustomization::HandleSceneUpdated()
{
	AChaosVDParticleActor* ParticleActor = CurrentObservedActor.Get();
	if (!ParticleActor)
	{
		ResetCachedView();
		return;
	}

	// If we have selected a mesh instance, the only data being added to the details panel is the Shape Instance data, so can just update that data here
	if (TSharedPtr<FChaosVDMeshDataInstanceHandle> SelectedGeometryInstance = ParticleActor->GetSelectedMeshInstance().Pin())
	{
		ParticleActor->VisitGeometryInstances([this, SelectedGeometryInstance](const TSharedRef<FChaosVDMeshDataInstanceHandle>& MeshDataHandle)
		{
			if (MeshDataHandle == SelectedGeometryInstance)
			{
				CachedGeometryDataInstanceCopy = MeshDataHandle->GetState();
			}
		});
	}
	else
	{
		TSharedPtr<const FChaosVDParticleDataWrapper> ParticleDataPtr = ParticleActor->GetParticleData();
		CachedParticleData = ParticleDataPtr ? *ParticleDataPtr : FChaosVDParticleDataWrapper();
	}
}

void FChaosVDParticleActorCustomization::ResetCachedView()
{
	CurrentObservedActor = nullptr;
	CachedParticleData = FChaosVDParticleDataWrapper();
	CachedGeometryDataInstanceCopy = FChaosVDMeshDataInstanceState();
}

void FChaosVDParticleActorCustomization::RegisterCVDScene(const TSharedPtr<FChaosVDScene>& InScene)
{
	TSharedPtr<FChaosVDScene> CurrentScene = SceneWeakPtr.Pin();
	if (InScene != CurrentScene)
	{
		if (CurrentScene)
		{
			CurrentScene->OnSceneUpdated().RemoveAll(this);
		}

		if (InScene)
		{
			InScene->OnSceneUpdated().AddSP(this, &FChaosVDParticleActorCustomization::HandleSceneUpdated);
		}

		SceneWeakPtr = InScene;
	}
}

#undef LOCTEXT_NAMESPACE
