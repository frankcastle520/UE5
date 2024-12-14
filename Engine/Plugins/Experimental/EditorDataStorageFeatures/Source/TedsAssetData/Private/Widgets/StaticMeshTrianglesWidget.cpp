// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/StaticMeshTrianglesWidget.h"

#include "Elements/Framework/TypedElementAttributeBinding.h"
#include "Internationalization/Text.h"
#include "TedsAssetDataColumns.h"
#include "UObject/ObjectMacros.h"
#include "Widgets/Text/STextBlock.h"

void UStaticMeshTrianglesWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
                                                        IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;
	DataStorageUi.RegisterWidgetFactory<FStaticMeshTrianglesWidgetConstructor>(TEXT("General.Cell"), TColumn<FItemStringAttributeColumn_Experimental>("Triangles"));
}

FStaticMeshTrianglesWidgetConstructor::FStaticMeshTrianglesWidgetConstructor()
	: FSimpleWidgetConstructor(StaticStruct())
{
}

TSharedPtr<SWidget> FStaticMeshTrianglesWidgetConstructor::CreateWidget(IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi, UE::Editor::DataStorage::RowHandle TargetRow, UE::Editor::DataStorage::RowHandle WidgetRow,
	const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	UE::Editor::DataStorage::FAttributeBinder Binder(TargetRow, DataStorage);
	
	return SNew(STextBlock)
			.Text(Binder.BindText(&FItemStringAttributeColumn_Experimental::Value, FName("Triangles")));
			
}
