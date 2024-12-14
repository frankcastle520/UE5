// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionLevelHelper.h"
#include "Misc/PackageName.h"
#include "WorldPartition/WorldPartitionPackageHelper.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"
#include "Containers/StringFwd.h"
#include "WorldPartition/WorldPartitionActorContainerID.h"
#include "WorldPartition/IWorldPartitionObjectResolver.h"

#if WITH_EDITOR
#include "Engine/Level.h"
#include "Engine/LevelStreamingGCHelper.h"
#include "Misc/Paths.h"
#include "Model.h"
#include "UnrealEngine.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionLog.h"
#include "WorldPartition/WorldPartitionPackageHelper.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "WorldPartition/ContentBundle/ContentBundleEditor.h"
#include "WorldPartition/WorldPartitionPropertyOverride.h"
#include "GameFramework/WorldSettings.h"
#include "LevelUtils.h"
#include "ActorFolder.h"

FUObjectAnnotationSparse<FWorldPartitionLevelHelper::FActorPropertyOverridesAnnotation, true> FWorldPartitionLevelHelper::ActorPropertyOverridesAnnotation;
#endif

bool FWorldPartitionResolveData::ResolveObject(UWorld* InWorld, const FSoftObjectPath& InObjectPath, UObject*& OutObject) const
{
	OutObject = nullptr;
	if (InWorld)
	{
		if (IsValid() && SourceWorldAssetPath == InObjectPath.GetAssetPath())
		{
			const FString SubPathString = FWorldPartitionLevelHelper::AddActorContainerIDToSubPathString(ContainerID, InObjectPath.GetSubPathString());
			// We don't read the return value as we always want to return true when using the resolve data.
			InWorld->ResolveSubobject(*SubPathString, OutObject, /*bLoadIfExists*/false);
			return true;
		}
	}

	return false;
}

FString FWorldPartitionLevelHelper::AddActorContainerID(const FActorContainerID& InContainerID, const FString& InActorName)
{
	const FName ActorName(*InActorName);
	const FString ActorPlainName(ActorName.GetPlainNameString() + TEXT("_") + InContainerID.ToShortString());
	return FName(*ActorPlainName, ActorName.GetNumber()).ToString();}

FString FWorldPartitionLevelHelper::AddActorContainerIDToSubPathString(const FActorContainerID& InContainerID, const FString& InSubPathString)
{
	if (!InContainerID.IsMainContainer())
	{
		constexpr const TCHAR PersistenLevelName[] = TEXT("PersistentLevel.");
		constexpr const int32 DotPos = UE_ARRAY_COUNT(PersistenLevelName);
		if (InSubPathString.StartsWith(PersistenLevelName))
		{
			const int32 SubObjectPos = InSubPathString.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromStart, DotPos);
			if (SubObjectPos == INDEX_NONE)
			{
				return AddActorContainerID(InContainerID, InSubPathString);
			}
			else
			{
				return AddActorContainerID(InContainerID, InSubPathString.Mid(0, SubObjectPos)) + InSubPathString.Mid(SubObjectPos);
			}
		}
	}

	return InSubPathString;
}

#if WITH_EDITOR
FWorldPartitionLevelHelper& FWorldPartitionLevelHelper::Get()
{
	static FWorldPartitionLevelHelper Instance;
	return Instance;
}

FWorldPartitionLevelHelper::FWorldPartitionLevelHelper()
{
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddRaw(this, &FWorldPartitionLevelHelper::PreGarbageCollect);
}

void FWorldPartitionLevelHelper::PreGarbageCollect()
{
	for (TWeakObjectPtr<UPackage>& PackageToUnload : PreGCPackagesToUnload)
	{
		// Test if WeakObjectPtr is valid since clean up could have happened outside of this helper
		if (PackageToUnload.IsValid())
		{
			FWorldPartitionPackageHelper::UnloadPackage(PackageToUnload.Get());
		}
	}
	PreGCPackagesToUnload.Reset();
}

void FWorldPartitionLevelHelper::ApplyConstructionScriptPropertyOverridesFromAnnotation(AActor* InActor)
{
	if (IsValid(InActor))
	{
		FWorldPartitionLevelHelper::FActorPropertyOverridesAnnotation Annotation = FWorldPartitionLevelHelper::ActorPropertyOverridesAnnotation.GetAndRemoveAnnotation(InActor);
		if (!Annotation.IsDefault())
		{
			if (InActor->GetRootComponent())
			{
				const FTransform InverseTransform = Annotation.ContainerTransform.Inverse();
				FLevelUtils::FApplyLevelTransformParams TransformParams(InActor->GetLevel(), InverseTransform);
				TransformParams.Actor = InActor;
				TransformParams.bDoPostEditMove = false;
				TransformParams.bSetRelativeTransformDirectly = true;

				FLevelUtils::ApplyLevelTransform(TransformParams);
			}

			for (const FActorPropertyOverride& ActorOverride : Annotation.ActorPropertyOverrides)
			{
				const bool bConstructionScriptProperties = true;
				UWorldPartitionPropertyOverride::ApplyPropertyOverrides(&ActorOverride, InActor, bConstructionScriptProperties);
			}

			if (InActor->GetRootComponent())
			{
				FLevelUtils::FApplyLevelTransformParams TransformParams(InActor->GetLevel(), Annotation.ContainerTransform);
				TransformParams.Actor = InActor;
				TransformParams.bDoPostEditMove = false;
				TransformParams.bSetRelativeTransformDirectly = true;

				FLevelUtils::ApplyLevelTransform(TransformParams);
				InActor->GetRootComponent()->UpdateComponentToWorld();
				InActor->MarkComponentsRenderStateDirty();
			}
		}
	}
}

void FWorldPartitionLevelHelper::AddReference(UPackage* InPackage, FPackageReferencer* InReferencer)
{
	check(InPackage);
	FPackageReference& RefInfo = PackageReferences.FindOrAdd(InPackage->GetFName());
	check(RefInfo.Package == nullptr || RefInfo.Package == InPackage);
	RefInfo.Package = InPackage;
	RefInfo.Referencers.Add(InReferencer);
	PreGCPackagesToUnload.Remove(InPackage);
}

void FWorldPartitionLevelHelper::RemoveReferences(FPackageReferencer* InReferencer)
{
	for (auto It = PackageReferences.CreateIterator(); It; ++It)
	{
		FPackageReference& RefInfo = It->Value;
		RefInfo.Referencers.Remove(InReferencer);
		if (RefInfo.Referencers.Num() == 0)
		{
			// Test if WeakObjectPtr is valid since clean up could have happened outside of this helper
			if (RefInfo.Package.IsValid())
			{
				PreGCPackagesToUnload.Add(RefInfo.Package);
			}
			It.RemoveCurrent();
		}
	}
}

void FWorldPartitionLevelHelper::FPackageReferencer::AddReference(UPackage* InPackage)
{
	FWorldPartitionLevelHelper::Get().AddReference(InPackage, this);
}

void FWorldPartitionLevelHelper::FPackageReferencer::RemoveReferences()
{
	FWorldPartitionLevelHelper::Get().RemoveReferences(this);
}


 /**
  * Defaults World's initialization values for World Partition StreamingLevels
  */
UWorld::InitializationValues FWorldPartitionLevelHelper::GetWorldInitializationValues()
{
	return UWorld::InitializationValues()
		.InitializeScenes(false)
		.AllowAudioPlayback(false)
		.RequiresHitProxies(false)
		.CreatePhysicsScene(false)
		.CreateNavigation(false)
		.CreateAISystem(false)
		.ShouldSimulatePhysics(false)
		.EnableTraceCollision(false)
		.SetTransactional(false)
		.CreateFXSystem(false);
}

/**
 * Moves external actors into the given level
 */
void FWorldPartitionLevelHelper::MoveExternalActorsToLevel(const TArray<FWorldPartitionRuntimeCellObjectMapping>& InChildPackages, ULevel* InLevel, TArray<UPackage*>& OutModifiedPackages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWorldPartitionLevelHelper::MoveExternalActorsToLevel);

	check(InLevel);
	UPackage* LevelPackage = InLevel->GetPackage();

	// Gather existing actors to validate only the one we expect are added to the level
	TSet<FName> LevelActors;
	for (AActor* Actor : InLevel->Actors)
	{
		if (Actor)
		{
			LevelActors.Add(Actor->GetFName());
		}
	}

	// Move all actors to Cell level
	TSet<AActor*> LoadedActors;

	for (const FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InChildPackages)
	{
		// We assume actor failed to duplicate if LoadedPath equals NAME_None (warning already logged we can skip this mapping)
		if (PackageObjectMapping.LoadedPath == NAME_None && !PackageObjectMapping.ContainerID.IsMainContainer())
		{
			continue;
		}

		if (PackageObjectMapping.bIsEditorOnly)
		{
			continue;
		}

		AActor* Actor = FindObject<AActor>(nullptr, *PackageObjectMapping.LoadedPath.ToString());
		if (Actor)
		{
			UPackage* ActorPackage = Actor->GetPackage();
			check(ActorPackage);

			const bool bIsActoPackageExternal = Actor->IsPackageExternal();
			const bool bSameOuter = (InLevel == Actor->GetOuter());

			Actor->SetPackageExternal(false, false);

			// Avoid calling Rename on the actor if it's already outered to InLevel as this will cause it's name to be changed. 
			// (UObject::Rename doesn't check if Rename is being called with existing outer and assigns new name)
			if (!bSameOuter)
			{
				Actor->Rename(nullptr, InLevel, REN_NonTransactional | REN_DoNotDirty | REN_DontCreateRedirectors);

				// AActor::Rename will register components but doesn't call RerunConstructionScripts like AddLoadedActors does.
				// If bIsWorldInitialized is false. RerunConstructionScripts will get called as part of UEditorEngine::InitializePhysicsSceneForSaveIfNecessary during Cell package save
				// Current behavior is that the PersistentLevel Cell is initialized here (PopulateGeneratorPackageForCook) and other cells aren't yet (PopulateGeneratedPackageForCook)
				if (InLevel->GetWorld()->bIsWorldInitialized)
				{
					Actor->RerunConstructionScripts();
					ApplyConstructionScriptPropertyOverridesFromAnnotation(Actor);
				}
			}
			else if (!InLevel->Actors.Contains(Actor))
			{
				LoadedActors.Emplace(Actor);
			}
			check(Actor->GetPackage() == LevelPackage);

			// Process objects found in the source actor package
			if (bIsActoPackageExternal)
			{
				TArray<UObject*> Objects;

				// Skip Garbage objects as the initial Rename on an actor with an ChildActorComponent can destroy its child actors.
				// This happens when the component has bNeedsRecreate set to true (when it has a valid ChildActorTemplate).
				const bool bIncludeNestedSubobjects = false;
				GetObjectsWithPackage(ActorPackage, Objects, bIncludeNestedSubobjects, RF_NoFlags, EInternalObjectFlags::Garbage);

				for (UObject* Object : Objects)
				{
					if (Object->GetFName() != NAME_PackageMetaData)
					{
						if (Object->GetOuter()->IsA<ULevel>())
						{
							// Move objects that are outered the level in the destination level
							AActor* NestedActor = Cast<AActor>(Object);
							if (InLevel != Object->GetOuter())
							{
								Object->Rename(nullptr, InLevel, REN_NonTransactional | REN_DoNotDirty | REN_DontCreateRedirectors);
							}
							else if (NestedActor && !InLevel->Actors.Contains(NestedActor))
							{
								LoadedActors.Emplace(NestedActor);
							}
							if (NestedActor)
							{
								LevelActors.Add(NestedActor->GetFName());
							}
						}
						else
						{
							// Move objects in the destination level package
							Object->Rename(nullptr, LevelPackage, REN_NonTransactional | REN_DoNotDirty | REN_DontCreateRedirectors);
						}
					}
				}

				// Trash this package to guarantee that any potential future load of this actor won't find the old empty package
				// @todo_ow: Decide if we want to support actor reloads during cook. If not, remove this code, detect the reload and report an error.
				FLevelStreamingGCHelper::TrashPackage(ActorPackage);
			}

			OutModifiedPackages.Add(ActorPackage);
			LevelActors.Add(Actor->GetFName());
		}
		else
		{
			UE_LOG(LogWorldPartition, Warning, TEXT("Can't find actor %s."), *PackageObjectMapping.Path.ToString());
		}
	}

	InLevel->AddLoadedActors(LoadedActors.Array());

	for (AActor* Actor : InLevel->Actors)
	{
		if (IsValid(Actor) && Actor->HasAllFlags(RF_WasLoaded))
		{
			checkf(LevelActors.Contains(Actor->GetFName()), TEXT("Actor %s(%s) was unexpectedly loaded when moving actors to streaming cell"), *Actor->GetActorNameOrLabel(), *Actor->GetName());
		}
	}
}

void FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths(ULevel* InLevel, UWorldPartition* InWorldPartition)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FWorldPartitionLevelHelper::RemapLevelSoftObjectPaths);

	check(InLevel);
	check(InWorldPartition);

	FSoftObjectPathFixupArchive FixupSerializer([InWorldPartition](FSoftObjectPath& Value)
	{
		if(!Value.IsNull())
		{
			InWorldPartition->RemapSoftObjectPath(Value);
		}
	});
	FixupSerializer.Fixup(InLevel);
}

FSoftObjectPath FWorldPartitionLevelHelper::RemapActorPath(const FActorContainerID& InContainerID, const FString& InSourceWorldPath, const FSoftObjectPath& InActorPath)
{
	// If Path is in an instanced package it will now be remapped to its source package
	FSoftObjectPath OutActorPath(FTopLevelAssetPath(InSourceWorldPath), InActorPath.GetSubPathString());
	
	if(!InContainerID.IsMainContainer())
	{
		// This gets called by UWorldPartitionLevelStreamingPolicy::PrepareActorToCellRemapping and FWorldPartitionLevelHelper::LoadActors
		// 
		// At this point we are changing the top level asset and remapping the SubPathString to add a ContainerID suffix so
		// '/Game/SomePath/LevelInstance.LevelInstance:PersistentLevel.ActorA' becomes
		// '/Game/SomeOtherPath/SourceWorldName.SourceWorldName:PersistentLevel.ActorA_{ContainerID}'
		FString RemappedSubPathString = FWorldPartitionLevelHelper::AddActorContainerIDToSubPathString(InContainerID, InActorPath.GetSubPathString());
		OutActorPath.SetSubPathString(RemappedSubPathString);
	}
	
	return OutActorPath;
}

bool FWorldPartitionLevelHelper::RemapLevelCellPathInContentBundle(ULevel* Level, const class FContentBundleEditor* ContentBundleEditor, const UWorldPartitionRuntimeCell* Cell)
{
	FString CellPath = ContentBundleEditor->GetExternalStreamingObjectPackagePath();
	CellPath += TEXT(".");
	CellPath += URuntimeHashExternalStreamingObjectBase::GetCookedExternalStreamingObjectName();
	CellPath += TEXT(".");
	CellPath += Cell->GetName();
	FSetWorldPartitionRuntimeCell SetWorldPartitionRuntimeCell(Level, FSoftObjectPath(CellPath));
	return Level->IsWorldPartitionRuntimeCell();
}

/**
 * Creates an empty Level used in World Partition
 */
ULevel* FWorldPartitionLevelHelper::CreateEmptyLevelForRuntimeCell(const UWorldPartitionRuntimeCell* Cell, const UWorld* InWorld, const FString& InWorldAssetName, UPackage* InPackage)
{
	// Create or use given package
	UPackage* CellPackage = nullptr;
	if (InPackage)
	{
		check(FindObject<UPackage>(nullptr, *InPackage->GetName()));
		CellPackage = InPackage;
	}
	else
	{
		FString PackageName = FPackageName::ObjectPathToPackageName(InWorldAssetName);
		check(!FindObject<UPackage>(nullptr, *PackageName));
		CellPackage = CreatePackage(*PackageName);
		CellPackage->SetPackageFlags(PKG_NewlyCreated);
	}

	if (InWorld->IsPlayInEditor())
	{
		check(!InPackage);
		CellPackage->SetPackageFlags(PKG_PlayInEditor);
		CellPackage->SetPIEInstanceID(InWorld->GetPackage()->GetPIEInstanceID());
	}

	// Create World & Persistent Level
	UWorld::InitializationValues IVS = FWorldPartitionLevelHelper::GetWorldInitializationValues();
	const FName WorldName = FName(FPackageName::ObjectPathToObjectName(InWorldAssetName));
	check(!FindObject<UWorld>(CellPackage, *WorldName.ToString()));
	UWorld* NewWorld = UWorld::CreateWorld(InWorld->WorldType, /*bInformEngineOfWorld*/false, WorldName, CellPackage, /*bAddToRoot*/false, InWorld->GetFeatureLevel(), &IVS, /*bInSkipInitWorld*/true);
	check(NewWorld);
	NewWorld->SetFlags(RF_Public | RF_Standalone);
	check(NewWorld->GetWorldSettings());
	check(UWorld::FindWorldInPackage(CellPackage) == NewWorld);
	check(InPackage || (NewWorld->GetPathName() == InWorldAssetName));
	// We don't need the cell level's world setting to replicate
	FSetActorReplicates SetActorReplicates(NewWorld->GetWorldSettings(), false);
	
	// Setup of streaming cell Runtime Level
	ULevel* NewLevel = NewWorld->PersistentLevel;
	check(NewLevel);
	check(NewLevel->GetFName() == InWorld->PersistentLevel->GetFName());
	check(NewLevel->OwningWorld == NewWorld);
	check(NewLevel->Model);
	check(!NewLevel->bIsVisible);

	NewLevel->WorldPartitionRuntimeCell = const_cast<UWorldPartitionRuntimeCell*>(Cell);
	
	// Mark the level package as fully loaded
	CellPackage->MarkAsFullyLoaded();

	// Mark the level package as containing a map
	CellPackage->ThisContainsMap();

	// Set the guids on the constructed level to something based on the generator rather than allowing indeterminism by
	// constructing new Guids on every cook
	// @todo_ow: revisit for static lighting support. We need to base the LevelBuildDataId on the relevant information from the
	// actor's package.
	NewLevel->LevelBuildDataId = InWorld->PersistentLevel->LevelBuildDataId;
	check(InWorld->PersistentLevel->Model && NewLevel->Model);
	NewLevel->Model->LightingGuid = InWorld->PersistentLevel->Model->LightingGuid;

	return NewLevel;
}

bool FWorldPartitionLevelHelper::LoadActors(UWorld* InOuterWorld, ULevel* InDestLevel, TArrayView<FWorldPartitionRuntimeCellObjectMapping> InActorPackages, FWorldPartitionLevelHelper::FPackageReferencer& InPackageReferencer, TFunction<void(bool)> InCompletionCallback, bool bInLoadAsync, FLinkerInstancingContext InInstancingContext)
{
	FLoadActorsParams Params = FLoadActorsParams()
		.SetOuterWorld(InOuterWorld)
		.SetDestLevel(InDestLevel)
		.SetActorPackages(InActorPackages)
		.SetPackageReferencer(&InPackageReferencer)
		.SetCompletionCallback(InCompletionCallback)
		.SetLoadAsync(bInLoadAsync)
		.SetInstancingContext(InInstancingContext);

	return LoadActors(MoveTemp(Params));
}

bool FWorldPartitionLevelHelper::LoadActorsWithPropertyOverridesInternal(FLoadActorsParams&& InParams)
{
	TMap<FString, FName> PropertyOverridesToLoad;

	struct FLoadProgress
	{
		int32 NumPendingLoadRequests = 0;
		int32 NumFailedLoadedRequests = 0;
		
		TMap<FSoftObjectPath, TSet<FActorContainerID>> AssetToContainerIDs;

		FLoadActorsParams Params;
		FLoadedPropertyOverrides LoadedPropertyOverrides;
	};
	TSharedPtr<FLoadProgress> LoadProgress = MakeShared<FLoadProgress>();
	LoadProgress->Params = MoveTemp(InParams);

	// Build up list of Property Overrides to load and an assocation between Property Override Asset Path and the overrides Owner Container ID
	for (const FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : LoadProgress->Params.ActorPackages)
	{
		for (const FWorldPartitionRuntimeCellPropertyOverride& PropertyOverride : PackageObjectMapping.PropertyOverrides)
		{
			FName PackageName = PropertyOverridesToLoad.FindOrAdd(PropertyOverride.AssetPath, PropertyOverride.PackageName);
			check(PackageName == PropertyOverride.PackageName);
			LoadProgress->AssetToContainerIDs.FindOrAdd(PropertyOverride.AssetPath).Add(PropertyOverride.OwnerContainerID);
		}
	}

	// Nothing to load, move on to load actors
	if (PropertyOverridesToLoad.IsEmpty())
	{
		return LoadActorsInternal(MoveTemp(LoadProgress->Params), MoveTemp(LoadProgress->LoadedPropertyOverrides));
	}

	LoadProgress->NumPendingLoadRequests = PropertyOverridesToLoad.Num();

	// Do Loading
	for (auto const&[AssetPath, PackageName] : PropertyOverridesToLoad)
	{
		FSoftObjectPath SoftAssetPath(AssetPath);
		
		FLinkerInstancingContext InstancingContext;
		InstancingContext.AddTag(ULevel::DontLoadExternalObjectsTag);

		FSoftObjectPath RemappedPath = SoftAssetPath;

		// Loading embedded asset
		if (!SoftAssetPath.GetSubPathString().IsEmpty())
		{
			FString WorldPackageName = SoftAssetPath.GetLongPackageName();
			FName RemappedContainerPackage = FName(*(WorldPackageName + TEXT("_LoadPropertyOverride")));
			InstancingContext.AddPackageMapping(*WorldPackageName, RemappedContainerPackage);

			const FName AssetPackageInstanceName = FName(*ULevel::GetExternalActorPackageInstanceName(RemappedContainerPackage.ToString(), PackageName.ToString()));

			InstancingContext.AddPackageMapping(PackageName, AssetPackageInstanceName);
			InstancingContext.FixupSoftObjectPath(RemappedPath);
		}

		FName RemappedPackageName = InstancingContext.RemapPackage(PackageName);
		FName PackageToLoad = PackageName;

		FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([LoadProgress, SoftAssetPath, RemappedPath](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
		{
			check(LoadProgress->NumPendingLoadRequests);
			LoadProgress->NumPendingLoadRequests--;
			
			if (UWorldPartitionPropertyOverride* LoadedOverride = Cast<UWorldPartitionPropertyOverride>(RemappedPath.ResolveObject()))
			{
				// Reference World Package and Actor Package
				LoadProgress->Params.PackageReferencer->AddReference(LoadedOverride->GetOutermostObject()->GetPackage());
				LoadProgress->Params.PackageReferencer->AddReference(LoadedOverride->GetPackage());

				TSet<FActorContainerID>& OwnerContainerIDs = LoadProgress->AssetToContainerIDs.FindChecked(SoftAssetPath);
				for (FActorContainerID OwnerContainerID : OwnerContainerIDs)
				{
					LoadProgress->LoadedPropertyOverrides.PropertyOverrides.Add(OwnerContainerID, LoadedOverride);
				}
			}

			if (!LoadProgress->NumPendingLoadRequests)
			{
				LoadActorsInternal(MoveTemp(LoadProgress->Params), MoveTemp(LoadProgress->LoadedPropertyOverrides));
			}
		});

		if (LoadProgress->Params.bLoadAsync)
		{
			FPackagePath PackagePath = FPackagePath::FromPackageNameChecked(PackageToLoad);

			int32 RequestID = ::LoadPackageAsync(PackagePath, RemappedPackageName, CompletionCallback, PKG_None, -1, 0, &InstancingContext);
			if (LoadProgress->Params.AsyncRequestIDs)
			{
				LoadProgress->Params.AsyncRequestIDs->Add(RequestID);
			}
		}
		else
		{
			UPackage* InstancingPackage = nullptr;
			if (PackageName != PackageToLoad)
			{
				InstancingPackage = CreatePackage(*RemappedPackageName.ToString());
			}

			UPackage* Package = LoadPackage(InstancingPackage, *PackageToLoad.ToString(), LOAD_None, nullptr, &InstancingContext);
			CompletionCallback.Execute(PackageToLoad, Package, Package ? EAsyncLoadingResult::Succeeded : EAsyncLoadingResult::Failed);
		}
	}

	return LoadProgress->NumPendingLoadRequests == 0;
}

// deprecated
bool FWorldPartitionLevelHelper::LoadActors(const FLoadActorsParams& InParams)
{
	FLoadActorsParams ParamsCopy = FLoadActorsParams()
		.SetActorPackages(InParams.ActorPackages)
		.SetCompletionCallback(InParams.CompletionCallback)
		.SetDestLevel(InParams.DestLevel)
		.SetInstancingContext(MoveTemp(InParams.InstancingContext))
		.SetLoadAsync(InParams.bLoadAsync, InParams.AsyncRequestIDs)
		.SetOuterWorld(InParams.OuterWorld)
		.SetPackageReferencer(InParams.PackageReferencer);
	return LoadActors(MoveTemp(ParamsCopy));
}

bool FWorldPartitionLevelHelper::LoadActors(FLoadActorsParams&& InParams)
{	
	return FWorldPartitionLevelHelper::LoadActorsWithPropertyOverridesInternal(MoveTemp(InParams));
}

bool FWorldPartitionLevelHelper::LoadActorsInternal(FLoadActorsParams&& InParams, FLoadedPropertyOverrides&& InLoadedPropertyOverrides)
{
	TArray<FWorldPartitionRuntimeCellObjectMapping*> ActorPackagesToLoad;
	TMap<FActorContainerID, FLinkerInstancingContext> LinkerInstancingContexts;

	// Generate a unique name to load a level instance embedded actor if there are multiple instances of this level instance and possibly across 
	// multiple instances of the WP world:
	auto GetContainerPackage = [](const FActorContainerID& InContainerID, const FString& InPackageName, const UObject* InContextObject, bool bUniquePackage) -> FName
	{
		TStringBuilder<512> PackageNameBuilder;
		
		// Distinguish between instances of the same level instance	
		PackageNameBuilder.Appendf(TEXT("/Temp%s_%s"), *InPackageName, *InContainerID.ToShortString());
		
		// Distinguish between instances of the same top level WP world; only for PIE, in cook we always cook the source WP and not an instance 
		// and actor packages no longer exist at runtime)
		const FString ContextObjectPathName = GetPathNameSafe(InContextObject);
		const uint64 ContextObjectPathNameHash = CityHash64(TCHAR_TO_ANSI(*ContextObjectPathName), ContextObjectPathName.Len());
		PackageNameBuilder.Appendf(TEXT("_%llx"), ContextObjectPathNameHash);

		if (!IsRunningCommandlet() && bUniquePackage)
		{
			// Distinguish between loading the same package after a reload between GCs (only for PIE)
			static uint32 ContextObjectUniqueID = 0;
			PackageNameBuilder.Appendf(TEXT("_%llx"), ContextObjectUniqueID++);
		}

		return PackageNameBuilder.ToString();
	};

	if (!InParams.ActorPackages.IsEmpty())
	{
		ActorPackagesToLoad.Reserve(InParams.ActorPackages.Num());

		// Add main container context
		LinkerInstancingContexts.Add(FActorContainerID::GetMainContainerID(), MoveTemp(InParams.InstancingContext));

		for (FWorldPartitionRuntimeCellObjectMapping& PackageObjectMapping : InParams.ActorPackages)
		{
			FLinkerInstancingContext* Context = LinkerInstancingContexts.Find(PackageObjectMapping.ContainerID);
			if (!Context)
			{
				check(!PackageObjectMapping.ContainerID.IsMainContainer());
		
				FLinkerInstancingContext& NewContext = LinkerInstancingContexts.Add(PackageObjectMapping.ContainerID);

				// Make sure here we don't remap the SoftObjectPaths through the linker when loading the embedded actor packages. 
				// A remapping will happen in the packaged loaded callback later in this method.
				NewContext.SetSoftObjectPathRemappingEnabled(false); 
		
				// Don't load external objects as we are going to individually load them
				NewContext.AddTag(ULevel::DontLoadExternalObjectsTag);
				NewContext.AddTag(ULevel::DontLoadExternalFoldersTag);

				// We only want unique packages for non-OFPA actors, @todo_ow: remove this and duplicate actors from non-OFPA levels instead of renaming.
				const bool bUniquePackage = !PackageObjectMapping.Package.ToString().Contains(FPackagePath::GetExternalActorsFolderName());
				const FName ContainerPackageInstanceName(GetContainerPackage(PackageObjectMapping.ContainerID, PackageObjectMapping.ContainerPackage.ToString(), InParams.OuterWorld, bUniquePackage));
				NewContext.AddPackageMapping(PackageObjectMapping.ContainerPackage, ContainerPackageInstanceName);
				Context = &NewContext;
			}
		
			const FName ContainerPackageInstanceName = Context->RemapPackage(PackageObjectMapping.ContainerPackage);

			if (PackageObjectMapping.bIsEditorOnly || PackageObjectMapping.ContainerPackage != ContainerPackageInstanceName)
			{
				const FName ActorPackageName = *FPackageName::ObjectPathToPackageName(PackageObjectMapping.Package.ToString());
				const FName ActorPackageInstanceName = PackageObjectMapping.bIsEditorOnly ? NAME_None : FName(*ULevel::GetExternalActorPackageInstanceName(ContainerPackageInstanceName.ToString(), ActorPackageName.ToString()));

				Context->AddPackageMapping(ActorPackageName, ActorPackageInstanceName);
			}

			if (!PackageObjectMapping.bIsEditorOnly)
			{
				ActorPackagesToLoad.Add(&PackageObjectMapping);
			}
		}
	}

	if (ActorPackagesToLoad.IsEmpty())
	{
		InParams.CompletionCallback(true);
		return true;
	}

	struct FLoadProgress
	{
		int32 NumPendingLoadRequests;
		int32 NumFailedLoadedRequests;
	};

	TSharedPtr<FLoadProgress> LoadProgress = MakeShared<FLoadProgress>();
	LoadProgress->NumPendingLoadRequests = ActorPackagesToLoad.Num();
	LoadProgress->NumFailedLoadedRequests = 0;

	for (FWorldPartitionRuntimeCellObjectMapping* PackageObjectMapping : ActorPackagesToLoad)
	{
		const FName PackageToLoad(*FPackageName::ObjectPathToPackageName(PackageObjectMapping->Package.ToString()));
		const FLinkerInstancingContext ContainerInstancingContext = FLinkerInstancingContext::DuplicateContext(LinkerInstancingContexts.FindChecked(PackageObjectMapping->ContainerID));
		const FName PackageName = ContainerInstancingContext.RemapPackage(PackageToLoad);

		FLoadPackageAsyncDelegate CompletionCallback = FLoadPackageAsyncDelegate::CreateLambda([LoadProgress, PackageObjectMapping, LoadedOverrides = InLoadedPropertyOverrides, PackageReferencer = InParams.PackageReferencer, OuterWorld = InParams.OuterWorld, DestLevel = InParams.DestLevel, CompletionCallback = InParams.CompletionCallback](const FName& LoadedPackageName, UPackage* LoadedPackage, EAsyncLoadingResult::Type Result)
		{
			const FName ActorName = *FPaths::GetExtension(PackageObjectMapping->Path.ToString());
			check(LoadProgress->NumPendingLoadRequests);
			LoadProgress->NumPendingLoadRequests--;

			AActor* Actor = nullptr;

			if (LoadedPackage)
			{
				if (LoadedPackage->ContainsMap())
				{
					if (UWorld* LoadedWorld = UWorld::FindWorldInPackage(LoadedPackage))
					{
						Actor = FindObject<AActor>(LoadedWorld->PersistentLevel, *ActorName.ToString());
					}
				}
				else
				{
					// In PIE, we make sure to clear RF_Standalone flag on objects in external packages (UMetaData) 
					// This guarantees that external packages of actors that are destroyed during the PIE session will
					// properly get GC'ed and will allow future edits/modifications of OFPA actors.
					if (DestLevel && DestLevel->GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor))
					{
						ForEachObjectWithPackage(LoadedPackage, [](UObject* Object)
						{
							Object->ClearFlags(RF_Standalone);
							return true;
						}, false);
					}

					Actor = FindObject<AActor>(LoadedPackage, *ActorName.ToString());
				}
			}

			if (Actor)
			{
				const UWorld* ContainerWorld = PackageObjectMapping->ContainerID.IsMainContainer() ? OuterWorld : Actor->GetTypedOuter<UWorld>();
				
				TOptional<FName> SrcActorFolderPath;

				// Make sure Source level actor folder fixup was called
				if (ContainerWorld->PersistentLevel->IsUsingActorFolders())
				{ 
					if (!ContainerWorld->PersistentLevel->LoadedExternalActorFolders.IsEmpty())
					{
						ContainerWorld->PersistentLevel->bFixupActorFoldersAtLoad = false;
						ContainerWorld->PersistentLevel->FixupActorFolders();
					}

					// Since actor's level doesn't necessarily uses actor folders, access Folder Guid directly
					const bool bDirectAccess = true;
					const FGuid ActorFolderGuid = Actor->GetFolderGuid(bDirectAccess);
					// Resolve folder guid from source container level and resolve/backup the folder path
					UActorFolder* SrcFolder = ContainerWorld->PersistentLevel->GetActorFolder(ActorFolderGuid);
					SrcActorFolderPath = SrcFolder ? SrcFolder->GetPath() : NAME_None;
				}

				if (!PackageObjectMapping->ContainerID.IsMainContainer())
				{					
					// Add Cache handle on world so it gets unloaded properly
					PackageReferencer->AddReference(ContainerWorld->GetPackage());
										
					// We only care about the source paths here
					FString SourceWorldPath, DummyUnusedPath;
					// Verify that it is indeed an instanced world
					verify(ContainerWorld->GetSoftObjectPathMapping(SourceWorldPath, DummyUnusedPath));
					FString SourceOuterWorldPath;
					OuterWorld->GetSoftObjectPathMapping(SourceOuterWorldPath, DummyUnusedPath);

					// Rename through UObject to avoid changing Actor's external packaging and folder properties
					Actor->UObject::Rename(*AddActorContainerID(PackageObjectMapping->ContainerID, Actor->GetName()), DestLevel, REN_NonTransactional | REN_DoNotDirty | REN_DontCreateRedirectors);

					// Handle child actors
					Actor->ForEachComponent<UChildActorComponent>(true, [DestLevel = DestLevel, PackageObjectMapping](UChildActorComponent* ChildActorComponent)
					{
						if (AActor* ChildActor = ChildActorComponent->GetChildActor())
						{
							ChildActor->UObject::Rename(*AddActorContainerID(PackageObjectMapping->ContainerID, ChildActor->GetName()), DestLevel, REN_NonTransactional | REN_DoNotDirty | REN_DontCreateRedirectors);
						}
					});

					// Apply Pre-ConstructionScript Properties
					TArray<FActorPropertyOverride> ActorPropertyOverrides;
					for (auto ActorOverrideMapping : PackageObjectMapping->PropertyOverrides)
					{
						if (const UWorldPartitionPropertyOverride*const* LoadedOverride = LoadedOverrides.PropertyOverrides.Find(ActorOverrideMapping.OwnerContainerID))
						{
							if (const FContainerPropertyOverride* ContainerOverride = (*LoadedOverride)->PropertyOverridesPerContainer.Find(ActorOverrideMapping.ContainerPath))
							{
								if (const FActorPropertyOverride* ActorOverride = ContainerOverride->ActorOverrides.Find(Actor->GetActorGuid()))
								{
									const bool bConstructionScriptProperties = false;
									UWorldPartitionPropertyOverride::ApplyPropertyOverrides(ActorOverride, Actor, bConstructionScriptProperties);
									
									// Store ActorOverride for Post Construction Script apply
									ActorPropertyOverrides.Add(*ActorOverride);
								}
							}
						}
					}

					// Store annotation for Post RerunConstructionScript apply
					if (ActorPropertyOverrides.Num() > 0)
					{
						ActorPropertyOverridesAnnotation.AddAnnotation(Actor, FWorldPartitionLevelHelper::FActorPropertyOverridesAnnotation(MoveTemp(ActorPropertyOverrides), PackageObjectMapping->ContainerTransform));
					}

					const FTransform TransformToApply = PackageObjectMapping->ContainerTransform * PackageObjectMapping->EditorOnlyParentTransform;
					FLevelUtils::FApplyLevelTransformParams TransformParams(nullptr, TransformToApply);
					TransformParams.Actor = Actor;
					TransformParams.bDoPostEditMove = false;
					FLevelUtils::ApplyLevelTransform(TransformParams);

					// Set the actor's instance guid
					FSetActorInstanceGuid SetActorInstanceGuid(Actor, PackageObjectMapping->ActorInstanceGuid);
						
					// Path to use when searching for this actor in MoveExternalActorsToLevel
					PackageObjectMapping->LoadedPath = *Actor->GetPathName();

					// Fixup any FSoftObjectPath from this Actor (and its SubObjects) in this container to another object in the same container with a ContainerID suffix that can be remapped to
					// a Cell package in the StreamingPolicy.
					// 
					// At  this point we are remapping the SubPathString and adding a ContainerID suffix so
					// '/Game/SomePath/WorldName.WorldName:PersistentLevel.ActorA' becomes
					// '/Game/SomeOtherPath/OuterWorldName.OuterWorldName:PersistentLevel.ActorA_{ContainerID}'
					FSoftObjectPathFixupArchive FixupArchive([&](FSoftObjectPath& Value)
					{
						if (!Value.IsNull() && Value.GetAssetPathString().Equals(SourceWorldPath, ESearchCase::IgnoreCase))
						{
							OuterWorld->GetWorldPartition()->ConvertContainerPathToEditorPath(PackageObjectMapping->ContainerID, FSoftObjectPath(Value), Value);
						}
					});
					FixupArchive.Fixup(Actor);

					if (IWorldPartitionObjectResolver* ObjectResolver = Cast<IWorldPartitionObjectResolver>(Actor))
					{
						ObjectResolver->SetWorldPartitionResolveData(FWorldPartitionResolveData(PackageObjectMapping->ContainerID, FTopLevelAssetPath(SourceWorldPath)));
					}
				}
				else if (!PackageObjectMapping->EditorOnlyParentTransform.Equals(FTransform::Identity))
				{
					FLevelUtils::FApplyLevelTransformParams TransformParams(nullptr, PackageObjectMapping->EditorOnlyParentTransform);
					TransformParams.Actor = Actor;
					TransformParams.bDoPostEditMove = false;
					FLevelUtils::ApplyLevelTransform(TransformParams);
				}

				if (DestLevel)
				{
					// Propagate resolved actor folder path
					check(!DestLevel->IsUsingActorFolders());
					if (SrcActorFolderPath.IsSet())
					{
						Actor->SetFolderPath(*SrcActorFolderPath);
					}

					DestLevel->Actors.Add(Actor);
					checkf(Actor->GetLevel() == DestLevel, TEXT("Levels mismatch, got : %s, expected: %s\nActor: %s\nActorFullName: %s\nActorPackage: %s"), *DestLevel->GetFullName(), *Actor->GetLevel()->GetFullName(), *Actor->GetActorNameOrLabel(), *Actor->GetFullName(), *Actor->GetPackage()->GetFullName());

					// Handle child actors
					Actor->ForEachComponent<UChildActorComponent>(true, [DestLevel = DestLevel](UChildActorComponent* ChildActorComponent)
					{
						if (AActor* ChildActor = ChildActorComponent->GetChildActor())
						{
							DestLevel->Actors.Add(ChildActor);
							check(ChildActor->GetLevel() == DestLevel);
						}
					});
				}

				UE_LOG(LogWorldPartition, Verbose, TEXT(" ==> Loaded %s (remaining: %d)"), *Actor->GetFullName(), LoadProgress->NumPendingLoadRequests);
			}
			else
			{
				if (LoadedPackage)
				{
					UE_LOG(LogWorldPartition, Warning, TEXT("\tPackage Content for '%s:"), *LoadedPackage->GetName());
					ForEachObjectWithOuter(LoadedPackage, [](UObject* Object)
					{
						UE_LOG(LogWorldPartition, Warning, TEXT("\t\tObject %s, Flags 0x%llx"), *Object->GetPathName(), static_cast<uint64>(Object->GetFlags()));
						return true;
					}, true);
				}

				ensureMsgf(false, TEXT("Failed to find actor '%s' in package '%s'."), *ActorName.ToString(), *LoadedPackageName.ToString());

				LoadProgress->NumFailedLoadedRequests++;
			}

			if (!LoadProgress->NumPendingLoadRequests)
			{
				CompletionCallback(!LoadProgress->NumFailedLoadedRequests);
			}
		});

		// If the package already exists, we are loading actors from a non-OFPA level package, just fire the completion callback in this case as all actors are
		// already loaded in.
		if (UPackage* ExistingPackage = FindPackage(nullptr, *PackageName.ToString()))
		{
			CompletionCallback.Execute(PackageToLoad, ExistingPackage, EAsyncLoadingResult::Succeeded);
		}
		else if (InParams.bLoadAsync)
		{
			check(InParams.DestLevel);
			const UPackage* DestPackage = InParams.DestLevel->GetPackage();
			const EPackageFlags PackageFlags = InParams.DestLevel->GetPackage()->HasAnyPackageFlags(PKG_PlayInEditor) ? PKG_PlayInEditor : PKG_None;
			const FPackagePath PackagePath = FPackagePath::FromPackageNameChecked(PackageToLoad);
			int32 RequestID = ::LoadPackageAsync(PackagePath, PackageName, CompletionCallback, PackageFlags, DestPackage->GetPIEInstanceID(), 0, &ContainerInstancingContext);
			if (InParams.AsyncRequestIDs)
			{
				InParams.AsyncRequestIDs->Add(RequestID);
			}
		}
		else
		{
			UPackage* InstancingPackage = nullptr;
			if (PackageName != PackageToLoad)
			{
				InstancingPackage = CreatePackage(*PackageName.ToString());
			}

			UPackage* Package = LoadPackage(InstancingPackage, *PackageToLoad.ToString(), LOAD_None, nullptr, &ContainerInstancingContext);
			CompletionCallback.Execute(PackageToLoad, Package, Package ? EAsyncLoadingResult::Succeeded : EAsyncLoadingResult::Failed);
		}
	}

	return (LoadProgress->NumPendingLoadRequests == 0);
}

void FWorldPartitionLevelHelper::SetForcePackageTrashingAtCleanup(ULevel* Level, bool bForcePackageTrashingAtCleanup)
{
	Level->bForcePackageTrashingAtCleanup = bForcePackageTrashingAtCleanup;
}

#endif
