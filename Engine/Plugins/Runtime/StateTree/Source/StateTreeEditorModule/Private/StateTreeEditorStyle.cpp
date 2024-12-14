// Copyright Epic Games, Inc. All Rights Reserved.

#include "StateTreeEditorStyle.h"
#include "Brushes/SlateBoxBrush.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "Styling/CoreStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/SlateTypes.h"
#include "Misc/Paths.h"
#include "Styling/StyleColors.h"
#include "StateTreeTypes.h"
#include "Styling/SlateStyleMacros.h"


namespace UE::StateTree::Editor
{

class FContentRootScope
{
public:
	FContentRootScope(FStateTreeEditorStyle* InStyle, const FString& NewContentRoot)
		: Style(InStyle)
		, PreviousContentRoot(InStyle->GetContentRootDir())
	{
		Style->SetContentRoot(NewContentRoot);
	}

	~FContentRootScope()
	{
		Style->SetContentRoot(PreviousContentRoot);
	}
private:
	FStateTreeEditorStyle* Style;
	FString PreviousContentRoot;
};

}; // UE::StateTree::Editor

FStateTreeEditorStyle::FStateTreeEditorStyle()
	: FSlateStyleSet(TEXT("StateTreeEditorStyle"))
{
	const FString EngineSlateContentDir = FPaths::EngineContentDir() / TEXT("Slate");
	const FString EngineEditorSlateContentDir = FPaths::EngineContentDir() / TEXT("Editor/Slate");
	SetCoreContentRoot(EngineSlateContentDir);

	const FString StateTreePluginContentDir = FPaths::EnginePluginsDir() / TEXT("Runtime/StateTree/Resources");
	SetContentRoot(StateTreePluginContentDir);

	const FScrollBarStyle ScrollBar = FAppStyle::GetWidgetStyle<FScrollBarStyle>("ScrollBar");
	const FTextBlockStyle& NormalText = FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");

	// State
	{
		const FTextBlockStyle StateIcon = FTextBlockStyle(NormalText)
			.SetFont(FAppStyle::Get().GetFontStyle("FontAwesome.12"))
			.SetColorAndOpacity(FLinearColor(230.0f / 255.0f, 230.0f / 255.0f, 230.0f / 255.0f, 0.5f));
		Set("StateTree.Icon", StateIcon);

		const FTextBlockStyle StateTitle = FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Bold", 12))
			.SetColorAndOpacity(FLinearColor(230.0f / 255.0f, 230.0f / 255.0f, 230.0f / 255.0f, 0.9f));
		Set("StateTree.State.Title", StateTitle);

		const FEditableTextBoxStyle StateTitleEditableText = FEditableTextBoxStyle()
			.SetTextStyle(NormalText)
			.SetFont(DEFAULT_FONT("Bold", 12))
			.SetBackgroundImageNormal(CORE_BOX_BRUSH("Common/TextBox", FMargin(4.0f / 16.0f)))
			.SetBackgroundImageHovered(CORE_BOX_BRUSH("Common/TextBox_Hovered", FMargin(4.0f / 16.0f)))
			.SetBackgroundImageFocused(CORE_BOX_BRUSH("Common/TextBox_Hovered", FMargin(4.0f / 16.0f)))
			.SetBackgroundImageReadOnly(CORE_BOX_BRUSH("Common/TextBox_ReadOnly", FMargin(4.0f / 16.0f)))
			.SetBackgroundColor(FLinearColor(0,0,0,0.1f))
			.SetPadding(FMargin(0))
			.SetScrollBarStyle(ScrollBar);
		Set("StateTree.State.TitleEditableText", StateTitleEditableText);

		Set("StateTree.State.TitleInlineEditableText", FInlineEditableTextBlockStyle()
			.SetTextStyle(StateTitle)
			.SetEditableTextBoxStyle(StateTitleEditableText));

		Set("StateTree.State.Border", new FSlateBorderBrush(NAME_None, FMargin(2.0f)));

		Set("StateTree.State", new FSlateRoundedBoxBrush(FLinearColor::White, 2.0f));
	}

	// Details
	{
		const FTextBlockStyle Details = FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Regular", 10))
			.SetColorAndOpacity(FLinearColor(230.0f / 255.0f, 230.0f / 255.0f, 230.0f / 255.0f, 0.75f));
		Set("StateTree.Details", Details);

		Set("StateTree.Node.Label", new FSlateRoundedBoxBrush(FStyleColors::AccentGray, 6.f));

		// For multi selection with mixed values for a given property
		const FLinearColor Color = FStyleColors::Hover.GetSpecifiedColor();
		const FLinearColor HollowColor = Color.CopyWithNewOpacity(0.0);
		Set("StateTree.Node.Label.Mixed", new FSlateRoundedBoxBrush(HollowColor, 6.0f, Color, 1.0f));

		const FTextBlockStyle DetailsCategory = FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Bold", 8));
		Set("StateTree.Category", DetailsCategory);
	}

	// Task
	{
		const FLinearColor ForegroundCol =  FStyleColors::Foreground.GetSpecifiedColor();

		Set("StateTree.Task.Title", FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Regular", 10))
			.SetColorAndOpacity(ForegroundCol.CopyWithNewOpacity(0.8f)));

		Set("StateTree.Task.Title.Bold", FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Bold", 10))
			.SetColorAndOpacity(ForegroundCol.CopyWithNewOpacity(0.8f)));

		Set("StateTree.Task.Title.Subdued", FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Regular", 10))
			.SetColorAndOpacity(ForegroundCol.CopyWithNewOpacity(0.4f)));

		// Tasks to be show up a bit darker than the state
		Set("StateTree.Task.Rect", new FSlateColorBrush(FLinearColor(FVector3f(0.67f))));
	}

	// Details rich text
	{
		Set("Details.Normal", FTextBlockStyle(NormalText)
			.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"))));

		Set("Details.Bold", FTextBlockStyle(NormalText)
			.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont"))));

		Set("Details.Italic", FTextBlockStyle(NormalText)
			.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.ItalicFont"))));

		Set("Details.Subdued", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(FSlateColor::UseSubduedForeground())
			.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"))));
	}

	// Normal rich text
	{
		Set("Normal.Normal", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(FSlateColor::UseForeground())
			.SetFont(DEFAULT_FONT("Regular", 10)));

		Set("Normal.Bold", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(FSlateColor::UseForeground())
			.SetFont(DEFAULT_FONT("Bold", 10)));

		Set("Normal.Italic", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(FSlateColor::UseForeground())
			.SetFont(DEFAULT_FONT("Italic", 10)));

		Set("Normal.Subdued", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(FSlateColor::UseSubduedForeground())
			.SetFont(DEFAULT_FONT("Regular", 10)));
	}

	// Transition rich text
	{
		const FLinearColor ForegroundCol =  FStyleColors::White.GetSpecifiedColor();
		Set("Transition.Normal", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(ForegroundCol.CopyWithNewOpacity(0.9f))
			.SetFont(DEFAULT_FONT("Regular", 11)));

		Set("Transition.Bold", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(ForegroundCol.CopyWithNewOpacity(0.9f))
			.SetFont(DEFAULT_FONT("Bold", 11)));

		Set("Transition.Italic", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(ForegroundCol.CopyWithNewOpacity(0.9f))
			.SetFont(DEFAULT_FONT("Italic", 11)));

		Set("Transition.Subdued", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(ForegroundCol.CopyWithNewOpacity(0.5f))
			.SetFont(DEFAULT_FONT("Regular", 11)));
	}

	// Diff tool
	{
		Set("DiffTools.Added", FLinearColor(0.3f, 1.f, 0.3f)); // green
		Set("DiffTools.Removed", FLinearColor(1.0f, 0.2f, 0.3f)); // red
		Set("DiffTools.Changed", FLinearColor(0.85f, 0.71f, 0.25f)); // yellow
		Set("DiffTools.Moved", FLinearColor(0.5f, 0.8f, 1.f)); // light blue
		Set("DiffTools.Enabled", FLinearColor(0.7f, 1.f, 0.7f)); // light green
		Set("DiffTools.Disabled", FLinearColor(1.0f, 0.6f, 0.5f)); // light red
		Set("DiffTools.Properties", FLinearColor(0.2f, 0.4f, 1.f)); // blue
	}

	// Debugger
	{
		Set("StateTreeDebugger.Element.Normal",
			FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Regular", 10)));

		Set("StateTreeDebugger.Element.Bold",
			FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Bold", 10)));

		Set("StateTreeDebugger.Element.Subdued",
			FTextBlockStyle(NormalText)
			.SetFont(DEFAULT_FONT("Regular", 10))
			.SetColorAndOpacity(FSlateColor::UseSubduedForeground()));
	}
	
	const FLinearColor SelectionColor = FColor(0, 0, 0, 32);
	const FTableRowStyle& NormalTableRowStyle = FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("TableView.Row");
	Set("StateTree.Selection",
		FTableRowStyle(NormalTableRowStyle)
		.SetActiveBrush(CORE_IMAGE_BRUSH("Common/Selection", CoreStyleConstants::Icon8x8, SelectionColor))
		.SetActiveHoveredBrush(CORE_IMAGE_BRUSH("Common/Selection", CoreStyleConstants::Icon8x8, SelectionColor))
		.SetInactiveBrush(CORE_IMAGE_BRUSH("Common/Selection", CoreStyleConstants::Icon8x8, SelectionColor))
		.SetInactiveHoveredBrush(CORE_IMAGE_BRUSH("Common/Selection", CoreStyleConstants::Icon8x8, SelectionColor))
		.SetSelectorFocusedBrush(CORE_IMAGE_BRUSH("Common/Selection", CoreStyleConstants::Icon8x8, SelectionColor))
	);

	const FComboButtonStyle& ComboButtonStyle = FCoreStyle::Get().GetWidgetStyle<FComboButtonStyle>("ComboButton");

	// Expression Operand combo button
	const FButtonStyle OperandButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FStyleColors::AccentGreen.GetSpecifiedColor().Desaturate(0.3f), 4.0f))
		.SetHovered(FSlateRoundedBoxBrush(FStyleColors::AccentGreen.GetSpecifiedColor().Desaturate(0.2f), 4.0f))
		.SetPressed(FSlateRoundedBoxBrush(FStyleColors::AccentGreen.GetSpecifiedColor().Desaturate(0.1f), 4.0f))
		.SetNormalForeground(FStyleColors::Foreground)
		.SetHoveredForeground(FStyleColors::ForegroundHover)
		.SetPressedForeground(FStyleColors::ForegroundHover)
		.SetDisabledForeground(FStyleColors::ForegroundHover)
		.SetNormalPadding(FMargin(2, 2, 2, 2))
		.SetPressedPadding(FMargin(2, 3, 2, 1));

	Set("StateTree.Node.Operand.ComboBox", FComboButtonStyle(ComboButtonStyle).SetButtonStyle(OperandButton));

	Set("StateTree.Node.Operand", FTextBlockStyle(NormalText)
		.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
		.SetFontSize(8));

	Set("StateTree.Node.Parens", FTextBlockStyle(NormalText)
		.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.SetFontSize(12));

	// Parameter labels
	Set("StateTree.Param.Label", FTextBlockStyle(NormalText)
		.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
		.SetFontSize(7));

	Set("StateTree.Param.Background", new FSlateRoundedBoxBrush(FStyleColors::Hover, 6.f));
	
	// Expression Indent combo button
	const FButtonStyle IndentButton = FButtonStyle()
		.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 2.0f))
		.SetHovered(FSlateRoundedBoxBrush(FStyleColors::Background, 2.0f, FStyleColors::InputOutline, 1.0f))
		.SetPressed(FSlateRoundedBoxBrush(FStyleColors::Background, 2.0f, FStyleColors::Hover, 1.0f))
		.SetNormalForeground(FStyleColors::Transparent)
		.SetHoveredForeground(FStyleColors::Hover)
		.SetPressedForeground(FStyleColors::Foreground)
		.SetNormalPadding(FMargin(2, 2, 2, 2))
		.SetPressedPadding(FMargin(2, 3, 2, 1));
	
	Set("StateTree.Node.Indent.ComboBox", FComboButtonStyle(ComboButtonStyle).SetButtonStyle(IndentButton));

	
	// Node text styles
	{
		FEditableTextStyle EditableTextStyle = FEditableTextStyle(FAppStyle::GetWidgetStyle<FEditableTextStyle>("NormalEditableText"));
		EditableTextStyle.Font = FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"));
		EditableTextStyle.Font.Size = 10.0f;
		Set("StateTree.Node.Editable", EditableTextStyle);

		FEditableTextBoxStyle EditableTextBlockStyle = FEditableTextBoxStyle(FAppStyle::GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox"));
		EditableTextStyle.Font = FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"));
		EditableTextStyle.Font.Size = 10.0f;
		Set("StateTree.Node.EditableTextBlock", EditableTextBlockStyle);

		const FTextBlockStyle StateNodeNormalText = FTextBlockStyle(NormalText)
			.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.SetFontSize(10);
		Set("StateTree.Node.Normal", StateNodeNormalText);

		Set("StateTree.Node.Bold", FTextBlockStyle(NormalText)
			.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
			.SetFontSize(10));

		Set("StateTree.Node.Subdued", FTextBlockStyle(NormalText)
			.SetColorAndOpacity(FSlateColor::UseSubduedForeground())
			.SetFont(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.SetFontSize(10));

		Set("StateTree.Node.TitleInlineEditableText", FInlineEditableTextBlockStyle()
			.SetTextStyle(StateNodeNormalText)
			.SetEditableTextBoxStyle(EditableTextBlockStyle));
	}

	
	// Command icons
	{
		// From generic Engine
		UE::StateTree::Editor::FContentRootScope Scope(this, EngineSlateContentDir);
		Set("StateTreeEditor.CutStates", new IMAGE_BRUSH_SVG("Starship/Common/Cut", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.CopyStates", new IMAGE_BRUSH_SVG("Starship/Common/Copy", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.DuplicateStates", new IMAGE_BRUSH_SVG("Starship/Common/Duplicate", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.DeleteStates", new IMAGE_BRUSH_SVG("Starship/Common/Delete", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.RenameState", new IMAGE_BRUSH_SVG("Starship/Common/Rename", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.AutoScroll", new IMAGE_BRUSH_SVG("Starship/Insights/AutoScrollRight_20", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.Debugger.ResetTracks", new IMAGE_BRUSH_SVG("Starship/Common/Delete", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.Debugger.State.Enter", new CORE_IMAGE_BRUSH_SVG("Starship/Common/arrow-right", CoreStyleConstants::Icon16x16, FStyleColors::Foreground));
		Set("StateTreeEditor.Debugger.State.Exit", new CORE_IMAGE_BRUSH_SVG("Starship/Common/arrow-left", CoreStyleConstants::Icon16x16, FStyleColors::Foreground));
		Set("StateTreeEditor.Debugger.State.Selected", new CORE_IMAGE_BRUSH_SVG("Starship/Common/arrow-right", CoreStyleConstants::Icon16x16, FStyleColors::AccentYellow));
		Set("StateTreeEditor.Debugger.State.Completed", new CORE_IMAGE_BRUSH_SVG("Starship/Common/check", CoreStyleConstants::Icon16x16, FStyleColors::AccentGreen));

		Set("StateTreeEditor.Debugger.Task.Enter", new CORE_IMAGE_BRUSH_SVG("Starship/Common/arrow-right", CoreStyleConstants::Icon16x16, FStyleColors::Foreground));
		Set("StateTreeEditor.Debugger.Task.Exit", new CORE_IMAGE_BRUSH_SVG("Starship/Common/arrow-left", CoreStyleConstants::Icon16x16, FStyleColors::Foreground));
		Set("StateTreeEditor.Debugger.Task.Failed", new CORE_IMAGE_BRUSH_SVG("Starship/Common/close-small", CoreStyleConstants::Icon16x16, FStyleColors::AccentRed));
		Set("StateTreeEditor.Debugger.Task.Succeeded", new CORE_IMAGE_BRUSH_SVG("Starship/Common/check", CoreStyleConstants::Icon16x16, FStyleColors::AccentGreen));
		Set("StateTreeEditor.Debugger.Task.Stopped", new CORE_IMAGE_BRUSH_SVG("Starship/Common/close-small", CoreStyleConstants::Icon16x16, FStyleColors::AccentRed));

		Set("StateTreeEditor.Debugger.Condition.Passed", new CORE_IMAGE_BRUSH_SVG("Starship/Common/check", CoreStyleConstants::Icon16x16, FStyleColors::AccentGreen));
		Set("StateTreeEditor.Debugger.Condition.Failed", new CORE_IMAGE_BRUSH_SVG("Starship/Common/close-small", CoreStyleConstants::Icon16x16, FStyleColors::AccentRed));
		Set("StateTreeEditor.Debugger.Condition.OnEvaluating", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Update", CoreStyleConstants::Icon16x16, FStyleColors::AccentYellow));

		Set("StateTreeEditor.Debugger.Unset", new CORE_IMAGE_BRUSH_SVG("Starship/Common/help", CoreStyleConstants::Icon16x16, FStyleColors::AccentBlack));

		// Common Node Icons
		Set("Node.EnableDisable", new CORE_IMAGE_BRUSH_SVG("Starship/Common/check-circle", CoreStyleConstants::Icon16x16));
		Set("Node.Time", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Recent", CoreStyleConstants::Icon16x16));
		Set("Node.Sync", new CORE_IMAGE_BRUSH_SVG("Starship/Common/Update", CoreStyleConstants::Icon16x16));
	}

	{
		// From generic Engine Editor
		UE::StateTree::Editor::FContentRootScope Scope(this, EngineEditorSlateContentDir);

		Set("StateTreeEditor.Debugger.StartRecording", new IMAGE_BRUSH("Sequencer/Transport_Bar/Record_24x", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Debugger.StopRecording", new IMAGE_BRUSH("Sequencer/Transport_Bar/Recording_24x", CoreStyleConstants::Icon16x16));
		
		Set("StateTreeEditor.Debugger.PreviousFrameWithStateChange", new IMAGE_BRUSH("Sequencer/Transport_Bar/Go_To_Front_24x", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Debugger.PreviousFrameWithEvents", new IMAGE_BRUSH("Sequencer/Transport_Bar/Step_Backwards_24x", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Debugger.NextFrameWithEvents", new IMAGE_BRUSH("Sequencer/Transport_Bar/Step_Forward_24x", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Debugger.NextFrameWithStateChange", new IMAGE_BRUSH("Sequencer/Transport_Bar/Go_To_End_24x", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.Debugger.ToggleOnEnterStateBreakpoint", new IMAGE_BRUSH_SVG("Starship/Blueprints/Breakpoint_Valid", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Debugger.EnableOnEnterStateBreakpoint", new IMAGE_BRUSH_SVG("Starship/Blueprints/Breakpoint_Valid", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Debugger.EnableOnExitStateBreakpoint", new IMAGE_BRUSH_SVG("Starship/Blueprints/Breakpoint_Valid", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.DebugOptions", new IMAGE_BRUSH_SVG("Starship/Common/Bug", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.Debugger.OwnerTrack", new IMAGE_BRUSH_SVG("Starship/AssetIcons/AIController_64", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Debugger.InstanceTrack", new IMAGE_BRUSH_SVG("Starship/AssetIcons/AnimInstance_64", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.EnableStates", new IMAGE_BRUSH("Icons/Empty_16x", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Debugger.Breakpoint.EnabledAndValid", new IMAGE_BRUSH_SVG( "Starship/Blueprints/Breakpoint_Valid", CoreStyleConstants::Icon16x16, FStyleColors::AccentRed));
		Set("StateTreeEditor.Debugger.ResumeDebuggerAnalysis", new IMAGE_BRUSH_SVG("Starship/Common/Timeline", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.Transition.None", new CORE_IMAGE_BRUSH_SVG("Starship/Common/x-circle", CoreStyleConstants::Icon16x16, FSlateColor::UseSubduedForeground()));
		Set("StateTreeEditor.Transition.Succeeded", new CORE_IMAGE_BRUSH_SVG("Starship/Common/check", CoreStyleConstants::Icon16x16, FStyleColors::AccentGreen));
		Set("StateTreeEditor.Transition.Failed", new CORE_IMAGE_BRUSH_SVG("Starship/Common/close-small", CoreStyleConstants::Icon16x16, FStyleColors::AccentRed));

		Set("StateTreeEditor.Transition.Succeeded", new CORE_IMAGE_BRUSH_SVG("Starship/Common/check", CoreStyleConstants::Icon16x16, FStyleColors::AccentGreen));
		Set("StateTreeEditor.Transition.Failed", new CORE_IMAGE_BRUSH_SVG("Starship/Common/close-small", CoreStyleConstants::Icon16x16, FStyleColors::AccentRed));

		// Common Node Icons
		Set("Node.Navigation", new IMAGE_BRUSH_SVG("Starship/Common/Navigation", CoreStyleConstants::Icon16x16));
		Set("Node.Event", new IMAGE_BRUSH_SVG("Starship/Common/Event", CoreStyleConstants::Icon16x16));
		Set("Node.Animation", new IMAGE_BRUSH_SVG("Starship/Common/Animation", CoreStyleConstants::Icon16x16));
		Set("Node.Debug", new IMAGE_BRUSH_SVG("Starship/Common/Debug", CoreStyleConstants::Icon16x16));
		Set("Node.Find", new IMAGE_BRUSH_SVG("Starship/Common/Find", CoreStyleConstants::Icon16x16));
	}

	{
		// From plugin
		Set("StateTreeEditor.AddSiblingState", new IMAGE_BRUSH_SVG("Icons/Sibling_State", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.AddChildState", new IMAGE_BRUSH_SVG("Icons/Child_State", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.PasteStatesAsSiblings", new IMAGE_BRUSH_SVG("Icons/Sibling_State", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.PasteStatesAsChildren", new IMAGE_BRUSH_SVG("Icons/Child_State", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.SelectNone", new IMAGE_BRUSH_SVG("Icons/Select_None", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.TryEnterState", new IMAGE_BRUSH_SVG("Icons/Try_Enter_State", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.TrySelectChildrenInOrder", new IMAGE_BRUSH_SVG("Icons/Try_Select_Children_In_Order", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.TrySelectChildrenAtRandom", new IMAGE_BRUSH_SVG("Icons/Try_Select_Children_At_Random", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.TryFollowTransitions", new IMAGE_BRUSH_SVG("Icons/Try_Follow_Transitions", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.StateConditions", new IMAGE_BRUSH_SVG("Icons/State_Conditions", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.Conditions", new IMAGE_BRUSH_SVG("Icons/Conditions", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Conditions.Large", new IMAGE_BRUSH_SVG("Icons/Conditions", CoreStyleConstants::Icon24x24));
		Set("StateTreeEditor.Evaluators", new IMAGE_BRUSH_SVG("Icons/Evaluators", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Parameters", new IMAGE_BRUSH_SVG("Icons/Parameters", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Utility", new IMAGE_BRUSH_SVG("Icons/Utility", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Utility.Large", new IMAGE_BRUSH_SVG("Icons/Utility", CoreStyleConstants::Icon24x24));
		Set("StateTreeEditor.Tasks", new IMAGE_BRUSH_SVG("Icons/Tasks", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.Tasks.Large", new IMAGE_BRUSH_SVG("Icons/Tasks", CoreStyleConstants::Icon24x24));
		Set("StateTreeEditor.Transitions", new IMAGE_BRUSH_SVG("Icons/Transitions", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.StateSubtree", new IMAGE_BRUSH_SVG("Icons/State_Subtree", CoreStyleConstants::Icon16x16));
		Set("StateTreeEditor.StateLinked", new IMAGE_BRUSH_SVG("Icons/State_Linked", CoreStyleConstants::Icon16x16));

		Set("StateTreeEditor.Transition.Dash", new IMAGE_BRUSH_SVG("Icons/Transition_Dash", CoreStyleConstants::Icon16x16, FStyleColors::Foreground));
		Set("StateTreeEditor.Transition.Goto", new IMAGE_BRUSH_SVG("Icons/Transition_Goto", CoreStyleConstants::Icon16x16, FStyleColors::Foreground));
		Set("StateTreeEditor.Transition.Next", new IMAGE_BRUSH_SVG("Icons/Transition_Next", CoreStyleConstants::Icon16x16, FStyleColors::Foreground));
		Set("StateTreeEditor.Transition.Parent", new IMAGE_BRUSH_SVG("Icons/Transition_Parent", CoreStyleConstants::Icon16x16, FStyleColors::Foreground));

		Set("StateTreeEditor.Transition.Condition", new IMAGE_BRUSH_SVG("Icons/State_Conditions", CoreStyleConstants::Icon16x16, FStyleColors::AccentGray));
		Set("StateTreeEditor.Debugger.Condition.OnTransition", new IMAGE_BRUSH_SVG("Icons/State_Conditions", CoreStyleConstants::Icon16x16, FStyleColors::AccentGray));

		Set("StateTreeEditor.Debugger.Log.Warning", new IMAGE_BRUSH_SVG("Icons/Alert", CoreStyleConstants::Icon16x16, FStyleColors::AccentYellow));
		Set("StateTreeEditor.Debugger.Log.Error", new IMAGE_BRUSH_SVG("Icons/Failure", CoreStyleConstants::Icon16x16, FStyleColors::AccentRed));

		// Common Node Icons
		Set("Node.Movement", new IMAGE_BRUSH_SVG("Icons/Movement", CoreStyleConstants::Icon16x16));
		Set("Node.Tag", new IMAGE_BRUSH_SVG("Icons/Tag", CoreStyleConstants::Icon16x16));
		Set("Node.RunParallel", new IMAGE_BRUSH_SVG("Icons/RunParallel", CoreStyleConstants::Icon16x16));
		Set("Node.Task", new IMAGE_BRUSH_SVG("Icons/Task", CoreStyleConstants::Icon16x16));
		Set("Node.Text", new IMAGE_BRUSH_SVG("Icons/Text", CoreStyleConstants::Icon16x16));
		Set("Node.Function", new IMAGE_BRUSH_SVG("Icons/Function", CoreStyleConstants::Icon16x16));
	}
}

void FStateTreeEditorStyle::Register()
{
	FSlateStyleRegistry::RegisterSlateStyle(Get());
}

void FStateTreeEditorStyle::Unregister()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(Get());
}

FStateTreeEditorStyle& FStateTreeEditorStyle::Get()
{
	static FStateTreeEditorStyle Instance;
	return Instance;
}

const FSlateBrush* FStateTreeEditorStyle::GetBrushForSelectionBehaviorType(EStateTreeStateSelectionBehavior InBehaviour, bool bHasChildren, EStateTreeStateType StateType)
{	
	if (InBehaviour == EStateTreeStateSelectionBehavior::None)
	{
		return Get().GetBrush("StateTreeEditor.SelectNone");
	}
	else if (InBehaviour == EStateTreeStateSelectionBehavior::TryEnterState)
	{
		return Get().GetBrush("StateTreeEditor.TryEnterState");			
	}
	else if (InBehaviour == EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder
		|| InBehaviour == EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility
		|| InBehaviour == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility)
	{
		if (!bHasChildren
			|| StateType == EStateTreeStateType::Linked
			|| StateType == EStateTreeStateType::LinkedAsset)
		{
			return Get().GetBrush("StateTreeEditor.TryEnterState");			
		}
		else
		{
			return Get().GetBrush("StateTreeEditor.TrySelectChildrenInOrder");
		}
	}
	else if (InBehaviour == EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom)
	{
		return Get().GetBrush("StateTreeEditor.TrySelectChildrenAtRandom");
	}
	else if (InBehaviour == EStateTreeStateSelectionBehavior::TryFollowTransitions)
	{
		return Get().GetBrush("StateTreeEditor.TryFollowTransitions");
	}

	return nullptr;
}
