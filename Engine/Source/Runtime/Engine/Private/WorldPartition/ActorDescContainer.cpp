// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/ActorDescContainer.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ActorDescContainer)

#if WITH_EDITOR
#include "Editor.h"
#include "Algo/Transform.h"
#include "Engine/Level.h"
#include "ExternalPackageHelper.h"
#include "UObject/ObjectSaveContext.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionActorDescUtils.h"
#include "WorldPartition/WorldPartitionClassDescRegistry.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/DataLayer/ExternalDataLayerAsset.h"
#include "WorldPartition/ActorDescContainerSubsystem.h"
#include "DeletedObjectPlaceholder.h"

UActorDescContainer::FActorDescContainerInitializeDelegate UActorDescContainer::OnActorDescContainerInitialized;

FUObjectAnnotationSparse<FDeletedObjectPlaceholderAnnotation, true> UActorDescContainer::DeletedObjectPlaceholdersAnnotation;

FDeletedObjectPlaceholderAnnotation::FDeletedObjectPlaceholderAnnotation(const UDeletedObjectPlaceholder* InDeletedObjectPlaceholder, const FString& InActorDescContainerName)
	: DeletedObjectPlaceholder(InDeletedObjectPlaceholder)
	, ActorDescContainerName(InActorDescContainerName)
{
}

UActorDescContainer* FDeletedObjectPlaceholderAnnotation::GetActorDescContainer() const
{
	UActorDescContainerSubsystem* ActorDescContainerSubsystem = UActorDescContainerSubsystem::Get();
	UActorDescContainer* ActorDescContainer = ActorDescContainerSubsystem ? ActorDescContainerSubsystem->GetActorDescContainer(ActorDescContainerName) : nullptr;
	return ActorDescContainer;
}

#endif

UActorDescContainer::UActorDescContainer(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
#if WITH_EDITOR
	, bContainerInitialized(false)
	, bRegisteredDelegates(false)
#endif
{}

#if WITH_EDITOR
void UActorDescContainer::Initialize(const FInitializeParams& InitParams)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UActorDescContainer::Initialize);
	check(!bContainerInitialized);
	if (InitParams.PreInitialize)
	{
		InitParams.PreInitialize(this);
	}

	ContainerPackageName = InitParams.PackageName;
	if (InitParams.ExternalDataLayerAsset)
	{
		ensure(!InitParams.ContentBundleGuid.IsValid());
		ExternalDataLayerAsset = InitParams.ExternalDataLayerAsset;
	}
	else if (InitParams.ContentBundleGuid.IsValid())
	{
		ContentBundleGuid = InitParams.ContentBundleGuid;
	}

	TArray<FAssetData> ExternalAssets;
	TArray<FString> InternalAssets;
	if (!ContainerPackageName.IsNone() && !FPackageName::IsTempPackage(ContainerPackageName.ToString()))
	{
		const FString ContainerExternalActorsPath = GetExternalActorPath();

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

		// Do a synchronous scan of the level external actors path.
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ScanSynchronous);
			AssetRegistry.ScanSynchronous({ ContainerExternalActorsPath }, TArray<FString>());
		}

		// Gather external actors
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GetExternalAssets);

			FARFilter Filter;
			Filter.bRecursivePaths = true;
			Filter.bIncludeOnlyOnDiskAssets = true;
			Filter.PackagePaths.Add(*ContainerExternalActorsPath);

			FExternalPackageHelper::GetSortedAssets(Filter, ExternalAssets);
		}

		// Gather non-external actors
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(GetInternalAssets);

			FARFilter Filter;
			Filter.bIncludeOnlyOnDiskAssets = true;
			Filter.PackageNames.Add(ContainerPackageName);

			TArray<FAssetData> WorldAssetData;
			AssetRegistry.GetAssets(Filter, WorldAssetData);

			// Transform world assets
			static FName NAME_ActorsMetaData(TEXT("ActorsMetaData"));
			for (const FAssetData& AssetData : WorldAssetData)
			{
				FString ActorsMetaDataStr;
				if (AssetData.GetTagValue(NAME_ActorsMetaData, ActorsMetaDataStr))
				{
					TArray<FString> ActorsMetaData;
					if (ActorsMetaDataStr.ParseIntoArray(ActorsMetaData, TEXT(";")))
					{
						InternalAssets.Append(ActorsMetaData);
					}
				}
			}
		}
	}

	UE_LOG(LogWorldPartition, Verbose, TEXT("Parsed actor descriptor container package '%s': %d external actors, %d internal actors"), *InitParams.PackageName.ToString(), ExternalAssets.Num(), InternalAssets.Num());

	FWorldPartitionClassDescRegistry& ClassDescRegistry = FWorldPartitionClassDescRegistry::Get();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GatherDescriptorsClass);
		
		TSet<FTopLevelAssetPath> ClassPaths;
		for (const FAssetData& Asset : ExternalAssets)
		{
			ClassPaths.Add(Asset.AssetClassPath);
		}

		ClassDescRegistry.PrefetchClassDescs(ClassPaths.Array());
	}

	TMap<FGuid, TUniquePtr<FWorldPartitionActorDesc>> ValidActorDescs;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CreateDescriptors);

		TMap<FName, FWorldPartitionActorDesc*> ActorDescsByPackage;
		for (const FAssetData& Asset : ExternalAssets)
		{
			TUniquePtr<FWorldPartitionActorDesc> ActorDesc = FWorldPartitionActorDescUtils::GetActorDescriptorFromAssetData(Asset);
								
			if (!ActorDesc.IsValid())
			{
				UE_LOG(LogWorldPartition, Warning, TEXT("Invalid actor descriptor for actor '%s' from package '%s'"), *Asset.GetObjectPathString(), *Asset.PackageName.ToString());
				InvalidActors.Emplace(Asset);
			} 
			else if (!ActorDesc->GetNativeClass().IsValid())
			{
				UE_LOG(LogWorldPartition, Warning, TEXT("Invalid actor native class: Actor: '%s' (guid '%s') from package '%s'"),
					*ActorDesc->GetActorNameString(),
					*ActorDesc->GetGuid().ToString(),
					*ActorDesc->GetActorPackage().ToString());
				InvalidActors.Emplace(Asset);
			}
			else if (ActorDesc->GetBaseClass().IsValid() && !ClassDescRegistry.IsRegisteredClass(ActorDesc->GetBaseClass()))
			{
				UE_LOG(LogWorldPartition, Warning, TEXT("Unknown actor base class `%s`: Actor: '%s' (guid '%s') from package '%s'"),
					*ActorDesc->GetBaseClass().ToString(),
					*ActorDesc->GetActorNameString(),
					*ActorDesc->GetGuid().ToString(),
					*ActorDesc->GetActorPackage().ToString());
				InvalidActors.Emplace(Asset);
			}
			else if (InitParams.FilterActorDesc && !InitParams.FilterActorDesc(ActorDesc.Get()))
			{
				InvalidActors.Emplace(Asset);
			}
			// At this point, the actor descriptor is well formed and valid on its own. We now make validations based on the already registered
			// actor descriptors, such as duplicated actor GUIDs or multiple actors in the same package, etc.
			else if (FWorldPartitionActorDesc* ExistingDescPackage = ActorDescsByPackage.FindRef(ActorDesc->GetActorPackage()))
			{
				UE_LOG(LogWorldPartition, Warning, TEXT("Duplicate actor descriptor in package `%s`: Actor: '%s' -> Existing actor '%s'"), 
					*ActorDesc->GetActorPackage().ToString(), 
					*ActorDesc->GetActorNameString(), 
					*ExistingDescPackage->GetActorNameString());

				// No need to add all actors in the same package several times as we only want to open the package for delete when repairing
				if (ValidActorDescs.Contains(ActorDesc->GetGuid()))
				{
					InvalidActors.Emplace(Asset);
					ValidActorDescs.Remove(ActorDesc->GetGuid());
				}
			}
			else if (TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDescPtr = ValidActorDescs.Find(ActorDesc->GetGuid()))
			{
				const FWorldPartitionActorDesc* ExistingActorDesc = ExistingActorDescPtr->Get();
				check(ExistingActorDesc->GetGuid() == ActorDesc->GetGuid());
				UE_LOG(LogWorldPartition, Warning, TEXT("Duplicate actor descriptor guid `%s`: Actor: '%s' from package '%s' -> Existing actor '%s' from package '%s'"), 
					*ActorDesc->GetGuid().ToString(), 
					*ActorDesc->GetActorNameString(), 
					*ActorDesc->GetActorPackage().ToString(),
					*ExistingActorDesc->GetActorNameString(),
					*ExistingActorDesc->GetActorPackage().ToString());
				InvalidActors.Emplace(Asset);
			}
			else
			{
				ActorDescsByPackage.Add(ActorDesc->GetActorPackage(), ActorDesc.Get());
				ValidActorDescs.Add(ActorDesc->GetGuid(), MoveTemp(ActorDesc));
			}
		}

		for (const FString& InternalAsset : InternalAssets)
		{
			FWorldPartitionActorDescUtils::FActorDescInitParams ActorDescInitParams(InternalAsset);

			TUniquePtr<FWorldPartitionActorDesc> ActorDesc = FWorldPartitionActorDescUtils::GetActorDescriptorFromInitParams(ActorDescInitParams, ContainerPackageName);

			ActorDescsByPackage.Add(ActorDesc->GetActorPackage(), ActorDesc.Get());
			ValidActorDescs.Add(ActorDesc->GetGuid(), MoveTemp(ActorDesc));
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(RegisterDescriptors);
		for (auto& [ActorGuid, ActorDesc] : ValidActorDescs)
		{
			RegisterActorDescriptor(ActorDesc.Release());
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(OnActorDescContainerInitialized)
		OnActorDescContainerInitialized.Broadcast(this);
	}

	bRegisteredDelegates = InitParams.bShouldRegisterEditorDeletages && ShouldRegisterDelegates();
	
	if (bRegisteredDelegates)
	{
		RegisterEditorDelegates();
	}

	bContainerInitialized = true;
}

void UActorDescContainer::Uninitialize()
{
	if (bContainerInitialized)
	{
		if (bRegisteredDelegates)
		{
			UnregisterEditorDelegates();
			bRegisteredDelegates = false;
		}
		bContainerInitialized = false;
	}

	for (TUniquePtr<FWorldPartitionActorDesc>& ActorDescPtr : ActorDescList)
	{
		if (FWorldPartitionActorDesc* ActorDesc = ActorDescPtr.Get())
		{
			UnregisterActorDescriptor(ActorDesc);
		}
		ActorDescPtr.Reset();
	}
}

void UActorDescContainer::BeginDestroy()
{
	Super::BeginDestroy();

	Uninitialize();
}

FString UActorDescContainer::GetExternalActorPath() const
{
	return ULevel::GetExternalActorsPath(ContainerPackageName.ToString());
}

FString UActorDescContainer::GetExternalObjectPath() const
{
	return FExternalPackageHelper::GetExternalObjectsPath(ContainerPackageName.ToString());
}

bool UActorDescContainer::HasExternalContent() const
{
	check(!ExternalDataLayerAsset || ExternalDataLayerAsset->GetUID().IsValid());
	return ExternalDataLayerAsset ? true : GetContentBundleGuid().IsValid();
}

bool UActorDescContainer::IsActorDescHandled(const AActor* InActor, bool bInUseLoadedPath) const
{
	// Actor External Content Guid must match Container's External Content Guid to be considered
	// AWorldDataLayers actors are an exception as they don't have an External Content Guid
	const bool bIsCandidateActor = InActor->IsA<AWorldDataLayers>() ||
		(!HasExternalContent() && !InActor->HasExternalContent()) ||
		(ExternalDataLayerAsset && (ExternalDataLayerAsset == InActor->GetExternalDataLayerAsset())) ||
		(ContentBundleGuid.IsValid() && (ContentBundleGuid == InActor->GetContentBundleGuid()));

	if (bIsCandidateActor)
	{
		const FName LoadedPackageName = InActor->GetPackage()->GetLoadedPath().GetPackageFName();
		const FString ActorPackageName = bInUseLoadedPath && !LoadedPackageName.IsNone() ? LoadedPackageName.ToString() : InActor->GetPackage()->GetName();
		const FString ExternalActorPath = GetExternalActorPath() / TEXT("");
		return ActorPackageName.StartsWith(ExternalActorPath);
	}
	return false;
}

bool UActorDescContainer::IsActorDescHandled(const AActor* InActor) const
{
	return IsActorDescHandled(InActor, false);
}

void UActorDescContainer::RegisterActorDescriptor(FWorldPartitionActorDesc* ActorDesc)
{
	FActorDescList::AddActorDescriptor(ActorDesc);

	ActorDesc->SetContainer(this);
		
	ActorsByName.Add(ActorDesc->GetActorName(), ActorsByGuid.FindChecked(ActorDesc->GetGuid()));
}

void UActorDescContainer::UnregisterActorDescriptor(FWorldPartitionActorDesc* ActorDesc)
{
	FActorDescList::RemoveActorDescriptor(ActorDesc);
	ActorDesc->SetContainer(nullptr);
	verifyf(ActorsByName.Remove(ActorDesc->GetActorName()), TEXT("Missing actor '%s' from container '%s'"), *ActorDesc->GetActorNameString(), *ContainerPackageName.ToString());
}

bool UActorDescContainer::ShouldHandleActorEvent(const AActor* Actor, bool bInUseLoadedPath) const
{
	return Actor && IsActorDescHandled(Actor, bInUseLoadedPath) && Actor->IsMainPackageActor() && Actor->GetLevel();
}

bool UActorDescContainer::ShouldHandleActorEvent(const AActor* Actor)
{
	return ShouldHandleActorEvent(Actor, false);
}

const FWorldPartitionActorDesc* UActorDescContainer::GetActorDescByPath(const FString& ActorPath) const
{
	FString ActorName;
	FString ActorContext;
	if (!ActorPath.Split(TEXT("."), &ActorContext, &ActorName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		ActorName = ActorPath;
	}

	return GetActorDescByName(FName(*ActorName));
}

const FWorldPartitionActorDesc* UActorDescContainer::GetActorDescByPath(const FSoftObjectPath& ActorPath) const
{
	return GetActorDescByPath(ActorPath.ToString());
}

const FWorldPartitionActorDesc* UActorDescContainer::GetActorDescByName(FName ActorName) const
{
	if (const TUniquePtr<FWorldPartitionActorDesc>* const* ActorDesc = ActorsByName.Find(ActorName))
	{
		return (*ActorDesc)->Get();
	}

	return nullptr;
}

bool UActorDescContainer::ShouldHandleDeletedObjectPlaceholderEvent(const UDeletedObjectPlaceholder* InDeletedObjectPlaceholder) const
{
	const FExternalDataLayerUID ContainerExternalDataLayerUID = ExternalDataLayerAsset ? ExternalDataLayerAsset->GetUID() : FExternalDataLayerUID();
	if (ContainerExternalDataLayerUID == InDeletedObjectPlaceholder->GetExternalDataLayerUID())
	{
		const FString PackageName = InDeletedObjectPlaceholder->GetPackage()->GetName();
		const FString ContainerExternalActorPath = GetExternalActorPath() / TEXT("");
		return PackageName.StartsWith(ContainerExternalActorPath);
	}
	return false;
};

void UActorDescContainer::OnDeletedObjectPlaceholderCreated(const UDeletedObjectPlaceholder* InDeletedObjectPlaceholder)
{
	const UObject* OriginalObject = InDeletedObjectPlaceholder->GetOriginalObject();
	if (const AActor* Actor = Cast<AActor>(OriginalObject))
	{
		if (ShouldHandleDeletedObjectPlaceholderEvent(InDeletedObjectPlaceholder))
		{
			if (GetActorDescriptor(Actor->GetActorGuid()))
			{
				DeletedObjectPlaceholdersAnnotation.AddAnnotation(Actor, FDeletedObjectPlaceholderAnnotation(InDeletedObjectPlaceholder, GetContainerName()));
			}
		}
	}
}

void UActorDescContainer::OnObjectPreSave(UObject* Object, FObjectPreSaveContext SaveContext)
{
	if (!SaveContext.IsProceduralSave() && !(SaveContext.GetSaveFlags() & SAVE_FromAutosave))
	{
		if (const AActor* Actor = Cast<AActor>(Object))
		{
			if (ShouldHandleActorEvent(Actor))
			{
				check(IsValidChecked(Actor));

				// Handle the case where the actor changed package but the old/empty package has not been processed/deleted
				// One case where this can happen is if the user choses to save the new package but unchecks the deleted package
				// Remove(unhash) the corresponding original actor (guid) from its original container before adding the new one (unhash before hashing)
				if (FDeletedObjectPlaceholderAnnotation Annotation = DeletedObjectPlaceholdersAnnotation.GetAndRemoveAnnotation(Actor); Annotation.IsValid())
				{
					// In the case where the object changed to a new container and created a new package, then changed back to its original location,
					// OnDeletedObjectPlaceholderCreated will not be called for the newly created package
					// This is why we need to validate that the annotation's container is still relevant by the annotation's DeletedObjectPlaceholder using ShouldHandleDeletedObjectPlaceholderEvent
					UActorDescContainer* ActorDescContainer = Annotation.GetActorDescContainer();
					if (ActorDescContainer && ActorDescContainer->ShouldHandleDeletedObjectPlaceholderEvent(Annotation.GetDeletedObjectPlaceholder()))
					{
						check(Annotation.GetDeletedObjectPlaceholder()->GetOriginalObject() == Actor);
						verify(ActorDescContainer->RemoveActor(Actor->GetActorGuid()));
					}
				}
				
				if (TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = GetActorDescriptor(Actor->GetActorGuid()))
				{
					// Existing actor
					OnActorDescUpdating(ExistingActorDesc->Get());
					FWorldPartitionActorDescUtils::UpdateActorDescriptorFromActor(Actor, *ExistingActorDesc);
					OnActorDescUpdated(ExistingActorDesc->Get());
				}
				else
				{
					// New actor
					FWorldPartitionActorDesc* AddedActorDesc = Actor->CreateActorDesc().Release();
					RegisterActorDescriptor(AddedActorDesc);
					OnActorDescAdded(AddedActorDesc);
				}
			}
		}
	}
}

void UActorDescContainer::OnPackageDeleted(UPackage* Package)
{
	if (const AActor* Actor = AActor::FindActorInPackage(Package))
	{
		if (ShouldHandleActorEvent(Actor))
		{
			RemoveActor(Actor->GetActorGuid());
		}
	}
	else if (UDeletedObjectPlaceholder* DeletedObjectPlaceholder = UDeletedObjectPlaceholder::FindInPackage(Package))
	{
		if (ShouldHandleDeletedObjectPlaceholderEvent(DeletedObjectPlaceholder))
		{
			// Here we validate that we didn't already processed the DeletedObjectPlaceholder in OnObjectPreSave
			const AActor* OriginalActor = CastChecked<AActor>(DeletedObjectPlaceholder->GetOriginalObject());
			if (FDeletedObjectPlaceholderAnnotation Annotation = DeletedObjectPlaceholdersAnnotation.GetAndRemoveAnnotation(OriginalActor); Annotation.IsValid())
			{
				check(Annotation.GetDeletedObjectPlaceholder() == DeletedObjectPlaceholder);
				check(Annotation.GetActorDescContainer() == this);
				verify(RemoveActor(OriginalActor->GetActorGuid()));
			}
		}
	}
}

void UActorDescContainer::OnClassDescriptorUpdated(const FWorldPartitionActorDesc* InClassDesc)
{
	FWorldPartitionClassDescRegistry& ClassDescRegistry = FWorldPartitionClassDescRegistry::Get();

	TArray<FString> ActorPackages;
	for (FActorDescList::TIterator<> ActorDescIterator(this); ActorDescIterator; ++ActorDescIterator)
	{
		if (ActorDescIterator->GetBaseClass().IsValid())
		{
			if (const FWorldPartitionActorDesc* ActorClassDesc = ClassDescRegistry.GetClassDescDefaultForActor(ActorDescIterator->GetBaseClass()))
			{
				if (ClassDescRegistry.IsDerivedFrom(ActorClassDesc, InClassDesc))
				{
					ActorPackages.Add(ActorDescIterator->GetActorPackage().ToString());
				}
			}
		}
	}

	if (ActorPackages.Num())
	{
		FARFilter Filter;
		Filter.bIncludeOnlyOnDiskAssets = true;
		Filter.PackageNames.Reserve(ActorPackages.Num());
		Algo::Transform(ActorPackages, Filter.PackageNames, [](const FString& ActorPath) { return *ActorPath; });

		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		AssetRegistry.ScanSynchronous(TArray<FString>(), ActorPackages);

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssets(Filter, Assets);

		for (const FAssetData& Asset : Assets)
		{
			TUniquePtr<FWorldPartitionActorDesc> NewActorDesc = FWorldPartitionActorDescUtils::GetActorDescriptorFromAssetData(Asset);
								
			if (NewActorDesc.IsValid() && NewActorDesc->GetNativeClass().IsValid())
			{
				if (TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = GetActorDescriptor(NewActorDesc->GetGuid()))
				{
					OnActorDescUpdating(ExistingActorDesc->Get());
					FWorldPartitionActorDescUtils::UpdateActorDescriptorFromActorDescriptor(NewActorDesc, *ExistingActorDesc);
					OnActorDescUpdated(ExistingActorDesc->Get());
				}
			}
		}
	}
}

bool UActorDescContainer::RemoveActor(const FGuid& ActorGuid)
{
	if (TUniquePtr<FWorldPartitionActorDesc>* ExistingActorDesc = GetActorDescriptor(ActorGuid))
	{
		OnActorDescRemoved(ExistingActorDesc->Get());
		UnregisterActorDescriptor(ExistingActorDesc->Get());
		ExistingActorDesc->Reset();
		return true;
	}

	return false;
}

bool UActorDescContainer::ShouldRegisterDelegates() const
{
	return GEditor && !IsTemplate() && !IsRunningCookCommandlet();
}

void UActorDescContainer::RegisterEditorDelegates()
{
	FCoreUObjectDelegates::OnObjectPreSave.AddUObject(this, &UActorDescContainer::OnObjectPreSave);
	FEditorDelegates::OnPackageDeleted.AddUObject(this, &UActorDescContainer::OnPackageDeleted);

	FWorldPartitionClassDescRegistry& ClassDescRegistry = FWorldPartitionClassDescRegistry::Get();
	ClassDescRegistry.OnClassDescriptorUpdated().AddUObject(this, &UActorDescContainer::OnClassDescriptorUpdated);

	UDeletedObjectPlaceholder::OnObjectCreated.AddUObject(this, &UActorDescContainer::OnDeletedObjectPlaceholderCreated);
}

void UActorDescContainer::UnregisterEditorDelegates()
{
	FCoreUObjectDelegates::OnObjectPreSave.RemoveAll(this);
	FEditorDelegates::OnPackageDeleted.RemoveAll(this);

	FWorldPartitionClassDescRegistry& ClassDescRegistry = FWorldPartitionClassDescRegistry::Get();
	ClassDescRegistry.OnClassDescriptorUpdated().RemoveAll(this);

	UDeletedObjectPlaceholder::OnObjectCreated.RemoveAll(this);
}

void UActorDescContainer::OnActorDescAdded(FWorldPartitionActorDesc* NewActorDesc)
{
	OnActorDescAddedEvent.Broadcast(NewActorDesc);
}

void UActorDescContainer::OnActorDescRemoved(FWorldPartitionActorDesc* ActorDesc)
{
	OnActorDescRemovedEvent.Broadcast(ActorDesc);
}

void UActorDescContainer::OnActorDescUpdating(FWorldPartitionActorDesc* ActorDesc)
{
	OnActorDescUpdatingEvent.Broadcast(ActorDesc);
}

void UActorDescContainer::OnActorDescUpdated(FWorldPartitionActorDesc* ActorDesc)
{
	OnActorDescUpdatedEvent.Broadcast(ActorDesc);
}
#endif

