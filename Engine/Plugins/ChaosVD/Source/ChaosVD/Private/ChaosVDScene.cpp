// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDScene.h"

#include "Actors/ChaosVDSolverInfoActor.h"
#include "ChaosVDGeometryBuilder.h"
#include "ChaosVDModule.h"
#include "ChaosVDParticleActor.h"
#include "Chaos/ImplicitObject.h"
#include "ChaosVDRecording.h"
#include "ChaosVDSelectionCustomization.h"
#include "ChaosVDSettingsManager.h"
#include "ChaosVDSkySphereInterface.h"
#include "Components/ChaosVDSceneQueryDataComponent.h"
#include "Components/ChaosVDSolverCharacterGroundConstraintDataComponent.h"
#include "Components/ChaosVDSolverCollisionDataComponent.h"
#include "Components/ChaosVDSolverJointConstraintDataComponent.h"
#include "DataWrappers/ChaosVDParticleDataWrapper.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/Level.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Elements/Actor/ActorElementData.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementSelectionSet.h"
#include "Materials/Material.h"
#include "Misc/ScopedSlowTask.h"
#include "Selection.h"
#include "Actors/ChaosVDGameFrameInfoActor.h"
#include "Settings/ChaosVDCoreSettings.h"
#include "UObject/Package.h"
#include "Actors/ChaosVDGeometryContainer.h"
#include "Components/LightComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/TextureCube.h"

#define LOCTEXT_NAMESPACE "ChaosVisualDebugger"

FChaosVDScene::FChaosVDScene() = default;

FChaosVDScene::~FChaosVDScene() = default;

namespace ChaosVDSceneUIOptions
{
	constexpr float DelayToShowProgressDialogThreshold = 1.0f;
	constexpr bool bShowCancelButton = false;
	constexpr bool bAllowInPIE = false;
}

namespace Chaos::VisualDebugger::Cvars
{
	static bool bReInitializeGeometryBuilderOnCleanup = true;
	static FAutoConsoleVariableRef CVarChaosVDReInitializeGeometryBuilderOnCleanup(
		TEXT("p.Chaos.VD.Tool.ReInitializeGeometryBuilderOnCleanup"),
		bReInitializeGeometryBuilderOnCleanup,
		TEXT("If true, any static mesh component and static mesh component created will be destroyed when a new CVD recording is loaded"));
}

void FChaosVDScene::Initialize()
{
	if (!ensure(!bIsInitialized))
	{
		return;
	}

	InitializeSelectionSets();
	
	StreamableManager = MakeShared<FStreamableManager>();

	if (UChaosVDCoreSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCoreSettings>())
	{
		// TODO: Do an async load instead, and prepare a loading screen or notification popup
		// Jira for tracking UE-191639
		StreamableManager->RequestSyncLoad(Settings->QueryOnlyMeshesMaterial.ToSoftObjectPath());
		StreamableManager->RequestSyncLoad(Settings->SimOnlyMeshesMaterial.ToSoftObjectPath());
		StreamableManager->RequestSyncLoad(Settings->InstancedMeshesMaterial.ToSoftObjectPath());
		StreamableManager->RequestSyncLoad(Settings->InstancedMeshesQueryOnlyMaterial.ToSoftObjectPath());
		StreamableManager->RequestSyncLoad(Settings->AmbientCubeMapTexture.ToSoftObjectPath());
	}
	
	PhysicsVDWorld = CreatePhysicsVDWorld();

	GeometryGenerator = MakeShared<FChaosVDGeometryBuilder>();

	GeometryGenerator->Initialize(AsWeak());

	bIsInitialized = true;
}


void FChaosVDScene::PerformGarbageCollection()
{
	FScopedSlowTask CollectingGarbageSlowTask(1, LOCTEXT("CollectingGarbageDataMessage", "Collecting Garbage ..."));
	CollectingGarbageSlowTask.MakeDialog();

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	CollectingGarbageSlowTask.EnterProgressFrame();
}


void FChaosVDScene::DeInitialize()
{
	constexpr float AmountOfWork = 1.0f;
	FScopedSlowTask ClosingSceneSlowTask(AmountOfWork, LOCTEXT("ClosingSceneMessage", "Closing Scene ..."));
	ClosingSceneSlowTask.MakeDialog();

	if (!ensure(bIsInitialized))
	{
		return;
	}

	CleanUpScene();

	DeInitializeSelectionSets();

	GeometryGenerator.Reset();

	if (PhysicsVDWorld)
	{
		PhysicsVDWorld->RemoveOnActorDestroyedHandler(ActorDestroyedHandle);

		PhysicsVDWorld->DestroyWorld(true);
		GEngine->DestroyWorldContext(PhysicsVDWorld);

		PhysicsVDWorld->MarkAsGarbage();
		PhysicsVDWorld = nullptr;
	}

	PerformGarbageCollection();

	bIsInitialized = false;
}

void FChaosVDScene::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(PhysicsVDWorld);
	Collector.AddReferencedObject(SelectionSet);
	Collector.AddReferencedObject(ObjectSelection);
	Collector.AddReferencedObject(ActorSelection);
	Collector.AddReferencedObject(ComponentSelection);
	Collector.AddStableReferenceArray(&AvailableDataContainerActors);
}

void FChaosVDScene::UpdateFromRecordedStepData(const int32 SolverID, const FChaosVDStepData& InRecordedStepData, const FChaosVDSolverFrameData& InFrameData)
{
	AChaosVDSolverInfoActor* SolverSceneData = nullptr;
	if (AChaosVDSolverInfoActor** SolverSceneDataPtrPtr = SolverDataContainerBySolverID.Find(SolverID))
	{
		SolverSceneData = *SolverSceneDataPtrPtr;
	}
	else
	{
		UE_LOG(LogChaosVDEditor, Warning, TEXT("[%s] Attempted to playback a solver frame from an invalid solver container"), ANSI_TO_TCHAR(__FUNCTION__));	
	}

	if (!SolverSceneData)
	{
		return;
	}

	SolverSceneData->SetSimulationTransform(InFrameData.SimulationTransform);
	
	TSet<int32> ParticlesIDsInRecordedStepData;
	ParticlesIDsInRecordedStepData.Reserve(InRecordedStepData.RecordedParticlesData.Num());
	
	{
		constexpr float AmountOfWork = 1.0f;
		const float PercentagePerElement = 1.0f / InRecordedStepData.RecordedParticlesData.Num();

		const FText ProgressBarTitle = FText::Format(FTextFormat(LOCTEXT("ProcessingParticleData", "Processing Particle Data for {0} Solver with ID {1} ...")), FText::FromName(SolverSceneData->GetSolverName()), FText::AsNumber(SolverID));
		FScopedSlowTask UpdatingSceneSlowTask(AmountOfWork, ProgressBarTitle);
		UpdatingSceneSlowTask.MakeDialogDelayed(ChaosVDSceneUIOptions::DelayToShowProgressDialogThreshold, ChaosVDSceneUIOptions::bShowCancelButton, ChaosVDSceneUIOptions::bAllowInPIE);
	
		// Go over existing Particle VD Instances and update them or create them if needed 
		for (const TSharedPtr<FChaosVDParticleDataWrapper>& Particle : InRecordedStepData.RecordedParticlesData)
		{
			const int32 ParticleVDInstanceID = GetIDForRecordedParticleData(Particle);
			ParticlesIDsInRecordedStepData.Add(ParticleVDInstanceID);

			if (InRecordedStepData.ParticlesDestroyedIDs.Contains(ParticleVDInstanceID))
			{
				// Do not process the particle if it was destroyed in the same step
				continue;
			}

			if (AChaosVDParticleActor* ExistingParticleVDInstancePtr = SolverSceneData->GetParticleActor(ParticleVDInstanceID))
			{
				// We have new data for this particle, so re-activate the existing actor
				if (!ExistingParticleVDInstancePtr->IsActive())
				{
					ExistingParticleVDInstancePtr->SetIsActive(true);
				}

				ExistingParticleVDInstancePtr->UpdateFromRecordedParticleData(Particle, InFrameData.SimulationTransform);
			}
			else
			{
				if (AChaosVDParticleActor* NewParticleVDInstance = SpawnParticleFromRecordedData(Particle, InFrameData))
				{
					// TODO: Precalculate the max num of entries we would see in the loaded file, and use that number to pre-allocate this map
					SolverSceneData->RegisterParticleActor(ParticleVDInstanceID, NewParticleVDInstance);
				}
				else
				{
					//TODO: Handle this error
					ensure(false);
				}
			}
			
			UpdatingSceneSlowTask.EnterProgressFrame(PercentagePerElement);
		}
	}

	UpdateParticlesCollisionData(InRecordedStepData, SolverID);

	UpdateJointConstraintsData(InRecordedStepData, SolverID);
	
	const TMap<int32, TObjectPtr<AChaosVDParticleActor>>& AllSolverParticlesByID = SolverSceneData->GetAllParticleActorsByIDMap();

	for (const TPair<int32, TObjectPtr<AChaosVDParticleActor>>& ParticleActorWithID : AllSolverParticlesByID)
	{
		// If we are playing back a keyframe, the scene should only contain what it is in the recorded data
		const bool bShouldDestroyParticleAnyway = InFrameData.bIsKeyFrame && EnumHasAnyFlags(InRecordedStepData.StageFlags, EChaosVDSolverStageFlags::ExplicitStage) && !ParticlesIDsInRecordedStepData.Contains(ParticleActorWithID.Key);
		
		if (bShouldDestroyParticleAnyway || InFrameData.ParticlesDestroyedIDs.Contains(ParticleActorWithID.Key))
		{
			// In large maps moving at high speed (like when moving on a vehicle), level streaming adds/removes hundreds of actors (and therefore particles) constantly.
			// Destroying particle actors is expensive, specially if we need to spawn them again sooner as we will need to rebuild-them.
			// So, we deactivate them instead.

			// TODO: We need an actor pool system, so we can keep memory under control as well.
			if (AChaosVDParticleActor* ActorToDeactivate = ToRawPtr(ParticleActorWithID.Value))
			{
				if (IsObjectSelected(ActorToDeactivate))
				{
					ClearSelectionAndNotify();
				}

				ActorToDeactivate->SetIsActive(false);
			}

		}
	}
	
	OnSceneUpdated().Broadcast();
}

void FChaosVDScene::UpdateParticlesCollisionData(const FChaosVDStepData& InRecordedStepData, int32 SolverID)
{
	if (AChaosVDSolverInfoActor* SolverDataInfoContainer = SolverDataContainerBySolverID.FindChecked(SolverID))
	{
		if (UChaosVDSolverCollisionDataComponent* CollisionDataContainer = SolverDataInfoContainer->GetCollisionDataComponent())
		{
			CollisionDataContainer->UpdateCollisionData(InRecordedStepData.RecordedMidPhases);
		}
	}
}

void FChaosVDScene::UpdateJointConstraintsData(const FChaosVDStepData& InRecordedStepData, int32 SolverID)
{
	if (AChaosVDSolverInfoActor* SolverDataInfoContainer = SolverDataContainerBySolverID.FindChecked(SolverID))
	{
		if (UChaosVDSolverJointConstraintDataComponent* JointsDataContainer = SolverDataInfoContainer->GetJointsDataComponent())
		{
			JointsDataContainer->UpdateConstraintData(InRecordedStepData.RecordedJointConstraints);
		}
	}
}

void FChaosVDScene::HandleNewGeometryData(const Chaos::FConstImplicitObjectPtr& GeometryData, const uint32 GeometryID)
{
	if (TArray<IChaosVDGeometryOwnerInterface*>* ObjectsWaitingPtr = ObjectsWaitingForGeometry.Find(GeometryID))
	{
		TArray<IChaosVDGeometryOwnerInterface*>& ObjectsWaitingRef = *ObjectsWaitingPtr;
		for (IChaosVDGeometryOwnerInterface* ObjectWaiting : ObjectsWaitingRef)
		{
			if (ObjectWaiting)
			{
				ObjectWaiting->HandleNewGeometryLoaded(GeometryID, GeometryData);
			}
		}

		// Keep the array allocated in case another particle needs to go to the waiting list
		ObjectsWaitingRef.Reset();
	}
}

AChaosVDSolverInfoActor* FChaosVDScene::GetOrCreateSolverInfoActor(int32 SolverID)
{
	if (AChaosVDSolverInfoActor** SolverInfoActorPtrPtr = SolverDataContainerBySolverID.Find(SolverID))
	{
		return *SolverInfoActorPtrPtr;
	}
	
	AChaosVDSolverInfoActor* SolverDataInfo = PhysicsVDWorld->SpawnActor<AChaosVDSolverInfoActor>();
	check(SolverDataInfo);

	FName SolverName = LoadedRecording->GetSolverFName_AssumedLocked(SolverID);
	FString NameAsString = SolverName.ToString();
	const bool bIsServer = NameAsString.Contains(TEXT("Server"));

	const FStringFormatOrderedArguments Args {NameAsString, FString::FromInt(SolverID)};
	const FName FolderPath = *FString::Format(TEXT("Solver {0} | ID {1}"), Args);

	SolverDataInfo->SetFolderPath(FolderPath);

	SolverDataInfo->SetSolverID(SolverID);
	SolverDataInfo->SetSolverName(SolverName);
	SolverDataInfo->SetScene(AsWeak());
	SolverDataInfo->SetIsServer(bIsServer);

	SolverDataContainerBySolverID.Add(SolverID, SolverDataInfo);
	AvailableDataContainerActors.Add(SolverDataInfo);

	SolverInfoActorCreatedDelegate.Broadcast(SolverDataInfo);

	return SolverDataInfo;
}

AChaosVDGameFrameInfoActor* FChaosVDScene::GetOrCreateGameFrameInfoActor()
{
	if (!GameFrameDataInfoActor)
	{
		const FName FolderPath("ChaosVisualDebugger/GameFrameData");

		GameFrameDataInfoActor = PhysicsVDWorld->SpawnActor<AChaosVDGameFrameInfoActor>();
		GameFrameDataInfoActor->SetFolderPath(FolderPath);
		GameFrameDataInfoActor->SetScene(AsWeak());
		AvailableDataContainerActors.Add(GameFrameDataInfoActor);
	}

	return GameFrameDataInfoActor;
}

void FChaosVDScene::HandleEnterNewGameFrame(int32 FrameNumber, const TArray<int32, TInlineAllocator<16>>& AvailableSolversIds, const FChaosVDGameFrameData& InNewGameFrameData, TArray<int32, TInlineAllocator<16>>& OutRemovedSolversIds)
{
	// Currently the particle actors from all the solvers are in the same level, and we manage them by keeping track
	// of to which solvers they belong using maps.
	// Using Level instead or a Sub ChaosVDScene could be a better solution
	// I'm intentionally not making that change right now until the "level streaming" solution for the tool is defined
	// As that would impose restriction on how levels could be used. For now the map approach is simpler and will be easier to refactor later on.

	TSet<int32> AvailableSolversSet;
	AvailableSolversSet.Reserve(AvailableSolversIds.Num());

	for (int32 SolverID : AvailableSolversIds)
	{
		AvailableSolversSet.Add(SolverID);

		if (AChaosVDSolverInfoActor* SolverInfoActor = GetOrCreateSolverInfoActor(SolverID))
		{
			SolverInfoActor->UpdateFromNewGameFrameData(InNewGameFrameData);
		}
	}

	int32 AmountRemoved = 0;

	for (TMap<int32, AChaosVDSolverInfoActor*>::TIterator RemoveIterator = SolverDataContainerBySolverID.CreateIterator(); RemoveIterator; ++RemoveIterator)
	{
		if (!AvailableSolversSet.Contains(RemoveIterator.Key()))
		{
			UE_LOG(LogChaosVDEditor, Log, TEXT("[%s] Removing Solver [%d] as it is no longer present in the recording"), ANSI_TO_TCHAR(__FUNCTION__), RemoveIterator.Key());

			if (AChaosVDSolverInfoActor* SolverInfoActor = RemoveIterator.Value())
			{
				AvailableDataContainerActors.Remove(SolverInfoActor);
				PhysicsVDWorld->DestroyActor(SolverInfoActor);
			}

			OutRemovedSolversIds.Add(RemoveIterator.Key());

			RemoveIterator.RemoveCurrent();
			AmountRemoved++;
		}
	}

	if (AmountRemoved > 0)
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}

	if (AChaosVDGameFrameInfoActor* GameFrameDataContainer = GetOrCreateGameFrameInfoActor())
	{
		GameFrameDataContainer->UpdateFromNewGameFrameData(InNewGameFrameData);
	}
}

void FChaosVDScene::HandleEnterNewSolverFrame(int32 FrameNumber, const FChaosVDSolverFrameData& InFrameData)
{
	if (AChaosVDSolverInfoActor** SolverDataInfoContainerPtrPtr = SolverDataContainerBySolverID.Find(InFrameData.SolverID))
	{
		UChaosVDSolverCharacterGroundConstraintDataComponent* DataContainer = SolverDataInfoContainerPtrPtr ? (*SolverDataInfoContainerPtrPtr)->GetCharacterGroundConstraintDataComponent() : nullptr;

		// TODO: Some times when playback is stopped, we might not have all the solver info actors ready when we go to the first frame
		// This should not happen. For now I am making a change to avoid the crash and ensure. I created UE-217610 to find the issue and prepare a proper fix
		if (ensure(DataContainer))
		{
			DataContainer->UpdateConstraintData(InFrameData.RecordedCharacterGroundConstraints);
		}
	}
}

void FChaosVDScene::CleanUpScene(EChaosVDSceneCleanUpOptions Options)
{
	// AvailableDataContainerActors should always be at least the number of solver actors created
	ensure(AvailableDataContainerActors.Num() >= SolverDataContainerBySolverID.Num());

	if (AvailableDataContainerActors.Num() > 0)
	{
		constexpr float AmountOfWork = 1.0f;
		const float PercentagePerElement = 1.0f / AvailableDataContainerActors.Num();

		FScopedSlowTask CleaningSceneSlowTask(AmountOfWork, LOCTEXT("CleaningupSceneSolverMessage", "Clearing Solver Data ..."));
		CleaningSceneSlowTask.MakeDialog();

		ClearSelectionAndNotify();

		if (PhysicsVDWorld)
		{
			for (TObjectPtr<AChaosVDDataContainerBaseActor>& DataContainerActor : AvailableDataContainerActors)
			{
				if (DataContainerActor)
				{
					PhysicsVDWorld->DestroyActor(DataContainerActor.Get());
				}

				CleaningSceneSlowTask.EnterProgressFrame(PercentagePerElement);
			}
		}

		AvailableDataContainerActors.Reset();
		SolverDataContainerBySolverID.Reset();
		GameFrameDataInfoActor = nullptr;
	}

	if (Chaos::VisualDebugger::Cvars:: bReInitializeGeometryBuilderOnCleanup && EnumHasAnyFlags(Options, EChaosVDSceneCleanUpOptions::ReInitializeGeometryBuilder))
	{
		if (AChaosVDGeometryContainer* AsGeometryContainer = Cast<AChaosVDGeometryContainer>(MeshComponentContainerActor))
		{
			AsGeometryContainer->CleanUp();
		}

		GeometryGenerator->DeInitialize();
		GeometryGenerator.Reset();
	
		GeometryGenerator = MakeShared<FChaosVDGeometryBuilder>();
		GeometryGenerator->Initialize(AsWeak());
	}

	if (EnumHasAnyFlags(Options, EChaosVDSceneCleanUpOptions::CollectGarbage))
	{
		PerformGarbageCollection();
	}
}

Chaos::FConstImplicitObjectPtr FChaosVDScene::GetUpdatedGeometry(int32 GeometryID) const
{
	if (ensure(LoadedRecording.IsValid()))
	{
		if (const Chaos::FConstImplicitObjectPtr* Geometry = LoadedRecording->GetGeometryMap().Find(GeometryID))
		{
			return *Geometry;
		}
		else
		{
			UE_LOG(LogChaosVDEditor, Warning, TEXT("Geometry for key [%d] is not loaded in the recording yet"), GeometryID);
		}
	}

	return nullptr;
}

void FChaosVDScene::AddObjectWaitingForGeometry(uint32 GeometryID, IChaosVDGeometryOwnerInterface* ObjectWaitingForGeometry)
{
	if (!ObjectWaitingForGeometry)
	{
		return;
	}

	ObjectsWaitingForGeometry.FindOrAdd(GeometryID).Add(ObjectWaitingForGeometry);
}

AChaosVDParticleActor* FChaosVDScene::GetParticleActor(int32 SolverID, int32 ParticleID)
{
	if (AChaosVDSolverInfoActor** SolverDataInfo = SolverDataContainerBySolverID.Find(SolverID))
	{
		return (*SolverDataInfo)->GetParticleActor(ParticleID);
	}

	return nullptr;
}

AChaosVDSolverInfoActor* FChaosVDScene::GetSolverInfoActor(int32 SolverID)
{
	if (AChaosVDSolverInfoActor** SolverDataInfo = SolverDataContainerBySolverID.Find(SolverID))
	{
		return *SolverDataInfo;
	}

	return nullptr;
}

bool FChaosVDScene::IsSolverForServer(int32 SolverID) const
{
	if (const AChaosVDSolverInfoActor* const * PSolverDataInfo = SolverDataContainerBySolverID.Find(SolverID))
	{
		const AChaosVDSolverInfoActor* SolverDataInfo = *PSolverDataInfo;
		return SolverDataInfo->GetIsServer();
	}

	return false;
}

AChaosVDParticleActor* FChaosVDScene::SpawnParticleFromRecordedData(const TSharedPtr<FChaosVDParticleDataWrapper>& InParticleData, const FChaosVDSolverFrameData& InFrameData)
{
	using namespace Chaos;

	if (!InParticleData.IsValid())
	{
		return nullptr;
	}

	if (AChaosVDParticleActor* NewActor = PhysicsVDWorld->SpawnActor<AChaosVDParticleActor>())
	{
		NewActor->SetIsActive(true);
		NewActor->SetScene(AsShared());
		NewActor->SetIsServerParticle(IsSolverForServer(InParticleData->SolverID));
		NewActor->UpdateFromRecordedParticleData(InParticleData, InFrameData.SimulationTransform);

		// CVD's Outliner mode will update the label based on the particle data without needing to go trough all the code that Set Actor lable goes trough
		// which can take +0.1 sec per actor
		ParticleLabelUpdateDelegate.Broadcast(NewActor);

		return NewActor;
	}

	return nullptr;
}

int32 FChaosVDScene::GetIDForRecordedParticleData(const TSharedPtr<FChaosVDParticleDataWrapper>& InParticleData) const
{
	return InParticleData ? InParticleData->ParticleIndex : INDEX_NONE;
}

void FChaosVDScene::CreateBaseLights(UWorld* TargetWorld) const
{
	if (!TargetWorld)
	{
		return;
	}

	const FName LightingFolderPath("ChaosVisualDebugger/Lighting");

	const FVector SpawnPosition(0.0, 0.0, 2000.0);
	
	if (const UChaosVDCoreSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCoreSettings>())
	{
		if (ADirectionalLight* DirectionalLightActor = TargetWorld->SpawnActor<ADirectionalLight>())
		{
			DirectionalLightActor->SetCastShadows(false);
			DirectionalLightActor->SetMobility(EComponentMobility::Movable);
			DirectionalLightActor->SetActorLocation(SpawnPosition);
			
			DirectionalLightActor->SetBrightness(4.0f);

			DirectionalLightActor->SetFolderPath(LightingFolderPath);

			TSubclassOf<AActor> SkySphereClass = Settings->SkySphereActorClass.TryLoadClass<AActor>();
			SkySphere = TargetWorld->SpawnActor(SkySphereClass.Get());
			if (SkySphere)
			{
				SkySphere->SetActorLocation(SpawnPosition);
				SkySphere->SetFolderPath(LightingFolderPath);
				
				if (SkySphere->Implements<UChaosVDSkySphereInterface>())
				{
					FEditorScriptExecutionGuard AllowEditorScriptGuard;
					IChaosVDSkySphereInterface::Execute_SetDirectionalLightSource(SkySphere, DirectionalLightActor);
				}

				// Keep it dark to reduce visual noise.
				// TODO: We should hide these components altogether when we switch to a unlit wireframe mode 
				const TSet<UActorComponent*>& Components = SkySphere->GetComponents();
				for (UActorComponent* Component : Components)
				{
					if (UStaticMeshComponent* AsStaticMeshComponent = Cast<UStaticMeshComponent>(Component))
					{
						AsStaticMeshComponent->bOverrideWireframeColor = true;
						AsStaticMeshComponent->WireframeColorOverride = FColor::Black;
					}
				}
			}
		}
	}
}

void FChaosVDScene::CreatePostProcessingVolumes(UWorld* TargetWorld)
{
	const FName LightingFolderPath("ChaosVisualDebugger/Lighting");

	if (const UChaosVDCoreSettings* Settings = FChaosVDSettingsManager::Get().GetSettingsObject<UChaosVDCoreSettings>())
	{
		APostProcessVolume* PostProcessingVolume = TargetWorld->SpawnActor<APostProcessVolume>();
		if (ensure(PostProcessingVolume))
		{
			PostProcessingVolume->SetFolderPath(LightingFolderPath);
			PostProcessingVolume->Settings.bOverride_AmbientCubemapIntensity = true;
			PostProcessingVolume->Settings.AmbientCubemapIntensity = 0.3f;
			PostProcessingVolume->bUnbound = true;
			PostProcessingVolume->bEnabled = true;

			UTextureCube* AmbientCubemap = Settings->AmbientCubeMapTexture.Get();
			if (ensure(AmbientCubemap))
			{
				PostProcessingVolume->Settings.AmbientCubemap = AmbientCubemap;
			}
			
			PostProcessingVolume->MarkComponentsRenderStateDirty();
		}
	}
}

AActor* FChaosVDScene::CreateMeshComponentsContainer(UWorld* TargetWorld)
{
	const FName GeometryFolderPath("ChaosVisualDebugger/GeneratedMeshComponents");

	MeshComponentContainerActor = TargetWorld->SpawnActor<AChaosVDGeometryContainer>();
	MeshComponentContainerActor->SetFolderPath(GeometryFolderPath);

	return MeshComponentContainerActor;
}

UWorld* FChaosVDScene::CreatePhysicsVDWorld()
{
	const FName UniqueWorldName = FName(FGuid::NewGuid().ToString());
	UWorld* NewWorld = NewObject<UWorld>( GetTransientPackage(), UniqueWorldName );
	
	NewWorld->WorldType = EWorldType::EditorPreview;

	FWorldContext& WorldContext = GEngine->CreateNewWorldContext( NewWorld->WorldType );
	WorldContext.SetCurrentWorld(NewWorld);

	NewWorld->InitializeNewWorld( UWorld::InitializationValues()
										  .AllowAudioPlayback( false )
										  .CreatePhysicsScene( false )
										  .RequiresHitProxies( true )
										  .CreateNavigation( false )
										  .CreateAISystem( false )
										  .ShouldSimulatePhysics( false )
										  .SetTransactional( false )
	);

	if (ULevel* Level = NewWorld->GetCurrentLevel())
	{
		Level->SetUseActorFolders(true);
	}

	CreateBaseLights(NewWorld);
	CreateMeshComponentsContainer(NewWorld);
	CreatePostProcessingVolumes(NewWorld);

	ActorDestroyedHandle = NewWorld->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateRaw(this, &FChaosVDScene::HandleActorDestroyed));
	
	return NewWorld;
}

FTypedElementHandle FChaosVDScene::GetSelectionHandleForObject(const UObject* Object) const
{
	FTypedElementHandle Handle;
	if (const AActor* Actor = Cast<AActor>(Object))
	{
		Handle = UEngineElementsLibrary::AcquireEditorActorElementHandle(Actor);
	}
	else if (const UActorComponent* Component = Cast<UActorComponent>(Object))
	{
		Handle = UEngineElementsLibrary::AcquireEditorComponentElementHandle(Component);
	}
	else
	{
		Handle = UEngineElementsLibrary::AcquireEditorObjectElementHandle(Object);
	}

	return Handle;
}

void FChaosVDScene::UpdateSelectionProxiesForActors(TArrayView<AActor*> SelectedActors)
{
	for (AActor* SelectedActor : SelectedActors)
	{
		if (SelectedActor)
		{
			SelectedActor->PushSelectionToProxies();
		}
	}
}

void FChaosVDScene::HandleDeSelectElement(const TTypedElement<ITypedElementSelectionInterface>& InElementSelectionHandle, FTypedElementListRef InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	if (AActor* DeselectedActor = ActorElementDataUtil::GetActorFromHandle(InElementSelectionHandle))
	{
		if (IChaosVDSelectableObject* SelectionAwareActor = Cast<IChaosVDSelectableObject>(DeselectedActor))
		{
			SelectionAwareActor->HandleDeSelected();
		}
	}

	// TODO: Add support for Component and Object Selection Events - This will be needed when we move away from using actors to represent particles
}

void FChaosVDScene::HandleSelectElement(const TTypedElement<ITypedElementSelectionInterface>& InElementSelectionHandle, FTypedElementListRef InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	if (AActor* SelectedActor = ActorElementDataUtil::GetActorFromHandle(InElementSelectionHandle))
	{
		if (IChaosVDSelectableObject* SelectionAwareActor = Cast<IChaosVDSelectableObject>(SelectedActor))
		{
			SelectionAwareActor->HandleSelected();
		}
	}

	// TODO: Add support for Component and Object Selection Events - This will be needed when we move away from using actors to represent particles
}

void FChaosVDScene::ClearSelectionAndNotify()
{
	if (!SelectionSet)
	{
		return;
	}

	SelectionSet->ClearSelection(FTypedElementSelectionOptions());
	SelectionSet->NotifyPendingChanges();
}

void FChaosVDScene::InitializeSelectionSets()
{
	SelectionSet = NewObject<UTypedElementSelectionSet>(GetTransientPackage(), NAME_None, RF_Transactional);
	SelectionSet->AddToRoot();

	SelectionSet->RegisterInterfaceCustomizationByTypeName(NAME_Actor, MakeUnique<FChaosVDSelectionCustomization>(AsShared()));
	SelectionSet->RegisterInterfaceCustomizationByTypeName(NAME_Components, MakeUnique<FChaosVDSelectionCustomization>(AsShared()));
	SelectionSet->RegisterInterfaceCustomizationByTypeName(NAME_Object, MakeUnique<FChaosVDSelectionCustomization>(AsShared()));

	FString ActorSelectionObjectName = FString::Printf(TEXT("CVDSelectedActors-%s"), *FGuid::NewGuid().ToString());
	ActorSelection = USelection::CreateActorSelection(GetTransientPackage(), *ActorSelectionObjectName, RF_Transactional);
	ActorSelection->SetElementSelectionSet(SelectionSet);

	FString ComponentSelectionObjectName = FString::Printf(TEXT("CVDSelectedComponents-%s"), *FGuid::NewGuid().ToString());
	ComponentSelection = USelection::CreateComponentSelection(GetTransientPackage(), *ComponentSelectionObjectName, RF_Transactional);
	ComponentSelection->SetElementSelectionSet(SelectionSet);

	FString ObjectSelectionObjectName = FString::Printf(TEXT("CVDSelectedObjects-%s"), *FGuid::NewGuid().ToString());
	ObjectSelection = USelection::CreateObjectSelection(GetTransientPackage(), *ObjectSelectionObjectName, RF_Transactional);
	ObjectSelection->SetElementSelectionSet(SelectionSet);

	SolverDataSelectionObject = MakeShared<FChaosVDSolverDataSelection>();
}

void FChaosVDScene::DeInitializeSelectionSets()
{
	ActorSelection->SetElementSelectionSet(nullptr);
	ComponentSelection->SetElementSelectionSet(nullptr);
	ObjectSelection->SetElementSelectionSet(nullptr);

	SelectionSet->OnPreChange().RemoveAll(this);
	SelectionSet->OnChanged().RemoveAll(this);
}

void FChaosVDScene::HandleActorDestroyed(AActor* ActorDestroyed)
{
	if (IsObjectSelected(ActorDestroyed))
	{
		ClearSelectionAndNotify();
	}
}

void FChaosVDScene::SetSelectedObject(UObject* SelectedObject)
{
	if (!SelectionSet)
	{
		return;
	}

	if (!::IsValid(SelectedObject))
	{
		ClearSelectionAndNotify();
		return;
	}

	if (IsObjectSelected(SelectedObject))
	{
		// Already selected, nothing to do here
		return;
	}

	SelectionSet->ClearSelection(FTypedElementSelectionOptions());

	TArray<FTypedElementHandle> NewEditorSelection = { GetSelectionHandleForObject(SelectedObject) };

	SelectionSet->SetSelection(NewEditorSelection, FTypedElementSelectionOptions());
	SelectionSet->NotifyPendingChanges();
}

bool FChaosVDScene::IsObjectSelected(const UObject* Object)
{
	if (!SelectionSet)
	{
		return false;
	}

	if (!::IsValid(Object))
	{
		return false;
	}

	return SelectionSet->IsElementSelected(GetSelectionHandleForObject(Object), FTypedElementIsSelectedOptions());;
}

#undef LOCTEXT_NAMESPACE
