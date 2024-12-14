// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compatibility/TypedElementActorParentQueries.h"

#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementHiearchyColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Framework/TypedElementIndexHasher.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "GameFramework/Actor.h"

namespace UE::Editor::DataStorage::Queries::Private
{
	static bool bAddParentColumnToActors = false;
	
	// Cvar to allow the TEDS-Outliner to automatically take over the level editor's 4 Outliners instead of appearing as a separate tab
	static FAutoConsoleVariableRef CVarUseTEDSOutliner(
		TEXT("TEDS.AddParentColumnToActors"),
		bAddParentColumnToActors,
		TEXT("Mirror parent information for actors to TEDS (only works when set on startup)"));
};

void UActorParentDataStorageFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	if(UE::Editor::DataStorage::Queries::Private::bAddParentColumnToActors)
	{
		RegisterAddParentColumn(DataStorage);
		RegisterUpdateOrRemoveParentColumn(DataStorage);
	}
}

void UActorParentDataStorageFactory::RegisterAddParentColumn(IEditorDataStorageProvider& DataStorage) const
{
	using namespace UE::Editor::DataStorage::Queries;
	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Add parent column to actor"),
			FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& Actor)
			{
				if (const AActor* ActorInstance = Cast<AActor>(Actor.Object))
				{
					if (AActor* Parent = ActorInstance->GetAttachParentActor())
					{
						uint64 IdHash = GenerateIndexHash(Parent);
						RowHandle ParentRow = Context.FindIndexedRow(IdHash);
						if (Context.IsRowAvailable(ParentRow))
						{
							Context.AddColumn(Row, FTableRowParentColumn{ .Parent = ParentRow });
						}
						else
						{
							Context.AddColumn(Row, FUnresolvedTableRowParentColumn{ .ParentIdHash = IdHash });
						}
					}
				}
			})
		.Where()
			.All<FTypedElementSyncFromWorldTag, FTypedElementActorTag>()
			.None<FTableRowParentColumn, FUnresolvedTableRowParentColumn>()
		.Compile());
}

void UActorParentDataStorageFactory::RegisterUpdateOrRemoveParentColumn(IEditorDataStorageProvider& DataStorage) const
{
	using namespace UE::Editor::DataStorage::Queries;
	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync actor's parent to column"),
			FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncExternalToDataStorage))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](IQueryContext& Context, RowHandle Row, const FTypedElementUObjectColumn& Actor, FTableRowParentColumn& Parent)
			{
				if (const AActor* ActorInstance = Cast<AActor>(Actor.Object))
				{
					if (AActor* ParentActor = ActorInstance->GetAttachParentActor())
					{
						uint64 IdHash = GenerateIndexHash(ParentActor);
						RowHandle ParentRow = Context.FindIndexedRow(IdHash);

						if (Parent.Parent != ParentRow)
						{
							if (Context.IsRowAvailable(ParentRow))
							{
								Parent.Parent = ParentRow;
							}
							else
							{
								Context.RemoveColumns<FTableRowParentColumn>(Row);
								Context.AddColumn(Row, FUnresolvedTableRowParentColumn{ .ParentIdHash = IdHash });
							}
						}
						return;
					}
				}
				Context.RemoveColumns<FTableRowParentColumn>(Row);
			})
		.Where()
			.All<FTypedElementActorTag, FTypedElementSyncFromWorldTag>()
		.Compile());
}
