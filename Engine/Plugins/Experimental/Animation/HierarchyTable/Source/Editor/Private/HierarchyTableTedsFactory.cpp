// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTableTedsFactory.h"

#include "Elements/Columns/TypedElementLabelColumns.h"
#include "Elements/Common/TypedElementDataStorageLog.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#include "Elements/Framework/TypedElementRegistry.h"

void UTypedElementHierarchyTableFactory::RegisterTables(IEditorDataStorageProvider& DataStorage)
{
	DataStorage.RegisterTable(
		TTypedElementColumnTypeList<
		FTypedElementLabelColumn>{},
		TEXT("Editor_HierarchyTableTable"));
}

void UTypedElementHierarchyTableFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
}