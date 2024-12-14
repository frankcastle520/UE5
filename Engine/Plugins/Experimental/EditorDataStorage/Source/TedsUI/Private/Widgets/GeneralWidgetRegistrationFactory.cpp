// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/GeneralWidgetRegistrationFactory.h"

#include "Elements/Interfaces/TypedElementDataStorageUiInterface.h"

#define LOCTEXT_NAMESPACE "TypedElementsUI_GeneralRegistration"

const FName UGeneralWidgetRegistrationFactory::CellPurpose(TEXT("General.Cell"));
const FName UGeneralWidgetRegistrationFactory::LargeCellPurpose(TEXT("General.Cell.Large"));
const FName UGeneralWidgetRegistrationFactory::HeaderPurpose(TEXT("General.Header"));
const FName UGeneralWidgetRegistrationFactory::CellDefaultPurpose(TEXT("General.Cell.Default"));
const FName UGeneralWidgetRegistrationFactory::HeaderDefaultPurpose(TEXT("General.Header.Default"));

void UGeneralWidgetRegistrationFactory::RegisterWidgetPurposes(IEditorDataStorageUiProvider& DataStorageUi) const
{
	DataStorageUi.RegisterWidgetPurpose(CellPurpose, IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn,
		LOCTEXT("GeneralCellPurpose", "General purpose widgets that can be used as cells for specific columns or column combinations."));

	DataStorageUi.RegisterWidgetPurpose(LargeCellPurpose, IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn,
		LOCTEXT("GeneralLargeCellPurpose", "General purpose widgets that are specifically designed to be embedded in a space larger than a single cell."));

	DataStorageUi.RegisterWidgetPurpose(HeaderPurpose, IEditorDataStorageUiProvider::EPurposeType::UniqueByNameAndColumn,
		LOCTEXT("GeneralHeaderPurpose", "General purpose widget that can be used as a header."));

	DataStorageUi.RegisterWidgetPurpose(HeaderDefaultPurpose, IEditorDataStorageUiProvider::EPurposeType::UniqueByName,
		LOCTEXT("GeneralHeaderDefaultPurpose", "The default widget to use in headers if no other specialization is provided."));
	
	DataStorageUi.RegisterWidgetPurpose(CellDefaultPurpose, IEditorDataStorageUiProvider::EPurposeType::UniqueByName,
		LOCTEXT("GeneralCellDefaultPurpose", "The default widget to use in cells if no other specialization is provided."));
}

#undef LOCTEXT_NAMESPACE
