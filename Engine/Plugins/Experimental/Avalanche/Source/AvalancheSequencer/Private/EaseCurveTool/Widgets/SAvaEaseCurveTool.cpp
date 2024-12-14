// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAvaEaseCurveTool.h"
#include "Curves/KeyHandle.h"
#include "Curves/RichCurve.h"
#include "EaseCurveTool/AvaEaseCurvePreset.h"
#include "EaseCurveTool/AvaEaseCurveTool.h"
#include "EaseCurveTool/AvaEaseCurveToolCommands.h"
#include "EaseCurveTool/AvaEaseCurveToolSettings.h"
#include "EaseCurveTool/AvaEaseCurveSubsystem.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurveEditor.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurvePreset.h"
#include "EaseCurveTool/Widgets/AvaEaseCurveToolContextMenu.h"
#include "Editor.h"
#include "EngineAnalytics.h"
#include "SAvaEaseCurveTangents.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UIAction.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "ToolMenu.h"
#include "ToolMenus.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "SAvaEaseCurveTool"

void SAvaEaseCurveTool::Construct(const FArguments& InArgs, const TSharedRef<FAvaEaseCurveTool>& InEaseCurveTool)
{
	ToolMode = InArgs._ToolMode;
	ToolOperation = InArgs._ToolOperation;

	EaseCurveTool = InEaseCurveTool;

	BindCommands();

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(0.f, 1.f, 0.f, 0.f)
			[
				SAssignNew(CurvePresetWidget, SAvaEaseCurvePreset)
				.OnPresetChanged(this, &SAvaEaseCurveTool::OnPresetChanged)
				.OnQuickPresetChanged(this, &SAvaEaseCurveTool::OnQuickPresetChanged)
				.OnGetNewPresetTangents_Lambda([this](FAvaEaseCurveTangents& OutTangents) -> bool
					{
						OutTangents = EaseCurveTool->GetEaseCurveTangents();
						return true;
					})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(0.f, 4.f, 0.f, 0.f)
			[
				ConstructCurveEditorPanel()
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(SAvaEaseCurveTangents)
				.InitialTangents(GetTangents())
				.OnStartTangentChanged(this, &SAvaEaseCurveTool::OnStartTangentSpinBoxChanged)
				.OnStartWeightChanged(this, &SAvaEaseCurveTool::OnStartTangentWeightSpinBoxChanged)
				.OnEndTangentChanged(this, &SAvaEaseCurveTool::OnEndTangentSpinBoxChanged)
				.OnEndWeightChanged(this, &SAvaEaseCurveTool::OnEndTangentWeightSpinBoxChanged)
				.OnBeginSliderMovement_Lambda([this]()
					{
						EaseCurveTool->BeginTransaction(LOCTEXT("SliderDragStartLabel", "Ease Curve Slider Drag"));
					})
				.OnEndSliderMovement_Lambda([this](const float InNewValue)
					{
						EaseCurveTool->EndTransaction();
					})
			]
		];

	if (GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	CurvePresetWidget->SetSelectedItem(InArgs._InitialTangents);
}

TSharedRef<SWidget> SAvaEaseCurveTool::ConstructCurveEditorPanel()
{
	CurrentGraphSize = GetDefault<UAvaEaseCurveToolSettings>()->GetGraphSize();
	
	ContextMenu = MakeShared<FAvaEaseCurveToolContextMenu>(CommandList
		, FAvaEaseCurveToolOnGraphSizeChanged::CreateLambda([this](const int32 InNewSize)
			{
				CurrentGraphSize = InNewSize;
			}));

	const TSharedRef<FAvaEaseCurveTool> EaseCurveToolRef = EaseCurveTool.ToSharedRef();
	
	return SNew(SBorder)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(CurveEaseEditorWidget, SAvaEaseCurveEditor, EaseCurveTool->GetToolCurve())
				.DisplayRate(EaseCurveToolRef, &FAvaEaseCurveTool::GetDisplayRate)
				.Operation(EaseCurveToolRef, &FAvaEaseCurveTool::GetToolOperation)
				.DesiredSize_Lambda([this]() -> FVector2D
					{
						return FVector2D(CurrentGraphSize);
					})
				// These two are lambda because the functions aren't const and the functions aren't const
				// because there is no const ForEachEaseableKey implementation
				.ShowEqualValueKeyError_Lambda([this]()
					{
						return !EaseCurveTool->HasCachedKeysToEase();
					})
				.IsEaseCurveSelection_Lambda([this]()
					{
						return EaseCurveTool->AreAllEaseCurves();
					})
				.OnTangentsChanged(this, &SAvaEaseCurveTool::HandleEditorTangentsChanged)
				.GridSnap_UObject(GetDefault<UAvaEaseCurveToolSettings>(), &UAvaEaseCurveToolSettings::GetGridSnap)
				.GridSize_UObject(GetDefault<UAvaEaseCurveToolSettings>(), &UAvaEaseCurveToolSettings::GetGridSize)
				.GetContextMenuContent(ContextMenu.ToSharedRef(), &FAvaEaseCurveToolContextMenu::GenerateWidget)
				.StartText(this, &SAvaEaseCurveTool::GetStartText)
				.StartTooltipText(this, &SAvaEaseCurveTool::GetStartTooltipText)
				.EndText(this, &SAvaEaseCurveTool::GetEndText)
				.EndTooltipText(this, &SAvaEaseCurveTool::GetEndTooltipText)
				.OnKeyDown(this, &SAvaEaseCurveTool::OnKeyDown)
				.OnDragStart(this, &SAvaEaseCurveTool::OnEditorDragStart)
				.OnDragEnd(this, &SAvaEaseCurveTool::OnEditorDragEnd)
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Top)
			[
				SNew(SImage)
				.Visibility(EVisibility::HitTestInvisible)
				.ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f))
				.Image(&(FCoreStyle::Get().GetWidgetStyle<FScrollBoxStyle>(TEXT("ScrollBox")).TopShadowBrush))
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Bottom)
			[
				SNew(SImage)
				.Visibility(EVisibility::HitTestInvisible)
				.ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f))
				.Image(&(FCoreStyle::Get().GetWidgetStyle<FScrollBoxStyle>(TEXT("ScrollBox")).BottomShadowBrush))
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Fill)
			[
				SNew(SImage)
				.Visibility(EVisibility::HitTestInvisible)
				.ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f))
				.Image(&(FCoreStyle::Get().GetWidgetStyle<FScrollBoxStyle>(TEXT("ScrollBox")).LeftShadowBrush))
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			[
				SNew(SImage)
				.Visibility(EVisibility::HitTestInvisible)
				.ColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f))
				.Image(&(FCoreStyle::Get().GetWidgetStyle<FScrollBoxStyle>(TEXT("ScrollBox")).RightShadowBrush))
			]
		];
}

void SAvaEaseCurveTool::HandleEditorTangentsChanged(const FAvaEaseCurveTangents& InTangents) const
{
	SetTangents(InTangents, ToolOperation.Get(), true, true, true);
}

void SAvaEaseCurveTool::OnEditorDragStart() const
{
	EaseCurveTool->BeginTransaction(LOCTEXT("EditorDragStartLabel", "Ease Curve Graph Drag"));
}

void SAvaEaseCurveTool::OnEditorDragEnd() const
{
	EaseCurveTool->EndTransaction();

	if (!EaseCurveTool->HasCachedKeysToEase())
	{
		ResetTangentsAndNotify();
	}
}

void SAvaEaseCurveTool::SetTangents(const FAvaEaseCurveTangents& InTangents, EAvaEaseCurveToolOperation InOperation
	, const bool bInSetEaseCurve, const bool bInBroadcastUpdate, const bool bInSetSequencerTangents) const
{
	if (CurvePresetWidget.IsValid() && !CurvePresetWidget->SetSelectedItem(InTangents))
	{
		CurvePresetWidget->ClearSelection();
	}

	// To change the graph UI tangents, we need to change the ease curve object tangents and the graph will reflect.
	if (bInSetEaseCurve && EaseCurveTool.IsValid())
	{
		EaseCurveTool->SetEaseCurveTangents(InTangents, InOperation, bInBroadcastUpdate, bInSetSequencerTangents);
	}

	if (GetDefault<UAvaEaseCurveToolSettings>()->GetAutoZoomToFit())
	{
		ZoomToFit();
	}
}

FAvaEaseCurveTangents SAvaEaseCurveTool::GetTangents() const
{
	return EaseCurveTool->GetEaseCurveTangents();
}

void SAvaEaseCurveTool::OnStartTangentSpinBoxChanged(const double InNewValue) const
{
	FAvaEaseCurveTangents NewTangents = EaseCurveTool->GetEaseCurveTangents();
	NewTangents.Start = InNewValue;
	SetTangents(NewTangents, ToolOperation.Get(), true, true, true);
}

void SAvaEaseCurveTool::OnStartTangentWeightSpinBoxChanged(const double InNewValue) const
{
	FAvaEaseCurveTangents NewTangents = EaseCurveTool->GetEaseCurveTangents();
	NewTangents.StartWeight = InNewValue;
	SetTangents(NewTangents, ToolOperation.Get(), true, true, true);
}

void SAvaEaseCurveTool::OnEndTangentSpinBoxChanged(const double InNewValue) const
{
	FAvaEaseCurveTangents NewTangents = EaseCurveTool->GetEaseCurveTangents();
	NewTangents.End = InNewValue;
	SetTangents(NewTangents, ToolOperation.Get(), true, true, true);
}

void SAvaEaseCurveTool::OnEndTangentWeightSpinBoxChanged(const double InNewValue) const
{
	FAvaEaseCurveTangents NewTangents = EaseCurveTool->GetEaseCurveTangents();
	NewTangents.EndWeight = InNewValue;
	SetTangents(NewTangents, ToolOperation.Get(), true, true, true);
}

void SAvaEaseCurveTool::OnPresetChanged(const TSharedPtr<FAvaEaseCurvePreset>& InPreset) const
{
	if (!EaseCurveTool->HasCachedKeysToEase())
	{
		ResetTangentsAndNotify();
		return;
	}

	if (InPreset.IsValid())
	{
		SetTangents(InPreset->Tangents, ToolOperation.Get(), true, true, true);
	}

	FSlateApplication::Get().SetAllUserFocus(CurveEaseEditorWidget);

	if (FEngineAnalytics::IsAvailable())
	{
		// Only send analytics for default presets
		const TMap<FString, TArray<FString>>& DefaultPresetNames = UAvaEaseCurveSubsystem::GetDefaultCategoryPresetNames();
		if (DefaultPresetNames.Contains(InPreset->Category)
			&& DefaultPresetNames[InPreset->Category].Contains(InPreset->Name))
		{
			TArray<FAnalyticsEventAttribute> Attributes;
			Attributes.Emplace(TEXT("Category"), InPreset->Category);
			Attributes.Emplace(TEXT("Name"), InPreset->Name);

			FEngineAnalytics::GetProvider().RecordEvent(TEXT("Editor.Usage.MotionDesign.EaseCurveTool.SetTangentsPreset"), Attributes);
		}
	}
}

void SAvaEaseCurveTool::OnQuickPresetChanged(const TSharedPtr<FAvaEaseCurvePreset>& InPreset) const
{
	FSlateApplication::Get().SetAllUserFocus(CurveEaseEditorWidget);
}

void SAvaEaseCurveTool::BindCommands()
{
	const FAvaEaseCurveToolCommands& EaseCurveToolCommands = FAvaEaseCurveToolCommands::Get();

	const TSharedRef<FAvaEaseCurveTool> EaseCurveToolRef = EaseCurveTool.ToSharedRef();

	CommandList = MakeShared<FUICommandList>();

	CommandList->MapAction(FGenericCommands::Get().Undo, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::UndoAction));
	
	CommandList->MapAction(FGenericCommands::Get().Redo, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::RedoAction));

	CommandList->MapAction(EaseCurveToolCommands.OpenToolSettings, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::OpenToolSettings));

	CommandList->MapAction(EaseCurveToolCommands.ResetToDefaultPresets, FExecuteAction::CreateSP(this, &SAvaEaseCurveTool::ResetToDefaultPresets));

	CommandList->MapAction(EaseCurveToolCommands.Refresh, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::UpdateEaseCurveFromSequencerKeySelections));
	
	CommandList->MapAction(EaseCurveToolCommands.Apply, FExecuteAction::CreateSP(this, &SAvaEaseCurveTool::ApplyTangents));

	CommandList->MapAction(EaseCurveToolCommands.ZoomToFit, FExecuteAction::CreateSP(this, &SAvaEaseCurveTool::ZoomToFit));

	CommandList->MapAction(EaseCurveToolCommands.ToggleGridSnap
		, FExecuteAction::CreateUObject(GetMutableDefault<UAvaEaseCurveToolSettings>(), &UAvaEaseCurveToolSettings::ToggleGridSnap)
		, FCanExecuteAction()
		, FIsActionChecked::CreateUObject(GetDefault<UAvaEaseCurveToolSettings>(), &UAvaEaseCurveToolSettings::GetGridSnap));

	CommandList->MapAction(EaseCurveToolCommands.ToggleAutoFlipTangents
		, FExecuteAction::CreateUObject(GetMutableDefault<UAvaEaseCurveToolSettings>(), &UAvaEaseCurveToolSettings::ToggleAutoFlipTangents)
		, FCanExecuteAction()
		, FIsActionChecked::CreateUObject(GetDefault<UAvaEaseCurveToolSettings>(), &UAvaEaseCurveToolSettings::GetAutoFlipTangents));

	CommandList->MapAction(EaseCurveToolCommands.ToggleAutoZoomToFit
		, FExecuteAction::CreateUObject(GetMutableDefault<UAvaEaseCurveToolSettings>(), &UAvaEaseCurveToolSettings::ToggleAutoZoomToFit)
		, FCanExecuteAction()
		, FIsActionChecked::CreateUObject(GetDefault<UAvaEaseCurveToolSettings>(), &UAvaEaseCurveToolSettings::GetAutoZoomToFit));

	CommandList->MapAction(EaseCurveToolCommands.SetOperationToEaseOut
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetToolOperation, EAvaEaseCurveToolOperation::Out)
		, FCanExecuteAction()
		, FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsToolOperation, EAvaEaseCurveToolOperation::Out));

	CommandList->MapAction(EaseCurveToolCommands.SetOperationToEaseInOut
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetToolOperation, EAvaEaseCurveToolOperation::InOut)
		, FCanExecuteAction()
		, FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsToolOperation, EAvaEaseCurveToolOperation::InOut));

	CommandList->MapAction(EaseCurveToolCommands.SetOperationToEaseIn
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetToolOperation, EAvaEaseCurveToolOperation::In)
		, FCanExecuteAction()
		, FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsToolOperation, EAvaEaseCurveToolOperation::In));

	CommandList->MapAction(EaseCurveToolCommands.ResetTangents
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::ResetEaseCurveTangents, EAvaEaseCurveToolOperation::InOut));

	CommandList->MapAction(EaseCurveToolCommands.ResetStartTangent
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::ResetEaseCurveTangents, EAvaEaseCurveToolOperation::Out));

	CommandList->MapAction(EaseCurveToolCommands.ResetEndTangent
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::ResetEaseCurveTangents, EAvaEaseCurveToolOperation::In));

	CommandList->MapAction(EaseCurveToolCommands.FlattenTangents
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::FlattenOrStraightenTangents, EAvaEaseCurveToolOperation::InOut, true));

	CommandList->MapAction(EaseCurveToolCommands.FlattenStartTangent
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::FlattenOrStraightenTangents, EAvaEaseCurveToolOperation::Out, true));

	CommandList->MapAction(EaseCurveToolCommands.FlattenEndTangent
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::FlattenOrStraightenTangents, EAvaEaseCurveToolOperation::In, true));

	CommandList->MapAction(EaseCurveToolCommands.StraightenTangents
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::FlattenOrStraightenTangents, EAvaEaseCurveToolOperation::InOut, false));

	CommandList->MapAction(EaseCurveToolCommands.StraightenStartTangent
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::FlattenOrStraightenTangents, EAvaEaseCurveToolOperation::Out, false));

	CommandList->MapAction(EaseCurveToolCommands.StraightenEndTangent
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::FlattenOrStraightenTangents, EAvaEaseCurveToolOperation::In, false));

	CommandList->MapAction(EaseCurveToolCommands.CopyTangents
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::CopyTangentsToClipboard)
		, FCanExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::CanCopyTangentsToClipboard));

	CommandList->MapAction(EaseCurveToolCommands.PasteTangents
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::PasteTangentsFromClipboard)
		, FCanExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::CanPasteTangentsFromClipboard));

	CommandList->MapAction(EaseCurveToolCommands.CreateExternalCurveAsset
		, FExecuteAction::CreateLambda([this]()
			{
				EaseCurveTool->CreateCurveAsset();
			})
		, FCanExecuteAction());

	CommandList->MapAction(EaseCurveToolCommands.SetKeyInterpConstant,
		FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetKeyInterpMode, RCIM_Constant, RCTM_Auto),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsKeyInterpMode, RCIM_Constant, RCTM_Auto));

	CommandList->MapAction(EaseCurveToolCommands.SetKeyInterpLinear,
		FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetKeyInterpMode, RCIM_Linear, RCTM_Auto),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsKeyInterpMode, RCIM_Linear, RCTM_Auto));

	CommandList->MapAction(EaseCurveToolCommands.SetKeyInterpCubicAuto,
		FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetKeyInterpMode, RCIM_Cubic, RCTM_Auto),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsKeyInterpMode, RCIM_Cubic, RCTM_Auto));

	CommandList->MapAction(EaseCurveToolCommands.SetKeyInterpCubicSmartAuto,
		FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetKeyInterpMode, RCIM_Cubic, RCTM_SmartAuto),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsKeyInterpMode, RCIM_Cubic, RCTM_SmartAuto));

	CommandList->MapAction(EaseCurveToolCommands.SetKeyInterpCubicUser,
		FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetKeyInterpMode, RCIM_Cubic, RCTM_User),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsKeyInterpMode, RCIM_Cubic, RCTM_User));

	CommandList->MapAction(EaseCurveToolCommands.SetKeyInterpCubicBreak,
		FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::SetKeyInterpMode, RCIM_Cubic, RCTM_Break),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::IsKeyInterpMode, RCIM_Cubic, RCTM_Break));

	CommandList->MapAction(EaseCurveToolCommands.QuickEase
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::ApplyQuickEaseToSequencerKeySelections, EAvaEaseCurveToolOperation::InOut));

	CommandList->MapAction(EaseCurveToolCommands.QuickEaseIn
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::ApplyQuickEaseToSequencerKeySelections, EAvaEaseCurveToolOperation::In));

	CommandList->MapAction(EaseCurveToolCommands.QuickEaseOut
		, FExecuteAction::CreateSP(EaseCurveToolRef, &FAvaEaseCurveTool::ApplyQuickEaseToSequencerKeySelections, EAvaEaseCurveToolOperation::Out));
}

void SAvaEaseCurveTool::UndoAction()
{
	if (GEditor)
	{
		GEditor->UndoTransaction();
	}
}

void SAvaEaseCurveTool::RedoAction()
{
	if (GEditor)
	{
		GEditor->RedoTransaction();
	}
}

void SAvaEaseCurveTool::ZoomToFit() const
{
	if (CurveEaseEditorWidget.IsValid())
	{
		CurveEaseEditorWidget->ZoomToFit();
	}
}

FKeyHandle SAvaEaseCurveTool::GetSelectedKeyHandle() const
{
	if (CurveEaseEditorWidget.IsValid())
	{
		return CurveEaseEditorWidget->GetSelectedKeyHandle();
	}
	return FKeyHandle::Invalid();
}

FReply SAvaEaseCurveTool::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FText SAvaEaseCurveTool::GetStartText() const
{
	return (ToolMode.Get(EAvaEaseCurveToolMode::DualKeyEdit) == EAvaEaseCurveToolMode::DualKeyEdit)
		? LOCTEXT("StartText", "Leave")
		: LOCTEXT("ArriveText", "Arrive");
}

FText SAvaEaseCurveTool::GetStartTooltipText() const
{
	return (ToolMode.Get(EAvaEaseCurveToolMode::DualKeyEdit) == EAvaEaseCurveToolMode::DualKeyEdit)
		? LOCTEXT("StartTooltipText", "Start: The selected key's leave tangent")
		: LOCTEXT("ArriveTooltipText", "Arrive");
}

FText SAvaEaseCurveTool::GetEndText() const
{
	return (ToolMode.Get(EAvaEaseCurveToolMode::DualKeyEdit) == EAvaEaseCurveToolMode::DualKeyEdit)
		? LOCTEXT("EndText", "Arrive")
		: LOCTEXT("LeaveText", "Leave");
}

FText SAvaEaseCurveTool::GetEndTooltipText() const
{
	return (ToolMode.Get(EAvaEaseCurveToolMode::DualKeyEdit) == EAvaEaseCurveToolMode::DualKeyEdit)
		? LOCTEXT("EndTooltipText", "End: The next key's arrive tangent")
		: LOCTEXT("LeaveTooltipText", "Leave");
}

void SAvaEaseCurveTool::ResetToDefaultPresets()
{
	const FText MessageBoxTitle = LOCTEXT("ResetToDefaultPresets", "Reset To Default Presets");
	const EAppReturnType::Type Response = FMessageDialog::Open(EAppMsgType::YesNoCancel
		, LOCTEXT("ConfirmResetToDefaultPresets", "Are you sure you want to reset to default presets?\n\n"
			"*CAUTION* All directories and files inside '[Project]/Config/EaseCurves' will be lost!")
		, MessageBoxTitle);
	if (Response == EAppReturnType::Yes)
	{
		UAvaEaseCurveSubsystem::Get().ResetToDefaultPresets(false);
	}
}

void SAvaEaseCurveTool::ApplyTangents()
{
	EaseCurveTool->SetEaseCurveTangents(EaseCurveTool->GetEaseCurveTangents(), EaseCurveTool->GetToolOperation(), /*bInBroadcastUpdate=*/true, /*bInSetSequencerTangents=*/true);
}

void SAvaEaseCurveTool::ResetTangentsAndNotify() const
{
	CurvePresetWidget->ClearSelection();

	SetTangents(FAvaEaseCurveTangents(), EAvaEaseCurveToolOperation::InOut, true, true, false);

	FAvaEaseCurveTool::ShowNotificationMessage(LOCTEXT("EqualValueKeys", "No different key values to create ease curve!"));
}

#undef LOCTEXT_NAMESPACE
