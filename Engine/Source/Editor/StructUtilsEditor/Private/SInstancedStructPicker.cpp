// Copyright Epic Games, Inc. All Rights Reserved.

#include "SInstancedStructPicker.h"
#include "DetailLayoutBuilder.h"
#include "Editor.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "Modules/ModuleManager.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/UserDefinedStruct.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "StructUtilsEditor"

namespace UE::StructUtils::Private
{
	FPropertyAccess::Result GetCommonScriptStruct(const TSharedPtr<IPropertyHandle>& StructProperty,
		const UScriptStruct*& OutCommonStruct)
	{
		bool bHasResult = false;
		bool bHasMultipleValues = false;

		StructProperty->EnumerateConstRawData([&OutCommonStruct, &bHasResult, &bHasMultipleValues](const void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (const FInstancedStruct* InstancedStruct = static_cast<const FInstancedStruct*>(RawData))
			{
				const UScriptStruct* Struct = InstancedStruct->GetScriptStruct();

				if (!bHasResult)
				{
					OutCommonStruct = Struct;
				}
				else if (OutCommonStruct != Struct)
				{
					bHasMultipleValues = true;
				}

				bHasResult = true;
			}

			return true;
		});

		if (bHasMultipleValues)
		{
			return FPropertyAccess::MultipleValues;
		}

		return bHasResult ? FPropertyAccess::Success : FPropertyAccess::Fail;
	}
} // UE::StructUtils::Private

////////////////////////////////////

bool FInstancedStructFilter::IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs)
{
	bool bStructAllowed = true;
	if (!AllowedStructs.IsEmpty())
	{
		bStructAllowed = false;
		for (const TSoftObjectPtr<const UScriptStruct>& AllowedStruct : AllowedStructs)
		{
			if (InStruct->IsChildOf(AllowedStruct.Get()))
			{
				bStructAllowed = true;
				break;
			}
		}
	}
	if (!DisallowedStructs.IsEmpty())
	{
		for (const TSoftObjectPtr<const UScriptStruct>& DisallowedStruct : DisallowedStructs)
		{
			if (InStruct->IsChildOf(DisallowedStruct.Get()))
			{
				bStructAllowed = false;
				break;
			}
		}
	}

	if (!bStructAllowed)
	{
		return false;
	}
	
	if (InStruct->IsA<UUserDefinedStruct>())
	{
		return bAllowUserDefinedStructs;
	}

	if (InStruct == BaseStruct)
	{
		return bAllowBaseStruct;
	}

	static const FName NAME_HiddenMetaTag = "Hidden";
	if (InStruct->HasMetaData(NAME_HiddenMetaTag))
	{
		return false;
	}

	if (AssetReferenceFilter.IsValid())
	{
		if (!AssetReferenceFilter->PassesFilter(FAssetData(InStruct)))
		{
			return false;
		}
	}

	// Query the native struct to see if it has the correct parent type (if any)
	const UScriptStruct* Struct = BaseStruct.Get();
	return !Struct || InStruct->IsChildOf(Struct);
}

bool FInstancedStructFilter::IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FSoftObjectPath& InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs)
{
	// User Defined Structs don't support inheritance, so only include them requested
	return bAllowUserDefinedStructs;
}

////////////////////////////////////

void SInstancedStructPicker::Construct(const FArguments& InArgs, TSharedPtr<IPropertyHandle> InStructProperty,
	TSharedPtr<IPropertyUtilities> InPropertyUtils)
{
	static const FName NAME_BaseStruct = "BaseStruct";
	static const FName NAME_StructTypeConst = "StructTypeConst";

	OnStructPicked = InArgs._OnStructPicked;
	StructProperty = MoveTemp(InStructProperty);
	PropUtils = MoveTemp(InPropertyUtils);

	if (!StructProperty.IsValid() || !PropUtils.IsValid())
	{
		return;
	}

	BaseScriptStruct = nullptr;
	{
		const FString& BaseStructName = StructProperty->GetMetaData(NAME_BaseStruct);
		if (!BaseStructName.IsEmpty())
		{
			if (UScriptStruct* Struct = UClass::TryFindTypeSlow<UScriptStruct>(BaseStructName))
			{
				BaseScriptStruct = Struct;
			}
			else
			{
				BaseScriptStruct = LoadObject<UScriptStruct>(nullptr, *BaseStructName);
			}
		}
	}

	ChildSlot
	[
		SAssignNew(ComboButton, SComboButton)
		.OnGetMenuContent(this, &SInstancedStructPicker::GenerateStructPicker)
		.ContentPadding(0)
		.IsEnabled(StructProperty->IsEditable() && !StructProperty->HasMetaData(NAME_StructTypeConst))
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
				.Image(this, &SInstancedStructPicker::GetDisplayValueIcon)
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SInstancedStructPicker::GetDisplayValueString)
				.ToolTipText(this, &SInstancedStructPicker::GetTooltipText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]
	];
}

FText SInstancedStructPicker::GetDisplayValueString() const
{
	const UScriptStruct* CommonStruct = nullptr;
	const FPropertyAccess::Result Result = UE::StructUtils::Private::GetCommonScriptStruct(StructProperty, CommonStruct);

	if (Result == FPropertyAccess::Success)
	{
		if (CommonStruct)
		{
			return CommonStruct->GetDisplayNameText();
		}
		return LOCTEXT("NullScriptStruct", "None");
	}
	if (Result == FPropertyAccess::MultipleValues)
	{
		return LOCTEXT("MultipleValues", "Multiple Values");
	}

	return FText::GetEmpty();
}

FText SInstancedStructPicker::GetTooltipText() const
{
	const UScriptStruct* CommonStruct = nullptr;
	const FPropertyAccess::Result Result = UE::StructUtils::Private::GetCommonScriptStruct(StructProperty, CommonStruct);

	if (CommonStruct && Result == FPropertyAccess::Success)
	{
		return CommonStruct->GetToolTipText();
	}

	return GetDisplayValueString();
}

const FSlateBrush* SInstancedStructPicker::GetDisplayValueIcon() const
{
	const UScriptStruct* CommonStruct = nullptr;
	if (UE::StructUtils::Private::GetCommonScriptStruct(StructProperty, CommonStruct) == FPropertyAccess::Success)
	{
		return FSlateIconFinder::FindIconBrushForClass(UScriptStruct::StaticClass());
	}

	return nullptr;
}

TSharedRef<SWidget> SInstancedStructPicker::GenerateStructPicker()
{
	static const FName NAME_ExcludeBaseStruct = "ExcludeBaseStruct";
	static const FName NAME_HideViewOptions = "HideViewOptions";
	static const FName NAME_ShowTreeView = "ShowTreeView";

	const bool bExcludeBaseStruct = StructProperty->HasMetaData(NAME_ExcludeBaseStruct);
	const bool bAllowNone = !(StructProperty->GetMetaDataProperty()->PropertyFlags & CPF_NoClear);
	const bool bHideViewOptions = StructProperty->HasMetaData(NAME_HideViewOptions);
	const bool bShowTreeView = StructProperty->HasMetaData(NAME_ShowTreeView);

	TSharedRef<FInstancedStructFilter> StructFilter = MakeShared<FInstancedStructFilter>();
	StructFilter->BaseStruct = BaseScriptStruct;
	StructFilter->bAllowUserDefinedStructs = BaseScriptStruct.IsExplicitlyNull(); // Only allow user defined structs when BaseStruct is not set.
	StructFilter->bAllowBaseStruct = !bExcludeBaseStruct;

	if (GEditor && StructProperty)
	{
		FAssetReferenceFilterContext AssetReferenceFilterContext;

		TArray<UPackage*> OuterPackages;
		StructProperty->GetOuterPackages(OuterPackages);
		for (UPackage* OuterPackage : OuterPackages)
		{
			AssetReferenceFilterContext.AddReferencingAsset(FAssetData(OuterPackage));
		}

		StructFilter->AssetReferenceFilter = GEditor->MakeAssetReferenceFilter(AssetReferenceFilterContext);

		auto SoftPointerTransform = [](const UScriptStruct* InStruct) -> TSoftObjectPtr<const UScriptStruct>
		{
			return InStruct;
		};
		
		Algo::Transform(PropertyCustomizationHelpers::GetStructsFromMetadataString(StructProperty->GetMetaData("AllowedClasses")), StructFilter->AllowedStructs, SoftPointerTransform);
		Algo::Transform(PropertyCustomizationHelpers::GetStructsFromMetadataString(StructProperty->GetMetaData("DisallowedClasses")), StructFilter->DisallowedStructs, SoftPointerTransform);

		TArray<UObject*> OwningObjects;
		StructProperty->GetOuterObjects(OwningObjects);
		for (UObject* OwningObject : OwningObjects)
		{
			if (OwningObject != nullptr)
			{
				const FString GetAllowedClassesFunctionName = StructProperty->GetMetaData("GetAllowedClasses");
				if (!GetAllowedClassesFunctionName.IsEmpty())
				{
					const UFunction* GetAllowedClassesFunction = OwningObject ? OwningObject->FindFunction(*GetAllowedClassesFunctionName) : nullptr;
					if (GetAllowedClassesFunction != nullptr)
					{
						DECLARE_DELEGATE_RetVal(TArray<TSoftObjectPtr<UScriptStruct>>, FGetAllowedClasses);
						StructFilter->AllowedStructs.Append(FGetAllowedClasses::CreateUFunction(OwningObject, GetAllowedClassesFunction->GetFName()).Execute());
					}
				}
		
				const FString GetDisallowedClassesFunctionName = StructProperty->GetMetaData("GetDisallowedClasses");
				if (!GetDisallowedClassesFunctionName.IsEmpty())
				{
					const UFunction* GetDisallowedClassesFunction = OwningObject ? OwningObject->FindFunction(*GetDisallowedClassesFunctionName) : nullptr;
					if (GetDisallowedClassesFunction != nullptr)
					{
						DECLARE_DELEGATE_RetVal(TArray<TSoftObjectPtr<UScriptStruct>>, FGetDisallowedClasses);
						StructFilter->DisallowedStructs.Append(FGetDisallowedClasses::CreateUFunction(OwningObject, GetDisallowedClassesFunction->GetFName()).Execute());
					}
				}
			}
		}
	}

	const UScriptStruct* SelectedStruct = nullptr;
	UE::StructUtils::Private::GetCommonScriptStruct(StructProperty, SelectedStruct);

	FStructViewerInitializationOptions Options;
	Options.bShowNoneOption = bAllowNone;
	Options.StructFilter = StructFilter;
	Options.NameTypeToDisplay = EStructViewerNameTypeToDisplay::DisplayName;
	Options.DisplayMode = bShowTreeView ? EStructViewerDisplayMode::TreeView : EStructViewerDisplayMode::ListView;
	Options.bAllowViewOptions = !bHideViewOptions;
	Options.SelectedStruct = SelectedStruct;

	const FOnStructPicked OnPicked(FOnStructPicked::CreateSP(this, &SInstancedStructPicker::StructPicked));

	return SNew(SBox)
		.WidthOverride(280)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.MaxHeight(500)
			[
				FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer").CreateStructViewer(Options, OnPicked)
			]
		];
}

void SInstancedStructPicker::StructPicked(const UScriptStruct* InStruct)
{
	if (StructProperty && StructProperty->IsValidHandle())
	{
		FScopedTransaction Transaction(LOCTEXT("OnStructPicked", "Set Struct"));

		StructProperty->NotifyPreChange();

		StructProperty->EnumerateRawData([InStruct](void* RawData, const int32 /*DataIndex*/, const int32 /*NumDatas*/)
		{
			if (FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(RawData))
			{
				InstancedStruct->InitializeAs(InStruct);
			}
			return true;
		});

		StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
		StructProperty->NotifyFinishedChangingProperties();

		// Property tree will be invalid after changing the struct type, force update.
		if (PropUtils.IsValid())
		{
			PropUtils->ForceRefresh();
		}
	}

	ComboButton->SetIsOpen(false);
	OnStructPicked.ExecuteIfBound(InStruct);
}

#undef LOCTEXT_NAMESPACE
