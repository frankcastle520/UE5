// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDSelectionMultipleViewCustomization.h"

#include "ChaosVDModule.h"
#include "ChaosVDSolverDataSelection.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "PropertyHandle.h"


TSharedRef<IDetailCustomization> FChaosVDSelectionMultipleViewCustomization::MakeInstance()
{
	return MakeShareable( new FChaosVDSelectionMultipleViewCustomization );
}

void FChaosVDSelectionMultipleViewCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TSharedPtr<FStructOnScope>> StructsBeingCustomized;
	DetailBuilder.GetStructsBeingCustomized(StructsBeingCustomized);

	if (!ensure(StructsBeingCustomized.Num() == 1))
	{
		UE_LOG(LogChaosVDEditor, Warning, TEXT("[%s] [%d] objects were selectioned but this customization panel only support single object selection."), ANSI_TO_TCHAR(__FUNCTION__), StructsBeingCustomized.Num())
	}

	if (StructsBeingCustomized.IsEmpty())
	{
		return;
	}
	TSharedPtr<FStructOnScope> SelectionViewStructOnScope = StructsBeingCustomized.IsEmpty() ? nullptr : StructsBeingCustomized[0];

	if (!SelectionViewStructOnScope)
	{
		return;
	}

	FChaosVDSelectionMultipleView* StructView = reinterpret_cast<FChaosVDSelectionMultipleView*>(SelectionViewStructOnScope->GetStructMemory());
	
	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory("Recorded Data");
	
	for (const TSharedPtr<FStructOnScope>& DataInstance : StructView->DataInstances)
	{
		if (!DataInstance || !DataInstance->IsValid())
		{
			continue;
		}

		if (IDetailPropertyRow* CreatedRow = CategoryBuilder.AddExternalStructure(DataInstance))
		{
			CreatedRow->DisplayName(DataInstance->GetStructPtr()->GetDisplayNameText());
			CreatedRow->ShouldAutoExpand(true);
		}
	}
}
