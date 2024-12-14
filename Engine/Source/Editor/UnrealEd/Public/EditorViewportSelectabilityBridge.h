// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "Templates/SharedPointer.h"

class FEditorViewportClient;

DECLARE_DELEGATE_RetVal(bool, FOnIsViewportSelectionLimited);
DECLARE_DELEGATE_RetVal_OneParam(bool, FOnIsObjectSelectableInViewport, UObject* /*InObject*/);
DECLARE_DELEGATE_RetVal(FText, FOnGetViewportSelectionLimitedText);

/**
 * Creates a link between a viewport and an outside module without requiring extra dependencies.
 * This could be moved to Editor/UnrealEd module to allow other modules that may need this functionality to access.
 */
class UNREALED_API FEditorViewportSelectabilityBridge
{
public:
	FEditorViewportSelectabilityBridge() = delete;
	FEditorViewportSelectabilityBridge(const TWeakPtr<FEditorViewportClient>& InEditorViewportClientWeak);

	/** @return Delegate used to check if viewport selection is limited */
	FOnIsViewportSelectionLimited& OnIsViewportSelectionLimited();
	bool IsViewportSelectionLimited() const;

	/** @return Delegate used to check if an object is selectable in the viewport */
	FOnIsObjectSelectableInViewport& OnGetIsObjectSelectableInViewport();
	/** @return True if the specified object is selectable in the viewport and not made unselectable by the Sequencer selection limiting */
	bool IsObjectSelectableInViewport(UObject* const InObject) const;

	/** @return Delegate used to get the text to display in the viewport when selection is limited */
	FOnGetViewportSelectionLimitedText& OnGetViewportSelectionLimitedText();
	FText GetViewportSelectionLimitedText() const;

private:
	TWeakPtr<FEditorViewportClient> EditorViewportClientWeak;

	FOnIsViewportSelectionLimited IsViewportSelectionLimitedDelegate;
	FOnIsObjectSelectableInViewport GetIsObjectSelectableInViewportDelegate;
	FOnGetViewportSelectionLimitedText GetViewportSelectionLimitedTextDelegate;
};
