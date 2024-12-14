// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Widgets/Editor/SDMToolBar.h"

#include "AssetRegistry/AssetData.h"
#include "DMWorldSubsystem.h"
#include "DynamicMaterialEditorModule.h"
#include "DynamicMaterialEditorSettings.h"
#include "DynamicMaterialEditorStyle.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "Layout/Margin.h"
#include "Material/DynamicMaterialInstance.h"
#include "Math/Vector2D.h"
#include "Model/DynamicMaterialModel.h"
#include "Model/DynamicMaterialModelBase.h"
#include "Model/DynamicMaterialModelDynamic.h"
#include "PackageTools.h"
#include "Selection.h"
#include "Styling/StyleColors.h"
#include "UI/Menus/DMToolBarMenus.h"
#include "UI/Widgets/SDMMaterialDesigner.h"
#include "UI/Widgets/SDMMaterialEditor.h"
#include "UObject/Object.h"
#include "Utils/DMMaterialInstanceFunctionLibrary.h"
#include "Utils/DMMaterialModelFunctionLibrary.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDMToolBar"

namespace UE::DynamicMaterialEditor::Private
{
	static const FMargin DefaultToolBarButtonContentPadding = FMargin(2.0f);
	static const FVector2D DefaultToolBarButtonSize = FVector2D(20.f);

	static const FMargin LargeIconToolBarButtonContentPadding = FMargin(4.f);
	static const FVector2D LargeIconToolBarButtonSize = FVector2D(16.f);
}

void SDMToolBar::Construct(const FArguments& InArgs, const TSharedRef<SDMMaterialEditor>& InEditorWidget, AActor* InActor)
{
	EditorWidgetWeak = InEditorWidget;
	MaterialActorWeak = InActor;
	SelectedMaterialElementIndex = INDEX_NONE;

	SetCanTick(false);
	
	ChildSlot
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.BorderImage(FDynamicMaterialEditorStyle::Get().GetBrush("Border.Bottom"))
			.BorderBackgroundColor(FLinearColor(1, 1, 1, 0.05f))
			.Padding(0.f, 3.f, 0.f, 3.f)
			[
				CreateToolBarEntries()
			]
		];

	SetActorPropertySelected(InActor);
	SetButtonVisibilities();
}

TSharedRef<SWidget> SDMToolBar::CreateToolBarEntries()
{
	using namespace UE::DynamicMaterialEditor::Private;

	return SNew(SHorizontalBox)
		
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		[
			SNew(SWrapBox)
			.Orientation(Orient_Horizontal)
			.UseAllottedSize(true)
			.HAlign(HAlign_Left)
			.InnerSlotPadding(FVector2D(5.0f))

			+ SWrapBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SAssignNew(SaveButtonWidget, SButton)
					.Visibility(EVisibility::Collapsed)
					.ContentPadding(LargeIconToolBarButtonContentPadding)
					.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("MaterialDesignerSaveTooltip", "Save the Material Designer asset\n\nCaution: If this asset lives inside an actor, the actor/level will be saved."))
					.OnClicked(this, &SDMToolBar::OnSaveClicked)
					[
						SNew(SImage)
						.Image(this, &SDMToolBar::GetSaveIcon)
						.DesiredSizeOverride(LargeIconToolBarButtonSize)
					]
				]		
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(SButton)
					.ContentPadding(LargeIconToolBarButtonContentPadding)
					.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("ExportMaterial", "Save As"))
					.OnClicked(this, &SDMToolBar::OnExportMaterialInstanceButtonClicked)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("AssetEditor.SaveAssetAs"))
						.DesiredSizeOverride(LargeIconToolBarButtonSize)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 0.f, 0.f)
				[
					SAssignNew(OpenParentButton, SButton)
					.Visibility(EVisibility::Collapsed)
					.ContentPadding(LargeIconToolBarButtonContentPadding)
					.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("MaterialDesignerOpenParentTooltip", "Open the parent of this Material Designer Instance."))
					.OnClicked(this, &SDMToolBar::OnOpenParentClicked)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.Blueprints")))
						.DesiredSizeOverride(LargeIconToolBarButtonSize)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 0.f, 0.f)
				[
					SAssignNew(ConvertToEditableButton, SButton)
					.Visibility(EVisibility::Collapsed)
					.ContentPadding(LargeIconToolBarButtonContentPadding)
					.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("MaterialDesignerConvertToEditableTooltip", "Convert this Material Designer Instance to a fully editable Material (and create a new shader)."))
					.OnClicked(this, &SDMToolBar::OnConvertToEditableClicked)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.Edit")))
						.DesiredSizeOverride(LargeIconToolBarButtonSize)
					]
				]
			]

			+ SWrapBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(AssetRowWidget, SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(LargeIconToolBarButtonContentPadding)
					.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("MaterialDesignerBrowseTooltip", "Browse to the selected asset in the content browser."))
					.OnClicked(this, &SDMToolBar::OnBrowseClicked)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.BrowseContent")))
						.DesiredSizeOverride(LargeIconToolBarButtonSize)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 0.f, 0.f)
				[
					SAssignNew(AssetNameWidget, STextBlock)
					.TextStyle(FDynamicMaterialEditorStyle::Get(), "ActorName")
				]
			]

			+ SWrapBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(InstanceWidget, STextBlock)
				.TextStyle(FDynamicMaterialEditorStyle::Get(), "ActorName")
				.Text(LOCTEXT("Instance", "(Inst)"))
				.Visibility(EVisibility::Collapsed)
			]

			+ SWrapBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(5.0f, 0.0f, 0.0f, 0.0f)
			[
				SAssignNew(ActorRowWidget, SHorizontalBox)
				.Visibility(EVisibility::Collapsed)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 5.f, 0.f)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush("ClassIcon.Actor"))
					.DesiredSizeOverride(LargeIconToolBarButtonSize)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.f, 0.f, 0.f, 0.f)
				.VAlign(EVerticalAlignment::VAlign_Center)
				[
					SAssignNew(ActorNameWidget, STextBlock)
					.TextStyle(FDynamicMaterialEditorStyle::Get(), "ActorName")
				]	
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(EVerticalAlignment::VAlign_Center)
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SAssignNew(PropertySelectorContainer, SBox)
					[
						CreateSlotsComboBoxWidget()
					]
				]	
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(EVerticalAlignment::VAlign_Center)
				.Padding(5.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ContentPadding(LargeIconToolBarButtonContentPadding)
					.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
					.ToolTipText(LOCTEXT("MaterialDesignerUseTooltip", "Replace the material in this slot with the one selected in the content browser."))
					.OnClicked(this, &SDMToolBar::OnUseClicked)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.Use")))
						.DesiredSizeOverride(LargeIconToolBarButtonSize)
					]
				]
			]
		]
		
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ContentPadding(DefaultToolBarButtonContentPadding)
			.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(LOCTEXT("MaterialDesignerFollowSelectionTooltip", "Toggles whether the Material Designer display will change when selecting new objects and actors."))
			.OnClicked(this, &SDMToolBar::OnFollowSelectionButtonClicked)
			[
				SNew(SImage)
				.Image(this, &SDMToolBar::GetFollowSelectionBrush)
				.DesiredSizeOverride(DefaultToolBarButtonSize)
				.ColorAndOpacity(this, &SDMToolBar::GetFollowSelectionColor)
			]
		]
		
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SComboButton)
			.HasDownArrow(false)
			.IsFocusable(true)
			.ContentPadding(DefaultToolBarButtonContentPadding)
			.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
			.ToolTipText(LOCTEXT("MaterialDesignerSettingsTooltip", "Material Designer Settings"))
			.OnGetMenuContent(this, &SDMToolBar::GenerateSettingsMenu)
			.ButtonContent()
			[
				SNew(SImage)
				.Image(FDynamicMaterialEditorStyle::Get().GetBrush("Icons.Menu.Dropdown"))
				.DesiredSizeOverride(DefaultToolBarButtonSize)
			]
		];
}

void SDMToolBar::SetActorPropertySelected(AActor* InActor)
{
	if (IsValid(InActor))
	{
		ActorNameWidget->SetText(GetActorName());
		ActorRowWidget->SetVisibility(EVisibility::Visible);

		TArray<FDMObjectMaterialProperty> ActorProperties = UDMMaterialInstanceFunctionLibrary::GetActorMaterialProperties(InActor);
		UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase();

		const int32 ActorPropertyCount = ActorProperties.Num();
		ActorMaterialProperties.Empty(ActorPropertyCount);

		for (int32 MaterialPropertyIdx = 0; MaterialPropertyIdx < ActorPropertyCount; ++MaterialPropertyIdx)
		{
			const FDMObjectMaterialProperty& MaterialProperty = ActorProperties[MaterialPropertyIdx];

			ActorMaterialProperties.Add(MakeShared<FDMObjectMaterialProperty>(MaterialProperty));

			if (MaterialProperty.GetMaterialModelBase() == MaterialModelBase)
			{
				SelectedMaterialElementIndex = MaterialPropertyIdx;
			}
		}
	}
	else
	{
		ActorMaterialProperties.Empty(0);
		ActorNameWidget->SetText(FText::GetEmpty());
		ActorRowWidget->SetVisibility(EVisibility::Collapsed);
	}

	PropertySelectorContainer->SetContent(CreateSlotsComboBoxWidget());
}

void SDMToolBar::SetButtonVisibilities()
{
	bool bIsAsset = false;
	bool bIsDynamic = false;

	UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase();

	if (IsValid(MaterialModelBase))
	{
		if (MaterialModelBase->IsAsset())
		{
			bIsAsset = true;
		}
		else if (UDynamicMaterialInstance* MaterialInstance = MaterialModelBase->GetDynamicMaterialInstance())
		{
			if (MaterialInstance->IsAsset())
			{
				bIsAsset = true;
			}
		}

		bIsDynamic = !MaterialModelBase->IsA<UDynamicMaterialModel>();
	}

	SaveButtonWidget->SetVisibility(CanSave() ? EVisibility::Visible : EVisibility::Collapsed);

	if (bIsAsset)
	{
		AssetNameWidget->SetText(GetAssetName());
		AssetNameWidget->SetToolTipText(GetAssetToolTip());
		AssetNameWidget->SetVisibility(EVisibility::Visible);

		AssetRowWidget->SetVisibility(EVisibility::Visible);
	}
	else
	{
		AssetNameWidget->SetText(FText::GetEmpty());
		AssetNameWidget->SetToolTipText(FText::GetEmpty());
		AssetNameWidget->SetVisibility(EVisibility::Collapsed);

		AssetRowWidget->SetVisibility(EVisibility::Collapsed);
	}

	if (bIsDynamic)
	{
		OpenParentButton->SetVisibility(EVisibility::Visible);
		ConvertToEditableButton->SetVisibility(EVisibility::Visible);
		InstanceWidget->SetVisibility(EVisibility::Visible);
	}
	else
	{
		OpenParentButton->SetVisibility(EVisibility::Collapsed);
		ConvertToEditableButton->SetVisibility(EVisibility::Collapsed);
		InstanceWidget->SetVisibility(EVisibility::Collapsed);
	}
}

TSharedRef<SWidget> SDMToolBar::CreateToolBarButton(TAttribute<const FSlateBrush*> InImageBrush, const TAttribute<FText>& InTooltipText, FOnClicked InOnClicked)
{
	using namespace UE::DynamicMaterialEditor::Private;

	return SNew(SButton)
		.ContentPadding(DefaultToolBarButtonContentPadding)
		.ButtonStyle(FDynamicMaterialEditorStyle::Get(), "HoverHintOnly")
		.ToolTipText(InTooltipText)
		.OnClicked(InOnClicked)
		[
			SNew(SImage)
			.Image(InImageBrush)
			.DesiredSizeOverride(DefaultToolBarButtonSize)
		];
}

TSharedRef<SWidget> SDMToolBar::CreateSlotsComboBoxWidget()
{
	UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase();

	if (!MaterialActorWeak.IsValid() || !IsValid(MaterialModelBase))
	{
		return SNullWidget::NullWidget;
	}

	const TSharedPtr<FDMObjectMaterialProperty> InitiallySelectedItem =
		ActorMaterialProperties.IsValidIndex(SelectedMaterialElementIndex) ? ActorMaterialProperties[SelectedMaterialElementIndex] : nullptr;

	return SNew(SComboBox<TSharedPtr<FDMObjectMaterialProperty>>)
		.IsEnabled(ActorMaterialProperties.Num() > 1)
		.InitiallySelectedItem(InitiallySelectedItem)
		.OptionsSource(&ActorMaterialProperties)
		.OnGenerateWidget(this, &SDMToolBar::GenerateSelectedMaterialSlotRow)
		.OnSelectionChanged(this, &SDMToolBar::OnMaterialSlotChanged)
		[
			SNew(STextBlock)
			.MinDesiredWidth(100.0f)
			.TextStyle(FDynamicMaterialEditorStyle::Get(), "RegularFont")
			.Text(this, &SDMToolBar::GetSelectedMaterialSlotName)
		];
}

TSharedRef<SWidget> SDMToolBar::GenerateSelectedMaterialSlotRow(TSharedPtr<FDMObjectMaterialProperty> InSelectedSlot) const
{
	if (InSelectedSlot.IsValid())
	{
		return SNew(STextBlock)
			.MinDesiredWidth(100.f)
			.TextStyle(FDynamicMaterialEditorStyle::Get(), "RegularFont")
			.Text(this, &SDMToolBar::GetSlotDisplayName, InSelectedSlot);
	}
	return SNullWidget::NullWidget;
}

FText SDMToolBar::GetSlotDisplayName(TSharedPtr<FDMObjectMaterialProperty> InSlot) const
{
	return InSlot->GetPropertyName(false);
}

FText SDMToolBar::GetSelectedMaterialSlotName() const
{
	if (ActorMaterialProperties.IsValidIndex(SelectedMaterialElementIndex) && ActorMaterialProperties[SelectedMaterialElementIndex].IsValid())
	{
		return GetSlotDisplayName(ActorMaterialProperties[SelectedMaterialElementIndex]);
	}
	return FText::GetEmpty();
}

void SDMToolBar::OnMaterialSlotChanged(TSharedPtr<FDMObjectMaterialProperty> InSelectedSlot, ESelectInfo::Type InSelectInfoType)
{
	if (!InSelectedSlot.IsValid())
	{
		return;
	}

	TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget();

	if (!EditorWidget.IsValid())
	{
		return;
	}

	TSharedPtr<SDMMaterialDesigner> DesignerWidget = EditorWidget->GetDesignerWidget();

	if (!DesignerWidget.IsValid())
	{
		return;
	}

	UDynamicMaterialModelBase* SelectedMaterialModelBase = InSelectedSlot->GetMaterialModelBase();

	if (IsValid(SelectedMaterialModelBase))
	{
		DesignerWidget->OpenObjectMaterialProperty(*InSelectedSlot);
	}
	else if (InSelectedSlot->GetOuter())
	{
		if (UDynamicMaterialModel* NewModel = UDMMaterialInstanceFunctionLibrary::CreateMaterialInObject(*InSelectedSlot.Get()))
		{
			DesignerWidget->OpenObjectMaterialProperty(*InSelectedSlot);
		}
	}
}

UDynamicMaterialModelBase* SDMToolBar::GetMaterialModelBase() const
{
	if (TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget())
	{
		return EditorWidget->GetMaterialModelBase();
	}

	return nullptr;
}

const FSlateBrush* SDMToolBar::GetFollowSelectionBrush() const
{
	static const FSlateBrush* Unlocked = FAppStyle::Get().GetBrush("Icons.Unlock");
	static const FSlateBrush* Locked = FAppStyle::Get().GetBrush("Icons.Lock");

	if (!SDMMaterialDesigner::IsFollowingSelection())
	{
		return Locked;
	}

	return Unlocked;
}

FSlateColor SDMToolBar::GetFollowSelectionColor() const
{
	// We want the icon to stand out when it's locked.
	static FSlateColor EnabledColor = FSlateColor(EStyleColor::AccentGray);
	static FSlateColor DisabledColor = FSlateColor(EStyleColor::Primary);

	if (SDMMaterialDesigner::IsFollowingSelection())
	{
		return EnabledColor;
	}

	return DisabledColor;
}

FReply SDMToolBar::OnFollowSelectionButtonClicked()
{
	if (UDynamicMaterialEditorSettings* Settings = UDynamicMaterialEditorSettings::Get())
	{
		Settings->bFollowSelection = !Settings->bFollowSelection;
		Settings->SaveConfig();
	}

	return FReply::Handled();
}

FReply SDMToolBar::OnExportMaterialInstanceButtonClicked()
{
	TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget();

	if (!EditorWidget.IsValid())
	{
		return FReply::Handled();
	}

	TSharedPtr<SDMMaterialDesigner> DesignerWidget = EditorWidget->GetDesignerWidget();

	if (!DesignerWidget.IsValid())
	{
		return FReply::Handled();
	}

	UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase();

	if (!MaterialModelBase)
	{
		return FReply::Handled();
	}

	UDynamicMaterialInstance* NewInstance = UDMMaterialModelFunctionLibrary::ExportMaterial(MaterialModelBase);

	if (!NewInstance)
	{
		return FReply::Handled();
	}

	DesignerWidget->OpenMaterialInstance(NewInstance);

	return FReply::Handled();
}

FReply SDMToolBar::OnBrowseClicked()
{
	UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase();

	if (!IsValid(MaterialModelBase))
	{
		return FReply::Handled();
	}

	UObject* Asset = nullptr;

	if (MaterialModelBase->IsAsset())
	{
		Asset = MaterialModelBase;
	}
	else if (UDynamicMaterialInstance* MaterialInstance = MaterialModelBase->GetDynamicMaterialInstance())
	{
		if (MaterialInstance->IsAsset())
		{
			Asset = MaterialInstance;
		}
	}

	if (!Asset)
	{
		return FReply::Handled();
	}

	TArray<FAssetData> AssetDataList;
	AssetDataList.Add(Asset);
	GEditor->SyncBrowserToObjects(AssetDataList);

	return FReply::Handled();
}

FReply SDMToolBar::OnUseClicked()
{
	if (!ActorMaterialProperties.IsValidIndex(SelectedMaterialElementIndex))
	{
		return FReply::Handled();
	}

	UDynamicMaterialModelBase* CurrentModelBase = GetMaterialModelBase();
	UDMWorldSubsystem* DMSubsystem = nullptr;

	if (CurrentModelBase)
	{
		AActor* Actor = MaterialActorWeak.Get();

		if (!IsValid(Actor))
		{
			return FReply::Handled();
		}

		UWorld* World = Actor->GetWorld();

		if (!IsValid(World))
		{
			return FReply::Handled();
		}

		DMSubsystem = World->GetSubsystem<UDMWorldSubsystem>();

		if (!DMSubsystem)
		{
			return FReply::Handled();
		}
	}

	USelection* Selection = GEditor->GetSelectedObjects();

	if (!Selection)
	{
		return FReply::Handled();
	}

	FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();

	UDynamicMaterialInstance* SelectedInstance = nullptr;

	TArray<UDynamicMaterialInstance*> SelectedInstances;
	Selection->GetSelectedObjects(SelectedInstances);

	for (UDynamicMaterialInstance* SelectedInstanceIter : SelectedInstances)
	{
		if (!IsValid(SelectedInstanceIter) || !SelectedInstanceIter->IsAsset())
		{
			continue;
		}

		SelectedInstance = SelectedInstanceIter;
		break;
	}

	if (!SelectedInstance)
	{
		return FReply::Handled();
	}

	FDMObjectMaterialProperty& CurrentActorProperty = *ActorMaterialProperties[SelectedMaterialElementIndex];

	if (!UDMMaterialInstanceFunctionLibrary::SetMaterialInObject(CurrentActorProperty, SelectedInstance))
	{
		return FReply::Handled();
	}

	if (TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget())
	{
		if (TSharedPtr<SDMMaterialDesigner> DesignerWidget = EditorWidget->GetDesignerWidget())
		{
			DesignerWidget->OpenObjectMaterialProperty(CurrentActorProperty);
		}
	}

	return FReply::Handled();
}

UPackage* SDMToolBar::GetSaveablePackage(UObject* InObject)
{
	if (!IsValid(InObject))
	{
		return nullptr;
	}

	UPackage* Package = InObject->GetPackage();

	if (!Package || Package->HasAllFlags(RF_Transient))
	{
		return nullptr;
	}

	return Package;
}

FText SDMToolBar::GetActorName() const
{
	if (const AActor* const SlotActor = GetMaterialActor())
	{
		return FText::FromString(SlotActor->GetActorLabel());
	}

	return FText::GetEmpty();
}

TSharedPtr<SDMMaterialEditor> SDMToolBar::GetEditorWidget() const
{
	return EditorWidgetWeak.Pin();
}

FText SDMToolBar::GetAssetName() const
{
	if (UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase())
	{
		if (UDynamicMaterialInstance* MaterialInstance = MaterialModelBase->GetDynamicMaterialInstance())
		{
			if (MaterialInstance->IsAsset())
			{
				return FText::FromString(MaterialInstance->GetName());
			}
		}
		else if (MaterialModelBase->IsAsset())
		{
			return FText::FromString(MaterialModelBase->GetName());
		}
	}

	return FText::GetEmpty();
}

FText SDMToolBar::GetAssetToolTip() const
{
	if (UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase())
	{
		if (UDynamicMaterialInstance* MaterialInstance = MaterialModelBase->GetDynamicMaterialInstance())
		{
			if (MaterialInstance->IsAsset())
			{
				return FText::FromString(MaterialInstance->GetPathName());
			}
		}
		else if (MaterialModelBase->IsAsset())
		{
			return FText::FromString(MaterialModelBase->GetPathName());
		}
	}

	return FText::GetEmpty();
}

bool SDMToolBar::CanSave() const
{
	if (UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase())
	{
		return !MaterialModelBase->GetTypedOuter<UWorld>();
	}

	return false;
}

const FSlateBrush* SDMToolBar::GetSaveIcon() const
{
	if (UPackage* Package = GetSaveablePackage(GetMaterialModelBase()))
	{
		if (Package->IsDirty())
		{
			return FAppStyle::Get().GetBrush("Icons.SaveModified");
		}
	}

	return FAppStyle::Get().GetBrush("Icons.Save");
}

FReply SDMToolBar::OnSaveClicked()
{
	if (UDynamicMaterialModelBase* MaterialModelBase = GetMaterialModelBase())
	{
		if (UPackage* Package = GetSaveablePackage(MaterialModelBase))
		{
			TArray<UObject*> AssetsToSave;
			AssetsToSave.Add(MaterialModelBase);
			UPackageTools::SavePackagesForObjects(AssetsToSave);
		}
	}

	return FReply::Handled();
}

FReply SDMToolBar::OnOpenParentClicked()
{
	if (TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget())
	{
		if (TSharedPtr<SDMMaterialDesigner> DesignerWidget = EditorWidget->GetDesignerWidget())
		{
			if (UDynamicMaterialModelDynamic* DynamicMaterialModel = Cast<UDynamicMaterialModelDynamic>(EditorWidget->GetMaterialModelBase()))
			{
				if (UDynamicMaterialModel* ParentModel = DynamicMaterialModel->ResolveMaterialModel())
				{
					DesignerWidget->OpenMaterialModelBase(ParentModel);
				}
			}
		}
	}

	return FReply::Handled();
}

FReply SDMToolBar::OnConvertToEditableClicked()
{
	UDynamicMaterialModelDynamic* CurrentModelDynamic = Cast<UDynamicMaterialModelDynamic>(GetMaterialModelBase());

	if (!CurrentModelDynamic)
	{
		UE_LOG(LogDynamicMaterialEditor, Error, TEXT("Tried to convert a null or non-dynamic model to editable."));
		return FReply::Handled();
	}

	UDynamicMaterialModel* ParentModel = CurrentModelDynamic->GetParentModel();

	if (!ParentModel)
	{
		UE_LOG(LogDynamicMaterialEditor, Error, TEXT("Failed to find parent model."));
		return FReply::Handled();
	}

	UDynamicMaterialInstance* OldInstance = CurrentModelDynamic->GetDynamicMaterialInstance();

	bool bIsAsset = false;

	if (CurrentModelDynamic->IsAsset())
	{
		bIsAsset = true;
	}
	else if (OldInstance)
	{
		if (OldInstance->IsAsset())
		{
			bIsAsset = true;
		}
	}

	UDMWorldSubsystem* DMSubsystem = nullptr;
	AActor* Actor = MaterialActorWeak.Get();
	TSharedPtr<FDMObjectMaterialProperty> CurrentActorProperty = nullptr;

	if (Actor && ActorMaterialProperties.IsValidIndex(SelectedMaterialElementIndex) && ActorMaterialProperties[SelectedMaterialElementIndex].IsValid())
	{
		UWorld* World = Actor->GetWorld();

		if (IsValid(World))
		{
			DMSubsystem = World->GetSubsystem<UDMWorldSubsystem>();
		}

		CurrentActorProperty = ActorMaterialProperties[SelectedMaterialElementIndex];
	}

	// In-actor models/instance must have a world subsystem to query.
	if (!bIsAsset && !DMSubsystem)
	{
		UE_LOG(LogDynamicMaterialEditor, Error, TEXT("Cannot create a new asset for embedded instances without an active world subsystem."));
		return FReply::Handled();
	}

	UDynamicMaterialInstance* NewInstance = nullptr;
	UDynamicMaterialModel* NewModel = nullptr;

	if (OldInstance)
	{
		NewInstance = UDMMaterialModelFunctionLibrary::ExportToTemplateMaterial(CurrentModelDynamic);

		if (NewInstance)
		{
			NewModel = NewInstance->GetMaterialModel();
		}
	}
	else
	{
		NewModel = UDMMaterialModelFunctionLibrary::ExportToTemplateMaterialModel(CurrentModelDynamic);
	}

	if (!NewModel)
	{
		UE_LOG(LogDynamicMaterialEditor, Error, TEXT("Failed to create new model."));
		return FReply::Handled();
	}

	// If it was in an actor, set it on the actor
	if (NewInstance && CurrentActorProperty.IsValid())
	{
		// Setting it on the actor will automatically open it if the actor property is currently active.
		UDMMaterialInstanceFunctionLibrary::SetMaterialInObject(*CurrentActorProperty, NewInstance);
	}
	else
	{
		if (TSharedPtr<SDMMaterialEditor> EditorWidget = GetEditorWidget())
		{
			if (TSharedPtr<SDMMaterialDesigner> DesignerWidget = EditorWidget->GetDesignerWidget())
			{
				DesignerWidget->OpenMaterialModelBase(NewModel);
			}
		}
	}

	return FReply::Handled();
}

TSharedRef<SWidget> SDMToolBar::GenerateSettingsMenu()
{
	return FDMToolBarMenus::MakeEditorLayoutMenu(GetEditorWidget());
}

#undef LOCTEXT_NAMESPACE
