// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compatibility/TypedElementActorTransformQueries.h"

#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementTransformColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "GameFramework/Actor.h"

void UActorTransformDataStorageFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	RegisterActorAddTransformColumn(DataStorage);
	RegisterActorLocalTransformToColumn(DataStorage);
	RegisterLocalTransformColumnToActor(DataStorage);
}

void UActorTransformDataStorageFactory::RegisterActorAddTransformColumn(IEditorDataStorageProvider& DataStorage) const
{
	using namespace UE::Editor::DataStorage::Queries;

	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Add transform column to actor"),
			FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& Actor)
			{
				if (const AActor* ActorInstance = Cast<AActor>(Actor.Object); ActorInstance != nullptr && ActorInstance->GetRootComponent())
				{
					Context.AddColumn(Row, FTypedElementLocalTransformColumn{ .Transform = ActorInstance->GetActorTransform() });
				}
			})
		.Where()
			.All<FTypedElementSyncFromWorldTag, FTypedElementActorTag>()
			.None<FTypedElementLocalTransformColumn>()
		.Compile());
}

void UActorTransformDataStorageFactory::RegisterActorLocalTransformToColumn(IEditorDataStorageProvider& DataStorage) const
{
	using namespace UE::Editor::DataStorage::Queries;

	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync actor transform to column"),
			FProcessor(EQueryTickPhase::PostPhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& Actor, FTypedElementLocalTransformColumn& Transform)
			{
				if (const AActor* ActorInstance = Cast<AActor>(Actor.Object); ActorInstance != nullptr && ActorInstance->GetRootComponent() != nullptr)
				{
					Transform.Transform = ActorInstance->GetActorTransform();
				}
				else
				{
					Context.RemoveColumns<FTypedElementLocalTransformColumn>(Row);
				}
			})
		.Where()
			.All<FTypedElementActorTag>()
			.Any<FTypedElementSyncFromWorldTag, FTypedElementSyncFromWorldInteractiveTag>()
		.Compile());
}

void UActorTransformDataStorageFactory::RegisterLocalTransformColumnToActor(IEditorDataStorageProvider& DataStorage) const
{
	using namespace UE::Editor::DataStorage::Queries;
	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync transform column to actor"),
			FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](FTypedElementUObjectColumn& Actor, const FTypedElementLocalTransformColumn& Transform)
			{
				if (AActor* ActorInstance = Cast<AActor>(Actor.Object); ActorInstance != nullptr)
				{
					ActorInstance->SetActorTransform(Transform.Transform);
				}
			})
		.Where()
			.All<FTypedElementActorTag, FTypedElementSyncBackToWorldTag>()
		.Compile());
}
