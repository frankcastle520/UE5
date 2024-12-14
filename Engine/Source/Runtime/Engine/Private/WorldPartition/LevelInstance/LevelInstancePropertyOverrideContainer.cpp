// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/LevelInstance/LevelInstancePropertyOverrideContainer.h"

#if WITH_EDITOR
#include "WorldPartition/LevelInstance/LevelInstancePropertyOverrideDesc.h"
#include "WorldPartition/WorldPartitionActorDescInstance.h"
#include "WorldPartition/ActorDescContainerSubsystem.h"
#endif

#if WITH_EDITOR

void ULevelInstancePropertyOverrideContainer::Initialize(const FInitializeParams& InitParams)
{
	// Call PreInit callback
	if (InitParams.PreInitialize)
	{
		InitParams.PreInitialize(this);
	}

	check(PropertyOverrideDesc);
	check(GetBaseContainer());
	check(GetBaseContainer()->GetContainerPackage() == InitParams.PackageName);
	
	SetIsProxy();

	// Copy values from Container we are proxying
	ContainerPackageName = GetBaseContainer()->GetContainerPackage();
	ContentBundleGuid = GetBaseContainer()->GetContentBundleGuid();
	ExternalDataLayerAsset = GetBaseContainer()->GetExternalDataLayerAsset();

	bContainerInitialized = true;
}

void ULevelInstancePropertyOverrideContainer::Uninitialize()
{
	// UActorDescContainer::BeginDestroy calls Uninitialize()
	if (bContainerInitialized)
	{
		// nothing to do except unregister delegates this class is a proxy to the PropertyOverrideDesc Base Container + Override Descs
		UnregisterBaseContainerDelegates();
		PropertyOverrideDesc = nullptr;
		bContainerInitialized = false;
	}
}

FActorDescList::FGuidActorDescMap& ULevelInstancePropertyOverrideContainer::GetProxyActorsByGuid() const
{
	return GetBaseContainer()->GetActorsByGuid();
}

UActorDescContainer* ULevelInstancePropertyOverrideContainer::GetBaseContainer() const
{
	check(PropertyOverrideDesc);
	return PropertyOverrideDesc->GetBaseContainer();
}

void ULevelInstancePropertyOverrideContainer::SetPropertyOverrideDesc(const TSharedPtr<FLevelInstancePropertyOverrideDesc>& InPropertyOverrideDesc)
{
	if (PropertyOverrideDesc == InPropertyOverrideDesc)
	{
		return;
	}
	check(InPropertyOverrideDesc);

	if (PropertyOverrideDesc)
	{
		UnregisterBaseContainerDelegates();
	}

	check(!PropertyOverrideDesc || (GetContainerName() == InPropertyOverrideDesc->GetContainerName()));
			
	PropertyOverrideDesc = InPropertyOverrideDesc;
	PropertyOverrideDesc->SetContainerForActorDescs(this);

	RegisterBaseContainerDelegates();
}

void ULevelInstancePropertyOverrideContainer::UnregisterBaseContainerDelegates()
{
	check(PropertyOverrideDesc);
	UActorDescContainer* BaseContainer = GetBaseContainer();
	check(BaseContainer);
	BaseContainer->OnActorDescRemovedEvent.RemoveAll(this);
	BaseContainer->OnActorDescUpdatingEvent.RemoveAll(this);
	BaseContainer->OnActorDescUpdatedEvent.RemoveAll(this);
}

void ULevelInstancePropertyOverrideContainer::RegisterBaseContainerDelegates()
{
	check(PropertyOverrideDesc)
	UActorDescContainer* BaseContainer = PropertyOverrideDesc->GetBaseContainer();
	check(BaseContainer);
	BaseContainer->OnActorDescRemovedEvent.AddUObject(this, &ULevelInstancePropertyOverrideContainer::OnBaseContainerActorDescRemoved);
	BaseContainer->OnActorDescUpdatingEvent.AddUObject(this, &ULevelInstancePropertyOverrideContainer::OnBaseContainerActorDescUpdating);
	BaseContainer->OnActorDescUpdatedEvent.AddUObject(this, &ULevelInstancePropertyOverrideContainer::OnBaseContainerActorDescUpdated);
}

void ULevelInstancePropertyOverrideContainer::OnBaseContainerActorDescRemoved(FWorldPartitionActorDesc* InActorDesc)
{
	OnActorDescRemoved(InActorDesc);
}

void ULevelInstancePropertyOverrideContainer::OnBaseContainerActorDescUpdating(FWorldPartitionActorDesc* InActorDesc)
{
	OnActorDescUpdating(InActorDesc);
}

void ULevelInstancePropertyOverrideContainer::OnBaseContainerActorDescUpdated(FWorldPartitionActorDesc* InActorDesc)
{
	OnActorDescUpdated(InActorDesc);
}

FString ULevelInstancePropertyOverrideContainer::GetContainerName() const
{
	check(PropertyOverrideDesc);
	return PropertyOverrideDesc->GetContainerName();
}

FWorldPartitionActorDesc* ULevelInstancePropertyOverrideContainer::GetActorDesc(const FGuid& InActorGuid)
{
	// Same pattern used for all Get*(ActorGuid) methods overriden, find the ActorDesc in the base container
	// If we find it then check if we have an override for it passing in an empty path as we are looking for an override on this top level container and not a child container
	if (FWorldPartitionActorDesc* BaseActorDesc = GetBaseContainer()->GetActorDesc(InActorGuid))
	{
		if (FWorldPartitionActorDesc* OverrideActorDesc = GetOverrideActorDesc(InActorGuid))
		{
			return OverrideActorDesc;
		}

		return BaseActorDesc;
	}

	return nullptr;
}

const FWorldPartitionActorDesc* ULevelInstancePropertyOverrideContainer::GetActorDesc(const FGuid& InActorGuid) const
{
	return const_cast<ULevelInstancePropertyOverrideContainer*>(this)->GetActorDesc(InActorGuid);
}

FWorldPartitionActorDesc& ULevelInstancePropertyOverrideContainer::GetActorDescChecked(const FGuid& InActorGuid)
{
	if (FWorldPartitionActorDesc* OverrideActorDesc = GetOverrideActorDesc(InActorGuid))
	{
		return *OverrideActorDesc;
	}

	return GetBaseContainer()->GetActorDescChecked(InActorGuid);
}

const FWorldPartitionActorDesc& ULevelInstancePropertyOverrideContainer::GetActorDescChecked(const FGuid& InActorGuid) const
{
	return const_cast<ULevelInstancePropertyOverrideContainer*>(this)->GetActorDescChecked(InActorGuid);
}

const FWorldPartitionActorDesc* ULevelInstancePropertyOverrideContainer::GetActorDescByPath(const FString& InActorPath) const
{
	if (const FWorldPartitionActorDesc* BaseActorDesc = GetBaseContainer()->GetActorDescByPath(InActorPath))
	{
		if (const FWorldPartitionActorDesc* OverrideActorDesc = GetOverrideActorDesc(BaseActorDesc->GetGuid()))
		{
			return OverrideActorDesc;
		}

		return BaseActorDesc;
	}

	return nullptr;
}

const FWorldPartitionActorDesc* ULevelInstancePropertyOverrideContainer::GetActorDescByPath(const FSoftObjectPath& InActorPath) const
{
	return GetActorDescByPath(InActorPath.ToString());
}

const FWorldPartitionActorDesc* ULevelInstancePropertyOverrideContainer::GetActorDescByName(FName InActorName) const
{
	if (const FWorldPartitionActorDesc* BaseActorDesc = GetBaseContainer()->GetActorDescByName(InActorName))
	{
		if (const FWorldPartitionActorDesc* OverrideActorDesc = GetOverrideActorDesc(BaseActorDesc->GetGuid()))
		{
			return OverrideActorDesc;
		}

		return BaseActorDesc;
	}

	return nullptr;
}

FWorldPartitionActorDesc* ULevelInstancePropertyOverrideContainer::GetOverrideActorDesc(const FGuid& InActorGuid, const FActorContainerPath& InContainerPath) const
{
	check(PropertyOverrideDesc);
	return PropertyOverrideDesc->GetOverrideActorDesc(InActorGuid, InContainerPath);
}

#endif

