// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowSimulationScene.h"
#include "Dataflow/DataflowSimulationManager.h"
#include "Dataflow/DataflowSimulationControls.h"
#include "Dataflow/DataflowEditor.h"
#include "Components/PrimitiveComponent.h"
#include "Chaos/CacheManagerActor.h"
#include "Misc/TransactionObjectEvent.h"
#include "EngineUtils.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "AssetEditorModeManager.h"
#include "AssetViewerSettings.h"
#include "Engine/Selection.h"

#include "Dataflow/Interfaces/DataflowInterfaceGeometryCachable.h"
#include "Dataflow/DataflowSimulationGeometryCache.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GeometryCache.h"

#if WITH_EDITOR
#include "Misc/FileHelper.h"
#endif
#define LOCTEXT_NAMESPACE "FDataflowSimulationScene"

//
// Simulation Scene
//

FDataflowSimulationScene::FDataflowSimulationScene(FPreviewScene::ConstructionValues ConstructionValues, UDataflowEditor* InEditor)
	: FDataflowPreviewSceneBase(ConstructionValues, InEditor)
{
	SceneDescription = NewObject<UDataflowSimulationSceneDescription>();
	SceneDescription->SetSimulationScene(this);

	SimulationGenerator = MakeShared<UE::Dataflow::FDataflowSimulationGenerator>();
	RootSceneActor = GetWorld()->SpawnActor<AChaosCacheManager>();

	if(GetEditorContent())
	{
#if WITH_EDITORONLY_DATA
		if(const UDataflow* DataflowAsset = GetEditorContent()->GetDataflowAsset())
		{
			SceneDescription->CacheParams = DataflowAsset->PreviewCacheParams;
			SceneDescription->CacheAsset = Cast<UChaosCacheCollection>(DataflowAsset->PreviewCacheAsset.LoadSynchronous());
			SceneDescription->BlueprintClass = DataflowAsset->PreviewBlueprintClass;
			SceneDescription->BlueprintTransform = DataflowAsset->PreviewBlueprintTransform; 
		}
		if(SceneDescription->BlueprintClass == nullptr)
		{
			SceneDescription->BlueprintClass = GetEditorContent()->GetPreviewClass();
		}
#endif
	}

#if WITH_EDITOR
	OnObjectsReinstancedHandle = FCoreUObjectDelegates::OnObjectsReinstanced.AddRaw(this, &FDataflowSimulationScene::OnObjectsReinstanced);
#endif

	CreateSimulationScene();
}

void FDataflowSimulationScene::OnObjectsReinstanced(const TMap<UObject*, UObject*>& ObjectsMap)
{
	if(UObject* const* InstancedActor = ObjectsMap.Find(PreviewActor))
	{
		if(*InstancedActor)
		{
			PreviewActor = Cast<AActor>(*InstancedActor);
		}
	}
}

FDataflowSimulationScene::~FDataflowSimulationScene()
{
	ResetSimulationScene();

#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectsReinstanced.Remove(OnObjectsReinstancedHandle);
#endif
}

void FDataflowSimulationScene::UnbindSceneSelection()
{
	if(PreviewActor)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
		PreviewActor->GetComponents(PrimComponents);

		for(UPrimitiveComponent* PrimComponent : PrimComponents)
		{
			PrimComponent->SelectionOverrideDelegate.Unbind();
		}
	}
}

void FDataflowSimulationScene::ResetSimulationScene()
{
	// Release any selected components before the PreviewActor is deleted from the scene
	if (const TSharedPtr<FAssetEditorModeManager> ModeManager = GetDataflowModeManager())
	{
		if (USelection* const SelectedComponents = ModeManager->GetSelectedComponents())
		{
			SelectedComponents->DeselectAll();
		}
	}

	// Destroy the spawned root actor
	if(PreviewActor && GetWorld())
	{
		GetWorld()->EditorDestroyActor(PreviewActor, true);
			
		// Since deletion can be delayed, rename to avoid future name collision
		// Call UObject::Rename directly on actor to avoid AActor::Rename which unnecessarily sunregister and re-register components
		PreviewActor->UObject::Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
	}
	
	// Unbind the scene selection
	UnbindSceneSelection();
}

void FDataflowSimulationScene::PauseSimulationScene() const
{
	if(SceneDescription && (SceneDescription->CacheAsset == nullptr))
	{
		GetWorld()->GetSubsystem<UDataflowSimulationManager>()->SetSimulationEnabled(false);
		UE::Dataflow::PauseSkeletonAnimation(PreviewActor);
	}
}

void FDataflowSimulationScene::StartSimulationScene() const
{
	if(SceneDescription && (SceneDescription->CacheAsset == nullptr))
	{
		GetWorld()->GetSubsystem<UDataflowSimulationManager>()->SetSimulationEnabled(true);
		UE::Dataflow::StartSkeletonAnimation(PreviewActor);
	}
}

void FDataflowSimulationScene::StepSimulationScene() const
{
	if(SceneDescription && (SceneDescription->CacheAsset == nullptr))
	{
		GetWorld()->GetSubsystem<UDataflowSimulationManager>()->SetSimulationEnabled(true);
		GetWorld()->GetSubsystem<UDataflowSimulationManager>()->SetSimulationStepping(true);
		UE::Dataflow::StepSkeletonAnimation(PreviewActor);
	}
}

void FDataflowSimulationScene::RebuildSimulationScene(const bool bIsSimulationEnabled)
{
	if(SceneDescription && (SceneDescription->CacheAsset == nullptr))
	{
		// Unregister components, cache manager, selection...
		ResetSimulationScene();

		// Register components, cache manager, selection...
		CreateSimulationScene();

		// Override the simulation enabled flag
		GetWorld()->GetSubsystem<UDataflowSimulationManager>()->SetSimulationEnabled(bIsSimulationEnabled);
	}
}

void FDataflowSimulationScene::BindSceneSelection()
{
	if(PreviewActor)
	{
		TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
		PreviewActor->GetComponents(PrimComponents);
		
		for(UPrimitiveComponent* PrimComponent : PrimComponents)
		{
			PrimComponent->SelectionOverrideDelegate =
				UPrimitiveComponent::FSelectionOverride::CreateRaw(this, &FDataflowPreviewSceneBase::IsComponentSelected);
		}
	}
}

void FDataflowSimulationScene::CreateSimulationScene()
{
	if(SimulationGenerator && SceneDescription && SceneDescription->BlueprintClass && GetWorld())
	{
		SimulationGenerator->SetCacheParams(SceneDescription->CacheParams);
		SimulationGenerator->SetCacheAsset(SceneDescription->CacheAsset);
		SimulationGenerator->SetBlueprintClass(SceneDescription->BlueprintClass);
		SimulationGenerator->SetBlueprintTransform(SceneDescription->BlueprintTransform);
		SimulationGenerator->SetDataflowContent(GetEditorContent());

		TimeRange = SceneDescription->CacheParams.TimeRange;
		NumFrames = (TimeRange[1] > TimeRange[0]) ? FMath::Floor((TimeRange[1] - TimeRange[0]) * SceneDescription->CacheParams.FrameRate) : 0;
		
		PreviewActor = UE::Dataflow::SpawnSimulatedActor(SceneDescription->BlueprintClass, Cast<AChaosCacheManager>(RootSceneActor),
			SceneDescription->CacheAsset, false, GetEditorContent(), SceneDescription->BlueprintTransform);

		// Setup all the skelmesh animations
		UE::Dataflow::SetupSkeletonAnimation(PreviewActor, SceneDescription->bSkeletalMeshVisibility);
		
		GetWorld()->GetSubsystem<UDataflowSimulationManager>()->SetSimulationEnabled(false);
	}

	// update the selection binding since we are constantly editing the graph
	BindSceneSelection();
}

void FDataflowSimulationScene::UpdateSimulationCache()
{
	if(SimulationGenerator.IsValid())
	{
		SimulationGenerator->RequestGeneratorAction(UE::Dataflow::EDataflowGeneratorActions::StartGenerate);
	}
}

void FDataflowSimulationScene::TickDataflowScene(const float DeltaSeconds)
{
	if(const TObjectPtr<UDataflowBaseContent>& EditorContent = GetEditorContent())
	{
		if (const TObjectPtr<UDataflow> DataflowGraph = EditorContent->GetDataflowAsset())
		{
			if (UE::Dataflow::ShouldResetWorld(DataflowGraph, GetWorld(), LastTimeStamp) || EditorContent->IsSimulationDirty())
			{
				// Unregister components, cache manager, selection...
				ResetSimulationScene();

				// Register components, cache manager, selection...
				CreateSimulationScene();

				// Reset the dirty flag
				EditorContent->SetSimulationDirty(false);
			}
		}

		// Load the cache at some point in time
		if(SceneDescription->CacheAsset)
		{
			// Update the cached simulation at some point in time
			if(RootSceneActor)
			{
				Cast<AChaosCacheManager>(RootSceneActor)->SetStartTime(SimulationTime);
			}
			// Update all the skelmesh animations at the simulation time
			UE::Dataflow::UpdateSkeletonAnimation(PreviewActor, SimulationTime);
		}
	}
	GetWorld()->Tick(ELevelTick::LEVELTICK_All, DeltaSeconds);
}

void FDataflowSimulationScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	FDataflowPreviewSceneBase::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(SceneDescription);
}

void FDataflowSimulationScene::SceneDescriptionPropertyChanged(const FName& PropertyName)
{
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDataflowSimulationSceneDescription, CacheParams))
	{
		if(SimulationGenerator)
		{
			SimulationGenerator->SetCacheParams(SceneDescription->CacheParams);
		}
	}
	else if(PropertyName == GET_MEMBER_NAME_CHECKED(UDataflowSimulationSceneDescription, CacheAsset))
	{
		if(SimulationGenerator)
		{
			SimulationGenerator->SetCacheAsset(SceneDescription->CacheAsset);
		}
	}
	else if(PropertyName == GET_MEMBER_NAME_CHECKED(UDataflowSimulationSceneDescription, BlueprintClass))
	{
		if(SimulationGenerator)
		{
			SimulationGenerator->SetBlueprintClass(SceneDescription->BlueprintClass);
		}
	}
	else if(PropertyName == GET_MEMBER_NAME_CHECKED(UDataflowSimulationSceneDescription, BlueprintTransform))
	{
		if(SimulationGenerator)
		{
			SimulationGenerator->SetBlueprintTransform(SceneDescription->BlueprintTransform);
		}
	}
	if(GetEditorContent())
	{
		if(UDataflow* DataflowAsset = GetEditorContent()->GetDataflowAsset())
		{
#if WITH_EDITORONLY_DATA
			DataflowAsset->PreviewCacheParams = SceneDescription->CacheParams;
			DataflowAsset->PreviewCacheAsset = SceneDescription->CacheAsset;
			DataflowAsset->PreviewBlueprintClass = SceneDescription->BlueprintClass;
			DataflowAsset->PreviewBlueprintTransform = SceneDescription->BlueprintTransform;
			
			DataflowAsset->MarkPackageDirty();
#endif
		}
	}
	
	// Unregister components, cache manager, selection...
	ResetSimulationScene();

	// Register components, cache manager, selection...
	CreateSimulationScene();
}

void UDataflowSimulationSceneDescription::GenerateGeometryCache()
{
	SimulationScene->ResetSimulationScene();
	SimulationScene->CreateSimulationScene();
	const FVector2f& TimeRange = SimulationScene->GetTimeRange();
	const int32 NumFrames = FMath::Floor((TimeRange[1] - TimeRange[0]) * CacheParams.FrameRate);
	float Time = TimeRange[0];
	float DeltaTime = (TimeRange[1] - TimeRange[0]) / NumFrames;
	TObjectPtr<AActor> GetRootActor = SimulationScene->GetRootActor();
	TObjectPtr<AActor> PreviewActor = SimulationScene->GetPreviewActor();
	if (CacheAsset && GeometryCacheAsset && GetRootActor && EmbeddedSkeletalMesh)
	{
		IDataflowGeometryCachable* GeometryCachable = nullptr; //interface for ChaosDeformableTetrahedralComponent

		RenderPositions.SetNum(NumFrames);
		TInlineComponentArray<UPrimitiveComponent*> PrimComponents;
		PreviewActor->GetComponents(PrimComponents);
		for (UPrimitiveComponent* PrimComponent : PrimComponents)
		{
			GeometryCachable = Cast<IDataflowGeometryCachable>(PrimComponent);
			if (GeometryCachable)
			{
				break;
			}
		}
		if (!GeometryCachable)
		{
			UE_LOG(LogDataflowSimulationGeometryCache, Error, TEXT("No GeometryCachable Component in the Preview Actor"));
			return;
		}
		TOptional<TArray<int32>> OptionalMap = GeometryCachable->GetMeshImportVertexMap(*EmbeddedSkeletalMesh);
		if (!OptionalMap)
		{
			return;
		}
		const TArray<int32>& Map = OptionalMap.GetValue();
		TArray<uint32> ImportedVertexNumbers = TArray<uint32>(reinterpret_cast<const uint32*>(Map.GetData()), Map.Num());
		for (int32 Frame = 0; Frame < SimulationScene->GetNumFrames(); ++Frame)
		{
			Time += DeltaTime;
			Cast<AChaosCacheManager>(GetRootActor)->SetStartTime(Time);
			RenderPositions[Frame] = GeometryCachable->GetGeometryCachePositions(EmbeddedSkeletalMesh);
		}
		UE::DataflowSimulationGeometryCache::SaveGeometryCache(*GeometryCacheAsset, *EmbeddedSkeletalMesh, ImportedVertexNumbers, RenderPositions);
		UE::DataflowSimulationGeometryCache::SavePackage(*GeometryCacheAsset);
	}
}

namespace UE::Dataflow::Private
{
	template<class T>
	T* CreateOrLoad(const FString& PackageName)
	{
		const FName AssetName(FPackageName::GetLongPackageAssetName(PackageName));
		if (UPackage* const Package = CreatePackage(*PackageName))
		{
			LoadPackage(nullptr, *PackageName, LOAD_Quiet | LOAD_EditorOnly);
			T* Asset = FindObject<T>(Package, *AssetName.ToString());
			if (!Asset)
			{
				Asset = NewObject<T>(Package, *AssetName.ToString(), RF_Public | RF_Standalone | RF_Transactional);
				Asset->MarkPackageDirty();
				FAssetRegistryModule::AssetCreated(Asset);
			}
			return Asset;
		}
		return nullptr;
	}

	TObjectPtr<UGeometryCache> NewGeometryCacheDialog(const UObject* NamingAsset = nullptr)
	{
		FSaveAssetDialogConfig Config;
		{
			if (NamingAsset)
			{
				const FString PackageName = NamingAsset->GetOutermost()->GetName();
				Config.DefaultPath = FPackageName::GetLongPackagePath(PackageName);
				Config.DefaultAssetName = FString::Printf(TEXT("GeometryCache_%s"), *NamingAsset->GetName());
			}
			Config.AssetClassNames.Add(UGeometryCache::StaticClass()->GetClassPathName());
			Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;
			Config.DialogTitleOverride = LOCTEXT("ExportGeometryCacheDialogTitle", "Export Geometry Cache As");
		}

		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

#if WITH_EDITOR
		FString NewPackageName;
		FText OutError;
		for (bool bFilenameValid = false; !bFilenameValid; bFilenameValid = FFileHelper::IsFilenameValidForSaving(NewPackageName, OutError))
		{
			const FString AssetPath = ContentBrowserModule.Get().CreateModalSaveAssetDialog(Config);
			if (AssetPath.IsEmpty())
			{
				return nullptr;
			}
			NewPackageName = FPackageName::ObjectPathToPackageName(AssetPath);
		}
		return CreateOrLoad<UGeometryCache>(NewPackageName);
#else
		return nullptr;
#endif
	}
};

void UDataflowSimulationSceneDescription::NewGeometryCache()
{
	const UObject* const NamingAsset = CacheAsset? CacheAsset.Get() : nullptr;
	GeometryCacheAsset = UE::Dataflow::Private::NewGeometryCacheDialog(NamingAsset);
}

void UDataflowSimulationSceneDescription::SetSimulationScene(FDataflowSimulationScene* InSimulationScene)
{
	SimulationScene = InSimulationScene;
}

void UDataflowSimulationSceneDescription::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (SimulationScene)
	{
		SimulationScene->SceneDescriptionPropertyChanged(PropertyChangedEvent.GetMemberPropertyName());
	}

	DataflowSimulationSceneDescriptionChanged.Broadcast();
}

void UDataflowSimulationSceneDescription::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	Super::PostTransacted(TransactionEvent);

	// On Undo/Redo, PostEditChangeProperty just gets an empty FPropertyChangedEvent. However this function gets enough info to figure out which property changed
	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo && TransactionEvent.HasPropertyChanges())
	{
		const TArray<FName>& PropertyNames = TransactionEvent.GetChangedProperties();
		for (const FName& PropertyName : PropertyNames)
		{
			SimulationScene->SceneDescriptionPropertyChanged(PropertyName);
		}
	}
}

#undef LOCTEXT_NAMESPACE

