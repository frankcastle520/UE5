// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variables/VariableProxyCustomization.h"

#include "AnimNextRigVMAssetEditorData.h"
#include "AnimNextVariableEntryProxy.h"
#include "DetailBuilderTypes.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "InstancedPropertyBagStructureDataProvider.h"
#include "SVariableOverride.h"
#include "Entries/AnimNextDataInterfaceEntry.h"
#include "Entries/AnimNextVariableEntry.h"
#include "Modules/ModuleManager.h"
#include "StructUtils/PropertyBag.h"

#define LOCTEXT_NAMESPACE "VariableProxyCustomization"

namespace UE::AnimNext::Editor
{

void FVariableProxyCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	// Don't customize default value if we have multi-selection
	if(Objects.Num() != 1)
	{
		return;
	}

	if (UAnimNextVariableEntryProxy* VariableProxy = CastChecked<UAnimNextVariableEntryProxy>(Objects[0].Get()))
	{
		IDetailCategoryBuilder& DefaultValueCategory = DetailBuilder.EditCategory(TEXT("DefaultValue"), LOCTEXT("DefaultValue", "Default Value"), ECategoryPriority::Default);

		UAnimNextVariableEntry* VariableEntry = VariableProxy->VariableEntry;
		UAnimNextDataInterfaceEntry* DataInterfaceEntry = VariableProxy->DataInterfaceEntry;

		UAnimNextRigVMAssetEditorData* VariableEditorData = VariableEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
		if(VariableEditorData)
		{
			VariableEditorData->ModifiedDelegate.AddSP(this, &FVariableProxyCustomization::HandleModified);
		}

		UAnimNextRigVMAssetEditorData* DataInterfaceEditorData = DataInterfaceEntry->GetTypedOuter<UAnimNextRigVMAssetEditorData>();
		if(DataInterfaceEditorData)
		{
			DataInterfaceEditorData->ModifiedDelegate.AddSP(this, &FVariableProxyCustomization::HandleModified);
		}

		const FName VariableName = VariableEntry->GetEntryName();
		FName ValueName;
		FInstancedPropertyBag* PropertyBag = nullptr;
		EAnimNextDataInterfaceValueOverrideStatus OverrideStatus = DataInterfaceEntry->FindValueOverridePropertyBagRecursive(VariableName, PropertyBag);
		switch(OverrideStatus)
		{
		case EAnimNextDataInterfaceValueOverrideStatus::NotOverridden:
			{
				// Use the internal property bag (copying from the source) and the default name
				InternalPropertyBag = VariableEntry->GetPropertyBag();
				PropertyBag = &InternalPropertyBag;
				ValueName = IAnimNextRigVMVariableInterface::ValueName;
				break;
			}
		case EAnimNextDataInterfaceValueOverrideStatus::OverriddenInThisAsset:
			{
				// Use the found property bag and the variable name
				check(PropertyBag);
				ValueName = VariableName;
				break;
			}
		case EAnimNextDataInterfaceValueOverrideStatus::OverriddenInParentAsset:
			{
				// Use the internal property bag (copying just the value from the overriding asset's property bag) and the default name
				check(PropertyBag);
				const FPropertyBagPropertyDesc* Desc = PropertyBag->FindPropertyDescByName(VariableName);
				check(Desc);
				const uint8* DataPtr = Desc->CachedProperty->ContainerPtrToValuePtr<uint8>(PropertyBag->GetValue().GetMemory());
				TConstArrayView<uint8> Value(DataPtr, Desc->CachedProperty->GetElementSize());
				InternalPropertyBag.ReplaceAllPropertiesAndValues(TConstArrayView<FPropertyBagPropertyDesc>(Desc, 1), TConstArrayView<TConstArrayView<uint8>>(&Value, 1));
				PropertyBag = &InternalPropertyBag;
				ValueName = VariableName;
				break;
			}
		}

		IDetailPropertyRow* DetailPropertyRow = DefaultValueCategory.AddExternalStructureProperty(MakeShared<FInstancePropertyBagStructureDataProvider>(*PropertyBag), ValueName);
		PropertyHandle = DetailPropertyRow->GetPropertyHandle();
		
		if (!PropertyHandle.IsValid())
		{
			return;
		}

		// Hide reset to default & replace with our own
		DetailPropertyRow->OverrideResetToDefault(FResetToDefaultOverride::Create(TAttribute<bool>(false)));
		PropertyHandle->SetPropertyDisplayName(FText::FromName(VariableEntry->GetEntryName()));
		TSharedPtr<SWidget> NameWidget, ValueWidget;
		DetailPropertyRow->GetDefaultWidgets(NameWidget, ValueWidget);
		DetailPropertyRow->CustomWidget(true)
		.NameContent()
		[
			NameWidget.IsValid() ? NameWidget.ToSharedRef() : SNullWidget::NullWidget
		]
		.ValueContent()
		[
			ValueWidget.IsValid() ? ValueWidget.ToSharedRef() : SNullWidget::NullWidget
		]
		.ResetToDefaultContent()
		[
			SNew(SVariableOverride, DataInterfaceEntry, VariableName)
			.OverrideStatus(OverrideStatus)
		];

		

		const auto OnPropertyValueChange = [this, WeakVariableEntry = TWeakObjectPtr<UAnimNextVariableEntry>(VariableEntry), WeakDataInterfaceEntry = TWeakObjectPtr<UAnimNextDataInterfaceEntry>(DataInterfaceEntry)](const FPropertyChangedEvent& InEvent)
		{
			UAnimNextVariableEntry* PinnedVariableEntry = WeakVariableEntry.Get();
			UAnimNextDataInterfaceEntry* PinnedDataInterfaceEntry = WeakDataInterfaceEntry.Get();
			if (PinnedVariableEntry == nullptr || PinnedDataInterfaceEntry == nullptr)
			{
				return;
			}

			const FName VariableName = PinnedVariableEntry->GetVariableName();
			if(!PinnedDataInterfaceEntry->HasValueOverride(VariableName))
			{
				// No value override yet, so we copy from the internal property bag
				check(InternalPropertyBag.IsValid() && InternalPropertyBag.GetPropertyBagStruct()->GetPropertyDescs().Num() == 1);
				const FProperty* Property = InternalPropertyBag.GetPropertyBagStruct()->GetPropertyDescs()[0].CachedProperty; 
				const uint8* DataPtr = Property->ContainerPtrToValuePtr<uint8>(InternalPropertyBag.GetValue().GetMemory());
				PinnedDataInterfaceEntry->SetValueOverride(VariableName, PinnedVariableEntry->GetType(), TConstArrayView<uint8>(DataPtr, Property->GetElementSize()));
				ensure(PinnedDataInterfaceEntry->GetValueOverrideStatusRecursive(VariableName) == EAnimNextDataInterfaceValueOverrideStatus::OverriddenInThisAsset);
			}

			PinnedDataInterfaceEntry->MarkPackageDirty();
			PinnedDataInterfaceEntry->BroadcastModified(EAnimNextEditorDataNotifType::VariableDefaultValueChanged);
		};

		PropertyHandle->SetOnPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateLambda(OnPropertyValueChange));
		PropertyHandle->SetOnChildPropertyValueChangedWithData(TDelegate<void(const FPropertyChangedEvent&)>::CreateLambda(OnPropertyValueChange));

	}
}

void FVariableProxyCustomization::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) 
{
	WeakDetailBuilder = DetailBuilder;
	CustomizeDetails(*DetailBuilder);
}

void FVariableProxyCustomization::HandleModified(UAnimNextRigVMAssetEditorData* InEditorData, EAnimNextEditorDataNotifType InType, UObject* InSubject)
{
	if( InType != EAnimNextEditorDataNotifType::VariableDefaultValueChanged &&
		InType != EAnimNextEditorDataNotifType::UndoRedo)
	{
		return;
	}

	if(TSharedPtr<IDetailLayoutBuilder> DetailBuilder = WeakDetailBuilder.Pin())
	{
		// Rebuild this customization to point at the correct data
		DetailBuilder->ForceRefreshDetails();
	}
}

}

#undef LOCTEXT_NAMESPACE
