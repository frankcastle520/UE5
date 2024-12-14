// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionStaticLightingBuilder.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Algo/ForEach.h"
#include "UObject/SavePackage.h"
#include "FileHelpers.h"

#include "EngineUtils.h"
#include "EngineModule.h"
#include "SourceControlHelpers.h"

#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "SourceControlOperations.h"

#include "Editor.h"

#include "WorldPartition/WorldPartitionHelpers.h"

#include "LightingBuildOptions.h"

#include "HAL/IConsoleManager.h"
#include "Engine/MapBuildDataRegistry.h"
#include "WorldPartition/StaticLightingData/MapBuildDataActor.h"

#include "WorldPartition/WorldPartitionStreamingDescriptor.h"
#include "Components/LightComponentBase.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "LevelInstance/LevelInstanceActor.h"
#include "LevelInstance/LevelInstanceComponent.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "ActorFolder.h"
#include "UObject/Linker.h" // For ResetLoaders
#include "StaticMeshComponentLODInfo.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionStaticLightingBuilder, All, All);

static const FString StaticLightingMappingsWorkingDirName = TEXT("StaticLightingMappingsTemp");

using FStreamingDescriptor = UE::Private::WorldPartition::FStreamingDescriptor;

UWorldPartitionStaticLightingBuilder::UWorldPartitionStaticLightingBuilder(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)	
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	FStaticLightingDescriptors::Set(&Descriptors);

	SourceControlHelper = nullptr;
	
	bBuildVLMOnly = FParse::Param(FCommandLine::Get(), TEXT("BuildVLMOnly"));

	QualityLevel = ELightingBuildQuality::Quality_Preview;

	BuildOptions |= FParse::Param(FCommandLine::Get(), TEXT("Delete")) ? EWPStaticLightingBuildStep::WPSL_Delete : EWPStaticLightingBuildStep::None;
	BuildOptions |= FParse::Param(FCommandLine::Get(), TEXT("Submit")) ? EWPStaticLightingBuildStep::WPSL_Submit : EWPStaticLightingBuildStep::None;
	BuildOptions |= FParse::Param(FCommandLine::Get(), TEXT("Build")) ? EWPStaticLightingBuildStep::WPSL_Build|EWPStaticLightingBuildStep::WPSL_Finalize: EWPStaticLightingBuildStep::None;	
	BuildOptions |= FParse::Param(FCommandLine::Get(), TEXT("Finalize")) ? EWPStaticLightingBuildStep::WPSL_Finalize: EWPStaticLightingBuildStep::None;	

	bForceSinglePass = FParse::Param(FCommandLine::Get(), TEXT("SinglePass"));
	bSaveDirtyPackages = FParse::Param(FCommandLine::Get(), TEXT("SaveAllDirtyPackages"));
	
	// Default behavior without any option is to build and finalize
	if (BuildOptions == EWPStaticLightingBuildStep::None)
	{
		BuildOptions = EWPStaticLightingBuildStep::WPSL_Build | EWPStaticLightingBuildStep::WPSL_Finalize;
	}

	// Parse quality level and limit to valid values
	int32 QualityLevelVal;
	FParse::Value(FCommandLine::Get(), TEXT("QualityLevel="), QualityLevelVal);
	QualityLevelVal = FMath::Clamp<int32>(QualityLevelVal, Quality_Preview, Quality_Production);
	QualityLevel = (ELightingBuildQuality)QualityLevelVal;

	// Setup mappings directory
	FParse::Value(FCommandLine::Get(), TEXT("MappingDirectory"), MappingsDirectory);
}

bool UWorldPartitionStaticLightingBuilder::RequiresCommandletRendering() const
{
	// The Lightmass export process uses the renderer to generate some data so we need rendering
	return true; 
}

bool UWorldPartitionStaticLightingBuilder::ShouldRunStep(const EWPStaticLightingBuildStep BuildStep) const
{
	return (BuildOptions & BuildStep) == BuildStep;
}

UWorldPartitionBuilder::ELoadingMode UWorldPartitionStaticLightingBuilder::GetLoadingMode() const 
{
	if (bForceSinglePass)
	{
		return ELoadingMode::EntireWorld;
	}

	// Until all issues are fixed with iterative mode
	return ELoadingMode::EntireWorld;
}

bool UWorldPartitionStaticLightingBuilder::ValidateParams() const
{
	return true;
}

bool UWorldPartitionStaticLightingBuilder::PreWorldInitialization(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	bool bResult = true;

	WorldPartition = World->GetWorldPartition();
	check(WorldPartition);

	if (MappingsDirectory.IsEmpty())
	{
		uint32 WorldPackageHash = GetTypeHash(World->GetPackage()->GetFullName());
		MappingsDirectory = FString::Printf(TEXT("%s/%s/%08x"), *FPaths::RootDir(), *StaticLightingMappingsWorkingDirName, WorldPackageHash);
	}

	bResult &= ValidateParams();

	// Delete intermediate results unless you're only finalizing
	if (ShouldRunStep(EWPStaticLightingBuildStep::WPSL_Build) ||
		ShouldRunStep(EWPStaticLightingBuildStep::WPSL_Delete))
	{
		bResult &= DeleteIntermediates();
	}

	return bResult;
}

bool UWorldPartitionStaticLightingBuilder::DeleteIntermediates()
{
	bool bResult = true;

	TArray<FString> Files;	
	
	IFileManager::Get().FindFiles(Files, *MappingsDirectory, TEXT(".lm"));

	for (const FString& File : Files)
	{
		FString FileName = FString::Printf(TEXT("%s\\%s"), *MappingsDirectory, *File);

		bool bDeleteResult = IFileManager::Get().Delete(*FileName);
		if (!bDeleteResult)
		{
			UE_LOG(LogWorldPartitionStaticLightingBuilder, Warning, TEXT("Could not delete intermediate file %s"), *FileName);
		}

		bResult &= bDeleteResult;
	}

	return bResult;
}

bool UWorldPartitionStaticLightingBuilder::PreRun(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{	
	Descriptors.InitializeFromWorld(World);

	if (ShouldRunStep(EWPStaticLightingBuildStep::WPSL_Finalize))
	{
		// Immediately delete stale packages if we'll be finalizing
		DeleteStalePackages(PackageHelper);
	}

	bool bResult = true;

	// Delete actors before we start loading world content
	if (ShouldRunStep(EWPStaticLightingBuildStep::WPSL_Delete))
	{
		bResult &= DeleteStaticLightingData(World, PackageHelper);
	}

	return bResult;
}
 
bool UWorldPartitionStaticLightingBuilder::RunInternal(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	// Ensure LevelInstances are loaded
	World->BlockTillLevelStreamingCompleted();

	TArray<FWorldPartitionReference> HLODRefs;
	// Force load all HLODs
	for (FActorDescContainerInstanceCollection::TIterator<AWorldPartitionHLOD> HLODIterator(WorldPartition); HLODIterator; ++HLODIterator)
	{
		FWorldPartitionReference HLODActorReference(WorldPartition, HLODIterator->GetGuid());
		HLODRefs.Add(HLODActorReference);

		// Transfer HLOD layer to appropriate cell in descriptors
		if (AActor* HLODActor = HLODActorReference.GetActor())
		{
			// Get the CellDesc through the actor and update the runtime grid
			if (FLightingActorDesc* HLODActorDesc = Descriptors.ActorGuidsToDesc.Find(HLODActor->GetActorInstanceGuid()))
			{
				if (FLightingCellDesc* CellDesc = Descriptors.LightingCellsDescs.Find(HLODActorDesc->CellLevelPackage))
				{
					CellDesc->RuntimeGrid = HLODActor->GetRuntimeGrid();
				}
				else
				{
					UE_LOG(LogWorldPartitionStaticLightingBuilder, Warning, TEXT("Could not locate owning cell descriptors (CellPackage %s) for HLOD actor %s"), *HLODActorDesc->CellLevelPackage.ToString(), *HLODIterator->GetActorLabelOrName().ToString());
				}
			}
		}
	}

	bool bRet = true;

	SourceControlHelper = MakeUnique<FSourceControlHelper>(PackageHelper, ModifiedFiles);

	if (ShouldRunStep(EWPStaticLightingBuildStep::WPSL_Build))
	{
		if (bBuildVLMOnly)
		{
			bRet = RunForVLM(World, InCellInfo, PackageHelper);
		}
		else
		{
			bRet = Run(World, InCellInfo, PackageHelper);
		}

	}

	if (bRet && ShouldRunStep(EWPStaticLightingBuildStep::WPSL_Finalize))
	{
		Finalize(World, PackageHelper);
	}

	if (bRet && ShouldRunStep(EWPStaticLightingBuildStep::WPSL_Submit))
	{
		bRet = Submit(World, PackageHelper);
	}

	SourceControlHelper.Reset();

	return bRet;
}

bool UWorldPartitionStaticLightingBuilder::Submit(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{	
	// Wait for pending async file writes before submitting
	UPackage::WaitForAsyncFileWrites();

	const FString ChangeDescription = FString::Printf(TEXT("Rebuilt static lighting for %s"), *World->GetPackage()->GetName());
	return OnFilesModified(ModifiedFiles.GetAllFiles(), ChangeDescription);
}

bool UWorldPartitionStaticLightingBuilder::DeletePackage(FStaticLightingDescriptors::FActorPackage& Package, FPackageSourceControlHelper& PackageHelper)
{
	bool bResult = true;

	if (Package.Guid.IsValid())
	{
		WorldPartition->RemoveActor(Package.Guid);
	}

	 if (UPackage* PackagePtr = FindObject<UPackage>(nullptr, *Package.PackageName.ToString()))
	 {
		ResetLoaders(PackagePtr);
	 }
	
	bResult &= PackageHelper.Delete(Package.PackageName.ToString());

	return bResult;
}

bool UWorldPartitionStaticLightingBuilder::DeleteStaticLightingData(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	TArray<FStaticLightingDescriptors::FActorPackage> PackagesToDelete;
	TArray<UPackage*> PackagesToSave;
		
	bool bResult = true;
	
	PackagesToDelete.Append(Descriptors.MapDataActorsPackage);
	PackagesToDelete.Append(Descriptors.StaleMapDataActorsPackage);
		
	bool bSaveWorld = false;
	if (UMapBuildDataRegistry* WorldRegistry = World->PersistentLevel->MapBuildData)
	{
		TWeakObjectPtr<UMapBuildDataRegistry> WeakRegistryPtr(WorldRegistry);
		PackagesToDelete.Add({WorldRegistry->GetPackage()->GetFName(), FGuid() });
		WorldRegistry->InvalidateStaticLighting(World, false);
		WorldRegistry->ClearFlags(RF_Standalone);
		ResetLoaders(WorldRegistry->GetPackage());
		
		World->MarkPackageDirty();
		World->PersistentLevel->MapBuildData = nullptr;

		FWorldPartitionHelpers::DoCollectGarbage();
		bSaveWorld = true;
	}

	if (UActorFolder* Folder = World->PersistentLevel->GetActorFolder("MapBuildData"))
	{
		if (!Folder->IsMarkedAsDeleted())
		{
			UE_LOG(LogWorldPartitionStaticLightingBuilder, Log, TEXT("Deleting and saving ActorFolder %s"), *Folder->GetFullName());
			Folder->MarkAsDeleted();
			PackagesToSave.Add(Folder->GetPackage());
		}
	}

	if (bSaveWorld)
	{
		UE_LOG(LogWorldPartitionStaticLightingBuilder, Log, TEXT("Saving World %s to unreference MapBuildData"), *World->GetFullName());
		TArray<UPackage*> Packages;
		PackagesToSave.Add(World->GetPackage());
	}	

	bResult &= SavePackages(PackagesToSave, PackageHelper, true);
	
	UE_LOG(LogWorldPartitionStaticLightingBuilder, Log, TEXT("Deleting AMapBuildData Actors"));
	for (FStaticLightingDescriptors::FActorPackage& PackageToDelete : PackagesToDelete)
	{
		UE_LOG(LogWorldPartitionStaticLightingBuilder, Log, TEXT("   => Deleting %s"), *PackageToDelete.PackageName.ToString());
		bResult &= DeletePackage(PackageToDelete, PackageHelper);
	}

	// flush out all the AMapBuildData actors info 
	for (TPair<FName, FLightingCellDesc> Cell : Descriptors.LightingCellsDescs)
	{
		Cell.Value.DataActor.Reset();
		Cell.Value.MapBuildData.Reset();
	}
	
	return bResult;
}

bool UWorldPartitionStaticLightingBuilder::Run(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	bool bRet = true;	

	UE_LOG(LogWorldPartitionStaticLightingBuilder, Verbose, TEXT("Building Volumetric Lightmaps for %s"), *World->GetName());
	 
	// Invoke static lighting computation
	FLightingBuildOptions LightingOptions;
	LightingOptions.QualityLevel =  QualityLevel;
	LightingOptions.MappingsDirectory = MappingsDirectory;

	bool bLightingBuildFailed = false;

	ULevelInstanceSubsystem* LevelInstanceSystem = World->GetSubsystem<ULevelInstanceSubsystem>();

	auto BuildFailedDelegate = [&bLightingBuildFailed, &World]() {		
		UE_LOG(LogWorldPartitionStaticLightingBuilder, Error, TEXT("[REPORT] Failed building lighting for %s"), *World->GetName());
		bLightingBuildFailed = true;
	};
	
	auto GatherPrecomputedLightingGuids = [] (AActor* InActor) -> TArray<FGuid>
	{
		TSet<FGuid> Guids;

		// Check ULightComponents & UPrimitiveComponents
		InActor->ForEachComponent<UPrimitiveComponent>(false, [&Guids](UPrimitiveComponent* Primitive)
		{
			Primitive->AddMapBuildDataGUIDs(Guids);
		});
		
		InActor->ForEachComponent<ULightComponentBase>(false, [&Guids](ULightComponentBase* Light)
		{
			Guids.Add(Light->LightGuid);
		});

		return Guids.Array();
	};

	// Associate with loaded actors
	for (TActorIterator<AActor> It(World); It; ++It)
	{		
		AActor* Actor = *It;		

		FGuid ActorGuid= Actor->GetActorInstanceGuid();
		UE_LOG(LogWorldPartitionStaticLightingBuilder, Verbose, TEXT("Iterating Actor %s, ActorInstance Guid %s"), *Actor->GetActorNameOrLabel() , *ActorGuid.ToString());

		if (!Actor->GetIsSpatiallyLoaded())
		{
			// Those Actors won't be discovered through the StreamingDesc so add them manually
			FLightingActorDesc& LightingActorDesc = Descriptors.ActorGuidsToDesc.Add(ActorGuid);
			LightingActorDesc.ActorGuid = ActorGuid;
		}
		
		if (AActor* LevelInstanceActor = Cast<AActor>(LevelInstanceSystem->GetOwningLevelInstance(Actor->GetLevel())))
		{
			if (FLightingActorDesc* LevelInstanceActorDesc = Descriptors.ActorGuidsToDesc.Find(LevelInstanceActor->GetActorInstanceGuid()))
			{
				FLightingActorDesc& ActorDesc = Descriptors.ActorGuidsToDesc.Add(ActorGuid);
				ActorDesc.ActorGuid = ActorGuid;
				ActorDesc.CellLevelPackage = LevelInstanceActorDesc->CellLevelPackage;
				FLightingCellDesc& CellDesc = Descriptors.LightingCellsDescs.FindChecked(ActorDesc.CellLevelPackage);
				CellDesc.ActorInstanceGuids.Add(ActorGuid);
			}
		}

		if (FLightingActorDesc* LightingActorDesc = Descriptors.ActorGuidsToDesc.Find(ActorGuid))
		{
			LightingActorDesc->Actor = Actor;
			LightingActorDesc->PrecomputedLightingGuids = GatherPrecomputedLightingGuids(Actor);
		}
	}

	// Actors that receive lighting are the ones in the identified zone
	LightingOptions.ShouldBuildLighting = [&](const AActor* InActor, bool& bBuildLightingForActor, bool& bIncludeActorInLightingScene, bool& bDeferActorMapping)
	{
		// always defer actor mappings
		bDeferActorMapping = !bForceSinglePass;
		bBuildLightingForActor = false;
		bIncludeActorInLightingScene = false;

		FBox ActorBounds;
		InActor->GetStreamingBounds(ActorBounds, ActorBounds);
		
		// include loaded actors in scene lighting computations 
		if (ActorBounds.Intersect(InCellInfo.EditorBounds))
		{
			bIncludeActorInLightingScene = true;
		}
		
		// test center instead of bounds to be in only one cell
		if (InCellInfo.Bounds.IsInside(ActorBounds.GetCenter()))
		{
			 bBuildLightingForActor = true;
		}
	};
	
	FDelegateHandle BuildFailedDelegateHandle = FEditorDelegates::OnLightingBuildFailed.AddLambda(BuildFailedDelegate);	
	GEditor->BuildLighting(LightingOptions);

	while (GEditor->IsLightingBuildCurrentlyRunning())
	{
		GEditor->UpdateBuildLighting();
	}

	if (!bLightingBuildFailed)
	{
	}
	else
	{
		bRet = false;
	}

	FEditorDelegates::OnLightingBuildFailed.Remove(BuildFailedDelegateHandle);
		
	return bRet;
}

bool UWorldPartitionStaticLightingBuilder::Finalize(UWorld* World, FPackageSourceControlHelper& PackageHelper)
{
	bool bRet = true;

	UE_LOG(LogWorldPartitionStaticLightingBuilder, Verbose, TEXT("Building Volumetric Lightmaps for %s"), *World->GetName());
	 
	//@todo_ow: potentially add a clear of the VLM data now if we know we'll be reloading it
	// Fully load the global MapBuildData package that contains the VLM data, since it's using 
	// bulk data with custom load requests we need to load all of it to be able to resave the package
	if (World->PersistentLevel->MapBuildData && !World->PersistentLevel->MapBuildData->GetPackage()->IsFullyLoaded())
	{
		World->PersistentLevel->MapBuildData->GetPackage()->FullyLoad();
	}

	bool bLightingBuildFailed = false;

	if (!bForceSinglePass)
	{
		// Invoke static lighting computation
		FLightingBuildOptions LightingOptions;
		LightingOptions.QualityLevel =  QualityLevel;
		LightingOptions.bApplyDeferedActorMappingPass = true;
		LightingOptions.bVolumetricLightmapFinalizerPass = false;
		LightingOptions.MappingsDirectory = MappingsDirectory;		

		auto BuildFailedDelegate = [&bLightingBuildFailed, &World]() {
			UE_LOG(LogWorldPartitionStaticLightingBuilder, Error, TEXT("[REPORT] Failed building lighting for %s"), *World->GetName());
			bLightingBuildFailed = true;
		};	

		// Actors that receive lighting are the ones in the identified zone
		LightingOptions.ShouldBuildLighting = [&](const AActor* InActor, bool& bBuildLightingForActor, bool& bIncludeActorInLightingScene, bool& bDeferActorMapping)
		{
			// always defer actor mappings
			bDeferActorMapping = false;
			bBuildLightingForActor = false;
			bIncludeActorInLightingScene = false;
		};
	
		FDelegateHandle BuildFailedDelegateHandle = FEditorDelegates::OnLightingBuildFailed.AddLambda(BuildFailedDelegate);
	
		GEditor->BuildLighting(LightingOptions);
		while (GEditor->IsLightingBuildCurrentlyRunning())
		{
			GEditor->UpdateBuildLighting();
		}

		FEditorDelegates::OnLightingBuildFailed.Remove(BuildFailedDelegateHandle);
	}

	if (!bLightingBuildFailed)
	{
		// Save the AMapBuildData actors + MapBuildData we just updated
		bRet &= Descriptors.CreateAndUpdateActors();

		TArray<UPackage*> PackagesToSave;
		if (World->PersistentLevel->MapBuildData)
		{
			PackagesToSave.Add(World->PersistentLevel->MapBuildData->GetPackage());
		}

		//@todo_ow: Add flag to detect when we need to save the world package instead of always saving it
		PackagesToSave.Add(World->PersistentLevel->GetPackage());

		for (TMap<FName, FLightingCellDesc>::TIterator It(Descriptors.LightingCellsDescs); It ; ++It)
		{
			if (It.Value().DataActor)
			{
				PackagesToSave.Add(It.Value().DataActor->GetPackage());
			}
		}

		if (bSaveDirtyPackages)
		{
			// Obtain list of dirty packages
			TArray<UPackage*> DirtyPackages;
			FEditorFileUtils::GetDirtyWorldPackages(DirtyPackages);
			DirtyPackages.Remove(nullptr);
			
			PackagesToSave.Append(DirtyPackages);
		}

		bRet &= SavePackages(PackagesToSave, PackageHelper, true);

		bRet &= DeleteStalePackages(PackageHelper);
		
		//@todo_ow: Grab the list of currently dirty assets and validate we haven't missed anything
	}
	else
	{
		bRet = false;
	}
	
	return bRet;
}

bool UWorldPartitionStaticLightingBuilder::DeleteStalePackages(FPackageSourceControlHelper& PackageHelper)
{		
	bool bRet = true;

	for (FStaticLightingDescriptors::FActorPackage& ActorPackage : Descriptors.StaleMapDataActorsPackage)
	{			
		UE_LOG(LogWorldPartitionStaticLightingBuilder, Log, TEXT("Deleting AMapBuildData Actor %s, Associated CellPackage: %s"), *ActorPackage.PackageName.ToString(), *ActorPackage.AssociatedLevelPackage.ToString());
		bool bDeleted = DeletePackage(ActorPackage, PackageHelper);
		if (!bDeleted)
		{
			UE_LOG(LogWorldPartitionStaticLightingBuilder, Log, TEXT("Failed to delete AMapBuildData Actor %s"), *ActorPackage.PackageName.ToString());
			bRet = false;
		}
	}

	return bRet;
}

bool UWorldPartitionStaticLightingBuilder::RunForVLM(UWorld* World, const FCellInfo& InCellInfo, FPackageSourceControlHelper& PackageHelper)
{
	bool bRet = true;
	check (bBuildVLMOnly);

	UE_LOG(LogWorldPartitionStaticLightingBuilder, Verbose, TEXT("Building Volumetric Lightmaps for %s"), *World->GetName());
	 
	// Invoke static lighting computation
	FLightingBuildOptions LightingOptions;
	LightingOptions.QualityLevel =  QualityLevel;

	bool bLightingBuildFailed = false;

	auto BuildFailedDelegate = [&bLightingBuildFailed, &World]() {
		UE_LOG(LogWorldPartitionStaticLightingBuilder, Error, TEXT("[REPORT] Failed building lighting for %s"), *World->GetName());
		bLightingBuildFailed = true;
	};

	FDelegateHandle BuildFailedDelegateHandle = FEditorDelegates::OnLightingBuildFailed.AddLambda(BuildFailedDelegate);
	
	GEditor->BuildLighting(LightingOptions);
	while (GEditor->IsLightingBuildCurrentlyRunning())
	{
		GEditor->UpdateBuildLighting();
	}
		
	if (!bLightingBuildFailed)
	{
		// Save the AMapBuildData actors + MapBuildData we just updated
		TArray<UPackage*> PackagesToSave;

		PackagesToSave.Add(World->PersistentLevel->MapBuildData->GetPackage());
	
		UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, false);
	}
	else
	{
		bRet = false;
	}

	FEditorDelegates::OnLightingBuildFailed.Remove(BuildFailedDelegateHandle);
		
	return bRet;
}

void FStaticLightingDescriptors::InitializeFromWorld(UWorld* InWorld)
{
	World = InWorld;
	UE::Private::WorldPartition::FStreamingDescriptor StreamingDesc;

	FStreamingDescriptor::FStreamingDescriptorParams Params;
	Params.FilteredClasses.Add(AMapBuildDataActor::StaticClass());
	UE::Private::WorldPartition::FStreamingDescriptor::GenerateStreamingDescriptor(World, StreamingDesc, Params);

	const FName MapBuildDataActorClass = FName(AMapBuildDataActor::StaticClass()->GetName());
	
	// Setup ActorGUID Map
	for (FStreamingDescriptor::FStreamingGrid& Grid : StreamingDesc.StreamingGrids)
	{
		for (FStreamingDescriptor::FStreamingCell& Cell : Grid.StreamingCells)
		{
			FLightingCellDesc& CellDesc = LightingCellsDescs.Add(Cell.CellPackage);
			CellDesc.CellLevelPackage = Cell.CellPackage;
			CellDesc.Bounds = Cell.Bounds;
			CellDesc.DataLayers = Cell.DataLayers;
			CellDesc.RuntimeGrid = Grid.Name;
		
			for (FStreamingDescriptor::FStreamingActor& Actor : Cell.Actors)
			{
				check(!ActorGuidsToDesc.Find(Actor.ActorGuid));

				UE_LOG(LogWorldPartitionStaticLightingBuilder, Verbose, TEXT("Discovered ActorInstance Guid %s, Cell %s / Grid %s"), *Actor.ActorGuid.ToString(), *Cell.CellPackage.ToString(), *Grid.Name.ToString());

				FLightingActorDesc& LightingActorDesc = ActorGuidsToDesc.Add(Actor.ActorGuid);
				LightingActorDesc.ActorGuid = Actor.ActorGuid;
				LightingActorDesc.ActorPackage = Actor.Package;
				LightingActorDesc.CellLevelPackage = Cell.CellPackage;

				CellDesc.ActorInstanceGuids.Add(Actor.ActorGuid);
			}
		}
	}
	
	// Link AMapBuildDataActor with their cell descs & identify stale AMapBuildData actors
	InWorld->GetWorldPartition()->ForEachActorDescContainerInstance([this](UActorDescContainerInstance* InContainerInstance)
	{
		for (UActorDescContainerInstance::TConstIterator<AMapBuildDataActor> MapBuildDataIterator(InContainerInstance); MapBuildDataIterator; ++MapBuildDataIterator)
		{			
			FMapBuildDataActorDesc* Desc = (FMapBuildDataActorDesc*)MapBuildDataIterator->GetActorDesc();
			
			MapDataActorsPackage.Add({Desc->GetActorPackage(), Desc->GetGuid()});

			if (FLightingCellDesc* CellDesc = LightingCellsDescs.Find(Desc->CellPackage))
			{
				if (!CellDesc->DataActor.IsValid())
				{
					// setup actor path 
					CellDesc->DataActor = Desc->GetActorSoftPath();
				}
				else
				{
					// We've got 2 AMapBuildDataActor for the same cell through some error (unsubmitted delete, etc...)
					// Make the other as stale, doesn't really matter which 
					StaleMapDataActorsPackage.Add({Desc->GetActorPackage(), Desc->GetGuid(), Desc->CellPackage});
				}
			}
			else
			{
				// Stale AMapBuildDataActor for this map 
				StaleMapDataActorsPackage.Add({Desc->GetActorPackage(), Desc->GetGuid(), Desc->CellPackage});
			}
		}
	});	
}

UMapBuildDataRegistry* FStaticLightingDescriptors::GetOrCreateRegistryForActor(AActor* Actor)
{	
	return GetRegistryForActor(Actor, true);
}

UMapBuildDataRegistry* FStaticLightingDescriptors::GetRegistryForActor(AActor* Actor, bool bCreateIfNotFound /* = false*/)
{
	FGuid ActorInstanceGuid = Actor->GetActorInstanceGuid();

	// Get the cell package from the ActorDesc
	if (FLightingActorDesc* ActorDesc = ActorGuidsToDesc.Find(ActorInstanceGuid))
	{
		FName CellPackage = ActorDesc->CellLevelPackage;

		if (FLightingCellDesc* CellDesc = LightingCellsDescs.Find(CellPackage))
		{
			// If MapBuildData is not assigned yet in CellDesc try to get it from the actor or create it
			if (!CellDesc->MapBuildData.Get())
			{				
				// If AMapBuildDataActor is loaded make use of it's MapBuildData
				if (AMapBuildDataActor* DataActor = CellDesc->DataActor.Get())
				{
					CellDesc->MapBuildData = DataActor->GetBuildData(bCreateIfNotFound);
				}
				else if (bCreateIfNotFound)
				{
					// Top level UObjects have to have both RF_Standalone and RF_Public to be saved into packages
					// Outered to the World's MapBuildData, we'll rename them later to the be outered to the Actor
					UMapBuildDataRegistry* MapBuildData = NewObject<UMapBuildDataRegistry>(World->PersistentLevel->GetOrCreateMapBuildData(), FName(FString::Printf(TEXT("MapBuildData_%s"), *CellDesc->CellLevelPackage.ToString())), RF_Standalone | RF_Public);
					CellDesc->MapBuildData = MapBuildData;
				}
			}

			return CellDesc->MapBuildData.Get();
		}
	}

	return nullptr;
}

static FStaticLightingDescriptors* GStaticLightingDescriptors = nullptr;

void FStaticLightingDescriptors::Set(FStaticLightingDescriptors* InValue)
{
	check(!GStaticLightingDescriptors);
	GStaticLightingDescriptors = InValue;
}

FStaticLightingDescriptors* FStaticLightingDescriptors::Get()
{
	return GStaticLightingDescriptors;
}

TArray<UMapBuildDataRegistry*> FStaticLightingDescriptors::GetAllMapBuildData()
{
	TArray<UMapBuildDataRegistry*> MapBuildDatas;

	for (TMap<FName,FLightingCellDesc>::TIterator It(LightingCellsDescs); It ; ++It)
	{
		FLightingCellDesc& CellDesc = It.Value();

		if (AMapBuildDataActor* DataActor = CellDesc.DataActor.Get())
		{
			if (UMapBuildDataRegistry* MapBuildData = DataActor->GetBuildData())
			{
				CellDesc.MapBuildData = MapBuildData;
			}
		}

		if (UMapBuildDataRegistry* Registry = CellDesc.MapBuildData.Get())
		{
			MapBuildDatas.Add(Registry);
		}
	}

	return MapBuildDatas;
}

bool FStaticLightingDescriptors::CreateAndUpdateActors()
{
	bool bResult = true;

	// Create/Update all the AMapBuildDataActor
	for (TMap<FName, FLightingCellDesc>::TIterator It(LightingCellsDescs); It ; ++It)
	{	
		FLightingCellDesc& CellDesc = It.Value();
		AMapBuildDataActor* DataActor = CellDesc.DataActor.Get();
		UMapBuildDataRegistry* MapBuildData = CellDesc.MapBuildData.Get();
		
		if (MapBuildData)
		{
			// This cell has data
			if (!DataActor)
			{
				// Generate Actor Name/Label

				// Trim path from CellLevelPackage 
				FString CellPackageName = CellDesc.CellLevelPackage.ToString();
				int32 LastSlash = 0;
				if (CellPackageName.FindLastChar('/', LastSlash))
				{
					CellPackageName.RightChopInline(LastSlash+1);
				}
				
				FString DataActorName = FString::Printf(TEXT("%s_MapBuildData_%s"), *World->GetName(), *CellPackageName);

				// Create the Actor
				FActorSpawnParameters SpawnParams;
				SpawnParams.Name = FName(DataActorName);
				SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Required_ErrorAndReturnNull;

				DataActor = World->SpawnActor<AMapBuildDataActor>(SpawnParams);
				if (DataActor)
				{
					DataActor->SetCellPackage(CellDesc.CellLevelPackage);
					DataActor->SetActorLabel(DataActorName);

					CellDesc.DataActor = DataActor;
				}
				else
				{
					bResult = false;
					continue;
				}
			}

			check(DataActor);
			DataActor->SetBuildData(MapBuildData);
			DataActor->RemoveAllDataLayers();
			DataActor->SetRuntimeGrid(CellDesc.RuntimeGrid);
			DataActor->SetFolderPath(TEXT("MapBuildData"));

			// Link HLODs actors to their MapBuildData actors to ensure they stream in the same cell
			for (const FGuid& ActorGuid : CellDesc.ActorInstanceGuids)
			{
				if (FLightingActorDesc* Desc = ActorGuidsToDesc.Find(ActorGuid))
				{
					if (AWorldPartitionHLOD* ActorPtr = Cast<AWorldPartitionHLOD>(Desc->Actor.Get()))
					{
						DataActor->LinkToActor(ActorPtr);
						break;
					}
				}
			}

			//@todo_ow: support for datalayers
			// Make sure the generated actor has the same data layers as the source actors
			/*for (const UDataLayerInstance* DataLayerInstance : CellDesc.DataLayers)
			{
				DataActor->AddDataLayer(DataLayerInstance);
			}*/
				
			//@todo_ow: Provide a good label, need to extend FStreamingCell to contain a meaningful name 
			//DataActor->SetActorLabel(???);
			FTransform ActorTransform;
			ActorTransform.SetTranslation(CellDesc.Bounds.GetCenter());

			DataActor->SetActorTransform(ActorTransform);
			DataActor->SetBounds(CellDesc.Bounds);
			DataActor->MarkPackageDirty();

			UE_LOG(LogWorldPartitionStaticLightingBuilder, Log, TEXT("Updated/Created AMapBuildData Actor %s"), *DataActor->GetName());
			for (FGuid ActorInstanceGuid : CellDesc.ActorInstanceGuids)
			{
				UE_LOG(LogWorldPartitionStaticLightingBuilder, Verbose, TEXT("  => ActorInstanceGuid %s"),  *ActorInstanceGuid.ToString());
			}
			DataActor->SetActorInstances(CellDesc.ActorInstanceGuids);
		}
		else if(CellDesc.DataActor.IsValid())
		{
			FGuid ActorInstanceGuid = DataActor ? DataActor->GetActorInstanceGuid() : FGuid();

			// Unnecessary Actor add to stale list
			StaleMapDataActorsPackage.Add({CellDesc.DataActor.ToSoftObjectPath().GetLongPackageFName(), ActorInstanceGuid, CellDesc.CellLevelPackage});
		}
	}

	return bResult;
}

FAutoConsoleCommand MarkPackageDirtyNewMapBuildDataId(
	TEXT("wp.StaticLighting.MarkPackageDirtyNewMapBuildDataId"),
	TEXT("Mark dirty all Actors with newly created MapBuildDataIDs"),
	FConsoleCommandDelegate::CreateLambda([]()
	{
		for (UStaticMeshComponent* StaticMeshComponent : TObjectRange<UStaticMeshComponent>())
		{
			if (StaticMeshComponent->IsTemplate())
			{
				continue;
			}

			for (FStaticMeshComponentLODInfo& LODInfo : StaticMeshComponent->LODData)
			{
				if (LODInfo.bMapBuildDataChanged)
				{
					UE_LOG(LogEngine, Log, TEXT("Marking component %s's package dirty"), *StaticMeshComponent->GetFullName());
					StaticMeshComponent->MarkPackageDirty();
				}
			}

			if (StaticMeshComponent->LODData.Num() == 0)
			{
				UE_LOG(LogEngine, Log, TEXT("Component %s has no LOD Data"), *StaticMeshComponent->GetFullName());
			}
		}		
		
	})
);

