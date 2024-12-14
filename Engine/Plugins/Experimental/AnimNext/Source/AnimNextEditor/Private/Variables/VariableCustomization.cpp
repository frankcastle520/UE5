// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/VariableCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraphSchema_K2.h"
#include "InstancedPropertyBagStructureDataProvider.h"
#include "UncookedOnlyUtils.h"
#include "PropertyHandle.h"
#include "Module/AnimNextModule.h" 
#include "Entries/AnimNextVariableEntry.h"
#include "Module/AnimNextModule_EditorData.h"

#define LOCTEXT_NAMESPACE "VariableCustomization"

namespace UE::AnimNext::Editor
{

void FVariableCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	if (Objects.IsEmpty())
	{
		return;
	}

	
	for(TWeakObjectPtr<UObject> WeakObject : Objects)
	{
		UAnimNextVariableEntry* Variable = Cast<UAnimNextVariableEntry>(WeakObject.Get());
		if(Variable == nullptr)
		{
			continue;
		}


		UAnimNextRigVMAsset* Asset = Variable->GetTypedOuter<UAnimNextRigVMAsset>();

		// Disable access specifier switching specifically for data interfaces
		if(UAnimNextDataInterface* DataInterface = ExactCast<UAnimNextDataInterface>(Asset))
		{
			TSharedPtr<IPropertyHandle> AccessProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UAnimNextVariableEntry, Access));
			AccessProperty->MarkHiddenByCustomization();
		}
	}

	// Don't customize default value if we have multi-selection
	if(Objects.Num() > 1)
	{
		return;
	}

	if (UAnimNextVariableEntry* Variable = Cast<UAnimNextVariableEntry>(Objects[0].Get()))
	{
		IDetailCategoryBuilder& VariablesCategory = DetailBuilder.EditCategory(TEXT("Variables"), FText::GetEmpty(), ECategoryPriority::Default);
		
		IDetailCategoryBuilder& DefaultValueCategory = DetailBuilder.EditCategory(TEXT("DefaultValue"), FText::GetEmpty(), ECategoryPriority::Default);

		TSharedRef< SWidget > ColumnWidget = SNullWidget::NullWidget;

		FAddPropertyParams AddPropertyParams;
		TArray<IDetailPropertyRow*> DetailPropertyRows;

		FInstancedPropertyBag& PropertyBag = Variable->GetMutablePropertyBag();
		const FPropertyBagPropertyDesc* PropertyDesc = PropertyBag.FindPropertyDescByName(IAnimNextRigVMVariableInterface::ValueName);
		if (PropertyDesc != nullptr)
		{
			IDetailPropertyRow* DetailPropertyRow = DefaultValueCategory.AddExternalStructureProperty(MakeShared<FInstancePropertyBagStructureDataProvider>(PropertyBag), IAnimNextRigVMVariableInterface::ValueName, EPropertyLocation::Default, AddPropertyParams);
			if (TSharedPtr<IPropertyHandle> Handle = DetailPropertyRow->GetPropertyHandle(); Handle.IsValid())
			{
				Handle->SetPropertyDisplayName(FText::FromName(Variable->GetEntryName()));

				const auto OnPropertyValueChange = [WeakVariable = TWeakObjectPtr<UAnimNextVariableEntry>(Variable)](const FPropertyChangedEvent& InEvent)
				{
					if (UAnimNextVariableEntry* PinnedVariable = WeakVariable.Get())
					{
						PinnedVariable->MarkPackageDirty();
						PinnedVariable->BroadcastModified(EAnimNextEditorDataNotifType::PropertyChanged);
					}
				};

				Handle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateLambda(OnPropertyValueChange));
				Handle->SetOnChildPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateLambda(OnPropertyValueChange));
			}
		}
	}
}

void FVariableCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) 
{
	CustomizeDetails(*DetailBuilder);
}

FText FVariableCustomization::GetName() const
{
	return FText();
}

void FVariableCustomization::SetName(const FText& InNewText, ETextCommit::Type InCommitType)
{
}

bool FVariableCustomization::OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage)
{
	return true;
}


}

#undef LOCTEXT_NAMESPACE
