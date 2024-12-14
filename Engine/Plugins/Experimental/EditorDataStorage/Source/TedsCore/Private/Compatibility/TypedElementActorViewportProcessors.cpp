// Copyright Epic Games, Inc. All Rights Reserved.

#include "Compatibility/TypedElementActorViewportProcessors.h"

#include "Components/PrimitiveComponent.h"
#include "Elements/Columns/TypedElementCompatibilityColumns.h"
#include "Elements/Columns/TypedElementViewportColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "HAL/IConsoleManager.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

namespace UE::Editor::DataStorage::Private
{
	FAutoConsoleCommandWithArgsAndOutputDevice SetOutlineColorConsoleCommand(
		TEXT("TEDS.Debug.SetOutlineColor"),
		TEXT("Adds an outline color to selected objects."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, FOutputDevice& Output)
			{
				using namespace UE::Editor::DataStorage::Queries;

				TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.Debug.AddOverlayColorToSelectionCommand);

				if (IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
				{
					static QueryHandle OverlayQuery = InvalidQueryHandle;
					if (OverlayQuery == InvalidQueryHandle)
					{
						OverlayQuery = DataStorage->RegisterQuery(
							Select()
							.Where()
								.All<FTypedElementSelectionColumn>()
							.Compile());
					}
				
					if (OverlayQuery == InvalidQueryHandle)
					{
						return;
					}

					if (Args.IsEmpty())
					{
						Output.Log(TEXT("Provide a color index (0-7) to use as outline"));
						return;
					}
				
					// Parse the color
					int32 ColorIndex;
					LexFromString(ColorIndex, *Args[0]);

					if (!(ColorIndex >= 0 && ColorIndex <= 7))
					{
						Output.Log(TEXT("Color index must be in range [0,7]"));
						return;
					}

					TArray<RowHandle> RowHandles;
				
					DataStorage->RunQuery(OverlayQuery, [ColorIndex, &RowHandles](const FQueryDescription&, IDirectQueryContext& Context)
					{
						RowHandles = Context.GetRowHandles();
					});

					for (RowHandle Row : RowHandles)
					{
						DataStorage->AddColumn(Row, 
							FTypedElementViewportOutlineColorColumn{ .SelectionOutlineColorIndex = static_cast<uint8>(ColorIndex) });
						DataStorage->AddColumn<FTypedElementSyncBackToWorldTag>(Row);
					}
				}
			}));

	FAutoConsoleCommandWithArgsAndOutputDevice SetSelectionOverlayColorConsoleCommand(
		TEXT("TEDS.Debug.SetOverlayColor"),
		TEXT("Adds an overlay color to selected objects."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, FOutputDevice& Output)
			{
				using namespace UE::Editor::DataStorage::Queries;

				TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.Debug.AddOverlayColorToSelectionCommand);

				if (IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
				{
					static QueryHandle OverlayQuery = InvalidQueryHandle;
					if (OverlayQuery == InvalidQueryHandle)
					{
						OverlayQuery = DataStorage->RegisterQuery(
							Select()
							.Where()
								.All<FTypedElementSelectionColumn>()
							.Compile());
					}
				
					if (OverlayQuery == InvalidQueryHandle)
					{
						return;
					}

					if (Args.IsEmpty())
					{
						Output.Log(TEXT("Provide a color in hexadecimal format (#RRGGBBAA) to overlay."));
						return;
					}
				
					// Parse the color
					FColor Color = FColor::FromHex(Args[0]);
					Color.A = FMath::Clamp(Color.A, 0, 128);

					TArray<RowHandle> RowHandles;
				
					DataStorage->RunQuery(OverlayQuery, [Color, &RowHandles](const FQueryDescription&, IDirectQueryContext& Context)
					{
						RowHandles = Context.GetRowHandles();
					});

					for (RowHandle Row : RowHandles)
					{
						DataStorage->RemoveColumn<FTypedElementViewportOverlayColorColumn>(Row);
						DataStorage->AddColumn(Row, FTypedElementViewportOverlayColorColumn{ .OverlayColor = Color });
					}
				}
			}));

	FAutoConsoleCommandWithArgsAndOutputDevice RemoveSelectionOverlayColorConsoleCommand(
		TEXT("TEDS.Debug.RemoveOverlayColor"),
		TEXT("Removes an overlay color to selected objects."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateLambda([](const TArray<FString>& Args, FOutputDevice& Output)
			{
				using namespace UE::Editor::DataStorage::Queries;

				TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.Debug.AddOverlayColorToSelectionCommand);

				if (IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
				{
					static QueryHandle OverlayQuery = InvalidQueryHandle;
					if (OverlayQuery == InvalidQueryHandle)
					{
						OverlayQuery = DataStorage->RegisterQuery(
							Select()
							.Where()
								.All<FTypedElementSelectionColumn>()
							.Compile());
					}
				
					if (OverlayQuery == InvalidQueryHandle)
					{
						return;
					}

					TArray<RowHandle> RowHandles;
				
					DataStorage->RunQuery(OverlayQuery, [&RowHandles](const FQueryDescription&, IDirectQueryContext& Context)
					{
						RowHandles = Context.GetRowHandles();
					});

					for (RowHandle Row : RowHandles)
					{
						DataStorage->RemoveColumn<FTypedElementViewportOverlayColorColumn>(Row);
					}
				}
			}));
} // namespace UE::Editor::DataStorage::Private

void UActorViewportDataStorageFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	RegisterOutlineColorColumnToActor(DataStorage);
	RegisterOverlayColorColumnToActor(DataStorage);
}

void UActorViewportDataStorageFactory::RegisterOutlineColorColumnToActor(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync viewport outline color column to actor"),
			FProcessor(EQueryTickPhase::DuringPhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::SyncDataStorageToExternal))
				.SetExecutionMode(EExecutionMode::GameThread),
			[](FTypedElementUObjectColumn& Actor, const FTypedElementViewportOutlineColorColumn& ViewportColor)
			{
				if (AActor* ActorInstance = Cast<AActor>(Actor.Object); ActorInstance != nullptr)
				{
					bool bIncludeFromChildActors = false;
					ActorInstance->ForEachComponent<UPrimitiveComponent>(bIncludeFromChildActors, [&ViewportColor](UPrimitiveComponent* PrimitiveComponent)
					{
						PrimitiveComponent->SetSelectionOutlineColorIndex(ViewportColor.SelectionOutlineColorIndex);
					});
				}
			})
		.Where()
			.All<FTypedElementActorTag, FTypedElementSyncBackToWorldTag>()
		.Compile());
}

void UActorViewportDataStorageFactory::RegisterOverlayColorColumnToActor(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Sync viewport overlay color column to actor"),
			FObserver::OnAdd<FTypedElementViewportOverlayColorColumn>()
				.SetExecutionMode(EExecutionMode::GameThread),
			[](FTypedElementUObjectColumn& Actor, const FTypedElementViewportOverlayColorColumn& ViewportColor)
			{
				if (AActor* ActorInstance = Cast<AActor>(Actor.Object); ActorInstance != nullptr)
				{
					bool bIncludeFromChildActors = true;
					ActorInstance->ForEachComponent<UPrimitiveComponent>(bIncludeFromChildActors, [&ViewportColor](UPrimitiveComponent* PrimitiveComponent)
					{
						PrimitiveComponent->SetOverlayColor(ViewportColor.OverlayColor);
						PrimitiveComponent->MarkRenderStateDirty();
					});
				}
			})
		.Where()
			.All<FTypedElementActorTag>()
		.Compile());

	DataStorage.RegisterQuery(
		Select(
			TEXT("Remove viewport overlay color column from actor"),
			FObserver::OnRemove<FTypedElementViewportOverlayColorColumn>(),
			[](FTypedElementUObjectColumn& Actor)
			{
				if (AActor* ActorInstance = Cast<AActor>(Actor.Object); ActorInstance != nullptr)
				{
					bool bIncludeFromChildActors = true;
					ActorInstance->ForEachComponent<UPrimitiveComponent>(bIncludeFromChildActors, [](UPrimitiveComponent* PrimitiveComponent)
					{
						PrimitiveComponent->RemoveOverlayColor();
						PrimitiveComponent->MarkRenderStateDirty();
					});
				}
			})
		.Where()
			.All<FTypedElementActorTag>()
		.Compile());
}
