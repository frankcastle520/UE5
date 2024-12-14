// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Columns/TypedElementAlertColumns.h"
#include "Elements/Common/TypedElementHandles.h"
#include "Elements/Interfaces/TypedElementDataStorageFactory.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Memento/TypedElementMementoTranslators.h"
#include "UObject/ObjectMacros.h"

#include "TypedElementAlertQueries.generated.h"

namespace UE::Editor::DataStorage
{
	struct IQueryContext;
}

/**
 * Calls to manage alerts, in particular child alerts.
 */
UCLASS()
class UTypedElementAlertQueriesFactory final : public UEditorDataStorageFactory
{
	GENERATED_BODY()

public:
	~UTypedElementAlertQueriesFactory() override = default;

	void RegisterQueries(IEditorDataStorageProvider& DataStorage) override;

private:
	void RegisterSubQueries(IEditorDataStorageProvider& DataStorage);
	void RegisterParentUpdatesQueries(IEditorDataStorageProvider& DataStorage);
	void RegisterChildAlertUpdatesQueries(IEditorDataStorageProvider& DataStorage);
	void RegisterOnAddQueries(IEditorDataStorageProvider& DataStorage);
	void RegisterOnRemoveQueries(IEditorDataStorageProvider& DataStorage);

	static void AddChildAlertsToHierarchy(
		UE::Editor::DataStorage::IQueryContext& Context, UE::Editor::DataStorage::RowHandle Parent, int32 ParentQueryIndex);

	static void IncrementParents(
		UE::Editor::DataStorage::IQueryContext& Context, UE::Editor::DataStorage::RowHandle Row, FTypedElementAlertColumnType AlertType,
		int32 ChildAlertQueryIndex);
	
	static void ResetChildAlertCounters(FTypedElementChildAlertColumn& ChildAlert);

	static bool MoveToNextParent(
		UE::Editor::DataStorage::RowHandle& Parent, UE::Editor::DataStorage::IQueryContext& Context, int32 SubQueryIndex);

	static const FName AlertConditionName;
	UE::Editor::DataStorage::QueryHandle ChildAlertColumnReadWriteQuery;
	UE::Editor::DataStorage::QueryHandle ParentReadOnlyQuery;
};

