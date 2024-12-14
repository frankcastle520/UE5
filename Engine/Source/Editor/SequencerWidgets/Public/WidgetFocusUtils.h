// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Templates/Function.h"
#include "Delegates/IDelegateInstance.h"

class SWidget;
struct FKeyEvent;
struct FPointerEvent;

/**
 * FPendingWidgetFocus is a utility class that stores a pending focus function when a SWidget is hovered over.
 * It aims to provide a way to focus on a widget without having to actually click on it. 
 * The focus function is stored on the mouse enter event and will only be executed if a key down event is sent while the widget is hovered.
 * If the mouse leave event is called without any key down event having been called, the function is reset and the focus is not modified at all.
 */

class SEQUENCERWIDGETS_API FPendingWidgetFocus : public TSharedFromThis<FPendingWidgetFocus>
{
public:

	FPendingWidgetFocus() = default;
	FPendingWidgetFocus(const TArray<FName>& InTypesKeepingFocus);

	static FPendingWidgetFocus MakeNoTextEdit();
	
	~FPendingWidgetFocus();

	void Enable(const bool InEnabled);
	bool IsEnabled() const;

	void SetPendingFocusIfNeeded(const TWeakPtr<SWidget>& InWidget);
	void ResetPendingFocus();

private:
	
	void OnPreInputKeyDown(const FKeyEvent&);
	void OnPreInputButtonDown(const FPointerEvent&);
	bool CanFocusBeStolen() const;

	TFunction<void()> PendingFocusFunction;
	FDelegateHandle PreInputKeyDownHandle;
	FDelegateHandle PreInputButtonDownHandle;

	TArray<FName> KeepingFocus;
};
