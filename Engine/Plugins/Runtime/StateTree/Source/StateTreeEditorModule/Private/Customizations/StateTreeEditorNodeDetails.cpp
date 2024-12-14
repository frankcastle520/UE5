// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorNodeDetails.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "DetailLayoutBuilder.h"
#include "IPropertyUtilities.h"
#include "IDetailChildrenBuilder.h"
#include "StateTree.h"
#include "StateTreeEditor.h"
#include "StateTreeEditorData.h"
#include "StateTreePropertyRef.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SStateTreeNodeTypePicker.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "InstancedStructDetails.h"
#include "StateTreeBindingExtension.h"
#include "StateTreeDelegates.h"
#include "StateTreePropertyHelpers.h"
#include "StateTreeEditorStyle.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Blueprint/StateTreeEvaluatorBlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Styling/StyleColors.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ScopedTransaction.h"
#include "StateTreeEditorNodeUtils.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Debugger/StateTreeDebuggerUIExtensions.h"
#include "StateTreePropertyBindings.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Styling/SlateTypes.h"
#include "TextStyleDecorator.h"
#include "Framework/Application/SlateApplication.h"
#include "StateTreePropertyFunctionBase.h"

#define LOCTEXT_NAMESPACE "StateTreeEditor"

/** Helper class to detect if there were issues when calling ImportText() */
class FStateTreeDefaultValueImportErrorContext : public FOutputDevice
{
public:

	int32 NumErrors;

	FStateTreeDefaultValueImportErrorContext()
		: FOutputDevice()
		, NumErrors(0)
	{
	}

	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
	{
		NumErrors++;
	}
};

namespace UE::StateTreeEditor::Internal
{
	/* Returns true if provided property is direct or indirect child of PropertyFunction */
	bool IsOwnedByPropertyFunctionNode(TSharedPtr<IPropertyHandle> Property)
	{
		while (Property)
		{
			if (FStructProperty* StructProperty = CastField<FStructProperty>(Property->GetProperty()))
			{
				if (StructProperty->Struct == FStateTreeEditorNode::StaticStruct())
				{
					if (const FStateTreeEditorNode* Node = UE::StateTreeEditor::EditorNodeUtils::GetCommonNode(Property))
					{
						if (const UScriptStruct* ScriptStruct = Node->Node.GetScriptStruct())
						{
							return ScriptStruct->IsChildOf<FStateTreePropertyFunctionBase>();
						}
					}
				}	
			}

			Property = Property->GetParentHandle();
		}

		return false;
	}

	/** @return text describing the pin type, matches SPinTypeSelector. */
	FText GetPinTypeText(const FEdGraphPinType& PinType)
	{
		const FName PinSubCategory = PinType.PinSubCategory;
		const UObject* PinSubCategoryObject = PinType.PinSubCategoryObject.Get();
		if (PinSubCategory != UEdGraphSchema_K2::PSC_Bitmask && PinSubCategoryObject)
		{
			if (const UField* Field = Cast<const UField>(PinSubCategoryObject))
			{
				return Field->GetDisplayNameText();
			}
			return FText::FromString(PinSubCategoryObject->GetName());
		}

		return UEdGraphSchema_K2::GetCategoryText(PinType.PinCategory, NAME_None, true);
	}

	/** @return UClass or UScriptStruct of class or struct property, nullptr for others. */
	UStruct* GetPropertyStruct(TSharedPtr<IPropertyHandle> PropHandle)
	{
		if (!PropHandle.IsValid())
		{
			return nullptr;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(PropHandle->GetProperty()))
		{
			return StructProperty->Struct;
		}
		
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(PropHandle->GetProperty()))
		{
			return ObjectProperty->PropertyClass;
		}

		return nullptr;
	}

	void ModifyRow(IDetailPropertyRow& ChildRow, const FGuid& ID, UStateTreeEditorData* EditorData)
	{
		FStateTreeEditorPropertyBindings* EditorPropBindings = EditorData ? EditorData->GetPropertyEditorBindings() : nullptr;
		if (!EditorPropBindings)
		{
			return;
		}
		
		TSharedPtr<IPropertyHandle> ChildPropHandle = ChildRow.GetPropertyHandle();
		check(ChildPropHandle.IsValid());
		
		const EStateTreePropertyUsage Usage = UE::StateTree::GetUsageFromMetaData(ChildPropHandle->GetProperty());
		const FProperty* Property = ChildPropHandle->GetProperty();
		
		// Hide output properties for PropertyFunctionNode.
		if (Usage == EStateTreePropertyUsage::Output && UE::StateTreeEditor::Internal::IsOwnedByPropertyFunctionNode(ChildPropHandle))
		{
			ChildRow.Visibility(EVisibility::Hidden);
			return;
		}

		// Conditionally control visibility of the value field of bound properties.
		if (Usage != EStateTreePropertyUsage::Invalid && ID.IsValid())
		{
			// Pass the node ID to binding extension. Since the properties are added using AddChildStructure(), we break the hierarchy and cannot access parent.
			ChildPropHandle->SetInstanceMetaData(UE::StateTree::PropertyBinding::StateTreeNodeIDName, LexToString(ID));

			FStateTreePropertyPath Path(ID, *Property->GetFName().ToString());
			TSharedPtr<SWidget> NameWidget;
			TSharedPtr<SWidget> ValueWidget;
			FDetailWidgetRow Row;
			ChildRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

			const bool bHasChildPropertyBinding = EditorPropBindings->HasPropertyBinding(Path, FStateTreeEditorPropertyBindings::ESearchMode::Includes);
			const bool bValidUsage = Usage == EStateTreePropertyUsage::Input || Usage == EStateTreePropertyUsage::Output || Usage == EStateTreePropertyUsage::Context;
			if (bHasChildPropertyBinding || bValidUsage)
			{				
				FEdGraphPinType PinType;
				const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();

				// Show referenced type for property refs.
				if (UE::StateTree::PropertyRefHelpers::IsPropertyRef(*Property))
				{
					// Use internal type to construct PinType if it's property of PropertyRef type.
					FStateTreeDataView TargetDataView;
					if (ensure(EditorData->GetDataViewByID(ID, TargetDataView)))
					{
						TArray<FStateTreePropertyPathIndirection> TargetIndirections;
						if (ensure(Path.ResolveIndirectionsWithValue(TargetDataView, TargetIndirections)))
						{
							const void* PropertyRef = TargetIndirections.Last().GetPropertyAddress();
							PinType = UE::StateTree::PropertyRefHelpers::GetPropertyRefInternalTypeAsPin(*Property, PropertyRef);
						}
					}
				}
				else
				{
					Schema->ConvertPropertyToPinType(Property, PinType);
				}

				auto IsValueVisible = TAttribute<EVisibility>::Create([Path, EditorPropBindings]() -> EVisibility
					{
						return EditorPropBindings->HasPropertyBinding(Path, FStateTreeEditorPropertyBindings::ESearchMode::Exact) ? EVisibility::Collapsed : EVisibility::Visible;
					});

				const FSlateBrush* Icon = FBlueprintEditorUtils::GetIconFromPin(PinType, true);
				FText Text = GetPinTypeText(PinType);
				
				FText ToolTip; 
				FLinearColor IconColor = Schema->GetPinTypeColor(PinType);
				FText Label;
				FText LabelToolTip;
				FSlateColor TextColor = FSlateColor::UseForeground();

				if (Usage == EStateTreePropertyUsage::Input)
				{
					Label = LOCTEXT("LabelInput", "IN");
					LabelToolTip = LOCTEXT("InputToolTip", "This is Input property. It is always expected to be bound to some other property.");
				}
				else if (Usage == EStateTreePropertyUsage::Output)
				{
					Label = LOCTEXT("LabelOutput", "OUT");
					LabelToolTip = LOCTEXT("OutputToolTip", "This is Output property. The node will always set it's value, other nodes can bind to it.");
				}
				else if (Usage == EStateTreePropertyUsage::Context)
				{
					Label = LOCTEXT("LabelContext", "CONTEXT");
					LabelToolTip = LOCTEXT("ContextObjectToolTip", "This is Context property. It is automatically connected to one of the Contex objects, or can be overridden with property binding.");

					if (UStruct* Struct = GetPropertyStruct(ChildPropHandle))
					{
						const FStateTreeBindableStructDesc Desc = EditorData->FindContextData(Struct, ChildPropHandle->GetProperty()->GetName());
						if (Desc.IsValid())
						{
							// Show as connected.
							Icon = FCoreStyle::Get().GetBrush("Icons.Link");
							Text = FText::FromName(Desc.Name);
							
							ToolTip = FText::Format(
								LOCTEXT("ToolTipConnected", "Connected to Context {0}."),
									FText::FromName(Desc.Name));
						}
						else
						{
							// Show as unconnected.
							Icon = FCoreStyle::Get().GetBrush("Icons.Warning");
							ToolTip = LOCTEXT("ToolTipNotConnected", "Could not connect Context property automatically.");
						}
					}
					else
					{
						// Mismatching type.
						Text = LOCTEXT("ContextObjectInvalidType", "Invalid type");
						ToolTip = LOCTEXT("ContextObjectInvalidTypeTooltip", "Context properties must be Object references or Structs.");
						Icon = FCoreStyle::Get().GetBrush("Icons.ErrorWithColor");
						IconColor = FLinearColor::White;
					}
				}
				else if (ensure(bHasChildPropertyBinding))
				{
					Icon = FCoreStyle::Get().GetBrush("Icons.Link");
					Text = LOCTEXT("PropertyMemberIsBoundTooltip", "{Member(s) Bound_B}");
					TextColor = FSlateColor::UseSubduedForeground();
				}
				
				ChildRow
					.CustomWidget(true)
					.NameContent()
					[
						SNew(SHorizontalBox)

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							NameWidget.ToSharedRef()
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f)
						[
							SNew(SBorder)
							.Padding(FMargin(6.0f, 1.0f))
							.BorderImage(FStateTreeEditorStyle::Get().GetBrush("StateTree.Param.Background"))
							.Visibility(Label.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
							[
								SNew(STextBlock)
								.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Param.Label")
								.ColorAndOpacity(FStyleColors::Foreground)
								.Text(Label)
								.ToolTipText(LabelToolTip)
							]
						]

					]
					.ValueContent()
					[
						SNew(SHorizontalBox)
						.Visibility(IsValueVisible)

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(4.0f, 0.0f)
						[
							SNew(SImage)
							.Image(Icon)
							.ColorAndOpacity(IconColor)
							.ToolTipText(ToolTip)
						]

						+SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(IDetailLayoutBuilder::GetDetailFont())
							.ColorAndOpacity(TextColor)
							.Text(Text)
							.ToolTipText(ToolTip)
						]
					];
			}
		}
	}

} // UE::StateTreeEditor::Internal

// Customized version of FInstancedStructDataDetails used to hide bindable properties.
class FBindableNodeInstanceDetails : public FInstancedStructDataDetails
{
public:

	FBindableNodeInstanceDetails(TSharedPtr<IPropertyHandle> InStructProperty, TSharedPtr<IPropertyHandle> InIDProperty, UStateTreeEditorData* InEditorData)
		: FInstancedStructDataDetails(InStructProperty)
		, EditorData(InEditorData)
		, IDProperty(InIDProperty)
	{
	}

	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow) override
	{
		FGuid ID;
		UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(IDProperty, ID);
		
		UE::StateTreeEditor::Internal::ModifyRow(ChildRow, ID, EditorData);
	}

	UStateTreeEditorData* EditorData;
	TSharedPtr<IPropertyHandle> IDProperty;
};

////////////////////////////////////

TSharedRef<IPropertyTypeCustomization> FStateTreeEditorNodeDetails::MakeInstance()
{
	return MakeShareable(new FStateTreeEditorNodeDetails);
}

FStateTreeEditorNodeDetails::~FStateTreeEditorNodeDetails()
{
	UE::StateTree::PropertyBinding::OnStateTreePropertyBindingChanged.Remove(OnBindingChangedHandle);
}

void FStateTreeEditorNodeDetails::CustomizeHeader(TSharedRef<class IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	StructProperty = StructPropertyHandle;
	PropUtils = StructCustomizationUtils.GetPropertyUtilities();

	NodeProperty = StructProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, Node));
	InstanceProperty = StructProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, Instance));
	InstanceObjectProperty = StructProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, InstanceObject));
	IDProperty = StructProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, ID));

	IndentProperty = StructProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, ExpressionIndent));
	OperandProperty = StructProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, ExpressionOperand));

	check(NodeProperty.IsValid());
	check(InstanceProperty.IsValid());
	check(IDProperty.IsValid());
	check(IndentProperty.IsValid());
	check(OperandProperty.IsValid());

	UE::StateTreeEditor::EditorNodeUtils::GetNodeBaseScriptStructAndClass(StructProperty, BaseScriptStruct, BaseClass);

	UE::StateTree::Delegates::OnIdentifierChanged.AddSP(this, &FStateTreeEditorNodeDetails::OnIdentifierChanged);
	OnBindingChangedHandle = UE::StateTree::PropertyBinding::OnStateTreePropertyBindingChanged.AddRaw(this, &FStateTreeEditorNodeDetails::OnBindingChanged);
	FindOuterObjects();

	// Don't draw the header if it's a PropertyFunction.
	if (UE::StateTreeEditor::Internal::IsOwnedByPropertyFunctionNode(StructProperty))
	{
		return;
	}

	const FIsResetToDefaultVisible IsResetVisible = FIsResetToDefaultVisible::CreateSP(this, &FStateTreeEditorNodeDetails::ShouldResetToDefault);
	const FResetToDefaultHandler ResetHandler = FResetToDefaultHandler::CreateSP(this, &FStateTreeEditorNodeDetails::ResetToDefault);
	const FResetToDefaultOverride ResetOverride = FResetToDefaultOverride::Create(IsResetVisible, ResetHandler);

	auto IndentColor = [this]() -> FSlateColor
	{
		return (RowBorder && RowBorder->IsHovered()) ? FSlateColor::UseForeground() : FSlateColor(FLinearColor::Transparent);
	};

	HeaderRow
		.WholeRowContent()
		.VAlign(VAlign_Center)
		[
			// Border to capture mouse clicks on the row (used for right click menu).
			SAssignNew(RowBorder, SBorder)
			.BorderImage(FStyleDefaults::GetNoBrush())
			.Padding(0)
			.OnMouseButtonDown(this, &FStateTreeEditorNodeDetails::OnRowMouseDown)
			.OnMouseButtonUp(this, &FStateTreeEditorNodeDetails::OnRowMouseUp)
			[
				SNew(SHorizontalBox)
				
				// Indent
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(30.f)
					.Visibility(this, &FStateTreeEditorNodeDetails::AreIndentButtonsVisible)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.OnClicked(this, &FStateTreeEditorNodeDetails::HandleIndentPlus)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(4.f, 4.f))
						.ToolTipText(LOCTEXT("IncreaseIdentTooltip", "Increment the depth of the expression row controlling parentheses and expression order"))
						[
							SNew(SImage)
							.DesiredSizeOverride(FVector2D(8.f, 8.f))
							.Image(FAppStyle::GetBrush("Icons.Plus"))
							.ColorAndOpacity_Lambda(IndentColor)
						]
					]
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(this, &FStateTreeEditorNodeDetails::GetIndentSize)
					.Visibility(this, &FStateTreeEditorNodeDetails::AreIndentButtonsVisible)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.OnClicked(this, &FStateTreeEditorNodeDetails::HandleIndentMinus)
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(4.f, 4.f))
						.ToolTipText(LOCTEXT("DecreaseIndentTooltip", "Decrement the depth of the expression row controlling parentheses and expression order"))
						[
							SNew(SImage)
							.DesiredSizeOverride(FVector2D(8.f, 8.f))
							.Image(FAppStyle::GetBrush("Icons.Minus"))
							.ColorAndOpacity_Lambda(IndentColor)
						]
					]
				]

				// Operand
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.WidthOverride(30.0f)
					.Padding(FMargin(2, 4, 2, 3))
					.VAlign(VAlign_Center)
					.Visibility(this, &FStateTreeEditorNodeDetails::IsOperandVisible)
					[
						SNew(SComboButton)
						.IsEnabled(TAttribute<bool>(this, &FStateTreeEditorNodeDetails::IsOperandEnabled))
						.ComboButtonStyle(FStateTreeEditorStyle::Get(), "StateTree.Node.Operand.ComboBox")
						.ButtonColorAndOpacity(this, &FStateTreeEditorNodeDetails::GetOperandColor)
						.HasDownArrow(false)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnGetMenuContent(this, &FStateTreeEditorNodeDetails::OnGetOperandContent)
						.ButtonContent()
						[
							SNew(STextBlock)
							.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Node.Operand")
							.Text(this, &FStateTreeEditorNodeDetails::GetOperandText)
						]
					]
				]
				// Open parens
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SBox)
					.Padding(FMargin(FMargin(0.0f, 0.0f, 4.0f, 0.0f)))
					.Visibility(this, &FStateTreeEditorNodeDetails::AreParensVisible)
					[
						SNew(STextBlock)
						.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Node.Parens")
						.Text(this, &FStateTreeEditorNodeDetails::GetOpenParens)
					]
				]
				// Description
				+ SHorizontalBox::Slot()
				.FillContentWidth(0.0f, 1.0f) // no growing, allow shrink
				.VAlign(VAlign_Center)
				.Padding(FMargin(0, 4))
				[
					SNew(SHorizontalBox)
					// Icon
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0, 0, 4, 0)
					[
						SNew(SImage)
						.Image(this, &FStateTreeEditorNodeDetails::GetIcon)
						.ColorAndOpacity(this, &FStateTreeEditorNodeDetails::GetIconColor)
						.Visibility(this, &FStateTreeEditorNodeDetails::IsIconVisible)
					]
					// Rich text description and name edit 
					+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(0, 0, 0, 0)
					[
						SAssignNew(NameSwitcher, SWidgetSwitcher)
						.WidgetIndex(0)
						+ SWidgetSwitcher::Slot()
						[
							SNew(SBox)
							.Padding(FMargin(1,0, 1, 1))
							[
								SNew(SRichTextBlock)
								.Text(this, &FStateTreeEditorNodeDetails::GetNodeDescription)
								.TextStyle(&FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("StateTree.Node.Normal"))
								.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
								.Visibility(this, &FStateTreeEditorNodeDetails::IsNodeDescriptionVisible)
								.ToolTipText(this, &FStateTreeEditorNodeDetails::GetNodeTooltip)
								+SRichTextBlock::Decorator(FTextStyleDecorator::Create(TEXT(""), FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("StateTree.Node.Normal")))
								+SRichTextBlock::Decorator(FTextStyleDecorator::Create(TEXT("b"), FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("StateTree.Node.Bold")))
								+SRichTextBlock::Decorator(FTextStyleDecorator::Create(TEXT("s"), FStateTreeEditorStyle::Get().GetWidgetStyle<FTextBlockStyle>("StateTree.Node.Subdued")))
							]
						]
						+ SWidgetSwitcher::Slot()
						[
							SAssignNew(NameEdit, SInlineEditableTextBlock)
							.Style(FStateTreeEditorStyle::Get(), "StateTree.Node.TitleInlineEditableText")
							.Text(this, &FStateTreeEditorNodeDetails::GetName)
							.OnTextCommitted(this, &FStateTreeEditorNodeDetails::HandleNameCommitted)
							.OnVerifyTextChanged(this, &FStateTreeEditorNodeDetails::HandleVerifyNameChanged)
							.Visibility(this, &FStateTreeEditorNodeDetails::IsNodeDescriptionVisible)
						]
					]
				]
				// Close parens
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.TextStyle(FStateTreeEditorStyle::Get(), "StateTree.Node.Parens")
					.Text(this, &FStateTreeEditorNodeDetails::GetCloseParens)
					.Visibility(this, &FStateTreeEditorNodeDetails::AreParensVisible)
				]

				// Debug and property widgets
				+ SHorizontalBox::Slot()
				.FillContentWidth(1.0f, 0.0f) // grow, no shrinking
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.Padding(FMargin(8, 0, 2, 0))
				[
					SNew(SHorizontalBox)
					
					// Debugger labels
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						UE::StateTreeEditor::DebuggerExtensions::CreateEditorNodeWidget(StructPropertyHandle, EditorData)
					]

					// Browse To BP Button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.Visibility(this, &FStateTreeEditorNodeDetails::IsBrowseToNodeBlueprintVisible)
						.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.OnClicked(this, &FStateTreeEditorNodeDetails::OnBrowseToNodeBlueprint)
							.ToolTipText(LOCTEXT("BrowseToCurrentNodeBP", "Browse to the current node blueprint in Content Browser"))
							.ContentPadding(.0f)
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.BrowseContent"))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
						]
					]
					// Edit BP Button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.Visibility(this, &FStateTreeEditorNodeDetails::IsEditNodeBlueprintVisible)
						.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.OnClicked(this, &FStateTreeEditorNodeDetails::OnEditNodeBlueprint)
							.ToolTipText(LOCTEXT("EditCurrentNodeBP", "Edit the current node blueprint in Editor"))
							.ContentPadding(.0f)
							[
								SNew(SImage)
								.Image(FAppStyle::GetBrush("Icons.Edit"))
								.ColorAndOpacity(FSlateColor::UseForeground())
							]
						]
					]

					// Options
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(FMargin(4.f, 0.f, 0.f, 0.f))
					[
						SNew(SComboButton)
						.ButtonStyle(FAppStyle::Get(), "SimpleButton")
						.OnGetMenuContent(this, &FStateTreeEditorNodeDetails::GenerateOptionsMenu)
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
				]
			]
		]
		.OverrideResetToDefault(ResetOverride)
		.CopyAction(FUIAction(FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnCopyNode)))
		.PasteAction(FUIAction(FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnPasteNode)));
}


FReply FStateTreeEditorNodeDetails::OnRowMouseDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply FStateTreeEditorNodeDetails::OnRowMouseUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(
			NameSwitcher.ToSharedRef(),
			WidgetPath,
			GenerateOptionsMenu(),
			MouseEvent.GetScreenSpacePosition(),
			FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

		return FReply::Handled();
	}
	
	return FReply::Unhandled();
}

void FStateTreeEditorNodeDetails::OnCopyNode()
{
	FString Value;
	// Use PPF_Copy so that all properties get copied.
	if (StructProperty->GetValueAsFormattedString(Value, PPF_Copy) == FPropertyAccess::Success)
	{
		FPlatformApplicationMisc::ClipboardCopy(*Value);
	}
}

void FStateTreeEditorNodeDetails::OnPasteNode()
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);

	if (PastedText.IsEmpty())
	{
		return;
	}
	
	// Create node from the clipboard data to figure out the node type. 
	FStateTreeEditorNode TempNode;
	UScriptStruct* NodeScriptStruct = TBaseStructure<FStateTreeEditorNode>::Get();

	TArray<UObject*> OuterObjects; 
	StructProperty->GetOuterObjects(OuterObjects);
	if (OuterObjects.IsEmpty())
	{
		return;
	}

	FStateTreeDefaultValueImportErrorContext ErrorPipe;
	NodeScriptStruct->ImportText(*PastedText, &TempNode, nullptr, PPF_None, &ErrorPipe, NodeScriptStruct->GetName());
	
	const UStruct* NodeTypeStruct = TempNode.Node.GetScriptStruct(); 
	// Only allow valid node types for this property (e.g. do not mix task with conditions).
	if (ErrorPipe.NumErrors > 0 || !NodeTypeStruct || !NodeTypeStruct->IsChildOf(BaseScriptStruct))
	{
		FNotificationInfo NotificationInfo(FText::GetEmpty());
		NotificationInfo.Text = FText::Format(LOCTEXT("NotSupportedByType", "This property only accepts nodes of type {0}."), BaseScriptStruct->GetDisplayNameText());
		NotificationInfo.ExpireDuration = 5.0f;
		FSlateNotificationManager::Get().AddNotification(NotificationInfo);
		return;
	}
	
	// Reject nodes that are not allowed by the schema.
	const UStateTreeSchema* Schema = EditorData ? EditorData->Schema : nullptr;
	if (Schema)
	{
		bool bNodeIsAllowed = false;
		
		// BP nodes are identified by the instance type.
		if (NodeTypeStruct->IsChildOf(FStateTreeBlueprintEvaluatorWrapper::StaticStruct())
			|| NodeTypeStruct->IsChildOf(FStateTreeBlueprintTaskWrapper::StaticStruct())
			|| NodeTypeStruct->IsChildOf(FStateTreeBlueprintConditionWrapper::StaticStruct())
			|| NodeTypeStruct->IsChildOf(FStateTreeBlueprintConsiderationWrapper::StaticStruct()))
		{
			if (const FStateTreeNodeBase* Node = TempNode.Node.GetPtr<FStateTreeNodeBase>())
			{
				NodeTypeStruct = Node->GetInstanceDataType(); // Report error with the BP node type, as that is what the user expects to see.
				if (const UClass* InstanceClass = Cast<UClass>(NodeTypeStruct))
				{
					bNodeIsAllowed = Schema->IsClassAllowed(InstanceClass);
				}
			}
		}
		else
		{
			bNodeIsAllowed = Schema->IsStructAllowed(TempNode.Node.GetScriptStruct());
		}
		
		if (!bNodeIsAllowed)
		{

			FNotificationInfo NotificationInfo(FText::GetEmpty());
			NotificationInfo.Text = FText::Format(LOCTEXT("NotSupportedBySchema", "Node {0} is not supported by {1} schema."),
										NodeTypeStruct->GetDisplayNameText(), Schema->GetClass()->GetDisplayNameText());
			NotificationInfo.ExpireDuration = 5.0f;
			FSlateNotificationManager::Get().AddNotification(NotificationInfo);
			return;
		}
	}
	
	FScopedTransaction Transaction(LOCTEXT("PasteNode", "Paste Node"));

	StructProperty->NotifyPreChange();

	// Reset GUIDs on paste
	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	if (OuterObjects.Num() == RawNodeData.Num())
	{
		for (int32 Index = 0; Index < RawNodeData.Num(); Index++)
		{
			UObject* OuterObject = OuterObjects[Index];
			FStateTreeEditorNode* EditorNode = static_cast<FStateTreeEditorNode*>(RawNodeData[Index]);
			if (EditorNode && OuterObject)
			{
				// Copy
				*EditorNode = TempNode;

				// Ensure unique instance value
				UE::StateTreeEditor::EditorNodeUtils::InstantiateStructSubobjects(*OuterObject, EditorNode->Node);
				if (EditorNode->InstanceObject)
				{
					EditorNode->InstanceObject = DuplicateObject(EditorNode->InstanceObject, OuterObject);
				}
				else
				{
					UE::StateTreeEditor::EditorNodeUtils::InstantiateStructSubobjects(*OuterObject, EditorNode->Instance);
				}
				
				const FGuid OldStructID = EditorNode->ID; 
				EditorNode->ID = FGuid::NewGuid();

				// Copy bindings from the copied node.
				if (OldStructID.IsValid() && EditorData)
				{
					if (FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings())
					{
						Bindings->CopyBindings(OldStructID, EditorNode->ID);
					}
				}

			}
		}
	}

	StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructProperty->NotifyFinishedChangingProperties();

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

bool FStateTreeEditorNodeDetails::ShouldResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle) const
{
	check(StructProperty);
	
	bool bAnyValid = false;
	
	TArray<const void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	for (const void* Data : RawNodeData)
	{
		if (const FStateTreeEditorNode* Node = static_cast<const FStateTreeEditorNode*>(Data))
		{
			if (Node->Node.IsValid())
			{
				bAnyValid = true;
				break;
			}
		}
	}
	
	// Assume that the default value is empty. Any valid means that some can be reset to empty.
	return bAnyValid;
}


void FStateTreeEditorNodeDetails::ResetToDefault(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	UE::StateTreeEditor::EditorNodeUtils::ModifyNodeInTransaction(LOCTEXT("OnTaskEnableToggled", "Toggled Task Enabled"),
		StructProperty,
		[](const TSharedPtr<IPropertyHandle>& StructPropertyHandle)
		{
			TArray<void*> RawNodeData;
			StructPropertyHandle->AccessRawData(RawNodeData);
			for (void* Data : RawNodeData)
			{
				if (FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(Data))
				{
					Node->Reset();
				}
			}
		});

	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeEditorNodeDetails::CustomizeChildren(TSharedRef<class IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FGuid ID;
	UE::StateTree::PropertyHelpers::GetStructValue<FGuid>(IDProperty, ID);

	// ID
	if (UE::StateTree::Editor::GbDisplayItemIds)
	{
		// ID
		StructBuilder.AddProperty(IDProperty.ToSharedRef());
	}
	
	// Node
	TSharedRef<FBindableNodeInstanceDetails> NodeDetails = MakeShareable(new FBindableNodeInstanceDetails(NodeProperty, {}, EditorData));
	StructBuilder.AddCustomBuilder(NodeDetails);

	// Instance
	TSharedRef<FBindableNodeInstanceDetails> InstanceDetails = MakeShareable(new FBindableNodeInstanceDetails(InstanceProperty, IDProperty, EditorData));
	StructBuilder.AddCustomBuilder(InstanceDetails);

	// InstanceObject
	// Get the actual UObject from the pointer.
	TSharedPtr<IPropertyHandle> InstanceObjectValueProperty = GetInstancedObjectValueHandle(InstanceObjectProperty);
	if (InstanceObjectValueProperty.IsValid())
	{
		uint32 NumChildren = 0;
		InstanceObjectValueProperty->GetNumChildren(NumChildren);

		// Find visible child properties and sort them so in order: Context, Input, Param, Output.
		struct FSortedChild
		{
			TSharedPtr<IPropertyHandle> PropertyHandle;
			EStateTreePropertyUsage Usage = EStateTreePropertyUsage::Invalid;
		};

		TArray<FSortedChild> SortedChildren;
		for (uint32 Index = 0; Index < NumChildren; Index++)
		{
			if (TSharedPtr<IPropertyHandle> ChildHandle = InstanceObjectValueProperty->GetChildHandle(Index); ChildHandle.IsValid())
			{
				FSortedChild Child;
				Child.PropertyHandle = ChildHandle;
				Child.Usage = UE::StateTree::GetUsageFromMetaData(Child.PropertyHandle->GetProperty());

				// If the property is set to one of these usages, display it even if it is not edit on instance.
				// It is a common mistake to forget to set the "eye" on these properties it and wonder why it does not show up.
				const bool bShouldShowByUsage = Child.Usage == EStateTreePropertyUsage::Input || Child.Usage == EStateTreePropertyUsage::Output || Child.Usage == EStateTreePropertyUsage::Context;
        		const bool bIsEditable = !Child.PropertyHandle->GetProperty()->HasAllPropertyFlags(CPF_DisableEditOnInstance);

				if (bShouldShowByUsage || bIsEditable)
				{
					SortedChildren.Add(Child);
				}
			}
		}

		SortedChildren.StableSort([](const FSortedChild& LHS, const FSortedChild& RHS) { return LHS.Usage < RHS.Usage; });

		for (FSortedChild& Child : SortedChildren)
		{
			IDetailPropertyRow& ChildRow = StructBuilder.AddProperty(Child.PropertyHandle.ToSharedRef());
			UE::StateTreeEditor::Internal::ModifyRow(ChildRow, ID, EditorData);
		}
	}
}

TSharedPtr<IPropertyHandle> FStateTreeEditorNodeDetails::GetInstancedObjectValueHandle(TSharedPtr<IPropertyHandle> PropertyHandle)
{
	TSharedPtr<IPropertyHandle> ChildHandle;

	uint32 NumChildren;
	PropertyHandle->GetNumChildren(NumChildren);

	if (NumChildren > 0)
	{
		// when the property is a (inlined) object property, the first child will be
		// the object instance, and its properties are the children underneath that
		ensure(NumChildren == 1);
		ChildHandle = PropertyHandle->GetChildHandle(0);
	}

	return ChildHandle;
}

void FStateTreeEditorNodeDetails::OnIdentifierChanged(const UStateTree& InStateTree)
{
	if (PropUtils && StateTree == &InStateTree)
	{
		PropUtils->ForceRefresh();
	}
}

void FStateTreeEditorNodeDetails::OnBindingChanged(const FStateTreePropertyPath& SourcePath, const FStateTreePropertyPath& TargetPath)
{
	check(StructProperty);

	if (!EditorData)
	{
		return;
	}

	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);

	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);

	if (OuterObjects.Num() != RawNodeData.Num())
	{
		return;
	}

	const FStateTreeBindingLookup BindingLookup(EditorData);

	for (int32 i = 0; i < OuterObjects.Num(); i++)
	{
		FStateTreeEditorNode* EditorNode = static_cast<FStateTreeEditorNode*>(RawNodeData[i]);
		UObject* OuterObject = OuterObjects[i]; // Immediate outer, i.e StateTreeState
		if (EditorNode && OuterObject && EditorNode->ID == TargetPath.GetStructID())
		{
			FStateTreeNodeBase* Node = EditorNode->Node.GetMutablePtr<FStateTreeNodeBase>();
			FStateTreeDataView InstanceView = EditorNode->GetInstance(); 

			if (Node && InstanceView.IsValid())
			{
				OuterObject->Modify();
				Node->OnBindingChanged(EditorNode->ID, InstanceView, SourcePath, TargetPath, BindingLookup);
			}
		}
	}
}

void FStateTreeEditorNodeDetails::FindOuterObjects()
{
	check(StructProperty);
	
	EditorData = nullptr;
	StateTree = nullptr;

	TArray<UObject*> OuterObjects;
	StructProperty->GetOuterObjects(OuterObjects);
	for (UObject* Outer : OuterObjects)
	{
		UStateTreeEditorData* OuterEditorData = Cast<UStateTreeEditorData>(Outer);
		if (OuterEditorData == nullptr)
		{
			OuterEditorData = Outer->GetTypedOuter<UStateTreeEditorData>();
		}
		
		UStateTree* OuterStateTree = OuterEditorData ? OuterEditorData->GetTypedOuter<UStateTree>() : nullptr;
		if (OuterEditorData && OuterStateTree)
		{
			StateTree = OuterStateTree;
			EditorData = OuterEditorData;
			break;
		}
	}
}

FOptionalSize FStateTreeEditorNodeDetails::GetIndentSize() const
{
	return FOptionalSize(static_cast<float>(GetIndent()) * 30.0f);
}

FReply FStateTreeEditorNodeDetails::HandleIndentPlus()
{
	SetIndent(GetIndent() + 1);
	return FReply::Handled();
}

FReply FStateTreeEditorNodeDetails::HandleIndentMinus()
{
	SetIndent(GetIndent() - 1);
	return FReply::Handled();
}

int32 FStateTreeEditorNodeDetails::GetIndent() const
{
	check(IndentProperty);
	
	uint8 Indent = 0;
	IndentProperty->GetValue(Indent);

	return Indent;
}

void FStateTreeEditorNodeDetails::SetIndent(const int32 Indent) const
{
	check(IndentProperty);
	
	IndentProperty->SetValue((uint8)FMath::Clamp(Indent, 0, UE::StateTree::MaxExpressionIndent - 1));
}

bool FStateTreeEditorNodeDetails::IsIndent(const int32 Indent) const
{
	return Indent == GetIndent();
}

bool FStateTreeEditorNodeDetails::IsFirstItem() const
{
	check(StructProperty);
	return StructProperty->GetIndexInArray() == 0;
}

int32 FStateTreeEditorNodeDetails::GetCurrIndent() const
{
	// First item needs to be zero indent to make the parentheses counting to work properly.
	return IsFirstItem() ? 0 : (GetIndent() + 1);
}

int32 FStateTreeEditorNodeDetails::GetNextIndent() const
{
	// Find the intent of the next item by finding the item in the parent array.
	check(StructProperty);
	TSharedPtr<IPropertyHandle> ParentProp = StructProperty->GetParentHandle();
	if (!ParentProp.IsValid())
	{
		return 0;
	}
	TSharedPtr<IPropertyHandleArray> ParentArray = ParentProp->AsArray();
	if (!ParentArray.IsValid())
	{
		return 0;
	}

	uint32 NumElements = 0;
	if (ParentArray->GetNumElements(NumElements) != FPropertyAccess::Success)
	{
		return 0;
	}
	
	const int32 NextIndex = StructProperty->GetIndexInArray() + 1;
	if (NextIndex >= (int32)NumElements)
	{
		return 0;
	}

	TSharedPtr<IPropertyHandle> NextStructProperty = ParentArray->GetElement(NextIndex);
	if (!NextStructProperty.IsValid())
	{
		return 0;
	}
	
	TSharedPtr<IPropertyHandle> NextIndentProperty = NextStructProperty->GetChildHandle(GET_MEMBER_NAME_CHECKED(FStateTreeEditorNode, ExpressionIndent));
	if (!NextIndentProperty.IsValid())
	{
		return 0;
	}
	
	uint8 Indent = 0;
	NextIndentProperty->GetValue(Indent);

	return Indent + 1;
}

FText FStateTreeEditorNodeDetails::GetOpenParens() const
{
	check(IndentProperty);

	const int32 CurrIndent = GetCurrIndent();
	const int32 NextIndent = GetNextIndent();
	const int32 DeltaIndent = NextIndent - CurrIndent;
	const int32 OpenParens = FMath::Max(0, DeltaIndent);

	static_assert(UE::StateTree::MaxExpressionIndent == 4);
	switch (OpenParens)
	{
		case 1: return FText::FromString(TEXT("("));
		case 2: return FText::FromString(TEXT("(("));
		case 3: return FText::FromString(TEXT("((("));
		case 4: return FText::FromString(TEXT("(((("));
	}
	return FText::GetEmpty();
}

FText FStateTreeEditorNodeDetails::GetCloseParens() const
{
	check(IndentProperty);

	const int32 CurrIndent = GetCurrIndent();
	const int32 NextIndent = GetNextIndent();
	const int32 DeltaIndent = NextIndent - CurrIndent;
	const int32 CloseParens = FMath::Max(0, -DeltaIndent);

	static_assert(UE::StateTree::MaxExpressionIndent == 4);
	switch (CloseParens)
	{
	case 1: return FText::FromString(TEXT(")"));
	case 2: return FText::FromString(TEXT("))"));
	case 3: return FText::FromString(TEXT(")))"));
	case 4: return FText::FromString(TEXT("))))"));
	}
	return FText::GetEmpty();
}

FText FStateTreeEditorNodeDetails::GetOperandText() const
{
	check(OperandProperty);

	// First item does not relate to anything existing, it could be empty. 
	// return IF to indicate that we're building condition and IS for consideration.
	if (IsFirstItem())
	{
		if (IsConditionVisible() == EVisibility::Visible)
		{
			return LOCTEXT("IfOperand", "IF");
		}
		else //IsConsiderationVisible() == EVisibility::Visible
		{
			return LOCTEXT("IsOperand", "IS");
		}
	}

	uint8 Value = 0;
	OperandProperty->GetValue(Value);
	const EStateTreeExpressionOperand Operand = (EStateTreeExpressionOperand)Value;

	if (Operand == EStateTreeExpressionOperand::And)
	{
		return LOCTEXT("AndOperand", "AND");
	}
	else if (Operand == EStateTreeExpressionOperand::Or)
	{
		return LOCTEXT("OrOperand", "OR");
	}
	else
	{
		ensureMsgf(false, TEXT("Unhandled operand %s"), *UEnum::GetValueAsString(Operand));
	}

	return FText::GetEmpty();
}

FSlateColor FStateTreeEditorNodeDetails::GetOperandColor() const
{
	check(OperandProperty);

	if (IsFirstItem())
	{
		return FStyleColors::Transparent;
	}

	uint8 Value = 0; 
	OperandProperty->GetValue(Value);
	const EStateTreeExpressionOperand Operand = (EStateTreeExpressionOperand)Value;

	if (Operand == EStateTreeExpressionOperand::And)
	{
		return FStyleColors::AccentPink;
	}
	else if (Operand == EStateTreeExpressionOperand::Or)
	{
		return FStyleColors::AccentBlue;
	}
	else
	{
		ensureMsgf(false, TEXT("Unhandled operand %s"), *UEnum::GetValueAsString(Operand));
	}

	return FStyleColors::Transparent;
}

TSharedRef<SWidget> FStateTreeEditorNodeDetails::OnGetOperandContent() const
{
	FMenuBuilder MenuBuilder(true, NULL);

	FUIAction AndAction(
		FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::SetOperand, EStateTreeExpressionOperand::And),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FStateTreeEditorNodeDetails::IsOperand, EStateTreeExpressionOperand::And));
	MenuBuilder.AddMenuEntry(LOCTEXT("AndOperand", "AND"), TAttribute<FText>(), FSlateIcon(), AndAction, FName(), EUserInterfaceActionType::Check);

	FUIAction OrAction(FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::SetOperand, EStateTreeExpressionOperand::Or),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FStateTreeEditorNodeDetails::IsOperand, EStateTreeExpressionOperand::Or));
	MenuBuilder.AddMenuEntry(LOCTEXT("OrOperand", "OR"), TAttribute<FText>(), FSlateIcon(), OrAction, FName(), EUserInterfaceActionType::Check);

	return MenuBuilder.MakeWidget();
}

bool FStateTreeEditorNodeDetails::IsOperandEnabled() const
{
	return !IsFirstItem();
}

bool FStateTreeEditorNodeDetails::IsOperand(const EStateTreeExpressionOperand Operand) const
{
	check(OperandProperty);

	uint8 Value = 0; 
	OperandProperty->GetValue(Value);
	const EStateTreeExpressionOperand CurrOperand = (EStateTreeExpressionOperand)Value;

	return CurrOperand == Operand;
}

void FStateTreeEditorNodeDetails::SetOperand(const EStateTreeExpressionOperand Operand) const
{
	check(OperandProperty);

	OperandProperty->SetValue((uint8)Operand);
}

EVisibility FStateTreeEditorNodeDetails::IsConditionVisible() const
{
	return UE::StateTreeEditor::EditorNodeUtils::IsConditionVisible(StructProperty);
}

EVisibility FStateTreeEditorNodeDetails::IsConsiderationVisible() const
{
	return UE::StateTreeEditor::EditorNodeUtils::IsConsiderationVisible(StructProperty);
}

EVisibility FStateTreeEditorNodeDetails::IsOperandVisible() const
{
	// Assume the Condition and Consideration's Visibility is either Visible or Collapsed
	if (IsConditionVisible() == EVisibility::Visible || IsConsiderationVisible() == EVisibility::Visible)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

EVisibility FStateTreeEditorNodeDetails::AreIndentButtonsVisible() const
{
	if (IsFirstItem())
	{
		return EVisibility::Collapsed;
	}

	// Assume the Condition and Consideration's Visibility is either Visible or Collapsed
	if (IsConditionVisible() == EVisibility::Visible || IsConsiderationVisible() == EVisibility::Visible)
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

EVisibility FStateTreeEditorNodeDetails::AreParensVisible() const
{
	//Assume the Condition and Consideration's Visibility is either Visible or Collapsed
	if (EVisibility::Visible.Value & (IsConditionVisible().Value | IsConsiderationVisible().Value))
	{
		return EVisibility::Visible;
	}

	return EVisibility::Collapsed;
}

EVisibility FStateTreeEditorNodeDetails::IsIconVisible() const
{
	return UE::StateTreeEditor::EditorNodeUtils::IsIconVisible(StructProperty);
}

const FSlateBrush* FStateTreeEditorNodeDetails::GetIcon() const
{
	return UE::StateTreeEditor::EditorNodeUtils::GetIcon(StructProperty).GetIcon();
}

FSlateColor FStateTreeEditorNodeDetails::GetIconColor() const
{
	return UE::StateTreeEditor::EditorNodeUtils::GetIconColor(StructProperty);
}

FReply FStateTreeEditorNodeDetails::OnDescriptionClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) const
{
	if (NameSwitcher && NameEdit)
	{
		if (NameSwitcher->GetActiveWidgetIndex() == 0)
		{
			// Enter edit mode
			NameSwitcher->SetActiveWidgetIndex(1);

			// Focus on name edit.
			FReply Reply = FReply::Handled();
			Reply.SetUserFocus(NameEdit.ToSharedRef());
			NameEdit->EnterEditingMode();
			return Reply;
		}
	}

	return FReply::Unhandled();
}

FText FStateTreeEditorNodeDetails::GetNodeDescription() const
{
	check(StructProperty);
	if (!EditorData)
	{
		return FText::GetEmpty();
	}
	
	// Multiple names do not make sense, just if only one node is selected.
	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	if (RawNodeData.Num() == 1)
	{
		FText Description = LOCTEXT("EmptyNodeRich", "<s>None</>");
		if (const FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(RawNodeData[0]))
		{
			return EditorData->GetNodeDescription(*Node, EStateTreeNodeFormatting::RichText);
		}
		return Description;
	}

	return LOCTEXT("MultipleSelectedRich", "<s>Multiple Selected</>");
}

EVisibility FStateTreeEditorNodeDetails::IsNodeDescriptionVisible() const
{
	const UScriptStruct* ScriptStruct = nullptr;
	if (const FStateTreeEditorNode* Node = UE::StateTreeEditor::EditorNodeUtils::GetCommonNode(StructProperty))
	{
		ScriptStruct = Node->Node.GetScriptStruct();
	}

	if (ScriptStruct != nullptr && ScriptStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
	{
		const UStateTreeSchema* Schema = EditorData ? EditorData->Schema : nullptr;
		if (Schema && Schema->AllowMultipleTasks() == false)
		{
			// Single task states use the state name as task name.
			return EVisibility::Collapsed;
		}
	}
	
	return EVisibility::Visible;
}

FText FStateTreeEditorNodeDetails::GetNodeTooltip() const
{
	check(StructProperty);
	if (!EditorData)
	{
		return FText::GetEmpty();
	}
	
	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	if (RawNodeData.Num() == 1)
	{
		FText NameText;
		FText PathText;
		FText DescText;

		if (const FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(RawNodeData[0]))
		{
			const UStruct* Struct = Node->GetInstance().GetStruct();
			if (Struct == nullptr || !Struct->IsChildOf<UStateTreeNodeBlueprintBase>())
			{
				Struct = Node->Node.GetScriptStruct();
			}

			if (Struct)
			{
				static const FName NAME_Tooltip(TEXT("Tooltip"));
				const FText StructToolTipText = Struct->HasMetaData(NAME_Tooltip) ? Struct->GetToolTipText() : FText::GetEmpty();

				FTextBuilder TooltipBuilder;
				TooltipBuilder.AppendLineFormat(LOCTEXT("NodeTooltip", "{0} ({1})"), Struct->GetDisplayNameText(), FText::FromString(Struct->GetPathName()));

				if (!StructToolTipText.IsEmpty())
				{
					TooltipBuilder.AppendLine();
					TooltipBuilder.AppendLine(StructToolTipText);
				}
				return TooltipBuilder.ToText();
			}
		}
	}

	return FText::GetEmpty();
}

FText FStateTreeEditorNodeDetails::GetName() const
{
	check(StructProperty);

	// Multiple names do not make sense, just if only one node is selected.
	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	if (RawNodeData.Num() == 1)
	{
		if (const FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(RawNodeData[0]))
		{
			if (const FStateTreeNodeBase* BaseNode = Node->Node.GetPtr<FStateTreeNodeBase>())
			{
				if (!BaseNode->Name.IsNone())
				{
					return FText::FromName(BaseNode->Name);
				}
				const FText Desc = EditorData->GetNodeDescription(*Node, EStateTreeNodeFormatting::Text);
				if (!Desc.IsEmpty())
				{
					return Desc;
				}
			}
		}

		return FText::GetEmpty();
	}

	return LOCTEXT("MultipleSelected", "Multiple Selected");
}

bool FStateTreeEditorNodeDetails::HandleVerifyNameChanged(const FText& InText, FText& OutErrorMessage) const
{
	const FString NewName = FText::TrimPrecedingAndTrailing(InText).ToString();
	if (NewName.Len() >= NAME_SIZE)
	{
		OutErrorMessage = LOCTEXT("VerifyNodeLabelFailed_MaxLength", "Max length exceeded");
		return false;
	}
	return NewName.Len() > 0;
}

void FStateTreeEditorNodeDetails::HandleNameCommitted(const FText& NewText, ETextCommit::Type InTextCommit) const
{
	check(StructProperty);

	if (InTextCommit == ETextCommit::OnEnter || InTextCommit == ETextCommit::OnUserMovedFocus)
	{
		// Remove excess whitespace and prevent categories with just spaces
		const FString NewName = FText::TrimPrecedingAndTrailing(NewText).ToString();
		if (NewName.Len() > 0 && NewName.Len() < NAME_SIZE)
		{
			if (GEditor)
			{
				GEditor->BeginTransaction(LOCTEXT("SetName", "Set Name"));
			}
			StructProperty->NotifyPreChange();

			TArray<void*> RawNodeData;
			StructProperty->AccessRawData(RawNodeData);

			for (void* Data : RawNodeData)
			{
				// Set Name
				if (FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(Data))
				{
					if (FStateTreeNodeBase* BaseNode = Node->Node.GetMutablePtr<FStateTreeNodeBase>())
					{
						BaseNode->Name = FName(NewName);
					}
				}
			}

			StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);

			if (StateTree)
			{
				UE::StateTree::Delegates::OnIdentifierChanged.Broadcast(*StateTree);
			}

			if (GEditor)
			{
				GEditor->EndTransaction();
			}

			StructProperty->NotifyFinishedChangingProperties();
		}
	}

	// Switch back to rich view.
	NameSwitcher->SetActiveWidgetIndex(0);
}

FText FStateTreeEditorNodeDetails::GetNodePickerTooltip() const
{
	check(StructProperty);
	if (!EditorData)
	{
		return FText::GetEmpty();
	}

	FTextBuilder TextBuilder;

	// Append full description.
	TArray<void*> RawNodeData;
	StructProperty->AccessRawData(RawNodeData);
	if (RawNodeData.Num() == 1)
	{
		FText Description = LOCTEXT("EmptyNodeStyled", "<s>None</>");
		if (const FStateTreeEditorNode* Node = static_cast<FStateTreeEditorNode*>(RawNodeData[0]))
		{
			TextBuilder.AppendLine(EditorData->GetNodeDescription(*Node));
		}
	}

	if (TextBuilder.GetNumLines() > 0)
	{
		TextBuilder.AppendLine(FText::GetEmpty());
	}
	
	// Text describing the type.
	if (const FStateTreeEditorNode* Node = UE::StateTreeEditor::EditorNodeUtils::GetCommonNode(StructProperty))
	{
		if (const UScriptStruct* ScriptStruct = Node->Node.GetScriptStruct())
		{
			if (ScriptStruct->IsChildOf(FStateTreeBlueprintEvaluatorWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintTaskWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintConditionWrapper::StaticStruct()))
			{
				if (Node->InstanceObject != nullptr
					&& Node->InstanceObject->GetClass() != nullptr)
				{
					TextBuilder.AppendLine(Node->InstanceObject->GetClass()->GetDisplayNameText());
				}
			}
			else
			{
				TextBuilder.AppendLine(ScriptStruct->GetDisplayNameText());
			}
		}
	}

	return TextBuilder.ToText();
}

FReply FStateTreeEditorNodeDetails::OnBrowseToNodeBlueprint() const
{
	const UObject* InstanceObject = nullptr;
	const FPropertyAccess::Result AccessResult = InstanceObjectProperty->GetValue(InstanceObject);
	if (AccessResult == FPropertyAccess::Success)
	{
		check(InstanceObject);
		if (const UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(InstanceObject->GetClass()))
		{
			//If the blueprint asset has been cooked, UBlueprint Object will be set to null and we need to browse to its BlueprintGeneratedClass
			GEditor->SyncBrowserToObject(BlueprintGeneratedClass->ClassGeneratedBy ? BlueprintGeneratedClass->ClassGeneratedBy.Get() : BlueprintGeneratedClass);
		}
	}

	return FReply::Handled();
}

FReply FStateTreeEditorNodeDetails::OnEditNodeBlueprint() const
{
	const UObject* InstanceObject = nullptr;
	const FPropertyAccess::Result AccessResult = InstanceObjectProperty->GetValue(InstanceObject);
	if (AccessResult == FPropertyAccess::Success)
	{
		check(InstanceObject);
		const UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(InstanceObject->GetClass());
		if (BlueprintGeneratedClass && BlueprintGeneratedClass->ClassGeneratedBy)
		{
			//Cooked blueprint asset is not editable
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(BlueprintGeneratedClass->ClassGeneratedBy);
		}
	}

	return FReply::Handled();
}

EVisibility FStateTreeEditorNodeDetails::IsBrowseToNodeBlueprintVisible() const
{
	const UObject* InstanceObject = nullptr;
	const FPropertyAccess::Result AccessResult = InstanceObjectProperty->GetValue(InstanceObject);
	if (AccessResult == FPropertyAccess::Success)
	{
		//The read could be null with an Success AccessResult in updating visibility 
		if (InstanceObject)
		{
			if (const UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(InstanceObject->GetClass()))
			{
				return EVisibility::Visible;
			}
		}
	}

	return EVisibility::Collapsed;
}

EVisibility FStateTreeEditorNodeDetails::IsEditNodeBlueprintVisible() const
{
	const UObject* InstanceObject = nullptr;
	const FPropertyAccess::Result AccessResult = InstanceObjectProperty->GetValue(InstanceObject);
	if (AccessResult == FPropertyAccess::Success)
	{
		//The read could be null with an Success AccessResult in updating visibility 
		if (InstanceObject)
		{
			const UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(InstanceObject->GetClass());
			if (BlueprintGeneratedClass && BlueprintGeneratedClass->ClassGeneratedBy)
			{
				//Cooked blueprint asset is not editable
				return EVisibility::Visible;
			}
		}
	}

	return EVisibility::Collapsed;
}

void FStateTreeEditorNodeDetails::GeneratePickerMenu(class FMenuBuilder& InMenuBuilder)
{
	// Expand and select currently selected item.
	const UStruct* CommonStruct  = nullptr;
	if (const FStateTreeEditorNode* Node = UE::StateTreeEditor::EditorNodeUtils::GetCommonNode(StructProperty))
	{
		if (const UScriptStruct* ScriptStruct = Node->Node.GetScriptStruct())
		{
			if (ScriptStruct->IsChildOf(FStateTreeBlueprintEvaluatorWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintTaskWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintConditionWrapper::StaticStruct())
				|| ScriptStruct->IsChildOf(FStateTreeBlueprintConsiderationWrapper::StaticStruct()))
			{
				if (Node->InstanceObject != nullptr)
				{
					CommonStruct = Node->InstanceObject->GetClass();
				}
			}
			else
			{
				CommonStruct = ScriptStruct;
			}
		}
	}

	TSharedRef<SStateTreeNodeTypePicker> Picker = SNew(SStateTreeNodeTypePicker)
		.Schema(EditorData->Schema)
		.BaseScriptStruct(BaseScriptStruct)
		.BaseClass(BaseClass)
		.CurrentStruct(CommonStruct)
		.OnNodeTypePicked(SStateTreeNodeTypePicker::FOnNodeStructPicked::CreateSP(this, &FStateTreeEditorNodeDetails::OnNodePicked));
	
	InMenuBuilder.AddWidget(SNew(SBox)
		.MinDesiredWidth(400.f)
		.MinDesiredHeight(300.f)
		.MaxDesiredHeight(300.f)
		.Padding(2)	
		[
			Picker
		],
		FText::GetEmpty(), /*bNoIdent*/true);
}
	
TSharedRef<SWidget> FStateTreeEditorNodeDetails::GenerateOptionsMenu()
{
	FMenuBuilder MenuBuilder(/*ShouldCloseWindowAfterMenuSelection*/true, /*CommandList*/nullptr);

	MenuBuilder.BeginSection(FName("Type"), LOCTEXT("Type", "Type"));

	// Change type
	MenuBuilder.AddSubMenu(
		LOCTEXT("ReplaceWith", "Replace With"),
		FText::GetEmpty(),
		FNewMenuDelegate::CreateSP(this, &FStateTreeEditorNodeDetails::GeneratePickerMenu));

	MenuBuilder.EndSection();

	MenuBuilder.BeginSection(FName("Edit"), LOCTEXT("Edit", "Edit"));

	// Copy
	MenuBuilder.AddMenuEntry(
		LOCTEXT("CopyItem", "Copy"),
		LOCTEXT("CopyItemTooltip", "Copy this item"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnCopyNode),
			FCanExecuteAction()
		));

	// Paste
	MenuBuilder.AddMenuEntry(
		LOCTEXT("PasteItem", "Paste"),
		LOCTEXT("PasteItemTooltip", "Paste into this item"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Paste"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnPasteNode),
			FCanExecuteAction()
		));

	// Duplicate
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DuplicateItem", "Duplicate"),
		LOCTEXT("DuplicateItemTooltip", "Duplicate this item"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Duplicate"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnDuplicateNode),
			FCanExecuteAction()
		));

	// Delete
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteItem", "Delete"),
		LOCTEXT("DeleteItemTooltip", "Delete this item"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Delete"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnDeleteNode),
			FCanExecuteAction()
		));

	// Delete
	MenuBuilder.AddMenuEntry(
		LOCTEXT("DeleteAllItems", "Delete all"),
		LOCTEXT("DeleteAllItemsTooltip", "Delete all items"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Delete"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnDeleteAllNodes),
			FCanExecuteAction()
		));

	// Rename
	MenuBuilder.AddMenuEntry(
		LOCTEXT("RenameNode", "Rename"),
		LOCTEXT("RenameNodeTooltip", "Rename this item"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Rename"),
		FUIAction(
			FExecuteAction::CreateSP(this, &FStateTreeEditorNodeDetails::OnRenameNode),
			FCanExecuteAction()
		));

	MenuBuilder.EndSection();

	// Append debugger items.
	UE::StateTreeEditor::DebuggerExtensions::AppendEditorNodeMenuItems(MenuBuilder, StructProperty, EditorData);

	return MenuBuilder.MakeWidget();
}

void FStateTreeEditorNodeDetails::OnDeleteNode() const
{
	const int32 Index = StructProperty->GetArrayIndex();
	if (const TSharedPtr<IPropertyHandle> ParentHandle = StructProperty->GetParentHandle())
	{
		if (const TSharedPtr<IPropertyHandleArray> ArrayHandle = ParentHandle->AsArray())
		{
			ArrayHandle->DeleteItem(Index);
		}
	}
}

void FStateTreeEditorNodeDetails::OnDeleteAllNodes() const
{
	if (const TSharedPtr<IPropertyHandle> ParentHandle = StructProperty->GetParentHandle())
	{
		if (const TSharedPtr<IPropertyHandleArray> ArrayHandle = ParentHandle->AsArray())
		{
			ArrayHandle->EmptyArray();
		}
	}
}

void FStateTreeEditorNodeDetails::OnDuplicateNode() const
{
	const int32 Index = StructProperty->GetArrayIndex();
	if (const TSharedPtr<IPropertyHandle> ParentHandle = StructProperty->GetParentHandle())
	{
		if (const TSharedPtr<IPropertyHandleArray> ArrayHandle = ParentHandle->AsArray())
		{
			ArrayHandle->DuplicateItem(Index);
		}
	}
}

void FStateTreeEditorNodeDetails::OnRenameNode() const
{
	if (NameSwitcher && NameEdit)
	{
		if (NameSwitcher->GetActiveWidgetIndex() == 0)
		{
			// Enter edit mode
			NameSwitcher->SetActiveWidgetIndex(1);

			FSlateApplication::Get().SetKeyboardFocus(NameEdit);
			FSlateApplication::Get().SetUserFocus(0, NameEdit);
			NameEdit->EnterEditingMode();
		}
	}
}

void FStateTreeEditorNodeDetails::OnNodePicked(const UStruct* InStruct) const
{
	GEditor->BeginTransaction(LOCTEXT("SelectNode", "Select Node"));

	StructProperty->NotifyPreChange();

	UE::StateTreeEditor::EditorNodeUtils::SetNodeType(StructProperty, InStruct);

	StructProperty->NotifyPostChange(EPropertyChangeType::ValueSet);
	StructProperty->NotifyFinishedChangingProperties();

	GEditor->EndTransaction();

	FSlateApplication::Get().DismissAllMenus();
	
	if (PropUtils)
	{
		PropUtils->ForceRefresh();
	}
}

#undef LOCTEXT_NAMESPACE
