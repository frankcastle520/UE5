// Copyright Epic Games, Inc. All Rights Reserved.

#include "RCAssetPathElementCustomization.h"

#include "Behaviour/Builtin/Path/RCSetAssetByPathBehaviour.h"
#include "DetailLayoutBuilder.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailChildrenBuilder.h"
#include "IPropertyUtilities.h"
#include "PropertyCustomizationHelpers.h"
#include "UI/RemoteControlPanelStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "RCAssetPathElementCustomization"

TSharedRef<IPropertyTypeCustomization> FRCAssetPathElementCustomization::MakeInstance()
{
	return MakeShared<FRCAssetPathElementCustomization>();
}

void FRCAssetPathElementCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
	ArrayEntryHandle = InPropertyHandle;
	PropertyUtilities = InCustomizationUtils.GetPropertyUtilities();
	IsInputHandle = ArrayEntryHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FRCAssetPathElement, bIsInput));
	PathHandle = ArrayEntryHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FRCAssetPathElement, Path));

	if (!IsInputHandle.IsValid() || !PathHandle.IsValid())
	{
		return;
	}

	PathHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FRCAssetPathElementCustomization::OnPathChanged));

	const TSharedRef<SToolTip> GetAssetPathToolTipWidget = SNew(SToolTip)
		.Text(LOCTEXT("RCGetAssetPathButton_Tooltip", "Get the path of the currently first selected asset in the content browser and set it to the current path"));

	const TSharedRef<SToolTip> CreateControllerToolTipWidget = SNew(SToolTip)
		.Text(LOCTEXT("RCCreateController_Tooltip", "Create a controller for the given RC Input path entry"));

	InHeaderRow.NameContent()
		[
			InPropertyHandle->CreatePropertyNameWidget()
		];

	InHeaderRow.ValueContent()
	[
		SNew(SHorizontalBox)
        // RC Input CheckBox
		+SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(5.f, 0.f)
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.Style(&FRemoteControlPanelStyle::Get()->GetWidgetStyle<FCheckBoxStyle>("RemoteControlPathBehaviour.AssetCheckBox"))
			.IsChecked(this, &FRCAssetPathElementCustomization::IsChecked)
			.OnCheckStateChanged(this, &FRCAssetPathElementCustomization::OnCheckStateChanged)
			.IsFocusable(false)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("RCInputButtonAssetPath", "RCInput"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		]

		// Path String
		+SHorizontalBox::Slot()
		.FillWidth(1.f)
		.Padding(5.f, 0.f)
		.VAlign(VAlign_Center)
		[
			PathHandle->CreatePropertyValueWidget(false)
		]

		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SWidgetSwitcher)
			.WidgetIndex(this, &FRCAssetPathElementCustomization::OnGetWidgetSwitcherIndex)
			// [0] Get Current Selected Asset Path Button
			+ SWidgetSwitcher::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &FRCAssetPathElementCustomization::OnGetAssetFromSelectionClicked)
				.ToolTip(GetAssetPathToolTipWidget)
				.IsFocusable(false)
				.ContentPadding(0)
				.Content()
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.Use"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]

			// [1] Create Controller button
			+ SWidgetSwitcher::Slot()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.OnClicked(this, &FRCAssetPathElementCustomization::OnCreateControllerButtonClicked)
				.ToolTip(CreateControllerToolTipWidget)
				.IsFocusable(false)
				.ContentPadding(0)
				.Content()
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("Icons.PlusCircle"))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		]
	];
}

void FRCAssetPathElementCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils)
{
}

void FRCAssetPathElementCustomization::OnPathChanged() const
{
	if (!PathHandle.IsValid() || !IsInputHandle.IsValid())
	{
		return;
	}

	bool bIsRCInput = false;
	IsInputHandle->GetValue(bIsRCInput);

	if (bIsRCInput)
	{
		return;
	}

	RemoveSlashFromPathEnd();
}

ECheckBoxState FRCAssetPathElementCustomization::IsChecked() const
{
	ECheckBoxState ReturnValue = ECheckBoxState::Undetermined;
	if (IsInputHandle.IsValid())
	{
		bool bIsInput;
		const FPropertyAccess::Result Result = IsInputHandle->GetValue(bIsInput);
		if (Result == FPropertyAccess::Success)
		{
			ReturnValue = bIsInput ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
	}
	return ReturnValue;
}

void FRCAssetPathElementCustomization::OnCheckStateChanged(ECheckBoxState InNewState) const
{
	if (!IsInputHandle.IsValid())
	{
		return;
	}
	const bool bIsInput = InNewState == ECheckBoxState::Checked;
	IsInputHandle->SetValue(bIsInput);

	if (!bIsInput)
	{
		RemoveSlashFromPathEnd();
	}
}

FReply FRCAssetPathElementCustomization::OnGetAssetFromSelectionClicked() const
{
	TArray<FAssetData> AssetData;
	GEditor->GetContentBrowserSelections(AssetData);
	if (AssetData.Num() <= 0)
	{
		return FReply::Handled();
	}
				
	// Clear it, in case it is an already used one.
	// Use the first one in the Array
	int32 IndexOfLast;
	const UObject* SelectedAsset = AssetData[0].GetAsset();
	FString PathString = SelectedAsset->GetPathName();

	// Remove the initial Game
	PathString.RemoveFromStart("/Game/");

	// Remove anything after the last /
	PathString.FindLastChar('/', IndexOfLast);
	if (IndexOfLast != INDEX_NONE)
	{
		PathString.RemoveAt(IndexOfLast, PathString.Len() - IndexOfLast);
	}
	else
	{
		// if the Index is -1 then it means that we are selecting an asset already in the topmost folder
		// So we clear the string since it will just contains the AssetName
		PathString.Empty();
	}

	if (IsInputHandle.IsValid())
	{
		IsInputHandle->SetValue(false);
	}
	if (PathHandle.IsValid())
	{
		PathHandle->SetValue(PathString);
	}
	return FReply::Handled();
}

FReply FRCAssetPathElementCustomization::OnCreateControllerButtonClicked() const
{
	if (PropertyUtilities.IsValid() && ArrayEntryHandle.IsValid())
	{
		FPropertyChangedEvent Event(ArrayEntryHandle->GetProperty(), EPropertyChangeType::ValueSet);
		Event.MemberProperty = ArrayEntryHandle->GetProperty();

		TMap<FString, int32> ArrayIndexPerObject;
		ArrayIndexPerObject.Add(Event.GetMemberPropertyName().ToString(), ArrayEntryHandle->GetArrayIndex());

		Event.SetArrayIndexPerObject(MakeArrayView(&ArrayIndexPerObject, 1));
		Event.ObjectIteratorIndex = 0;
		PropertyUtilities->NotifyFinishedChangingProperties(Event);
	}
	return FReply::Handled();
}

int32 FRCAssetPathElementCustomization::OnGetWidgetSwitcherIndex() const
{
	return IsChecked() == ECheckBoxState::Checked ? 1 : 0;
}

void FRCAssetPathElementCustomization::RemoveSlashFromPathEnd() const
{
	FString CurrentPath;
	PathHandle->GetValue(CurrentPath);

	// remove all / that are at the end of the path
	bool bRemoved = false;
	do
	{
		bRemoved = CurrentPath.RemoveFromEnd(TEXT("/"), 1);
	} while (bRemoved);

	PathHandle->SetValue(CurrentPath, EPropertyValueSetFlags::NotTransactable);
}

#undef LOCTEXT_NAMESPACE
