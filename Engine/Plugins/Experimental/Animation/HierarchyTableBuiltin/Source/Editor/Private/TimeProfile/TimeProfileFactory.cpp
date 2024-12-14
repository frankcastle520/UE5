// Copyright Epic Games, Inc. All Rights Reserved.

#include "TimeProfile/TimeProfileFactory.h"
#include "TimeProfile/TimeProfileProxyColumn.h"
#include "TimeProfile/TimeProfileWidgetConstructor.h"

void UHierarchyTableTimeFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage, IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorageUi.RegisterWidgetFactory<FHierarchyTableTimeWidgetConstructor_StartTime>(FName(TEXT("General.Cell")), TColumn<FHierarchyTableTimeColumn_StartTime>());
	DataStorageUi.RegisterWidgetFactory<FHierarchyTableTimeWidgetConstructor_EndTime>(FName(TEXT("General.Cell")), TColumn<FHierarchyTableTimeColumn_EndTime>());
	DataStorageUi.RegisterWidgetFactory<FHierarchyTableTimeWidgetConstructor_TimeFactor>(FName(TEXT("General.Cell")), TColumn<FHierarchyTableTimeColumn_TimeFactor>());
	DataStorageUi.RegisterWidgetFactory<FHierarchyTableTimeWidgetConstructor_Preview>(FName(TEXT("General.Cell")), TColumn<FHierarchyTableTimeColumn_Preview>());
}