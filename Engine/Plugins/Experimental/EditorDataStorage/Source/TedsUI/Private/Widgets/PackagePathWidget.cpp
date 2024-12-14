// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/PackagePathWidget.h"

#include "Elements/Columns/TypedElementPackageColumns.h"
#include "Elements/Columns/TypedElementMiscColumns.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Widgets/Text/STextBlock.h"

//
// UPackagePathWidgetFactory
//

void UPackagePathWidgetFactory::RegisterWidgetConstructors(IEditorDataStorageProvider& DataStorage,
	IEditorDataStorageUiProvider& DataStorageUi) const
{
	using namespace UE::Editor::DataStorage::Queries;

	DataStorageUi.RegisterWidgetFactory<FPackagePathWidgetConstructor>(FName(TEXT("General.Cell")),
		TColumn<FTypedElementPackagePathColumn>());
	DataStorageUi.RegisterWidgetFactory<FLoadedPackagePathWidgetConstructor>(FName(TEXT("General.Cell")),
		TColumn<FTypedElementPackageLoadedPathColumn>());
}



//
// FPackagePathWidgetConstructor
//

FPackagePathWidgetConstructor::FPackagePathWidgetConstructor()
	: Super(FPackagePathWidgetConstructor::StaticStruct())
{
}

FPackagePathWidgetConstructor::FPackagePathWidgetConstructor(const UScriptStruct* InTypeInfo)
	: Super(FPackagePathWidgetConstructor::StaticStruct())
{
}

TSharedPtr<SWidget> FPackagePathWidgetConstructor::CreateWidget(const UE::Editor::DataStorage::FMetaDataView& Arguments)
{
	return SNew(STextBlock)
		.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		.Justification(ETextJustify::Right);
}

bool FPackagePathWidgetConstructor::FinalizeWidget(
	IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi,
	UE::Editor::DataStorage::RowHandle Row,
	const TSharedPtr<SWidget>& Widget)
{
	UE::Editor::DataStorage::RowHandle TargetRow = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row)->Row;
	if (const FTypedElementPackagePathColumn* Path = DataStorage->GetColumn<FTypedElementPackagePathColumn>(TargetRow))
	{
		STextBlock* TextWidget = static_cast<STextBlock*>(Widget.Get());
		FText Text = FText::FromString(Path->Path);
		TextWidget->SetToolTipText(Text);
		TextWidget->SetText(MoveTemp(Text));
		return true;
	}
	else
	{
		return false;
	}
}



//
// FLoadedPackagePathWidgetConstructor
//

FLoadedPackagePathWidgetConstructor::FLoadedPackagePathWidgetConstructor()
	: Super(FLoadedPackagePathWidgetConstructor::StaticStruct())
{
}

bool FLoadedPackagePathWidgetConstructor::FinalizeWidget(
	IEditorDataStorageProvider* DataStorage,
	IEditorDataStorageUiProvider* DataStorageUi,
	UE::Editor::DataStorage::RowHandle Row,
	const TSharedPtr<SWidget>& Widget)
{
	UE::Editor::DataStorage::RowHandle TargetRow = DataStorage->GetColumn<FTypedElementRowReferenceColumn>(Row)->Row;
	if (const FTypedElementPackageLoadedPathColumn* Path = DataStorage->GetColumn<FTypedElementPackageLoadedPathColumn>(TargetRow))
	{
		STextBlock* TextWidget = static_cast<STextBlock*>(Widget.Get());
		FText Text = FText::FromString(Path->LoadedPath.GetLocalFullPath());
		TextWidget->SetToolTipText(Text);
		TextWidget->SetText(MoveTemp(Text));
		return true;
	}
	else
	{
		return false;
	}
}
