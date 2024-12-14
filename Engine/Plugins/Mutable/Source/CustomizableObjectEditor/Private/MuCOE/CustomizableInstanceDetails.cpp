// Copyright Epic Games, Inc. All Rights Reserved.

#include "MuCOE/CustomizableInstanceDetails.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "CustomizableObjectInstanceEditor.h"
#include "ContentBrowserModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailsView.h"
#include "IDetailGroup.h"
#include "Misc/TransactionObjectEvent.h"
#include "ScopedTransaction.h"
#include "Serialization/BufferArchive.h"
#include "Toolkits/ToolkitManager.h"
#include "Math/Transform.h"

#include "MuCO/CustomizableObjectInstancePrivate.h"
#include "MuCO/CustomizableObject.h"
#include "MuCO/CustomizableObjectInstance.h"
#include "MuCO/CustomizableObjectSystem.h"
#include "MuCOE/UnrealEditorPortabilityHelpers.h"

#include "AssetThumbnail.h"
#include "Slate/DeferredCleanupSlateBrush.h"
#include "SSearchableComboBox.h"
#include "Types/SlateEnums.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SRotatorInputBox.h"
#include "Widgets/Input/SVectorInputBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"

class UObject;

#define LOCTEXT_NAMESPACE "CustomizableInstanceDetails"

// Define here metadata keywords used in the properties details
namespace UIMetadataKeyWords
{
	// Key name for float slider decorators
	constexpr TCHAR const* FloatDecoratorName = TEXT("SliderImage");
}


TSharedRef<IDetailCustomization> FCustomizableInstanceDetails::MakeInstance()
{
	return MakeShareable(new FCustomizableInstanceDetails);
}


void FCustomizableInstanceDetails::CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder)
{
	LayoutBuilder = DetailBuilder;

	const IDetailsView* DetailsView = DetailBuilder->GetDetailsView();
	check(DetailsView->GetSelectedObjects().Num());

	CustomInstance = Cast<UCustomizableObjectInstance>(DetailsView->GetSelectedObjects()[0].Get());
	check(CustomInstance.IsValid());

	TSharedPtr<IToolkit> FoundAssetEditor = FToolkitManager::Get().FindEditorForAsset(CustomInstance.Get()); // Tab spawned in a COEInstanceEditor
	if (!FoundAssetEditor)
	{
		FoundAssetEditor = FToolkitManager::Get().FindEditorForAsset(CustomInstance->GetCustomizableObject()); // Tab spawned in a COEditor
	}
	check(FoundAssetEditor);

	WeakEditor = StaticCastSharedPtr<ICustomizableObjectInstanceEditor>(FoundAssetEditor).ToWeakPtr();

	if (CustomInstance->GetPrivate()->IsSelectedParameterProfileDirty())
	{
		CustomInstance->GetPrivate()->SaveParametersToProfile(CustomInstance->GetPrivate()->SelectedProfileIndex);
	}

	// Delegate to refresh the details when the instance has finished the Update
	CustomInstance->UpdatedNativeDelegate.AddSP(this, &FCustomizableInstanceDetails::InstanceUpdated);
	CustomInstance->GetPrivate()->OnInstanceTransactedDelegate.AddSP(this, &FCustomizableInstanceDetails::OnInstanceTransacted);

	// New Category that will store all properties widgets
	IDetailCategoryBuilder& ResourcesCategory = DetailBuilder->EditCategory("Generated Resources");
	IDetailCategoryBuilder& VisibilitySettingsCategory = DetailBuilder->EditCategory("ParametersVisibility");
	IDetailCategoryBuilder& ParametersCategory = DetailBuilder->EditCategory("Instance Parameters");
	IDetailCategoryBuilder& OldParametersCategory = DetailBuilder->EditCategory("Old Instance Parameters");
	IDetailCategoryBuilder& SkeletalMeshCategory = DetailBuilder->EditCategory("CustomizableSkeletalMesh");
	IDetailCategoryBuilder& TextureParametersCategory = DetailBuilder->EditCategory("TextureParameter");

	// Showing warning message in case that the instance has not been compiled
	UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();
	if (!CustomizableObject)
	{
		VisibilitySettingsCategory.AddCustomRow(LOCTEXT("CustomizableInstanceDetails_NoCOMessage", "Instance Parameters"))
		[
			SNew(STextBlock).Text(LOCTEXT("Model not compiled", "Model not compiled"))
		];

		return;
	}

	CustomizableObject->GetPostCompileDelegate().AddSP(this, &FCustomizableInstanceDetails::ObjectCompiled);

	TArray<UObject*> Private;
	Private.Add(CustomInstance->GetPrivate());

	FAddPropertyParams PrivatePropertyParams;
	PrivatePropertyParams.HideRootObjectNode(true);

	ResourcesCategory.InitiallyCollapsed(true);
	IDetailPropertyRow* PrivateDataRow = ResourcesCategory.AddExternalObjects(Private, EPropertyLocation::Default, PrivatePropertyParams);

	// Store which Texture Parameter values can be selected.
	GenerateTextureParameterOptions();

	// State Selector Widget
	VisibilitySettingsCategory.AddCustomRow(LOCTEXT("CustomizableInstanceDetails_StateSelector", "State"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("StateSelector_Text","State"))
		.ToolTipText(LOCTEXT("StateSelector_Tooltip","Select a state."))
	]
	.ValueContent()
	.HAlign(EHorizontalAlignment::HAlign_Fill)
	[
		GenerateStateSelector()
	];

	// Profile Selector Widget
	VisibilitySettingsCategory.AddCustomRow(LOCTEXT("CustomizableInstanceDetails_InstanceProfileSelector", "Preview Instance Parameter Profiles"))
	.NameContent()
	[
		SNew(STextBlock)
		.Text(LOCTEXT("ProfileSelector_Text","Parameter Profile"))
		.ToolTipText(LOCTEXT("ProfileSelector_Tooltip", "Select a profile to save the parameter options selected."))
	]
	.ValueContent()
	.HAlign(EHorizontalAlignment::HAlign_Fill)
	[
		GenerateInstanceProfileSelector()
	];

	// Only Runtime Parameters Option
	VisibilitySettingsCategory.AddCustomRow( LOCTEXT("CustomizableInstanceDetails_RuntimeParm", "Only Runtime") )
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Only Runtime"))
	]
	.ValueContent()
	.HAlign(EHorizontalAlignment::HAlign_Fill)
	[
		SNew(SCheckBox)
		.IsChecked(CustomInstance->GetPrivate()->bShowOnlyRuntimeParameters ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
		.OnCheckStateChanged(this, &FCustomizableInstanceDetails::OnShowOnlyRuntimeSelectionChanged)
	];
	
	// Only Relevant Parameters Option
	VisibilitySettingsCategory.AddCustomRow( LOCTEXT("CustomizableInstanceDetails_RelevantParam", "Only Relevant") )
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("Only Relevant"))
	]
	.ValueContent()
	.HAlign(EHorizontalAlignment::HAlign_Fill)
	[
		SNew(SCheckBox)
		.IsChecked(CustomInstance->GetPrivate()->bShowOnlyRelevantParameters ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
		.OnCheckStateChanged(this, &FCustomizableInstanceDetails::OnShowOnlyRelevantSelectionChanged)
	];
	
	// Show UI sections Option
	VisibilitySettingsCategory.AddCustomRow( LOCTEXT("CustomizableInstanceDetails_UISections", "UI Sections") )
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("UI Sections"))
	]
	.ValueContent()
	.HAlign(EHorizontalAlignment::HAlign_Fill)
	[
		SNew(SCheckBox)
		.IsChecked(CustomInstance->GetPrivate()->bShowUISections ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
		.OnCheckStateChanged(this, &FCustomizableInstanceDetails::OnUseUISectionsSelectionChanged)
	];

	
	// Show UI thumbnails Option
	VisibilitySettingsCategory.AddCustomRow( LOCTEXT("CustomizableInstanceDetails_UIThumbnails", "UI Thumbnails") )
	.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString("UI Thumbnails"))
	]
	.ValueContent()
	.HAlign(EHorizontalAlignment::HAlign_Fill)
	[
		SNew(SCheckBox)
		.IsChecked(CustomInstance->GetPrivate()->bShowUIThumbnails ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
		.OnCheckStateChanged(this, &FCustomizableInstanceDetails::OnUseUIThumbnailsSelectionChanged)
	];


	TSharedPtr<ICustomizableObjectInstanceEditor> Editor = GetEditorChecked();
	if (UCustomizableObjectEditorProperties* EditorProperties = Editor->GetEditorProperties())
	{
		IDetailPropertyRow* FilterPropertyRow = VisibilitySettingsCategory.AddExternalObjectProperty({ EditorProperties }, FName("Filter"));
		FilterPropertyRow->GetPropertyHandle()->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FCustomizableInstanceDetails::Refresh));

		Filter = EditorProperties->Filter.GameplayTagsFilter;
		FilterType = EditorProperties->Filter.GameplayTagsFilterType;
	}

	// Copy, Paste and Reset Parameters
	ParametersCategory.AddCustomRow(LOCTEXT("CustomizableInstanceDetails_CopyPasteResetButtons", "Copy Paste Reset"))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(2.0f)
		.Padding(0.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("Copy_Parameters", "Copy Parameters"))
			.OnClicked(this, &FCustomizableInstanceDetails::OnCopyAllParameters)
			.IsEnabled(CustomInstance->HasAnyParameters())
			.ToolTipText(FText::FromString(FString("Copy the preview Instance parameters")))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(2.0f)
		.Padding(0.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("Paste_Parameters", "Paste Parameters"))
			.OnClicked(this, &FCustomizableInstanceDetails::OnPasteAllParameters)
			.IsEnabled(CustomInstance->HasAnyParameters())
			.ToolTipText(FText::FromString(FString("Paste the preview Instance parameters")))
		]
		+ SHorizontalBox::Slot()
		.FillWidth(2.0f)
		.Padding(0.0f, 5.0f, 0.0f, 5.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.Text(LOCTEXT("Reset_Integer_Paramaters", "Reset parameters"))
			.OnClicked(this, &FCustomizableInstanceDetails::OnResetAllParameters)
			.IsEnabled(CustomInstance->HasAnyParameters())
			.ToolTipText(FText::FromString(FString("Clear the preview Instance parameters")))
		]
	];

	// Parameters Widgets
	bool bHiddenParamsRuntime = GenerateParametersView(ParametersCategory);

	if (bHiddenParamsRuntime)
	{
		FText HiddenParamsRuntimeMessage = LOCTEXT("CustomizableInstanceDetails_HiddemParamsRuntime", "Parameters are hidden due to their Runtime type. \nUncheck the Only Runtime checkbox to see them.");

		ParametersCategory.AddCustomRow(LOCTEXT("CustomizableInstanceDetails_HiddemParamsRuntimeRow", "Parameters are hidden"))
		[
			SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(2.0f)
				.Padding(0.0f, 15.0f, 0.0f, 5.0f)
				[
					SNew(STextBlock)
						.Text(HiddenParamsRuntimeMessage)
						.ToolTipText(HiddenParamsRuntimeMessage)
						.AutoWrapText(true)
				]
		];
	}
}


void FCustomizableInstanceDetails::Refresh() const
{
	if (IDetailLayoutBuilder* Layout = LayoutBuilder.Pin().Get()) // Raw because we don't want to keep alive the details builder when calling the force refresh details
	{
		Layout->ForceRefreshDetails();
	}
}


void FCustomizableInstanceDetails::UpdateInstance()
{
	CustomInstance->UpdateSkeletalMeshAsync(true, true);
	CustomInstance->PostEditChange();
}


void FCustomizableInstanceDetails::InstanceUpdated(UCustomizableObjectInstance* Instance) const
{
	// Check the instance update context to aboid unecessary UI updates.
	if (!bUpdatingSlider)
	{
		Refresh();
	}
}


void FCustomizableInstanceDetails::ObjectCompiled()
{
	if (!bUpdatingSlider)
	{
		Refresh();
	}
}


// STATE SELECTOR -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateStateSelector()
{
	UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();

	// States selector options
	const int NumStates = CustomizableObject->GetStateCount();
	const int CurrentState = CustomInstance->GetPrivate()->GetState();
	TSharedPtr<FString> CurrentStateName = nullptr;
	
	//I think that this is not necessary. There is always a "Default" state
	if (NumStates == 0)
	{
		StateNames.Add(MakeShareable(new FString("Default")));
		CurrentStateName = StateNames.Last();
	}

	for (int StateIndex = 0; StateIndex < NumStates; ++StateIndex)
	{
		if (StateIndex == CurrentState)
		{
			CurrentStateName = MakeShareable(new FString(CustomizableObject->GetStateName(StateIndex)));
			StateNames.Add(CurrentStateName);
		}
		else
		{
			StateNames.Add(MakeShareable(new FString(CustomizableObject->GetStateName(StateIndex))));
		}
	}

	StateNames.Sort([](const TSharedPtr<FString>& LHS, const TSharedPtr<FString>& RHS)
	{
		return *LHS < *RHS;
	});

	return SNew(STextComboBox)
		.OptionsSource(&StateNames)
		.InitiallySelectedItem((CustomInstance->GetPrivate()->GetState() != -1) ? CurrentStateName : StateNames[0])
		.OnSelectionChanged(this, &FCustomizableInstanceDetails::OnStateComboBoxSelectionChanged);
}


void FCustomizableInstanceDetails::OnStateComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	if (Selection.IsValid())
	{
		FScopedTransaction LocalTransaction(LOCTEXT("OnStateSelectionChanged", "Change State"));
		CustomInstance->Modify();
		CustomInstance->SetCurrentState(*Selection);
		UpdateInstance();

		// Non-continuous change: collect garbage.
		GEngine->ForceGarbageCollection();
	}
}


// INSTANCE PROFILE SELECTOR -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateInstanceProfileSelector()
{
	UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();
	const int32 ProfileIdx = CustomInstance->GetPrivate()->SelectedProfileIndex;

	ParameterProfileNames.Emplace(MakeShared<FString>("None"));
	TSharedPtr<FString> CurrentProfileName = ParameterProfileNames.Last();

	for (FProfileParameterDat& Profile : CustomInstance->GetCustomizableObject()->GetPrivate()->GetInstancePropertiesProfiles())
	{
		ParameterProfileNames.Emplace(MakeShared<FString>(Profile.ProfileName));

		if (ProfileIdx != INDEX_NONE)
		{
			const FProfileParameterDat CurrentInstanceProfile = CustomizableObject->GetPrivate()->GetInstancePropertiesProfiles()[ProfileIdx];

			if (Profile.ProfileName == CurrentInstanceProfile.ProfileName)
			{
				CurrentProfileName = ParameterProfileNames.Last();
			}
		}
	}

	ParameterProfileNames.Sort([](const TSharedPtr<FString>& LHS, const TSharedPtr<FString>& RHS)
	{
		return *LHS < *RHS;
	});

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextComboBox)
			.OptionsSource(&ParameterProfileNames)
			.OnSelectionChanged(this, &FCustomizableInstanceDetails::OnProfileSelectedChanged)
			.ToolTipText(FText::FromString(ParameterProfileNames.Num() > 1 ? FString("Select an existing profile") : FString("No profiles are available")))
			.InitiallySelectedItem(CurrentProfileName)
			.IsEnabled(ParameterProfileNames.Num() > 1)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("AddButtonLabel", " + "))
			.ToolTipText(FText::FromString(CustomInstance->HasAnyParameters() ? FString("Add new profile") : FString("Create a profile functionality is not available, no parameters were found.")))
			.IsEnabled(CustomInstance->HasAnyParameters())
			.IsFocusable(false)
			.OnClicked(this, &FCustomizableInstanceDetails::CreateParameterProfileWindow)
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("RemoveButtonLabel", " - "))
			.ToolTipText(FText::FromString(CustomInstance->HasAnyParameters() ? FString("Delete selected profile") : FString("Delete selected profile functionality is not available, no profile is selected.")))
			.IsEnabled(CustomInstance->GetPrivate()->SelectedProfileIndex != INDEX_NONE)
			.IsFocusable(false)
			.OnClicked(this, &FCustomizableInstanceDetails::RemoveParameterProfile)
		];
}


class SProfileParametersWindow : public SWindow
{
public:
	SLATE_BEGIN_ARGS(SProfileParametersWindow) {}
		SLATE_ARGUMENT(FText, DefaultAssetPath)
		SLATE_ARGUMENT(FText, DefaultFileName)
	SLATE_END_ARGS()

	SProfileParametersWindow() : UserResponse(EAppReturnType::Cancel) {}
	void Construct(const FArguments& InArgs);

public:
	/** Displays the dialog in a blocking fashion */
	EAppReturnType::Type ShowModal();

	/** FileName getter */
	FString GetFileName() const { return FileName.ToString(); }

protected:

	//FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);
	FReply OnButtonClick(EAppReturnType::Type ButtonID);
	void OnNameChange(const FText& NewName, ETextCommit::Type CommitInfo);
	
	EAppReturnType::Type UserResponse;
	FText AssetPath;
	FText FileName;

public:
	TWeakObjectPtr<UCustomizableObjectInstance> CustomInstance;

	FCustomizableInstanceDetails* InstanceDetails = nullptr;
};


FReply FCustomizableInstanceDetails::CreateParameterProfileWindow()
{
	const TSharedRef<SProfileParametersWindow> FolderDlg =
		SNew(SProfileParametersWindow)
		.DefaultAssetPath(LOCTEXT("DefaultAssethPath", "/Game"))
		.DefaultFileName(FText::FromString("ProfileParameterData"));

	FolderDlg->CustomInstance = CustomInstance;
	FolderDlg->InstanceDetails = this;
	FolderDlg->Construct(SProfileParametersWindow::FArguments());
	FolderDlg->ShowModal();

	return FReply::Handled();
}


FReply FCustomizableInstanceDetails::RemoveParameterProfile()
{
	UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();

	const int32 ProfileIdx = CustomInstance->GetPrivate()->SelectedProfileIndex;

	if (ProfileIdx == INDEX_NONE)
	{
		return FReply::Handled();
	}

	//TODO(Max):UE-212345
	//BeginTransaction(LOCTEXT("OnStateSelectionChanged", "Remove Profile"), true);

	TArray<FProfileParameterDat>& Profiles = CustomizableObject->GetPrivate()->GetInstancePropertiesProfiles();

	Profiles.RemoveAt(ProfileIdx);
	CustomInstance->GetPrivate()->SelectedProfileIndex = INDEX_NONE;
	CustomizableObject->Modify();

	UpdateInstance();

	//TODO(Max):UE-212345
	//EndTransaction();

	// Non-continuous change: collect garbage.
	GEngine->ForceGarbageCollection();

	return FReply::Handled();
}


void FCustomizableInstanceDetails::OnProfileSelectedChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo)
{
	if (Selection.IsValid())
	{
		const int32 ProfileIdx = CustomInstance->GetPrivate()->SelectedProfileIndex;
		if (CustomInstance->GetPrivate()->IsSelectedParameterProfileDirty())
		{
			CustomInstance->GetPrivate()->SaveParametersToProfile(ProfileIdx);
		}

		//TODO(Max):UE-212345
		//BeginTransaction(LOCTEXT("OnStateSelectionChanged", "Select Profile"), true);

		if (*Selection == "None")
		{
			CustomInstance->GetPrivate()->SelectedProfileIndex = INDEX_NONE;
		}
		else
		{
			//Set selected profile
			TArray<FProfileParameterDat>& Profiles = CustomInstance->GetCustomizableObject()->GetPrivate()->GetInstancePropertiesProfiles();
			for (int32 Idx = 0; Idx < Profiles.Num(); ++Idx)
			{
				if (Profiles[Idx].ProfileName == *Selection)
				{
					CustomInstance->GetPrivate()->SelectedProfileIndex = Idx;
					break;
				}
			}
		}

		CustomInstance->GetPrivate()->LoadParametersFromProfile(CustomInstance->GetPrivate()->SelectedProfileIndex);
		UpdateInstance();

		//TODO(Max):UE-212345
		//EndTransaction();

		// Non-continuous change: collect garbage.
		GEngine->ForceGarbageCollection();
	}
}


void FCustomizableInstanceDetails::OnShowOnlyRuntimeSelectionChanged(ECheckBoxState InCheckboxState)
{
	CustomInstance->GetPrivate()->bShowOnlyRuntimeParameters = InCheckboxState == ECheckBoxState::Checked;
	Refresh();
}


void FCustomizableInstanceDetails::OnShowOnlyRelevantSelectionChanged(ECheckBoxState InCheckboxState)
{
	CustomInstance->GetPrivate()->bShowOnlyRelevantParameters = InCheckboxState == ECheckBoxState::Checked;
	Refresh();
}


void FCustomizableInstanceDetails::OnUseUISectionsSelectionChanged(ECheckBoxState InCheckboxState)
{
	CustomInstance->GetPrivate()->bShowUISections = InCheckboxState == ECheckBoxState::Checked;
	Refresh();
}

void FCustomizableInstanceDetails::OnUseUIThumbnailsSelectionChanged(ECheckBoxState InCheckboxState)
{
	CustomInstance->GetPrivate()->bShowUIThumbnails = InCheckboxState == ECheckBoxState::Checked;
	Refresh();
}


// PARAMETER INFO STRUCT -----------------------------------------------------------------------------------------------------------------

struct FParameterInfo
{
	int32 ParamIndexInObject = -1;
	int32 ParamUIOrder = 0;
	FString ParamName = "";

	bool operator==(const FParameterInfo& Other)
	{
		return ParamName == Other.ParamName;
	}

	friend bool operator<(const FParameterInfo& A, const FParameterInfo& B)
	{
		if (A.ParamUIOrder != B.ParamUIOrder)
		{
			return A.ParamUIOrder < B.ParamUIOrder;
		}
		else
		{
			return A.ParamName < B.ParamName;
		}
	}
};


// PARAMETERS WIDGET GENERATION -----------------------------------------------------------------------------------------------------------------

bool FCustomizableInstanceDetails::GenerateParametersView(IDetailCategoryBuilder& DetailsCategory)
{
	ParamChildren.Empty();
	ParamHasParent.Empty();
	GeneratedSections.Empty();
	DynamicBrushes.Empty();

	TArray<FParameterInfo> ParametersTree;
	UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();

	if (!CustomizableObject)
	{
		return false;
	}

	bool bParametersHiddenRuntime = false;

	if (CustomInstance->GetPrivate()->bShowOnlyRuntimeParameters)
	{
		const int32 NumStateParameters = CustomizableObject->GetStateParameterCount(CustomInstance->GetPrivate()->GetState());

		if (NumStateParameters < CustomizableObject->GetParameterCount())
		{
			bParametersHiddenRuntime = true;
		}

		for (int32 ParamIndexInState = 0; ParamIndexInState < NumStateParameters; ++ParamIndexInState)
		{
			FParameterInfo ParameterSortInfo;
			ParameterSortInfo.ParamIndexInObject = CustomizableObject->GetStateParameterIndex(CustomInstance->GetPrivate()->GetState(), ParamIndexInState);

			if (CustomInstance->IsParameterRelevant(ParameterSortInfo.ParamIndexInObject) && IsVisible(ParameterSortInfo.ParamIndexInObject))
			{
				ParameterSortInfo.ParamName = CustomizableObject->GetParameterName(ParameterSortInfo.ParamIndexInObject);
				ParameterSortInfo.ParamUIOrder = CustomizableObject->GetParameterUIMetadata(ParameterSortInfo.ParamName).UIOrder;
				ParametersTree.Add(ParameterSortInfo);
			}
		}

		ParametersTree.Sort();

		for (int32 ParamIndex = 0; ParamIndex < ParametersTree.Num(); ++ParamIndex)
		{
			const FParameterInfo& ParamInfo = ParametersTree[ParamIndex];

			if (CustomInstance->GetPrivate()->bShowUISections)
			{
				IDetailGroup* CurrentSection = GenerateParameterSection(DetailsCategory, *CustomizableObject, ParamInfo.ParamName);
				check(CurrentSection);

				if (!IsMultidimensionalProjector(ParamInfo.ParamIndexInObject))
				{
					GenerateWidgetRow(CurrentSection->AddWidgetRow(), *CustomizableObject, ParamInfo.ParamName, ParamInfo.ParamIndexInObject);
				}
				else
				{
					IDetailGroup* ProjectorGroup = &CurrentSection->AddGroup(FName(*ParamInfo.ParamName), FText::GetEmpty());

					// Call Order between the following lines maters.
					ParentsGroups.Add(ParamInfo.ParamName, ProjectorGroup);
					GenerateWidgetRow(ProjectorGroup->HeaderRow(), *CustomizableObject, ParamInfo.ParamName, ParamInfo.ParamIndexInObject);
				}
			}
			else
			{
				if (!IsMultidimensionalProjector(ParamInfo.ParamIndexInObject))
				{
					GenerateWidgetRow(DetailsCategory.AddCustomRow(FText::FromString(ParamInfo.ParamName)), *CustomizableObject, ParamInfo.ParamName, ParamInfo.ParamIndexInObject);
				}
				else
				{
					IDetailGroup* ProjectorGroup = &DetailsCategory.AddGroup(FName(*ParamInfo.ParamName), FText::GetEmpty());

					// Call Order between the following lines maters.
					ParentsGroups.Add(ParamInfo.ParamName, ProjectorGroup);
					GenerateWidgetRow(ProjectorGroup->HeaderRow(), *CustomizableObject, ParamInfo.ParamName, ParamInfo.ParamIndexInObject);
				}
			}
		}
	}
	else
	{
		const int32 NumObjectParameter = CustomizableObject->GetParameterCount();

		//TODO: get all parameters and sort, then make the next "for" use that sorted list as source of indexes
		for (int32 ParamIndexInObject = 0; ParamIndexInObject < NumObjectParameter; ++ParamIndexInObject)
		{
			FParameterInfo ParameterSortInfo;
			ParameterSortInfo.ParamIndexInObject = ParamIndexInObject;
			if ((!CustomInstance->GetPrivate()->bShowOnlyRelevantParameters || CustomInstance->IsParameterRelevant(ParameterSortInfo.ParamIndexInObject)) && IsVisible(ParameterSortInfo.ParamIndexInObject))
			{
				ParameterSortInfo.ParamName = CustomizableObject->GetParameterName(ParameterSortInfo.ParamIndexInObject);
				ParameterSortInfo.ParamUIOrder = CustomizableObject->GetParameterUIMetadata(ParameterSortInfo.ParamName).UIOrder;
				ParametersTree.Add(ParameterSortInfo);
			}
		}

		ParametersTree.Sort();

		for (int32 ParamIndexInObject = 0; ParamIndexInObject < ParametersTree.Num(); ++ParamIndexInObject)
		{
			FillChildrenMap(ParametersTree[ParamIndexInObject].ParamIndexInObject);
		}

		for (int32 ParamIndexInObject = 0; ParamIndexInObject < ParametersTree.Num(); ++ParamIndexInObject)
		{
			if (!ParamHasParent.Find(ParametersTree[ParamIndexInObject].ParamIndexInObject))
			{
				RecursivelyAddParamAndChildren(*CustomizableObject, ParametersTree[ParamIndexInObject].ParamIndexInObject, "", DetailsCategory);
			}
		}
	}

	return bParametersHiddenRuntime;
}


void FCustomizableInstanceDetails::RecursivelyAddParamAndChildren(const UCustomizableObject& CustomizableObject, const int32 ParamIndexInObject, const FString ParentName, IDetailCategoryBuilder& DetailsCategory)
{
	const FString ParamName = CustomizableObject.GetParameterName(ParamIndexInObject);	
	TArray<int32> Children;
	
	ParamChildren.MultiFind(ParamName, Children, true);

	if (ParentName.IsEmpty())
	{
		if (CustomInstance->GetPrivate()->bShowUISections)
		{
			IDetailGroup* CurrentSection = GenerateParameterSection(DetailsCategory, CustomizableObject, ParamName);
			check(CurrentSection);

			if (Children.Num() == 0 && !IsMultidimensionalProjector(ParamIndexInObject))
			{
				GenerateWidgetRow(CurrentSection->AddWidgetRow(), CustomizableObject, ParamName, ParamIndexInObject);
			}
			else
			{
				IDetailGroup* ParentGroup = &CurrentSection->AddGroup(FName(*ParamName), FText::GetEmpty());
				
				// Call Order between the following lines maters.
				ParentsGroups.Add(ParamName, ParentGroup);
				GenerateWidgetRow(ParentGroup->HeaderRow(), CustomizableObject, ParamName, ParamIndexInObject);
			}
		}
		else
		{
			if (Children.Num() == 0 && !IsMultidimensionalProjector(ParamIndexInObject))
			{
				GenerateWidgetRow(DetailsCategory.AddCustomRow(FText::FromString(ParamName)), CustomizableObject, ParamName, ParamIndexInObject);
			}
			else
			{
				IDetailGroup* ParentGroup = &DetailsCategory.AddGroup(FName(*ParamName), FText::GetEmpty());
				
				// Call Order between the following lines maters.
				ParentsGroups.Add(ParamName, ParentGroup);
				GenerateWidgetRow(ParentGroup->HeaderRow(), CustomizableObject, ParamName, ParamIndexInObject);
			}
		}
	}
	else
	{
		IDetailGroup* ParentGroup = *ParentsGroups.Find(ParentName);

		if (Children.Num() == 0 && !IsMultidimensionalProjector(ParamIndexInObject))
		{
			GenerateWidgetRow(ParentGroup->AddWidgetRow(), CustomizableObject, ParamName, ParamIndexInObject);
		}
		else
		{
			IDetailGroup* ChildGroup = &ParentGroup->AddGroup(FName(*ParamName), FText::GetEmpty());

			// Call Order between the following lines maters
			ParentsGroups.Add(ParamName, ChildGroup);
			GenerateWidgetRow(ParentGroup->HeaderRow(), CustomizableObject, ParamName, ParamIndexInObject);
		}
	}

	for (const int32 ChildIndexInObject : Children)
	{
		RecursivelyAddParamAndChildren(CustomizableObject, ChildIndexInObject, ParamName, DetailsCategory);
	}
}


void FCustomizableInstanceDetails::FillChildrenMap(int32 ParamIndexInObject)
{
	const UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();
	const FString& ParamName = CustomizableObject->GetParameterName(ParamIndexInObject);
	FMutableParamUIMetadata UIMetadata = CustomizableObject->GetParameterUIMetadata(ParamName);

	if (const FString* ParentName = UIMetadata.ExtraInformation.Find(FString("__ParentParamName")))
	{
		ParamChildren.Add(*ParentName, ParamIndexInObject);
		ParamHasParent.Add(ParamIndexInObject, true);
	}
}


bool FCustomizableInstanceDetails::IsVisible(int32 ParamIndexInObject)
{
	const UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();
	const FString& ParamName = CustomizableObject->GetParameterName(ParamIndexInObject);
	FMutableParamUIMetadata UIMetadata = CustomizableObject->GetParameterUIMetadata(ParamName);
	const FString* ParentName = UIMetadata.ExtraInformation.Find(FString("__ParentParamName"));

	const bool IsAProjectorParam = ParamName.EndsWith(NUM_LAYERS_PARAMETER_POSTFIX)
		|| (ParamName.EndsWith(IMAGE_PARAMETER_POSTFIX) && CustomizableObject->IsParameterMultidimensional(ParamIndexInObject))
		|| (ParamName.EndsWith(OPACITY_PARAMETER_POSTFIX) && CustomizableObject->IsParameterMultidimensional(ParamIndexInObject))
		|| (ParamName.EndsWith(POSE_PARAMETER_POSTFIX));

	if (!IsAProjectorParam && ParentName && CustomInstance->GetPrivate()->bShowOnlyRelevantParameters)
	{
		const FString* Value = UIMetadata.ExtraInformation.Find(FString("__DisplayWhenParentValueEquals"));

		if (Value && CustomizableObject->FindParameter(*ParentName) != INDEX_NONE && CustomInstance->GetIntParameterSelectedOption(*ParentName) != *Value)
		{
			return false;
		}
	}

	return !IsAProjectorParam;
}


bool FCustomizableInstanceDetails::IsIntParameterFilteredOut(const UCustomizableObject& CustomizableObject, const FString& ParamName, const FString& ParamOption) const
{
	if (Filter.IsEmpty())
	{
		return false;
	}

	FMutableParamUIMetadata Metadata = CustomizableObject.GetIntParameterOptionUIMetadata(ParamName, ParamOption);

	switch (FilterType)
	{
	case EGameplayContainerMatchType::Any:
	{
		return !Metadata.GameplayTags.HasAny(Filter);
	}
	case EGameplayContainerMatchType::All:
	{
		return !Metadata.GameplayTags.HasAll(Filter);
	}
	default:
		unimplemented();
		return false;
	}
}



bool FCustomizableInstanceDetails::IsMultidimensionalProjector(int32 ParamIndexInObject)
{
	const UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();

	return CustomizableObject->GetParameterType(ParamIndexInObject) == EMutableParameterType::Projector && CustomInstance->GetCustomizableObject()->IsParameterMultidimensional(ParamIndexInObject);
}


IDetailGroup* FCustomizableInstanceDetails::GenerateParameterSection(IDetailCategoryBuilder& DetailsCategory, const UCustomizableObject& CustomizableObject, const FString& ParamName)
{
	const FMutableParamUIMetadata UIMetadata = CustomizableObject.GetParameterUIMetadata(ParamName);
	const FString SectionName = UIMetadata.UISectionName.IsEmpty() ? "Miscellaneous" : UIMetadata.UISectionName;
	IDetailGroup* CurrentSection = nullptr;

	if (GeneratedSections.Contains(SectionName))
	{
		CurrentSection = *GeneratedSections.Find(SectionName);
	}

	if (!CurrentSection)
	{
		CurrentSection = &DetailsCategory.AddGroup(FName(SectionName), FText::FromString(SectionName));

		GeneratedSections.Add(SectionName, CurrentSection);
	}

	return CurrentSection;
}


void FCustomizableInstanceDetails::GenerateWidgetRow(FDetailWidgetRow& WidgetRow, const UCustomizableObject& CustomizableObject, const FString& ParamName, const int32 ParamIndexInObject)
{
	WidgetRow.NameContent()
	[
		SNew(STextBlock)
		.Text(FText::FromString(ParamName))
	]
	.ValueContent()
	.HAlign(EHorizontalAlignment::HAlign_Fill)
	.VAlign(EVerticalAlignment::VAlign_Fill)
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.HAlign(HAlign_Fill)
		.Padding(0.0f, 5.0f, 0.0f, 5.0f)
		[
			GenerateParameterWidget(CustomizableObject, ParamName, ParamIndexInObject)
		]
	]
	.OverrideResetToDefault(FResetToDefaultOverride::Create(FSimpleDelegate::CreateSP(this, &FCustomizableInstanceDetails::OnResetParameterButtonClicked, ParamIndexInObject)))
	.FilterString(FText::FromString(ParamName));
}


TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateParameterWidget(const UCustomizableObject& CustomizableObject, const FString& ParamName, const int32 ParamIndexInObject)
{
	switch (CustomizableObject.GetParameterType(ParamIndexInObject))
	{
		case EMutableParameterType::Bool:
		{
			return GenerateBoolWidget(ParamName);
		}
		case EMutableParameterType::Float:
		{
			return GenerateFloatWidget(CustomizableObject, ParamName);
		}
		case EMutableParameterType::Color:
		{
			return GenerateColorWidget(ParamName);
		}
		case EMutableParameterType::Texture:
		{
			return GenerateTextureWidget(CustomizableObject, ParamName);
		}
		case EMutableParameterType::Transform:
		{
			return GenerateTransformWidget(ParamName);
		}
		case EMutableParameterType::Projector:
		{
			bool bIsParamMultidimensional = CustomInstance->GetCustomizableObject()->IsParameterMultidimensional(ParamIndexInObject);

			if (!bIsParamMultidimensional)
			{
				return GenerateSimpleProjector(ParamName);
			}
			else 
			{
				return GenerateMultidimensionalProjector(CustomizableObject, ParamName, ParamIndexInObject);
			}
		}
		case EMutableParameterType::Int:
		{
			return GenerateIntWidget(CustomizableObject, ParamName, ParamIndexInObject);
		}
		case EMutableParameterType::None:
		{
			return SNew(STextBlock).Text(LOCTEXT("ParameterTypeNotSupported_Text", "Parameter Type not supported"));
		}
	}

	return SNew(STextBlock);
}


// INT PARAMETERS -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateIntWidget(const UCustomizableObject& CustomizableObject, const FString& ParamName, const int32 ParamIndexInObject)
{
	const bool bMultidimensional = CustomInstance->GetCustomizableObject()->IsParameterMultidimensional(ParamIndexInObject);
	if (bMultidimensional)
	{
		return SNew(STextBlock).Text(LOCTEXT("MultidimensionalINTParameter_Text", "Multidimensional INT Parameter not supported"));
	}

	const int32 NumValues = CustomizableObject.GetIntParameterNumOptions(ParamIndexInObject);
	if (!NumValues)
	{
		return SNew(STextBlock).Text(LOCTEXT("NoAvailableOptions", "No Available Options"));
	}
	
	TSharedPtr<TArray<TSharedPtr<FString>>>* FoundOptions = IntParameterOptions.Find(ParamIndexInObject);
	TArray<TSharedPtr<FString>>& OptionNamesAttribute = FoundOptions && FoundOptions->IsValid() ? 
														*FoundOptions->Get() :
														*(IntParameterOptions.Add(ParamIndexInObject, MakeShared<TArray<TSharedPtr<FString>>>()).Get());

	OptionNamesAttribute.Empty();

	const FString SelectedOption = CustomInstance->GetIntParameterSelectedOption(ParamName, INDEX_NONE);

	// Tooltip for the selected option
	FString ToolTipText = FString("None");
	if (const FString* Identifier = CustomizableObject.GetPrivate()->GetModelResources().GroupNodeMap.FindKey(FCustomizableObjectIdPair(ParamName, SelectedOption)))
	{
		if (const FString* CustomizableObjectPath = CustomizableObject.GetPrivate()->GetModelResources().CustomizableObjectPathMap.Find(*Identifier))
		{
			ToolTipText = *CustomizableObjectPath;
		}
	}

	TSharedPtr<FString> SelectedOptionString;
	for (int32 i = 0; i < NumValues; ++i)
	{
		const FString PossibleValue = CustomizableObject.GetIntParameterAvailableOption(ParamIndexInObject, i);

		if (PossibleValue == SelectedOption) // Always add the selected option, even if it should be filtered.
		{
			SelectedOptionString = TSharedPtr<FString>(new FString(PossibleValue));
			OptionNamesAttribute.Add(SelectedOptionString);
		}

		else if (!IsIntParameterFilteredOut(CustomizableObject, ParamName, PossibleValue))
		{
			OptionNamesAttribute.Add(TSharedPtr<FString>(new FString(PossibleValue)));
		}
	}

	return SNew(SSearchableComboBox)
		.ToolTipText(FText::FromString(ToolTipText))
		.OptionsSource(&OptionNamesAttribute)
		.InitiallySelectedItem(SelectedOptionString)
		.Method(EPopupMethod::UseCurrentWindow)
		.OnSelectionChanged(this, &FCustomizableInstanceDetails::OnIntParameterComboBoxChanged, ParamName)
		.OnGenerateWidget(this, &FCustomizableInstanceDetails::OnGenerateWidgetIntParameter, ParamName)
		.Content()
		[
			SNew(STextBlock)
				.Text(FText::FromString(*SelectedOption))
		];
}


TSharedRef<SWidget> FCustomizableInstanceDetails::OnGenerateWidgetIntParameter(TSharedPtr<FString> OptionName, const FString ParameterName)
{
	// Final widget
	TSharedPtr<SHorizontalBox> IntWidgetBox = SNew(SHorizontalBox);

	if (CustomInstance->GetPrivate()->bShowUIThumbnails)
	{
		bool bUsesCustomThumbnail = false;

		// Asset with the thumbnail info
		FAssetData AssetData;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

		// Metadata of the represented int option
		const FMutableParamUIMetadata ParameterMetadata = CustomInstance->GetCustomizableObject()->GetIntParameterOptionUIMetadata(ParameterName, *OptionName);

		// Custom thumbnail has preference
		if (!ParameterMetadata.UIThumbnail.IsNull())
		{
			// Custom Thumbnail
			AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ParameterMetadata.UIThumbnail.ToSoftObjectPath());

			if (AssetData.IsValid())
			{
				if (UTexture2D* UIThumbnail = Cast<UTexture2D>(AssetData.GetAsset()))
				{
					// We need to store the generated texture.
					TSharedRef<FDeferredCleanupSlateBrush> Brush = FDeferredCleanupSlateBrush::CreateBrush(UIThumbnail, FVector2D(68.f, 68.0f), /* Texture size (64) + padding (4) */
						FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), ESlateBrushTileType::NoTile, ESlateBrushImageType::Linear);
					DynamicBrushes.Add(Brush);

					IntWidgetBox->AddSlot().AutoWidth()
					[
						SNew(SImage).Image(Brush->GetSlateBrush())
					];

					bUsesCustomThumbnail = true;
				}
			}
		}

		if (!bUsesCustomThumbnail)
		{
			// Asset thumbnail
			AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(ParameterMetadata.EditorUIThumbnailObject.ToSoftObjectPath());

			// We don't need to check if the asset data is valid here. We want to use the default thumbnail if there is no asset data.
			TSharedPtr<FAssetThumbnail> Thumbnail = MakeShareable(new FAssetThumbnail(AssetData, 64, 64, UThumbnailManager::Get().GetSharedThumbnailPool()));
			FAssetThumbnailConfig ThumbnailConfig;

			ThumbnailConfig.ColorStripOrientation = EThumbnailColorStripOrientation::VerticalRightEdge;
			ThumbnailConfig.Padding = FMargin(2.0f); // Prevents overlap with rounded corners; this matches what the Content Browser tiles do

			IntWidgetBox->AddSlot().AutoWidth()
			[
				Thumbnail->MakeThumbnailWidget(ThumbnailConfig)
			];

			if (!AssetData.IsValid())
			{
				IntWidgetBox->GetSlot(0).SetFillWidth(2.0f);
				IntWidgetBox->GetSlot(0).SetMaxSize(68.0f);
			}
		}
	}

	IntWidgetBox->AddSlot().VAlign(EVerticalAlignment::VAlign_Center).Padding(10.0f, 0.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock).Text(FText::FromString(*OptionName.Get()))
	];

	return IntWidgetBox.ToSharedRef();
}


void FCustomizableInstanceDetails::OnIntParameterComboBoxChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, FString ParamName)
{
	if (Selection.IsValid())
	{
		FScopedTransaction LocalTransaction(FText::Format(LOCTEXT("OnIntParameterSet", "Set Int Parameter: {0}"), FText::FromString(ParamName)));
		CustomInstance->Modify();
		CustomInstance->SetIntParameterSelectedOption(ParamName, *Selection);
		UpdateInstance();

		// Non-continuous change: collect garbage.
		GEngine->ForceGarbageCollection();
	}
}


// FLOAT PARAMETERS -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateFloatWidget(const UCustomizableObject& CustomizableObject, const FString& ParamName)
{
	const FMutableParamUIMetadata& UIMetadata = CustomizableObject.GetParameterUIMetadata(ParamName);

	if (const TSoftObjectPtr<UObject>* FloatDecoratorAsset = UIMetadata.ExtraAssets.Find(UIMetadataKeyWords::FloatDecoratorName))
	{
		// Checking if there is an image decorator for the float slider:
		if (UTexture2D* DecoratorTexture = Cast<UTexture2D>(FloatDecoratorAsset->LoadSynchronous()))
		{
			TSharedRef<FDeferredCleanupSlateBrush> Brush = FDeferredCleanupSlateBrush::CreateBrush(DecoratorTexture, FVector2D(DecoratorTexture->GetSizeX(), 1.0f), FLinearColor(1.0f, 1.0f, 1.0f, 1.0f), ESlateBrushTileType::NoTile, ESlateBrushImageType::Linear);
			DynamicBrushes.Add(Brush);

			// Add slider with the decorator as background
			return SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SBox)
				.Padding(0.0f, 8.0f) //Helps to shrinks the image from the borders
				[
					SNew(SImage)
					.Image(Brush->GetSlateBrush())
				]
			]

			+ SOverlay::Slot()
			[
				SNew(SSlider)
				.Value(this, &FCustomizableInstanceDetails::GetFloatParameterValue, ParamName, -1)
				.MinValue(UIMetadata.MinimumValue)
				.MaxValue(UIMetadata.MaximumValue)
				.OnValueChanged(this, &FCustomizableInstanceDetails::OnFloatParameterChanged, ParamName, -1)
				.OnMouseCaptureBegin(this, &FCustomizableInstanceDetails::OnFloatParameterSliderBegin)
				.OnMouseCaptureEnd(this, &FCustomizableInstanceDetails::OnFloatParameterSliderEnd)
				.Style(&FAppStyle::Get().GetWidgetStyle<FSliderStyle>("ColorPicker.Slider"))
				.IndentHandle(false)
				.SliderBarColor(FLinearColor::Transparent)
			];
		}
	}

	return SNew(SSpinBox<float>)
		.Value(this, &FCustomizableInstanceDetails::GetFloatParameterValue, ParamName, -1)
		.MinValue(UIMetadata.MinimumValue)
		.MaxValue(UIMetadata.MaximumValue)
		.OnValueChanged(this, &FCustomizableInstanceDetails::OnFloatParameterChanged, ParamName, -1)
		.OnValueCommitted(this, &FCustomizableInstanceDetails::OnFloatParameterCommited, ParamName, -1)
		.OnBeginSliderMovement(this, &FCustomizableInstanceDetails::OnFloatParameterSliderBegin)
		.OnEndSliderMovement(this, &FCustomizableInstanceDetails::OnFloatParameterSpinBoxEnd, ParamName, -1);
}


float FCustomizableInstanceDetails::GetFloatParameterValue(FString ParamName, int32 RangeIndex) const
{
	if (CustomInstance->GetCustomizableObject()->GetPrivate()->IsLocked()) // TODO Move, if necessary, to GetFloatParameterSelectedOption. UE-224815
	{
		// Prevent crashing if polling the float value during CO compilation
		return -1.f;
	}

	if (RangeIndex == INDEX_NONE)
	{
		return CustomInstance->GetFloatParameterSelectedOption(ParamName, RangeIndex);
	}
	else //multidimensional
	{
		// We may have deleted a range but the Instance has not been updated yet
		if (CustomInstance->GetFloatValueRange(ParamName) > RangeIndex)
		{
			return CustomInstance->GetFloatParameterSelectedOption(ParamName, RangeIndex);
		}
	}

	return 0;
}


void FCustomizableInstanceDetails::OnFloatParameterChanged(float Value, FString ParamName, int32 RangeIndex)
{
	float OldValue = CustomInstance->GetFloatParameterSelectedOption(ParamName, RangeIndex);

	if (OldValue != Value)
	{
		//No transaction is needed here as this is called when the transaction has already started
		CustomInstance->SetFloatParameterSelectedOption(ParamName, Value, RangeIndex);
		UpdateInstance();
	}
}


void FCustomizableInstanceDetails::OnFloatParameterSliderBegin()
{
	BeginTransaction(LOCTEXT("OnFloatParameterSliderBegin", "Set Float Slider"));
	bUpdatingSlider = true;
}


void FCustomizableInstanceDetails::OnFloatParameterSliderEnd()
{
	EndTransaction();
	bUpdatingSlider = false;
}


void FCustomizableInstanceDetails::OnFloatParameterSpinBoxEnd(float Value, FString ParamName, int32 RangeIndex)
{
	bUpdatingSlider = false;

	CustomInstance->SetFloatParameterSelectedOption(ParamName, Value, RangeIndex);
	UpdateInstance();
	EndTransaction();

	// Non-continuous change: collect garbage.
	GEngine->ForceGarbageCollection();
}


void FCustomizableInstanceDetails::OnFloatParameterCommited(float Value, ETextCommit::Type Type, const FString ParamName, int32 RangeIndex)
{
	if (Type == ETextCommit::OnEnter)
	{
		// Making sure that setting a float by text generates a transaction. OnSpinBoxSliderEnd is considered a floatcommit of type OnEnter (IDK why...)
		// and already generates a transaction when the Slid begins.
		if (!Transaction)
		{
			BeginTransaction(FText::Format(LOCTEXT("OnFloatParameterCommited", "Set Float Parameter: {0}"), FText::FromString(ParamName)));
		}

		CustomInstance->SetFloatParameterSelectedOption(ParamName, Value, RangeIndex);
		UpdateInstance();
		EndTransaction();

		// Non-continuous change: collect garbage.
		GEngine->ForceGarbageCollection();
	}
}


// TEXTURE PARAMETERS -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateTextureWidget(const UCustomizableObject& CustomizableObject, const FString& ParamName)
{
	const FName ParameterValue = CustomInstance->GetTextureParameterSelectedOption(ParamName);
	TSharedPtr<FString> InitiallySelected;

	// Look for the value index
	for (int32 ValueIndex = 0; ValueIndex < TextureParameterValueNames.Num(); ++ValueIndex)
	{
		if (ParameterValue == TextureParameterValues[ValueIndex])
		{
			InitiallySelected = TextureParameterValueNames[ValueIndex];
			break;
		}
	}

	return SNew(STextComboBox)
		.OptionsSource(&TextureParameterValueNames)
		.InitiallySelectedItem(InitiallySelected)
		.OnSelectionChanged(this, &FCustomizableInstanceDetails::OnTextureParameterComboBoxSelectionChanged, ParamName);
}


void FCustomizableInstanceDetails::GenerateTextureParameterOptions()
{
	UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();

	for (const FCustomizableObjectTextureParameterValue& TextureParameter : CustomInstance->GetPrivate()->GetDescriptor().GetTextureParameters())
	{
		// Get default values.
		const FName& DefaultValue = CustomizableObject->GetTextureParameterDefaultValue(TextureParameter.ParameterName);

		if (!TextureParameterValues.Contains(DefaultValue))
		{
			TextureParameterValueNames.Add(MakeShareable(new FString(DefaultValue.ToString())));
			TextureParameterValues.Add(DefaultValue);
		}

		// Selected parameter value
		const FName& SelectedValue = TextureParameter.ParameterValue;

		if (!TextureParameterValues.Contains(SelectedValue))
		{
			TextureParameterValueNames.Add(MakeShareable(new FString(SelectedValue.ToString())));
			TextureParameterValues.Add(SelectedValue);
		}
	}

	// Get values from registered providers providers.
	TArray<FCustomizableObjectExternalTexture> Textures = UCustomizableObjectSystem::GetInstance()->GetTextureParameterValues();
	for (int i = 0; i < Textures.Num(); ++i)
	{
		const FName& Value = FName(Textures[i].Value);

		if (!TextureParameterValues.Contains(Value))
		{
			TextureParameterValueNames.Add(MakeShareable(new FString(Textures[i].Name)));
			TextureParameterValues.Add(Value);
		}
	}

	// Get values from TextureParameterDeclarations.
	for (TObjectPtr<UTexture2D> Declaration : CustomInstance->TextureParameterDeclarations)
	{
		if (!Declaration)
		{
			continue;
		}

		const FName Value = FName(Declaration.GetPathName());

		if (!TextureParameterValues.Contains(Value))
		{
			TextureParameterValueNames.Add(MakeShareable(new FString(Declaration.GetName())));
			TextureParameterValues.Add(Value);
		}
	}
}


void FCustomizableInstanceDetails::OnTextureParameterComboBoxSelectionChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, FString ParamName)
{
	if (!Selection.IsValid())
	{
		return;
	}

	const int32 Index = TextureParameterValueNames.Find(Selection);
	check(Index != INDEX_NONE);
	
	FScopedTransaction LocalTransaction(FText::Format(LOCTEXT("OnTextureParameterSelected", "Set Texture Parameter: {0}"), FText::FromString(ParamName)));
	CustomInstance->Modify();
	CustomInstance->SetTextureParameterSelectedOption(ParamName, TextureParameterValues[Index].ToString());
	UpdateInstance();

	// Non-continuous change: collect garbage.
	GEngine->ForceGarbageCollection();
}


// COLOR PARAMETERS -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateColorWidget(const FString& ParamName)
{
	return SNew(SColorBlock)
		.Color(this, &FCustomizableInstanceDetails::GetColorParameterValue, ParamName)
		.ShowBackgroundForAlpha(false)
		.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
		.UseSRGB(false)
		.OnMouseButtonDown(this, &FCustomizableInstanceDetails::OnColorBlockMouseButtonDown, ParamName)
		.CornerRadius(FVector4(4.0f, 4.0f, 4.0f, 4.0f));
}


FLinearColor FCustomizableInstanceDetails::GetColorParameterValue(FString ParamName) const
{
	return CustomInstance->GetColorParameterSelectedOption(ParamName);
}


FReply FCustomizableInstanceDetails::OnColorBlockMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FString ParamName)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return FReply::Unhandled();
	}

	FLinearColor Col = GetColorParameterValue(ParamName);

	FColorPickerArgs args;
	args.bIsModal = true;
	args.bUseAlpha = true;
	args.bOnlyRefreshOnMouseUp = false;
	args.InitialColor = Col;
	args.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &FCustomizableInstanceDetails::OnSetColorFromColorPicker, ParamName);
	OpenColorPicker(args);

	return FReply::Handled();
}


void FCustomizableInstanceDetails::OnSetColorFromColorPicker(FLinearColor NewColor, FString PickerParamName)
{
	FScopedTransaction LocalTransaction(FText::Format(LOCTEXT("SetColorParameter", "Set Color Parameter: {0}"), FText::FromString(PickerParamName)));
	CustomInstance->Modify();
	CustomInstance->SetColorParameterSelectedOption(PickerParamName, NewColor);
	UpdateInstance();
}

// TRANSFORM PARAMETERS -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateTransformWidget(const FString& ParamName)
{
	auto OnLocationChanged = [this, ParamName](double Value, EAxis::Type Axis)
	{
		FTransform Transform = GetTransformParameterValue(ParamName);
		FVector Location = Transform.GetLocation();
		if (!FMath::IsNearlyEqualByULP(Value, Location.GetComponentForAxis(Axis)))
		{
			Location.SetComponentForAxis(Axis, Value);
			Transform.SetLocation(Location);
			OnTransformParameterChanged(Transform, ParamName);
		}
	};
	auto OnLocationCommitted = [this, ParamName](double Value, ETextCommit::Type Type, EAxis::Type Axis)
	{
		FTransform Transform = GetTransformParameterValue(ParamName);
		FVector Location = Transform.GetLocation();
		if (!FMath::IsNearlyEqualByULP(Value, Location.GetComponentForAxis(Axis)))
		{
			Location.SetComponentForAxis(Axis, Value);
			Transform.SetLocation(Location);
			OnTransformParameterCommitted(Transform, Type, ParamName);
		}
	};

	auto OnRotationChanged = [this, ParamName](double Value, EAxis::Type Axis)
	{
		FTransform Transform = GetTransformParameterValue(ParamName);
		FRotator Rotation = Transform.Rotator();
		Rotation.SetComponentForAxis(Axis, Value);
		
		if (!Transform.Rotator().Equals(Rotation))
		{
			Transform.SetRotation(Rotation.Quaternion());
			OnTransformParameterChanged(Transform, ParamName);
		}
	};
	auto OnRotationCommitted = [this, ParamName](double Value, ETextCommit::Type Type, EAxis::Type Axis)
	{
		FTransform Transform = GetTransformParameterValue(ParamName);
		FRotator Rotation = Transform.Rotator();
		Rotation.SetComponentForAxis(Axis, Value);
		
		if (!Transform.Rotator().Equals(Rotation))
		{
			Transform.SetRotation(Rotation.Quaternion());
			OnTransformParameterCommitted(Transform, Type, ParamName);
		}
	};
	
	auto OnScaleChanged = [this, ParamName](double Value, EAxis::Type Axis)
	{
		FTransform Transform = GetTransformParameterValue(ParamName);
		FVector Scale = Transform.GetScale3D();
		if (!FMath::IsNearlyEqualByULP(Value, Scale.GetComponentForAxis(Axis)))
		{
			Scale.SetComponentForAxis(Axis, Value);
			Transform.SetScale3D(Scale);
			OnTransformParameterChanged(Transform, ParamName);
		}
	};
	auto OnScaleCommitted = [this, ParamName](double Value, ETextCommit::Type Type, EAxis::Type Axis)
	{
		FTransform Transform = GetTransformParameterValue(ParamName);
		FVector Scale = Transform.GetScale3D();
		if (!FMath::IsNearlyEqualByULP(Value, Scale.GetComponentForAxis(Axis)))
		{
			Scale.SetComponentForAxis(Axis, Value);
			Transform.SetScale3D(Scale);
			OnTransformParameterCommitted(Transform, Type, ParamName);
		}
	};
	
	
	return SNew(SGridPanel)
		.FillColumn(1, 1)
		+ SGridPanel::Slot(0, 0)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Transform_Location", "Location"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+ SGridPanel::Slot(1, 0)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		.HAlign(HAlign_Fill)
		[
			SNew(SNumericVectorInputBox<double>)
			.bColorAxisLabels(true)
			.AllowSpin(true)
			.X_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).GetLocation().X; })
			.Y_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).GetLocation().Y; })
			.Z_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).GetLocation().Z; })
			.OnXChanged_Lambda([OnLocationChanged](float Value) { OnLocationChanged(Value, EAxis::X); })
			.OnYChanged_Lambda([OnLocationChanged](float Value) { OnLocationChanged(Value, EAxis::Y); })
			.OnZChanged_Lambda([OnLocationChanged](float Value) { OnLocationChanged(Value, EAxis::Z); })
			.OnXCommitted_Lambda([OnLocationCommitted](float Value, ETextCommit::Type Type) { OnLocationCommitted(Value, Type, EAxis::X); })
			.OnYCommitted_Lambda([OnLocationCommitted](float Value, ETextCommit::Type Type) { OnLocationCommitted(Value, Type, EAxis::Y); })
			.OnZCommitted_Lambda([OnLocationCommitted](float Value, ETextCommit::Type Type) { OnLocationCommitted(Value, Type, EAxis::Z); })
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		
		+ SGridPanel::Slot(0, 1)
		.HAlign(EHorizontalAlignment::HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Transform_Rotation", "Rotation"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+ SGridPanel::Slot(1, 1)
		.HAlign(HAlign_Fill)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SNumericRotatorInputBox<double>)
			.bColorAxisLabels(true)
			.AllowSpin(true)
			.Roll_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).Rotator().Roll; })
			.Pitch_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).Rotator().Pitch; })
			.Yaw_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).Rotator().Yaw; })
			.OnRollChanged_Lambda([OnRotationChanged](float Value) { OnRotationChanged(Value, EAxis::X); })
			.OnPitchChanged_Lambda([OnRotationChanged](float Value) { OnRotationChanged(Value, EAxis::Y); })
			.OnYawChanged_Lambda([OnRotationChanged](float Value) { OnRotationChanged(Value, EAxis::Z); })
			.OnRollCommitted_Lambda([OnRotationCommitted](float Value, ETextCommit::Type Type) { OnRotationCommitted(Value, Type, EAxis::X); })
			.OnPitchCommitted_Lambda([OnRotationCommitted](float Value, ETextCommit::Type Type) { OnRotationCommitted(Value, Type, EAxis::Y); })
			.OnYawCommitted_Lambda([OnRotationCommitted](float Value, ETextCommit::Type Type) { OnRotationCommitted(Value, Type, EAxis::Z); })
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]

		+ SGridPanel::Slot(0, 2)
		.HAlign(EHorizontalAlignment::HAlign_Right)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Transform_Scale", "Scale"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		+ SGridPanel::Slot(1, 2)
		.HAlign(HAlign_Fill)
		.Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SNumericVectorInputBox<double>)
			.bColorAxisLabels(true)
			.AllowSpin(false)
			.X_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).GetScale3D().X; })
			.Y_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).GetScale3D().Y; })
			.Z_Lambda([this, ParamName]() { return GetTransformParameterValue(ParamName).GetScale3D().Z; })

			.OnXChanged_Lambda([OnScaleChanged](float Value) { OnScaleChanged(Value, EAxis::X); })
			.OnYChanged_Lambda([OnScaleChanged](float Value) { OnScaleChanged(Value, EAxis::Y); })
			.OnZChanged_Lambda([OnScaleChanged](float Value) { OnScaleChanged(Value, EAxis::Z); })
			.OnXCommitted_Lambda([OnScaleCommitted](float Value, ETextCommit::Type Type) { OnScaleCommitted(Value, Type, EAxis::X); })
			.OnYCommitted_Lambda([OnScaleCommitted](float Value, ETextCommit::Type Type) { OnScaleCommitted(Value, Type, EAxis::Y); })
			.OnZCommitted_Lambda([OnScaleCommitted](float Value, ETextCommit::Type Type) { OnScaleCommitted(Value, Type, EAxis::Z); })
			.Font(IDetailLayoutBuilder::GetDetailFont())
		];
}

FTransform FCustomizableInstanceDetails::GetTransformParameterValue(const FString ParamName) const
{
	return CustomInstance->GetTransformParameterSelectedOption(ParamName);
}

void FCustomizableInstanceDetails::OnTransformParameterChanged(FTransform NewValue, const FString ParamName)
{
	FTransform OldValue = CustomInstance->GetTransformParameterSelectedOption(ParamName);

	if (!OldValue.Equals(NewValue))
	{
		//No transaction is needed here as this is called when the transaction has already started
		CustomInstance->SetTransformParameterSelectedOption(ParamName, NewValue);
		UpdateInstance();
	}
}

void FCustomizableInstanceDetails::OnTransformParameterCommitted(FTransform NewTransform, ETextCommit::Type Type, const FString ParamName)
{
	if (Type == ETextCommit::OnEnter)
	{
		if (!Transaction)
		{
			BeginTransaction(FText::Format(LOCTEXT("OnTransformParameterCommited", "Set Transform Parameter: {0}"), FText::FromString(ParamName)));
		}

		CustomInstance->SetTransformParameterSelectedOption(ParamName, NewTransform);
		UpdateInstance();
		EndTransaction();

		// Non-continuous change: collect garbage.
		GEngine->ForceGarbageCollection();
	}

}


// PROJECTOR PARAMETERS -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateSimpleProjector(const FString& ParamName)
{
	const TSharedPtr<ICustomizableObjectInstanceEditor> Editor = GetEditorChecked();
	const UProjectorParameter* ProjectorParameter = Editor->GetProjectorParameter();
	const bool bSelectedProjector = ProjectorParameter->IsProjectorSelected(ParamName);

	TSharedPtr<SButton> Button;
	TSharedPtr<SHorizontalBox> SimpleProjectorBox;

	SAssignNew(SimpleProjectorBox,SHorizontalBox)
	+ SHorizontalBox::Slot()
	.FillWidth(0.25f)
	[
		SAssignNew(Button, SButton)
		.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorSelectChanged, ParamName, -1)
		.HAlign(HAlign_Center)
		.Content()
		[
			SNew(STextBlock)
			.Text(bSelectedProjector ? LOCTEXT("Unselect Projector", "Unselect Projector") : LOCTEXT("Select Projector", "Select Projector"))
			.ToolTipText(bSelectedProjector ? LOCTEXT("Unselect Projector", "Unselect Projector") : LOCTEXT("Select Projector", "Select Projector"))
			.Font(LayoutBuilder.Pin()->GetDetailFont())
		]
	]

	+ SHorizontalBox::Slot()
	.FillWidth(0.25f)
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorCopyTransform, ParamName, -1)
		.Content()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CopyTransform_Text", "Copy Transform"))
			.ToolTipText(LOCTEXT("CopyTransform_Tooltip", "Copy Transform"))
			.Font(LayoutBuilder.Pin()->GetDetailFont())
		]
	]

	+ SHorizontalBox::Slot()
	.FillWidth(0.25f)
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorPasteTransform, ParamName, -1)
		.Content()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PasteTransform_Text", "Paste Transform"))
			.ToolTipText(LOCTEXT("PasteTransform_Tooltip", "Paste Transform"))
			.Font(LayoutBuilder.Pin()->GetDetailFont())
		]
	]

	+ SHorizontalBox::Slot()
	.FillWidth(0.25f)
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorResetTransform, ParamName, -1)
		.Content()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ResetTransform_Text", "Reset Transform"))
			.ToolTipText(LOCTEXT("ResetTransform_Tooltip", "Reset Transform"))
			.Font(LayoutBuilder.Pin()->GetDetailFont())
		]
	];

	Button->SetBorderBackgroundColor(bSelectedProjector ? FLinearColor::Green : FLinearColor::White);

	return SimpleProjectorBox.ToSharedRef();
}


FReply FCustomizableInstanceDetails::OnProjectorSelectChanged(const FString ParamName, const int32 RangeIndex) const
{
	const TSharedPtr<ICustomizableObjectInstanceEditor> Editor = GetEditorChecked();

	const UProjectorParameter* ProjectorParameter = Editor->GetProjectorParameter();
	if (ProjectorParameter->IsProjectorSelected(ParamName, RangeIndex))
	{
		Editor->HideGizmo();
	}
	else
	{
		Editor->ShowGizmoProjectorParameter(ParamName, RangeIndex);
	}

	return FReply::Handled();
}


FReply FCustomizableInstanceDetails::OnProjectorCopyTransform(const FString ParamName, const int32 RangeIndex) const
{
	const int32 ParameterIndexInObject = CustomInstance->GetCustomizableObject()->FindParameter(ParamName);
	const int32 ProjectorParamIndex = CustomInstance->FindProjectorParameterNameIndex(ParamName);

	if ((ParameterIndexInObject >= 0) && (ProjectorParamIndex >= 0))
	{
		const TArray<FCustomizableObjectProjectorParameterValue>& ProjectorParameters = CustomInstance->GetPrivate()->GetDescriptor().GetProjectorParameters();
		FCustomizableObjectProjector Value;

		if (RangeIndex == -1)
		{
			Value = ProjectorParameters[ProjectorParamIndex].Value;
		}
		else if (ProjectorParameters[ProjectorParamIndex].RangeValues.IsValidIndex(RangeIndex))
		{
			Value = ProjectorParameters[ProjectorParamIndex].RangeValues[RangeIndex];
		}
		else
		{
			check(false);
		}

		const UScriptStruct* Struct = Value.StaticStruct();
		FString Output = TEXT("");
		Struct->ExportText(Output, &Value, nullptr, nullptr, (PPF_ExportsNotFullyQualified | PPF_Copy | PPF_Delimited | PPF_IncludeTransient), nullptr);

		FPlatformApplicationMisc::ClipboardCopy(*Output);
	}

	return FReply::Handled();
}


FReply FCustomizableInstanceDetails::OnProjectorPasteTransform(const FString ParamName, const int32 RangeIndex)
{
	FString ClipboardText;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardText);
	FCustomizableObjectProjector DefaultValue;
	UScriptStruct* Struct = DefaultValue.StaticStruct();
	Struct->ImportText(*ClipboardText, &DefaultValue, nullptr, 0, GLog, GetPathNameSafe(Struct));

	FScopedTransaction LocalTransaction(LOCTEXT("PasteTransform", "Paste Projector Transform"));
	CustomInstance->Modify();
	CustomInstance->SetProjectorValue(ParamName,
		static_cast<FVector>(DefaultValue.Position),
		static_cast<FVector>(DefaultValue.Direction),
		static_cast<FVector>(DefaultValue.Up),
		static_cast<FVector>(DefaultValue.Scale),
		DefaultValue.Angle,
		RangeIndex);

	const TSharedPtr<ICustomizableObjectInstanceEditor> Editor = GetEditorChecked();

	Editor->ShowGizmoProjectorParameter(ParamName, RangeIndex);
	UpdateInstance();

	return FReply::Handled();
}


FReply FCustomizableInstanceDetails::OnProjectorResetTransform(const FString ParamName, const int32 RangeIndex)
{
	const FCustomizableObjectProjector DefaultValue = CustomInstance->GetCustomizableObject()->GetProjectorParameterDefaultValue(ParamName);

	FScopedTransaction LocalTransaction(LOCTEXT("ResetTransform", "Reset Projector Transform"));
	CustomInstance->Modify();
	CustomInstance->SetProjectorValue(ParamName,
		static_cast<FVector>(DefaultValue.Position),
		static_cast<FVector>(DefaultValue.Direction),
		static_cast<FVector>(DefaultValue.Up),
		static_cast<FVector>(DefaultValue.Scale),
		DefaultValue.Angle,
		RangeIndex);

	const TSharedPtr<ICustomizableObjectInstanceEditor> Editor = GetEditorChecked();

	Editor->ShowGizmoProjectorParameter(ParamName, RangeIndex);
	UpdateInstance();

	return FReply::Handled();
}


TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateMultidimensionalProjector(const UCustomizableObject& CustomizableObject, const FString& ParamName, const int32 ParamIndexInObject)
{
	const TSharedPtr<ICustomizableObjectInstanceEditor> Editor = GetEditorChecked();
	const TArray<FCustomizableObjectProjectorParameterValue>& ProjectorParameters = CustomInstance->GetPrivate()->GetDescriptor().GetProjectorParameters();
	const int32 ProjectorParamIndex = CustomInstance->FindProjectorParameterNameIndex(ParamName);

	check(ProjectorParamIndex < ProjectorParameters.Num());

	// Selected Pose UI
	const FString PoseSwitchEnumParamName = ParamName + POSE_PARAMETER_POSTFIX;
	const int32 PoseSwitchEnumParamIndexInObject = CustomizableObject.FindParameter(PoseSwitchEnumParamName);
	TSharedPtr<SVerticalBox> ProjectorBox = SNew(SVerticalBox);

	if (PoseSwitchEnumParamIndexInObject != INDEX_NONE)
	{
		const int32 NumPoseValues = CustomizableObject.GetIntParameterNumOptions(PoseSwitchEnumParamIndexInObject);

		TSharedPtr<TArray<TSharedPtr<FString>>>* FoundOptions = ProjectorParameterPoseOptions.Find(PoseSwitchEnumParamIndexInObject);
		TArray<TSharedPtr<FString>>& PoseOptionNamesAttribute = FoundOptions && FoundOptions->IsValid() ?
																*FoundOptions->Get()
																: *(ProjectorParameterPoseOptions.Add(PoseSwitchEnumParamIndexInObject, MakeShared<TArray<TSharedPtr<FString>>>()).Get());

		PoseOptionNamesAttribute.Empty();

		FString PoseValue = CustomInstance->GetIntParameterSelectedOption(PoseSwitchEnumParamName, -1);
		int32 PoseValueIndex = 0;

		for (int32 j = 0; j < NumPoseValues; ++j)
		{
			const FString PossibleValue = CustomizableObject.GetIntParameterAvailableOption(PoseSwitchEnumParamIndexInObject, j);
			if (PossibleValue == PoseValue)
			{
				PoseValueIndex = j;
			}

			PoseOptionNamesAttribute.Add(MakeShared<FString>(PossibleValue));
		}

		ProjectorBox->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		.FillHeight(10.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.FillWidth(0.45f)
			[
				SNew(SSearchableComboBox)
				.ToolTipText(LOCTEXT("Pose selector tooltip", "Select the skeletal mesh pose used for projection. This does not control the actual visual mesh pose in the viewport (or during gameplay for that matter). It has to be manually set. You can drag&drop a pose onto the preview viewport."))
				.OptionsSource(&PoseOptionNamesAttribute)
				.InitiallySelectedItem(PoseOptionNamesAttribute[PoseValueIndex])
				.Method(EPopupMethod::UseCurrentWindow)
				.OnSelectionChanged(this, &FCustomizableInstanceDetails::OnProjectorTextureParameterComboBoxChanged, PoseSwitchEnumParamName, -1)
				.OnGenerateWidget(this, &FCustomizableInstanceDetails::OnGenerateWidgetProjectorParameter)
				.Content()
				[
					SNew(STextBlock)
						.Text(FText::FromString(*PoseOptionNamesAttribute[PoseValueIndex]))
				]
			]

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			.FillWidth(0.3f)
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("Add Layer", "Add Layer"))
				.Text(LOCTEXT("Add Layer", "Add Layer"))
				.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorLayerAdded, ParamName)
				.HAlign(HAlign_Fill)
			]
		];
	}
	else
	{
		ProjectorBox->AddSlot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Fill)
		//.FillHeight(10.f)
		[
			SNew(SButton)
			.ToolTipText(LOCTEXT("Add Layer", "Add Layer"))
			.Text(LOCTEXT("Add Layer", "Add Layer"))
			.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorLayerAdded, ParamName)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
		];
	}

	const FString TextureSwitchEnumParamName = ParamName + IMAGE_PARAMETER_POSTFIX;
	const FString OpacitySliderParamName = ParamName + OPACITY_PARAMETER_POSTFIX;

	IDetailGroup* ProjectorGroup = *ParentsGroups.Find(ParamName);

	for (int32 RangeIndex = 0; RangeIndex < ProjectorParameters[ProjectorParamIndex].RangeValues.Num(); ++RangeIndex)
	{
		const int32 TextureSwitchEnumParamIndexInObject = CustomizableObject.FindParameter(TextureSwitchEnumParamName);
		check(TextureSwitchEnumParamIndexInObject >= 0); TSharedPtr<FString> CurrentStateName = nullptr;

		const UProjectorParameter* ProjectorParameter = Editor->GetProjectorParameter();
		const bool bSelectedProjector = ProjectorParameter->IsProjectorSelected(ParamName, RangeIndex);

		//Vertical box that owns all the layer properties
		TSharedPtr<SVerticalBox> LayerProperties = SNew(SVerticalBox);
		//Horizontal box that owns all the projector properties
		TSharedPtr<SHorizontalBox> ProjectorProperties;
		// Widget to set the opacity and remove a layer
		TSharedPtr<SHorizontalBox> OpacityRemoveWidget = SNew(SHorizontalBox);
		// Button Ptr needed to edit its style
		TSharedPtr<SButton> Button;

		SAssignNew(ProjectorProperties, SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(1, 0)
		[
			SNew(SBox)
			.MinDesiredWidth(115.f)
			.MaxDesiredWidth(115.f)
			[
				SAssignNew(Button, SButton)
				.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorSelectChanged, ParamName, RangeIndex)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Content()
				[
					SNew(STextBlock)
					.Text(bSelectedProjector ? LOCTEXT("Unselect Projector", "Unselect Projector") : LOCTEXT("Select Projector", "Select Projector"))
					.ToolTipText(bSelectedProjector ? LOCTEXT("Unselect Projector", "Unselect Projector") : LOCTEXT("Select Projector", "Select Projector"))
					.Justification(ETextJustify::Center)
					.Font(LayoutBuilder.Pin()->GetDetailFont())
				]
			]
		]

		+ SHorizontalBox::Slot()
		.Padding(1, 0)
		[
			SNew(SButton)
				.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorCopyTransform, ParamName, RangeIndex)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Content()
				[
					SNew(STextBlock)
						.ToolTipText(LOCTEXT("Copy Transform", "Copy Transform"))
						.Text(LOCTEXT("Copy Transform", "Copy Transform"))
						.AutoWrapText(true)
						.Justification(ETextJustify::Center)
						.Font(LayoutBuilder.Pin()->GetDetailFont())
				]
		]

		+ SHorizontalBox::Slot()
		.Padding(1, 0)
		[
			SNew(SButton)
				.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorPasteTransform, ParamName, RangeIndex)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Content()
				[
					SNew(STextBlock)
						.ToolTipText(LOCTEXT("Paste Transform", "Paste Transform"))
						.Text(LOCTEXT("Paste Transform", "Paste Transform"))
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
						.Font(LayoutBuilder.Pin()->GetDetailFont())
				]
		]

		+ SHorizontalBox::Slot()
		.Padding(1, 0)
		[
			SNew(SButton)
				.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorResetTransform, ParamName, RangeIndex)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Content()
				[
					SNew(STextBlock)
						.ToolTipText(LOCTEXT("Reset Transform", "Reset Transform"))
						.Text(LOCTEXT("Reset Transform", "Reset Transform"))
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
						.Font(LayoutBuilder.Pin()->GetDetailFont())
				]
		];

		Button->SetBorderBackgroundColor(bSelectedProjector ? FLinearColor::Green : FLinearColor::White);

		// If number of options is equal to 1, Mutable does not consider it multidimensional parameters
		int32 NumValues = CustomizableObject.GetIntParameterNumOptions(TextureSwitchEnumParamIndexInObject);
		FString Value = CustomizableObject.IsParameterMultidimensional(TextureSwitchEnumParamName) ? 
			CustomInstance->GetIntParameterSelectedOption(TextureSwitchEnumParamName, RangeIndex) : CustomInstance->GetIntParameterSelectedOption(TextureSwitchEnumParamName);

		TArray<TSharedPtr<FString>> OptionNamesAttribute;
		int32 ValueIndex = 0;

		for (int32 CandidateIndex = 0; CandidateIndex < NumValues; ++CandidateIndex)
		{
			FString PossibleValue = CustomizableObject.GetIntParameterAvailableOption(TextureSwitchEnumParamIndexInObject, CandidateIndex);
			if (PossibleValue == Value)
			{
				ValueIndex = CandidateIndex;
			}

			OptionNamesAttribute.Add(MakeShared<FString>(CustomizableObject.GetIntParameterAvailableOption(TextureSwitchEnumParamIndexInObject, CandidateIndex)));
		}
		
		// Avoid filling this arraw with repeated array  options
		if (RangeIndex == 0)
		{
			ProjectorTextureOptions.Add(MakeShared<TArray<TSharedPtr<FString>>>(OptionNamesAttribute));
		}

		if (NumValues > 1)
		{
			OpacityRemoveWidget->AddSlot()
			.Padding(1, 0)
			.FillWidth(0.3f)
			[
				SNew(SBox)
				[
					SNew(SSearchableComboBox)
					.OptionsSource(ProjectorTextureOptions.Last().Get())
					.InitiallySelectedItem((*ProjectorTextureOptions.Last())[ValueIndex])
					.OnGenerateWidget(this, &FCustomizableInstanceDetails::MakeTextureComboEntryWidget)
					.OnSelectionChanged(this, &FCustomizableInstanceDetails::OnProjectorTextureParameterComboBoxChanged, TextureSwitchEnumParamName, RangeIndex)
					.Content()
					[
						SNew(STextBlock)
						.Text(FText::FromString(*(*ProjectorTextureOptions.Last())[ValueIndex]))
					]
				]
			];
		}
		
		OpacityRemoveWidget->AddSlot()
		.HAlign(HAlign_Fill)
		.Padding(1, 0)
		.FillWidth(0.7f)
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(1, 0)
			.HAlign(EHorizontalAlignment::HAlign_Fill)
			[
				SNew(SSpinBox<float>)
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Value(this, &FCustomizableInstanceDetails::GetFloatParameterValue, OpacitySliderParamName, RangeIndex)
				.OnValueChanged(this, &FCustomizableInstanceDetails::OnFloatParameterChanged, OpacitySliderParamName, RangeIndex)
				.OnValueCommitted(this, &FCustomizableInstanceDetails::OnFloatParameterCommited, OpacitySliderParamName, RangeIndex)
				.OnBeginSliderMovement(this, &FCustomizableInstanceDetails::OnFloatParameterSliderBegin)
				.OnEndSliderMovement(this, &FCustomizableInstanceDetails::OnFloatParameterSpinBoxEnd, OpacitySliderParamName, RangeIndex)
				.Font(LayoutBuilder.Pin()->GetDetailFont())
			]

			+ SHorizontalBox::Slot()
			.Padding(1, 0)
			.AutoWidth()
			[
				SNew(SButton)
				.OnClicked(this, &FCustomizableInstanceDetails::OnProjectorLayerRemoved, ParamName, RangeIndex)
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Center)
				.Content()
				[
					SNew(STextBlock)
						.ToolTipText(LOCTEXT("LayerProjectorRemoveLayer_ToolTip", "Remove Layer"))
						.Text(LOCTEXT("LayerProjectorRemoveLayer_Text", "X"))
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
						.Font(LayoutBuilder.Pin()->GetDetailFont())
				]
			]
		];

		LayerProperties->AddSlot()
		.AutoHeight()
		[
			OpacityRemoveWidget.ToSharedRef()
		];
		LayerProperties->AddSlot()
		.AutoHeight()
		.Padding(0.0f,5.0f,0.0f,0.0f)
		[
			ProjectorProperties.ToSharedRef()
		];

		// Final composed widget
		ProjectorGroup->AddWidgetRow()
		.NameContent()
		.VAlign(EVerticalAlignment::VAlign_Center)
		.HAlign(EHorizontalAlignment::HAlign_Left)
		[
			SNew(STextBlock)
			.Text(FText::FromString("Layer " + FString::FromInt(RangeIndex)))
		]
		.ValueContent()
		.HAlign(EHorizontalAlignment::HAlign_Fill)
		.VAlign(EVerticalAlignment::VAlign_Fill)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.Padding(0.0f, 5.0f, 0.0f, 5.0f)
			[
				LayerProperties.ToSharedRef()
			]
		];
	}
	
	return ProjectorBox.ToSharedRef();
}


TSharedRef<SWidget> FCustomizableInstanceDetails::OnGenerateWidgetProjectorParameter(TSharedPtr<FString> InItem) const
{
	return SNew(STextBlock).Text(FText::FromString(*InItem.Get()));
}


FReply FCustomizableInstanceDetails::OnProjectorLayerAdded(FString ParamName)
{
	FScopedTransaction LocalTransaction(LOCTEXT("AddProjectorLayer", "Add Projector Layer"));
	CustomInstance->Modify();

	const int32 NumLayers = CustomInstance->MultilayerProjectorNumLayers(*ParamName) + 1;
	CustomInstance->MultilayerProjectorCreateLayer(*ParamName, NumLayers);

	UpdateInstance();

	FCoreUObjectDelegates::BroadcastOnObjectModified(CustomInstance.Get());

	return FReply::Handled();
}


FReply FCustomizableInstanceDetails::OnProjectorLayerRemoved(const FString ParamName, const int32 RangeIndex)
{
	FScopedTransaction LocalTransaction(LOCTEXT("RemoveProjectorLayer", "Remove Projector Layer"));
	CustomInstance->Modify();

	// Unselect projector if it's the deleted one
	if (GetEditorChecked()->GetProjectorParameter()->IsProjectorSelected(ParamName, RangeIndex))
	{
		GetEditorChecked()->HideGizmo();
	}

	CustomInstance->MultilayerProjectorRemoveLayerAt(*ParamName, RangeIndex);

	UpdateInstance();

	FCoreUObjectDelegates::BroadcastOnObjectModified(CustomInstance.Get());

	return FReply::Handled();
}


void FCustomizableInstanceDetails::OnProjectorTextureParameterComboBoxChanged(TSharedPtr<FString> Selection, ESelectInfo::Type SelectInfo, FString ParamName, int32 RangeIndex)
{
	if (Selection.IsValid())
	{
		FScopedTransaction LocalTransaction(LOCTEXT("SelectProjectorImage", "Select Projecto Image"));
		CustomInstance->Modify();
		CustomInstance->SetIntParameterSelectedOption(ParamName, *Selection, RangeIndex);
		UpdateInstance();

		// Non-continuous change: collect garbage.
		GEngine->ForceGarbageCollection();
	}
}


TSharedPtr<ICustomizableObjectInstanceEditor> FCustomizableInstanceDetails::GetEditorChecked() const
{
	TSharedPtr<ICustomizableObjectInstanceEditor> Editor = WeakEditor.Pin();
	check(Editor);

	return Editor;
}


TSharedRef<SWidget> FCustomizableInstanceDetails::MakeTextureComboEntryWidget(TSharedPtr<FString> InItem) const
{
	return SNew(STextBlock).Text(FText::FromString(*InItem.Get()));
}


// BOOL PARAMETERS -----------------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> FCustomizableInstanceDetails::GenerateBoolWidget(const FString& ParamName)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.HAlign(HAlign_Left)
		[
			SNew(SCheckBox)
			.HAlign(HAlign_Left)
			.IsChecked(this, &FCustomizableInstanceDetails::GetBoolParameterValue, ParamName)
			.OnCheckStateChanged(this, &FCustomizableInstanceDetails::OnBoolParameterChanged, ParamName)
		];
}


ECheckBoxState FCustomizableInstanceDetails::GetBoolParameterValue(FString ParamName) const
{
	const bool bResult = CustomInstance->GetBoolParameterSelectedOption(ParamName);
	return bResult ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}


void FCustomizableInstanceDetails::OnBoolParameterChanged(ECheckBoxState InCheckboxState, FString ParamName)
{
	FScopedTransaction LocalTransaction(FText::Format(LOCTEXT("SetParameterBool", "Set Bool Parameter: {0}"), FText::FromString(ParamName)));
	CustomInstance->Modify();
	CustomInstance->SetBoolParameterSelectedOption(ParamName, InCheckboxState == ECheckBoxState::Checked);
	UpdateInstance();

	// Non-continuous change: collect garbage.
	GEngine->ForceGarbageCollection();
}


// PARAMETERS -----------------------------------------------------------------------------------------------------------------

FReply FCustomizableInstanceDetails::OnCopyAllParameters()
{
	const FString ExportedText = CustomInstance->GetPrivate()->GetDescriptor().ToString();
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);

	return FReply::Handled();
}


FReply FCustomizableInstanceDetails::OnPasteAllParameters()
{
	FString ClipText;
	FPlatformApplicationMisc::ClipboardPaste(ClipText);
	
	FCustomizableObjectInstanceDescriptor& Descriptor = CustomInstance->GetPrivate()->GetDescriptor();
	const UScriptStruct* Struct = Descriptor.StaticStruct();

	UCustomizableObject* CustomizableObject = Descriptor.GetCustomizableObject();
	
	const int32 MinLOD = Descriptor.GetMinLod();
	const TArray<uint16> RequestedLODLevels = Descriptor.GetRequestedLODLevels();
	
	FScopedTransaction LocalTransaction(LOCTEXT("OnPasteAllParameters", "Paste All Parameters"));
	CustomInstance->Modify();

	if (Struct->ImportText(*ClipText, &Descriptor, nullptr, 0, GLog, GetPathNameSafe(Struct)))
	{
		Descriptor.SetCustomizableObject(CustomizableObject);
		
		// Keep current LOD
		Descriptor.SetMinLod(MinLOD);
		Descriptor.SetRequestedLODLevels(RequestedLODLevels);
		
		UpdateInstance();
	}
	
	return FReply::Handled();
}


FReply FCustomizableInstanceDetails::OnResetAllParameters()
{
	FScopedTransaction LocalTransaction(LOCTEXT("OnResetAllParameters", "Reset All Parameters"));
	CustomInstance->Modify();

	CustomInstance->SetDefaultValues();

	CustomInstance->GetPrivate()->SelectedProfileIndex = INDEX_NONE;
	UpdateInstance();

	// Non-continuous change: collect garbage.
	GEngine->ForceGarbageCollection();

	return FReply::Handled();
}


void FCustomizableInstanceDetails::OnResetParameterButtonClicked(int32 ParameterIndex)
{	
	FString ParameterName = CustomInstance->GetCustomizableObject()->GetParameterName(ParameterIndex);

	FScopedTransaction LocalTransaction(FText::Format(LOCTEXT("OnResetParameter", "Reset Parameter: {0}"), FText::FromString(ParameterName)));
	CustomInstance->Modify();
	CustomInstance->SetDefaultValue(ParameterName);
	UpdateInstance();
}

// TRANSACTION SYSTEM -----------------------------------------------------------------------------------------------------------------

void FCustomizableInstanceDetails::BeginTransaction(const FText& TransactionDesc, bool bModifyCustomizableObject)
{
	// We only allow the BeginTransaction to be called with the EndTransaction pair. We should never call a second transaction before the first was ended.
	check(!Transaction);

	Transaction.Reset(new FScopedTransaction(TransactionDesc));
	CustomInstance->Modify();

	//TODO(Max):UE-212345
	/*UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();
	if (CustomizableObject && bModifyCustomizableObject)
	{
		CustomizableObject->Modify();
	}*/
}


void FCustomizableInstanceDetails::EndTransaction()
{
	Transaction.Reset();

	//TODO(Max):UE-212345
	CustomInstance->GetPrivate()->SetSelectedParameterProfileDirty();
}


void FCustomizableInstanceDetails::OnInstanceTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	if (TransactionEvent.GetEventType() == ETransactionObjectEventType::UndoRedo)
	{
		// Update instance on undo/redo
		UpdateInstance();

		// We want to make sure that the gizom is hidden when we do an undo/redo transaction
		GetEditorChecked()->HideGizmo();
	}
}


// PROFILES WINDOW -----------------------------------------------------------------------------------------------------------------

void SProfileParametersWindow::Construct(const FArguments& InArgs)
{
	if (AssetPath.IsEmpty())
	{
		AssetPath = FText::FromString(TEXT("/Game/"));
	}

	FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	SWindow::Construct(SWindow::FArguments()
	.Title(LOCTEXT("SSelectFolderDlg_Title", "Add a name to the new profile"))
	.SupportsMinimize(false)
	.SupportsMaximize(false)
	.ClientSize(FVector2D(450, 85))
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot() // Add user input block
		.Padding(2)
		[
			SNew(SBorder)
			.BorderImage(UE_MUTABLE_GET_BRUSH("ToolPanel.GroupBorder"))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("CustomizableProfileName", "Customizable Profile Name"))
					.Font(FSlateFontInfo(FCoreStyle::GetDefaultFont(), 14, "Regular"))
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SEditableTextBox)
					.Text(InArgs._DefaultFileName)
					.OnTextCommitted(this, &SProfileParametersWindow::OnNameChange)
					.MinDesiredWidth(250)
				]
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Right)
		.Padding(5)
		[
			SNew(SUniformGridPanel)
			.SlotPadding(UE_MUTABLE_GET_MARGIN("StandardDialog.SlotPadding"))
			.MinDesiredSlotWidth(UE_MUTABLE_GET_FLOAT("StandardDialog.MinDesiredSlotWidth"))
			.MinDesiredSlotHeight(UE_MUTABLE_GET_FLOAT("StandardDialog.MinDesiredSlotHeight"))
			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.ContentPadding(UE_MUTABLE_GET_MARGIN("StandardDialog.ContentPadding"))
				.Text(LOCTEXT("OK", "OK"))
				.OnClicked(this, &SProfileParametersWindow::OnButtonClick, EAppReturnType::Ok)
			]
			+ SUniformGridPanel::Slot(1, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.ContentPadding(UE_MUTABLE_GET_MARGIN("StandardDialog.ContentPadding"))
				.Text(LOCTEXT("Cancel", "Cancel"))
				.OnClicked(this, &SProfileParametersWindow::OnButtonClick, EAppReturnType::Cancel)
			]
		]
	]);
}


EAppReturnType::Type SProfileParametersWindow::ShowModal()
{
	GEditor->EditorAddModalWindow(SharedThis(this));

	return UserResponse;
}


void SProfileParametersWindow::OnNameChange(const FText& NewName, ETextCommit::Type CommitInfo)
{
	if (CommitInfo == ETextCommit::OnEnter)
	{
		FileName = NewName;
		
		RequestDestroyWindow();

		//TODO(Max):UE-212345
		//const FScopedTransaction Transaction(LOCTEXT("OnEnterAddProfile", "Add Profile"));
		
		UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();
		//CustomizableObject->Modify();

		CustomizableObject->GetPrivate()->AddNewParameterProfile(GetFileName(), *CustomInstance.Get());

		if (CustomInstance->GetPrivate()->bSelectedProfileDirty && CustomInstance->GetPrivate()->SelectedProfileIndex != INDEX_NONE)
		{
			CustomInstance->GetPrivate()->SaveParametersToProfile(CustomInstance->GetPrivate()->SelectedProfileIndex);
		}
		CustomInstance->GetPrivate()->SelectedProfileIndex = CustomizableObject->GetPrivate()->GetInstancePropertiesProfiles().Num() - 1;

		if (InstanceDetails)
		{
			InstanceDetails->Refresh();
		}
	}
	else
	{
		FileName = NewName;
	}
}


FReply SProfileParametersWindow::OnButtonClick(EAppReturnType::Type ButtonID)
{
	if (ButtonID == EAppReturnType::Ok)
	{
		UserResponse = ButtonID;

		RequestDestroyWindow();

		//TODO(Max):UE-212345
		//const FScopedTransaction Transaction(LOCTEXT("OnOkAddProfile", "Add Profile"));
		
		UCustomizableObject* CustomizableObject = CustomInstance->GetCustomizableObject();
		CustomizableObject->GetPrivate()->AddNewParameterProfile(GetFileName(), *CustomInstance.Get());

		if (CustomInstance->GetPrivate()->bSelectedProfileDirty && CustomInstance->GetPrivate()->SelectedProfileIndex != INDEX_NONE)
		{
			CustomInstance->GetPrivate()->SaveParametersToProfile(CustomInstance->GetPrivate()->SelectedProfileIndex);
		}
		CustomInstance->GetPrivate()->SelectedProfileIndex = CustomizableObject->GetPrivate()->GetInstancePropertiesProfiles().Num() - 1;

		if (InstanceDetails)
		{
			InstanceDetails->Refresh();
		}
	}
	else if (ButtonID == EAppReturnType::Cancel)
	{
		UserResponse = ButtonID;

		RequestDestroyWindow();
	}
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
