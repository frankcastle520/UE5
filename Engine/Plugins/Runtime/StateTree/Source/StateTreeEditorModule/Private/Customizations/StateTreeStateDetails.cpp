// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeStateDetails.h"
#include "DetailLayoutBuilder.h"
#include "PropertyCustomizationHelpers.h"
#include "IPropertyUtilities.h"
#include "PropertyBagDetails.h"
#include "StateTree.h"
#include "StateTreeEditor.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNodeUtils.h"
#include "StateTreeEditorStyle.h"
#include "StateTreePropertyHelpers.h"
#include "StateTreeSchema.h"
#include "StateTreeStateParametersDetails.h"
#include "Debugger/StateTreeDebuggerUIExtensions.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SComboButton.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"


TSharedRef<IDetailCustomization> FStateTreeStateDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeStateDetails);
}

void FStateTreeStateDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	
	// Find StateTreeEditorData associated with this panel.
	UStateTreeEditorData* EditorData = nullptr;
	for (TWeakObjectPtr<UObject>& WeakObject : Objects)
	{
		if (UObject* Object = WeakObject.Get())
		{
			if (UStateTreeEditorData* OuterEditorData = Object->GetTypedOuter<UStateTreeEditorData>())
			{
				EditorData = OuterEditorData;
				break;
			}
		}
	}

	// Find StateTreeState associated with this panel.
	TWeakObjectPtr<UStateTreeState> WeakState;
	for (TWeakObjectPtr<UObject>& WeakObject : Objects)
	{
		if (UObject* Object = WeakObject.Get())
		{
			if (UStateTreeState* State = Cast<UStateTreeState>(Object))
			{
				WeakState = State;
				break;
			}
		}
	}
	
	const UStateTreeSchema* Schema = EditorData ? EditorData->Schema : nullptr;
	const FString SchemaPath = Schema ? Schema->GetClass()->GetPathName() : FString();
	TWeakObjectPtr<UStateTreeEditorData> WeakEditorData = EditorData;

	
	const TSharedPtr<IPropertyHandle> IDProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, ID));
	const TSharedPtr<IPropertyHandle> NameProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, Name));
	const TSharedPtr<IPropertyHandle> TagProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, Tag));
	const TSharedPtr<IPropertyHandle> ColorRefProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, ColorRef));
	const TSharedPtr<IPropertyHandle> EnabledProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, bEnabled));
	const TSharedPtr<IPropertyHandle> TasksProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, Tasks));
	const TSharedPtr<IPropertyHandle> SingleTaskProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, SingleTask));
	const TSharedPtr<IPropertyHandle> EnterConditionsProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, EnterConditions));
	const TSharedPtr<IPropertyHandle> ConsiderationsProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, Considerations));
	const TSharedPtr<IPropertyHandle> TransitionsProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, Transitions));
	const TSharedPtr<IPropertyHandle> TypeProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, Type));
	const TSharedPtr<IPropertyHandle> LinkedSubtreeProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, LinkedSubtree));
	const TSharedPtr<IPropertyHandle> LinkedAssetProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, LinkedAsset));
	const TSharedPtr<IPropertyHandle> ParametersProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, Parameters));
	const TSharedPtr<IPropertyHandle> SelectionBehaviorProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, SelectionBehavior));
	const TSharedPtr<IPropertyHandle> RequiredEventToEnterProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, RequiredEventToEnter));
	const TSharedPtr<IPropertyHandle> CheckPrerequisitesProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, bCheckPrerequisitesWhenActivatingChildDirectly));
	const TSharedPtr<IPropertyHandle> WeightProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UStateTreeState, Weight));

	PropUtils = DetailBuilder.GetPropertyUtilities();
	
	// Never show enabled
	EnabledProperty->MarkHiddenByCustomization();
	
	// Show ID only for debugging
	if (UE::StateTree::Editor::GbDisplayItemIds == false)
	{
		IDProperty->MarkHiddenByCustomization();
	}

	uint8 StateTypeValue = 0;
	TypeProperty->GetValue(StateTypeValue);
	const EStateTreeStateType StateType = (EStateTreeStateType)StateTypeValue;

	IDetailCategoryBuilder& StateCategory = DetailBuilder.EditCategory(TEXT("State"), LOCTEXT("StateDetailsState", "State"));
	StateCategory.SetSortOrder(0);
	{
		TSharedRef<SHorizontalBox> HeaderContent = SNew(SHorizontalBox)
			.IsEnabled(PropUtils.ToSharedRef(), &IPropertyUtilities::IsPropertyEditingEnabled)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
		
				// Debugger labels
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					UE::StateTreeEditor::DebuggerExtensions::CreateStateWidget(EnabledProperty, EditorData)
				]
				// Options
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SComboButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.OnGetMenuContent_Lambda([EnabledProperty, WeakEditorData]()
					{
						FMenuBuilder MenuBuilder(/*ShouldCloseWindowAfterMenuSelection*/true, /*CommandList*/nullptr);
						// Append debugger items.
						UE::StateTreeEditor::DebuggerExtensions::AppendStateMenuItems(MenuBuilder, EnabledProperty, WeakEditorData.Get());
						return MenuBuilder.MakeWidget();	
					})
					.ToolTipText(LOCTEXT("ItemActions", "Item actions"))
					.HasDownArrow(false)
					.ContentPadding(FMargin(4.f, 2.f))
					.ButtonContent()
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush("Icons.ChevronDown"))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
			];
		StateCategory.HeaderContent(HeaderContent);
	}

	// Name
	NameProperty->MarkHiddenByCustomization();
	StateCategory.AddProperty(NameProperty);

	// Tag
	TagProperty->MarkHiddenByCustomization();
	StateCategory.AddProperty(TagProperty);

	// Color
	ColorRefProperty->MarkHiddenByCustomization();
	StateCategory.AddProperty(ColorRefProperty);

	// Type
	TypeProperty->MarkHiddenByCustomization();
	StateCategory.AddProperty(TypeProperty);

	// Per state type properties
	SelectionBehaviorProperty->MarkHiddenByCustomization();
	LinkedSubtreeProperty->MarkHiddenByCustomization();
	LinkedAssetProperty->MarkHiddenByCustomization();

	if (StateType == EStateTreeStateType::State || StateType == EStateTreeStateType::Subtree)
	{
		StateCategory.AddProperty(SelectionBehaviorProperty);
	}
	else if (StateType == EStateTreeStateType::Linked)
	{
		StateCategory.AddProperty(LinkedSubtreeProperty);
	}
	else if (StateType == EStateTreeStateType::LinkedAsset)
	{
		// Custom widget for the linked asset, to add filtering to the assets.
		IDetailPropertyRow& Row = StateCategory.AddProperty(LinkedAssetProperty);
		Row.CustomWidget()
		.NameContent()
		[
			LinkedAssetProperty->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SObjectPropertyEntryBox)
			.PropertyHandle(LinkedAssetProperty)
			.AllowedClass(UStateTree::StaticClass())
			.ThumbnailPool(DetailBuilder.GetPropertyUtilities()->GetThumbnailPool())
			.OnShouldFilterAsset_Lambda([SchemaPath](const FAssetData& InAssetData)
			{
				return !SchemaPath.IsEmpty() && !InAssetData.TagsAndValues.ContainsKeyValue(UE::StateTree::SchemaTag, SchemaPath);
			})
		];
	}

	// Parameters category
	const FText ParametersDisplayName = LOCTEXT("EditorStateDetailsParameters", "Parameters");
	IDetailCategoryBuilder& ParametersCategory = DetailBuilder.EditCategory(TEXT("Parameters"), ParametersDisplayName);
	ParametersCategory.SetSortOrder(1);
	{
		// Show parameters as a category.
		ParametersProperty->MarkHiddenByCustomization();

		TSharedPtr<IPropertyHandle> ParametersParametersProperty = ParametersProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeStateParameters, Parameters)); // FInstancedPropertyBag
		check(ParametersParametersProperty);
		TSharedPtr<IPropertyHandle> ParametersFixedLayoutProperty = ParametersProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeStateParameters, bFixedLayout));
		check(ParametersFixedLayoutProperty);
		TSharedPtr<IPropertyHandle> ParametersIDProperty = ParametersProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeStateParameters, ID));
		check(ParametersIDProperty);

		bool bFixedLayout = false;
		ParametersFixedLayoutProperty->GetValue(bFixedLayout);

		
		const TSharedRef<SHorizontalBox> HeaderContentWidget = SNew(SHorizontalBox)
			.IsEnabled(PropUtils.ToSharedRef(), &IPropertyUtilities::IsPropertyEditingEnabled);

		HeaderContentWidget->AddSlot()
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
			[
				SNew(SImage)
					.ColorAndOpacity(UE::StateTree::Colors::Blue)
					.Image(FStateTreeEditorStyle::Get().GetBrush("StateTreeEditor.Parameters"))
			];
		HeaderContentWidget->AddSlot()
			.VAlign(VAlign_Center)
			.Padding(FMargin(4.0f, 0.0f, 0.0f, 0.0f))
			.AutoWidth()
			[
				SNew(STextBlock)
				.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Category")
				.Text(ParametersDisplayName)
			];

		if (!bFixedLayout)
		{
			HeaderContentWidget->AddSlot()
				.FillWidth(1.f)
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					FPropertyBagDetails::MakeAddPropertyWidget(
						ParametersParametersProperty,
						PropUtils.ToSharedRef(),
						EPropertyBagPropertyType::Bool,
						FLinearColor(UE::StateTree::Colors::Blue)).ToSharedRef()
				];
		}
		
		ParametersCategory.HeaderContent(HeaderContentWidget, /*FullRowContent*/true);

		FGuid ID;
		UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(ParametersIDProperty, ID);

		// Show the Value (FInstancedStruct) as child rows.
		TSharedRef<FStateTreeStateParametersInstanceDataDetails> InstanceDetails =
			MakeShareable(new FStateTreeStateParametersInstanceDataDetails(ParametersProperty, ParametersProperty, PropUtils, bFixedLayout, ID, WeakEditorData, WeakState));
		ParametersCategory.AddCustomBuilder(InstanceDetails);
	}
	
	// Enter conditions
	const FName EnterConditionsCategoryName(TEXT("Enter Conditions"));
	if (Schema && Schema->AllowEnterConditions())
	{

		IDetailCategoryBuilder& EnterConditionsCategory = UE::StateTreeEditor::EditorNodeUtils::MakeArrayCategory(
			DetailBuilder,
			EnterConditionsProperty,
			EnterConditionsCategoryName,
			LOCTEXT("StateDetailsEnterConditions", "Enter Conditions"),
			FName("StateTreeEditor.Conditions"),
			UE::StateTree::Colors::Yellow,
			UE::StateTree::Colors::Yellow.WithAlpha(192),
			LOCTEXT("EnterConditionsAddTooltip", "Add new Enter Condition"),
			/*SortOrder*/2);
		EnterConditionsProperty->MarkHiddenByCustomization();

		// Event
		RequiredEventToEnterProperty->MarkHiddenByCustomization();
		EnterConditionsCategory.AddProperty(RequiredEventToEnterProperty);

		// Check Prerequisites
		CheckPrerequisitesProperty->MarkHiddenByCustomization();
		EnterConditionsCategory.AddProperty(CheckPrerequisitesProperty);
	}
	else
	{
		DetailBuilder.EditCategory(EnterConditionsCategoryName).SetCategoryVisibility(false);
	}

	// Utility
	ConsiderationsProperty->MarkHiddenByCustomization();
	if (Schema && Schema->AllowUtilityConsiderations())
	{
		const FName UtilityCategoryName(TEXT("Selection Utility"));
		IDetailCategoryBuilder& UtilityConsiderationsCategory = UE::StateTreeEditor::EditorNodeUtils::MakeArrayCategory(
			DetailBuilder,
			ConsiderationsProperty,
			UtilityCategoryName,
			LOCTEXT("StateDetailsSelectionUtility", "Selection Utility"),
			FName("StateTreeEditor.Utility"),
			UE::StateTree::Colors::Orange,
			UE::StateTree::Colors::Orange.WithAlpha(192),
			LOCTEXT("UtilityAddTooltip", "Add new Utility Consideration"),
			/*SortOrder*/3);

		// Weight
		WeightProperty->MarkHiddenByCustomization();
		UtilityConsiderationsCategory.AddProperty(WeightProperty);
	}

	// Tasks
	if ((StateType == EStateTreeStateType::State || StateType == EStateTreeStateType::Subtree))
	{
		if (Schema && Schema->AllowMultipleTasks())
		{
			const FName TasksCategoryName(TEXT("Tasks"));
			UE::StateTreeEditor::EditorNodeUtils::MakeArrayCategory(
				DetailBuilder,
				TasksProperty,
				TasksCategoryName,
				LOCTEXT("StateDetailsTasks", "Tasks"),
				FName("StateTreeEditor.Tasks"),
				UE::StateTree::Colors::Cyan,
				UE::StateTree::Colors::Cyan.WithAlpha(192),
				LOCTEXT("StateDetailsTasksAddTooltip", "Add new Task"),
				/*SortOrder*/4);
			SingleTaskProperty->MarkHiddenByCustomization();
		}
		else
		{
			const FName TaskCategoryName(TEXT("Task"));
			IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TaskCategoryName);
			Category.SetSortOrder(4);
			
			IDetailPropertyRow& Row = Category.AddProperty(SingleTaskProperty);
			Row.ShouldAutoExpand(true);
			
			TasksProperty->MarkHiddenByCustomization();
		}
	}
	else
	{
		SingleTaskProperty->MarkHiddenByCustomization();
		TasksProperty->MarkHiddenByCustomization();
	}

	// Transitions
	UE::StateTreeEditor::EditorNodeUtils::MakeArrayCategory(
		DetailBuilder,
		TransitionsProperty,
		"Transitions",
		LOCTEXT("StateDetailsTransitions", "Transitions"),
		FName("StateTreeEditor.Transitions"),
		UE::StateTree::Colors::Magenta,
		UE::StateTree::Colors::Magenta.WithAlpha(192),
		LOCTEXT("StateDetailsTransitionsAddTooltip", "Add new Transition"),
		/*SortOrder*/5);

	// Refresh the UI when the type changes.	
	TypeProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([PropUtils = PropUtils] ()
	{
		if (PropUtils.IsValid())
		{
			PropUtils->ForceRefresh();
		}
	}));
}

void FStateTreeStateDetails::PostUndo(bool bSuccess)
{
	// Refresh view on undo or redo so that the customization based on e.g. State type will be reflected correctly.
	if (PropUtils.IsValid())
	{
		PropUtils->ForceRefresh();
	}
	
}

void FStateTreeStateDetails::PostRedo(bool bSuccess)
{
	// Refresh view on undo or redo so that the customization based on e.g. State type will be reflected correctly.
	if (PropUtils.IsValid())
	{
		PropUtils->ForceRefresh(); 
	}
}

#undef LOCTEXT_NAMESPACE
