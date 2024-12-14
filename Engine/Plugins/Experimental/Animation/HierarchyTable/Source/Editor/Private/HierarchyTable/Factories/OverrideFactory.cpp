// Copyright Epic Games, Inc. All Rights Reserved.

#include "HierarchyTable/Factories/OverrideFactory.h"
#include "HierarchyTable/Columns/OverrideColumn.h"
#include "HierarchyTable/Widgets/OverrideWidgetConstructor.h"
#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

void UHierarchyTableOverrideFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage, IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorageUi.RegisterWidgetFactory<FTypedElementWidgetConstructor_Override>(FName(TEXT("General.Cell")), 
		TColumn<FTypedElementOverrideColumn>());

	DataStorageUi.RegisterWidgetFactory<FTypedElementWidgetHeaderConstructor_Override>(FName(TEXT("General.Header")),
		TColumn<FTypedElementOverrideColumn>());
}