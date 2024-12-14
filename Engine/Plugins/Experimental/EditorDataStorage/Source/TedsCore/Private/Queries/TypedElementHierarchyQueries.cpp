// Copyright Epic Games, Inc. All Rights Reserved.

#include "Queries/TypedElementHierarchyQueries.h"

#include "Elements/Columns/TypedElementHiearchyColumns.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"

void UTypedElementHiearchyQueriesFactory::RegisterQueries(IEditorDataStorageProvider& DataStorage)
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorage.RegisterQuery(
		Select(
			TEXT("Resolve hierarchy rows"),
			FProcessor(EQueryTickPhase::PrePhysics, DataStorage.GetQueryTickGroupName(EQueryTickGroups::Default)),
			[](IQueryContext& Context, RowHandle Row, const FUnresolvedTableRowParentColumn& UnresolvedParent)
			{
				RowHandle ParentRow = Context.FindIndexedRow(UnresolvedParent.ParentIdHash);
				if (Context.IsRowAvailable(ParentRow))
				{
					Context.RemoveColumns<FUnresolvedTableRowParentColumn>(Row);
					Context.AddColumn(Row, FTableRowParentColumn{ .Parent = ParentRow });
				}
			})
		.Compile());
}
