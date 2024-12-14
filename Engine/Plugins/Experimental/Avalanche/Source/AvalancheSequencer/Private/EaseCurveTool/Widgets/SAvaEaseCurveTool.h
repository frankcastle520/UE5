// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EaseCurveTool/AvaEaseCurveTool.h"
#include "EditorUndoClient.h"
#include "Templates/SharedPointer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FAvaEaseCurveToolContextMenu;
class FCurveEditor;
class FText;
class FUICommandList;
class SAvaEaseCurveEditor;
class SAvaEaseCurvePreset;
class UCurveBase;
class UToolMenu;
struct FKeyHandle;
struct FRichCurve;

class SAvaEaseCurveTool
	: public SCompoundWidget
	, public FEditorUndoClient
{
public:
	static constexpr int32 DefaultGraphSize = 200;
	
	SLATE_BEGIN_ARGS(SAvaEaseCurveTool)
		: _ToolMode(EAvaEaseCurveToolMode::DualKeyEdit)
		, _ToolOperation(EAvaEaseCurveToolOperation::InOut)
	{}
		SLATE_ATTRIBUTE(EAvaEaseCurveToolMode, ToolMode)
		SLATE_ATTRIBUTE(EAvaEaseCurveToolOperation, ToolOperation)
		SLATE_ARGUMENT(FAvaEaseCurveTangents, InitialTangents)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<FAvaEaseCurveTool>& InEaseCurveTool);
	
	void SetTangents(const FAvaEaseCurveTangents& InTangents, EAvaEaseCurveToolOperation InOperation,
		const bool bInSetEaseCurve, const bool bInBroadcastUpdate, const bool bInSetSequencerTangents) const;

	FAvaEaseCurveTangents GetTangents() const;

	FKeyHandle GetSelectedKeyHandle() const;

	void ZoomToFit() const;

protected:
	TSharedRef<SWidget> ConstructCurveEditorPanel();

	void HandleEditorTangentsChanged(const FAvaEaseCurveTangents& InTangents) const;

	void OnStartTangentSpinBoxChanged(const double InNewValue) const;
	void OnStartTangentWeightSpinBoxChanged(const double InNewValue) const;
	void OnEndTangentSpinBoxChanged(const double InNewValue) const;
	void OnEndTangentWeightSpinBoxChanged(const double InNewValue) const;

	void OnPresetChanged(const TSharedPtr<FAvaEaseCurvePreset>& InPreset) const;
	void OnQuickPresetChanged(const TSharedPtr<FAvaEaseCurvePreset>& InPreset) const;

	void BindCommands();

	void UndoAction();
	void RedoAction();

	void OnEditorDragStart() const;
	void OnEditorDragEnd() const;

	FText GetStartText() const;
	FText GetStartTooltipText() const;
	FText GetEndText() const;
	FText GetEndTooltipText() const;

	void ResetToDefaultPresets();

	void ApplyTangents();

	void ResetTangentsAndNotify() const;

	//~ Begin SWidget
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	//~ End SWidget

	TSharedPtr<FUICommandList> CommandList;

	TSharedPtr<FAvaEaseCurveTool> EaseCurveTool;

	TAttribute<EAvaEaseCurveToolMode> ToolMode;
	TAttribute<EAvaEaseCurveToolOperation> ToolOperation;

	TSharedPtr<SAvaEaseCurveEditor> CurveEaseEditorWidget;
	TSharedPtr<SAvaEaseCurvePreset> CurvePresetWidget;

	int32 CurrentGraphSize = DefaultGraphSize;

	TSharedPtr<FAvaEaseCurveToolContextMenu> ContextMenu;
};
