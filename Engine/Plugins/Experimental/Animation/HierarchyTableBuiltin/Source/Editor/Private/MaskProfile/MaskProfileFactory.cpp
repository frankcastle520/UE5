// Copyright Epic Games, Inc. All Rights Reserved.

#include "MaskProfile/MaskProfileFactory.h"
#include "MaskProfile/MaskProfileProxyColumn.h"
#include "MaskProfile/MaskProfileWidgetConstructor.h"

void UHierarchyTableMaskFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage, IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorageUi.RegisterWidgetFactory<FHierarchyTableMaskWidgetConstructor_Value>(FName(TEXT("General.Cell")), TColumn<FHierarchyTableMaskColumn_Value>());
}