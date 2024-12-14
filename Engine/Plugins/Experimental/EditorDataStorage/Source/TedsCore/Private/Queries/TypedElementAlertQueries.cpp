// Copyright Epic Games, Inc. All Rights Reserved.

#include "Queries/TypedElementAlertQueries.h"

#include "Elements/Columns/TypedElementAlertColumns.h"
#include "Elements/Columns/TypedElementHiearchyColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Columns/TypedElementSelectionColumns.h"
#include "Elements/Common/EditorDataStorageFeatures.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "HAL/IConsoleManager.h"
#include "Math/UnrealMathUtility.h"

FAutoConsoleCommand AddRandomAlertToRowConsoleCommand(
	TEXT("TEDS.Debug.AddRandomAlertToSelectedRows"),
	TEXT("Add random alert to all selected rows that don't have one yet."),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			using namespace UE::Editor::DataStorage::Queries;

			TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.Debug.AddRandomAlertToSelectedRows);

			if (IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
			{
				static QueryHandle Query = [DataStorage]
				{
					return DataStorage->RegisterQuery(
						Select()
						.Where()
							.All<FTypedElementSelectionColumn>()
							.None<FTypedElementAlertColumn>()
						.Compile());
				}();

				TArray<RowHandle> Rows;
				DataStorage->RunQuery(Query, CreateDirectQueryCallbackBinding(
					[&Rows](IDirectQueryContext& Context, RowHandle Row)
					{
						Rows.Add(Row);
					}));
				for (RowHandle Row : Rows)
				{
					int32 Random = FMath::RandRange(0, 2);
					bool bIsWarning = (Random & 0x1) == 1;
					DataStorage->AddColumn(Row, FTypedElementAlertColumn
						{
							.Message = FText::FromString(bIsWarning ? TEXT("Test warning") : TEXT("Test error")),
							.AlertType = bIsWarning
								? FTypedElementAlertColumnType::Warning
								: FTypedElementAlertColumnType::Error
						});
					DataStorage->AddColumns<FTypedElementSyncBackToWorldTag>(Row);
					
					if (((Random >> 1) & 0x1) == 1)
					{
						DataStorage->AddColumn(Row, FTypedElementAlertActionColumn
							{
								.Action = [](RowHandle)
								{
									FPlatformMisc::MessageBoxExt(
										EAppMsgType::Ok, TEXT("Example of an alert action."),
										TEXT("TEDS.Debug.AddRandomAlertToSelectedRows"));
								}
							});
					}
				}
			}
		}
));

FAutoConsoleCommand ClearAllAlertsConsoleCommand(
	TEXT("TEDS.Debug.ClearAllAlertInfo"),
	TEXT("Removes all alerts and child alerts."),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			using namespace UE::Editor::DataStorage::Queries;

			TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.Debug.ClearAllAlertInfo);

			static TTypedElementColumnTypeList<FTypedElementSyncBackToWorldTag> BatchAddColumns;
			static TTypedElementColumnTypeList<FTypedElementAlertColumn, FTypedElementChildAlertColumn> BatchRemoveColumns;

			if (IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
			{
				static QueryHandle AlertInfoQuery = [DataStorage]
				{
					return DataStorage->RegisterQuery(
						Select()
						.Where()
							.Any<FTypedElementAlertColumn, FTypedElementChildAlertColumn>()
						.Compile());
				}();
				TArray<RowHandle> Rows;
				DataStorage->RunQuery(AlertInfoQuery, CreateDirectQueryCallbackBinding(
					[&Rows](IDirectQueryContext& Context, RowHandle Row)
					{
						Rows.Add(Row);
					}));
				for (RowHandle Row : Rows)
				{
					DataStorage->RemoveColumn<FTypedElementAlertColumn>(Row);
					DataStorage->RemoveColumn<FTypedElementChildAlertColumn>(Row);
					DataStorage->AddColumn<FTypedElementSyncBackToWorldTag>(Row);
				}
			}
		}
));

FAutoConsoleCommand ClearSelectedAlertsConsoleCommand(
	TEXT("TEDS.Debug.ClearSelectedAlerts"),
	TEXT("Removes all selected alerts."),
	FConsoleCommandDelegate::CreateLambda(
		[]()
		{
			using namespace UE::Editor::DataStorage::Queries;

			TRACE_CPUPROFILER_EVENT_SCOPE(TEDS.Debug.ClearSelectedAlerts);

			if (IEditorDataStorageProvider* DataStorage = GetMutableDataStorageFeature<IEditorDataStorageProvider>(StorageFeatureName))
			{
				static QueryHandle AlertQuery = [DataStorage]
				{
					return DataStorage->RegisterQuery(
						Select()
						.Where()
							.All<FTypedElementAlertColumn, FTypedElementSelectionColumn>()
						.Compile());
				}();
				TArray<RowHandle> Rows;
				DataStorage->RunQuery(AlertQuery, CreateDirectQueryCallbackBinding(
					[&Rows](IDirectQueryContext& Context, RowHandle Row)
					{
						Rows.Add(Row);
					}));
				for (RowHandle Row : Rows)
				{
					DataStorage->RemoveColumn<FTypedElementAlertColumn>(Row);
					DataStorage->AddColumn<FTypedElementSyncBackToWorldTag>(Row);
				}
			}
		}
));

const FName UTypedElementAlertQueriesFactory::AlertConditionName = FName(TEXT("Alerts"));

void UTypedElementAlertQueriesFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	RegisterSubQueries(DataStorage);
	RegisterParentUpdatesQueries(DataStorage);
	RegisterChildAlertUpdatesQueries(DataStorage);
	RegisterOnAddQueries(DataStorage);
	RegisterOnRemoveQueries(DataStorage);
}

void UTypedElementAlertQueriesFactory::RegisterSubQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	ChildAlertColumnReadWriteQuery = DataStorage.RegisterQuery(
		Select()
			.ReadWrite<FTypedElementChildAlertColumn>()
		.Compile());

	ParentReadOnlyQuery = DataStorage.RegisterQuery(
		Select()
			.ReadOnly<FTableRowParentColumn>()
		.Compile());
}

void UTypedElementAlertQueriesFactory::RegisterParentUpdatesQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Trigger alert update if alert's parent changed"),
			FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Default)),
			[](IQueryContext& Context, FTypedElementAlertColumn& Alert, const FTableRowParentColumn& Parent)
			{
				if (Alert.CachedParent != Parent.Parent)
				{
					Alert.CachedParent = Parent.Parent;
					Context.ActivateQueries(AlertConditionName);
				}
			})
		.Where()
			.Any<FTypedElementSyncBackToWorldTag, FTypedElementSyncFromWorldTag>()
		.Compile());

	DataStorage.RegisterQuery(
		Select(
			TEXT("Trigger alert update if child alert's parent changed"),
			FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Default)),
			[](IQueryContext& Context, FTypedElementChildAlertColumn& ChildAlert, const FTableRowParentColumn& Parent)
			{
				if (ChildAlert.CachedParent != Parent.Parent)
				{
					ChildAlert.CachedParent = Parent.Parent;
					Context.ActivateQueries(AlertConditionName);
				}
			})
		.Where()
			.Any<FTypedElementSyncBackToWorldTag, FTypedElementSyncFromWorldTag>()
		.Compile());
}

void UTypedElementAlertQueriesFactory::RegisterChildAlertUpdatesQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Add missing child alerts"),
			FPhaseAmble(FPhaseAmble::ELocation::Preamble, EQueryTickPhase::FrameEnd)
				.MakeActivatable(AlertConditionName),
			[](IQueryContext& Context, RowHandle Row, FTypedElementAlertColumn& Alert, const FTableRowParentColumn& Parent)
			{
				AddChildAlertsToHierarchy(Context, Parent.Parent, 0);
			})
		.DependsOn()
			.SubQuery(ParentReadOnlyQuery)
		.Compile());

	DataStorage.RegisterQuery(
		Select(
			TEXT("Clear child alerts"),
			FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::PreUpdate))
				.MakeActivatable(AlertConditionName),
			[](IQueryContext& Context, RowHandle Row, FTypedElementChildAlertColumn& ChildAlert)
			{
				ResetChildAlertCounters(ChildAlert);
			})
		.Compile());

	DataStorage.RegisterQuery(
		Select(
			TEXT("Increment child alerts"),
			FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Update))
				.MakeActivatable(AlertConditionName),
			[](IQueryContext& Context, RowHandle Row, FTypedElementAlertColumn& Alert)
			{
				IncrementParents(Context, Alert.CachedParent, Alert.AlertType, 0);
			})
		.DependsOn()
			.SubQuery(ChildAlertColumnReadWriteQuery)
		.Compile());

	DataStorage.RegisterQuery(
		Select(
			TEXT("Remove unused child alerts"),
			FProcessor(EQueryTickPhase::FrameEnd, DataStorage.GetQueryTickGroupName(EQueryTickGroups::PostUpdate))
				.MakeActivatable(AlertConditionName),
			[](IQueryContext& Context, RowHandle Row, FTypedElementChildAlertColumn& ChildAlert)
			{
				for (size_t It = 0; It < static_cast<size_t>(FTypedElementAlertColumnType::MAX); ++It)
				{
					if (ChildAlert.Counts[It] != 0)
					{
						return;
					}
				}
				Context.RemoveColumns<FTypedElementChildAlertColumn>(Row);
			})
		.Compile());
}

void UTypedElementAlertQueriesFactory::RegisterOnAddQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Register alert with parent on alert add"),
			FObserver::OnAdd<FTypedElementAlertColumn>(),
			[](IQueryContext& Context, RowHandle Row)
			{
				Context.ActivateQueries(AlertConditionName);
			})
		.Where()
			.All<FTableRowParentColumn>() // Only need to do an update pass if there are parents.
		.Compile());

	DataStorage.RegisterQuery(
		Select(
			TEXT("Register alert with parent on parent add"),
			FObserver::OnAdd<FTableRowParentColumn>(),
			[](IQueryContext& Context, RowHandle Row)
			{
				Context.ActivateQueries(AlertConditionName);
			})
		.Where()
			.Any<FTypedElementAlertColumn, FTypedElementChildAlertColumn>()
		.Compile());
}

void UTypedElementAlertQueriesFactory::RegisterOnRemoveQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;
	using namespace UE::Editor::DataStorage;
	
	DataStorage.RegisterQuery(
		Select(
			TEXT("Remove alert"),
			FObserver::OnRemove<FTypedElementAlertColumn>(),
			[](IQueryContext& Context, RowHandle Row, FTypedElementAlertColumn& Alert)
			{
				Context.ActivateQueries(AlertConditionName);
			})
		.Compile());

	DataStorage.RegisterQuery(
		Select(
			TEXT("Update alert upon parent removal"),
			FObserver::OnRemove<FTableRowParentColumn>(),
			[](IQueryContext& Context, RowHandle Row)
			{
				Context.ActivateQueries(AlertConditionName);
			})
		.Where()
			.Any<FTypedElementAlertColumn, FTypedElementChildAlertColumn>()
		.Compile());
}

void UTypedElementAlertQueriesFactory::AddChildAlertsToHierarchy(
	UE::Editor::DataStorage::IQueryContext& Context, UE::Editor::DataStorage::RowHandle Parent, int32 ParentQueryIndex)
{
	using namespace UE::Editor::DataStorage;

	bool bHasParent = true;
	do
	{
		RowHandle NextParent = Parent;
		bHasParent = MoveToNextParent(NextParent, Context, ParentQueryIndex);

		// Increment parent's child counter or create a child alert if one doesn't exist.
		if (!Context.HasColumn<FTypedElementChildAlertColumn>(Parent))
		{
			FTypedElementChildAlertColumn ChildAlert;
			ResetChildAlertCounters(ChildAlert);
			ChildAlert.CachedParent = NextParent;
			Context.AddColumn(Parent, MoveTemp(ChildAlert));
		}
		Parent = NextParent;
	} while (bHasParent);
}

void UTypedElementAlertQueriesFactory::IncrementParents(
	UE::Editor::DataStorage::IQueryContext& Context, UE::Editor::DataStorage::RowHandle Row, FTypedElementAlertColumnType AlertType,
	int32 ChildAlertQueryIndex)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	while (Context.IsRowAvailable(Row))
	{
		FQueryResult Result = Context.RunSubquery(ChildAlertQueryIndex, Row, CreateSubqueryCallbackBinding(
			[&ParentRow = Row, AlertType](
				ISubqueryContext& Context, RowHandle Row, FTypedElementChildAlertColumn& ChildAlert)
			{
				ChildAlert.Counts[static_cast<size_t>(AlertType)]++;
				Context.AddColumns<FTypedElementSyncBackToWorldTag>(Row);
				ParentRow = ChildAlert.CachedParent;
			}));
		checkf(Result.Count > 0, TEXT("Expected to be able to setup the child alert, but it was missing on the parent column."));
	}
}

void UTypedElementAlertQueriesFactory::ResetChildAlertCounters(FTypedElementChildAlertColumn& ChildAlert)
{
	for (size_t It = 0; It < static_cast<size_t>(FTypedElementAlertColumnType::MAX); ++It)
	{
		ChildAlert.Counts[It] = 0;
	}
}

bool UTypedElementAlertQueriesFactory::MoveToNextParent(
	UE::Editor::DataStorage::RowHandle& Parent, UE::Editor::DataStorage::IQueryContext& Context, int32 SubQueryIndex)
{
	using namespace UE::Editor::DataStorage::Queries;
	
	FQueryResult Result = Context.RunSubquery(SubQueryIndex, Parent, CreateSubqueryCallbackBinding(
		[&Parent](const FTableRowParentColumn& NextParent)
		{
			Parent = NextParent.Parent;
		}));
	Parent = Result.Count != 0 ? Parent : InvalidRowHandle;
	return Result.Count != 0;
}
