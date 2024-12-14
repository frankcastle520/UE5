// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EaseCurveTool/AvaEaseCurve.h"
#include "EaseCurveTool/AvaEaseCurveKeySelection.h"
#include "Curves/RichCurve.h"
#include "Curves/KeyHandle.h"
#include "EaseCurveTool/Widgets/SAvaEaseCurvePreset.h"
#include "EditorUndoClient.h"
#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FAvaSequencer;
class FCurveEditor;
class IAvaSequencer;
class ISequencer;
class SAvaEaseCurveTool;
class SCurveEditor;
class SCurveEditorPanel;
class SEditableTextBox;
class UAvaEaseCurve;
class UCurveFloat;
class UMovieSceneSection;
struct FAvaEaseCurveTangents;
struct FGuid;

namespace UE::Sequencer
{
	class FChannelModel;
	class FSequencerSelection;
}

/** Current default and only implemented is DualKeyEdit. */
enum class EAvaEaseCurveToolMode : uint8
{
	/** Edits the selected key's leave tangent and the next key's arrive tangent in the curve editor graph. */
	DualKeyEdit,
	/** Edits only the selected key.
	 * The leave tangent in the curve editor graph will set the sequence key arrive tangent.
	 * The arrive tangent in the curve editor graph will set the sequence key leave tangent. */
	SingleKeyEdit
};

enum class EAvaEaseCurveToolOperation : uint8
{
	InOut,
	In,
	Out
};

class FAvaEaseCurveTool
	: public TSharedFromThis<FAvaEaseCurveTool>
	, public FGCObject
	, public FSelfRegisteringEditorUndoClient
{
public:
	static void ShowNotificationMessage(const FText& InMessageText);

	/** Returns true if the clipboard paste data contains tangent information. */
	static bool TangentsFromClipboardPaste(FAvaEaseCurveTangents& OutTangents);

	FAvaEaseCurveTool(const TSharedRef<FAvaSequencer>& InSequencer);

	virtual ~FAvaEaseCurveTool() override;

	TSharedRef<SWidget> GenerateWidget();

	EVisibility GetVisibility() const;

	TObjectPtr<UAvaEaseCurve> GetToolCurve() const;
	FRichCurve* GetToolRichCurve() const;

	FAvaEaseCurveTangents GetEaseCurveTangents() const;

	/**
	 * Sets the internal ease curve tangents and optionally broadcasts a change event for the curve object.
	 * This is different from SetEaseCurveTangents_Internal in that it performs undo/redo transactions and
	 * optionally sets the selected tangents in the actual sequence.
	 */
	void SetEaseCurveTangents(const FAvaEaseCurveTangents& InTangents
		, const EAvaEaseCurveToolOperation InOperation
		, const bool bInBroadcastUpdate
		, const bool bInSetSequencerTangents
		, const FText& InTransactionText = NSLOCTEXT("AvaEaseCurveTool", "SetEaseCurveTangents", "Set Ease Curve Tangents"));

	void ResetEaseCurveTangents(const EAvaEaseCurveToolOperation InOperation);

	void FlattenOrStraightenTangents(const EAvaEaseCurveToolOperation InOperation, const bool bInFlattenTangents);

	/** Creates a new external float curve from the internet curve editor curve. */
	UCurveBase* CreateCurveAsset() const;

	void SetSequencerKeySelectionTangents(const FAvaEaseCurveTangents& InTangents, const EAvaEaseCurveToolOperation InOperation = EAvaEaseCurveToolOperation::InOut);

	void ApplyQuickEaseToSequencerKeySelections(const EAvaEaseCurveToolOperation InOperation = EAvaEaseCurveToolOperation::InOut);

	/** Updates the ease curve graph view based on the active sequencer key selection. */
	void UpdateEaseCurveFromSequencerKeySelections();

	EAvaEaseCurveToolOperation GetToolOperation() const;
	void SetToolOperation(const EAvaEaseCurveToolOperation InNewOperation);
	bool IsToolOperation(const EAvaEaseCurveToolOperation InNewOperation) const;

	bool CanCopyTangentsToClipboard() const;
	void CopyTangentsToClipboard() const;
	bool CanPasteTangentsFromClipboard() const;
	void PasteTangentsFromClipboard() const;

	bool IsKeyInterpMode(const ERichCurveInterpMode InInterpMode, const ERichCurveTangentMode InTangentMode) const;
	void SetKeyInterpMode(const ERichCurveInterpMode InInterpMode, const ERichCurveTangentMode InTangentMode) const;

	void BeginTransaction(const FText& InDescription) const;
	void EndTransaction() const;

	void UndoAction();
	void RedoAction();

	void OpenToolSettings() const;

	FFrameRate GetTickResolution() const;
	FFrameRate GetDisplayRate() const;

	bool HasCachedKeysToEase();

	bool AreAllEaseCurves();

	//~ Begin FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;
	//~ End FGCObject

	//~ Begin FEditorUndoClient
	virtual void PostUndo(bool bInSuccess) override;
	virtual void PostRedo(bool bInSuccess) override;
	//~ End FEditorUndoClient

protected:
	/** 
	 * Sets the internal ease curve tangents and optionally broadcasts a change event for the curve object.
	 * Changing the internal ease curve tangents will be directly reflected in the ease curve editor graph.
	 */
	void SetEaseCurveTangents_Internal(const FAvaEaseCurveTangents& InTangents, const EAvaEaseCurveToolOperation InOperation, const bool bInBroadcastUpdate) const;

	TWeakPtr<FAvaSequencer> AvaSequencerWeak;

	TObjectPtr<UAvaEaseCurve> EaseCurve;

	EAvaEaseCurveToolMode ToolMode = EAvaEaseCurveToolMode::DualKeyEdit;
	EAvaEaseCurveToolOperation OperationMode = EAvaEaseCurveToolOperation::InOut;

	TSharedPtr<SAvaEaseCurveTool> ToolWidget;

	/** Cached data set when a new sequencer selection is made. */
	FAvaEaseCurveKeySelection KeyCache;

private:
	void OnSequencerSelectionChanged(TArray<FGuid> InObjectGuids);
};
