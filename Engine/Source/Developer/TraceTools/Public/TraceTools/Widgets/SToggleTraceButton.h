// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ITraceController;

namespace UE::TraceTools
{

/**
 * A dynamic button that can start/stop trace. 
 * When tracing is started the button turns red and has a pulse animation.
 */
class SToggleTraceButton
	: public SCompoundWidget
{
	SLATE_DECLARE_WIDGET_API(SToggleTraceButton, SCompoundWidget, TRACETOOLS_API)

public:
	enum class EButtonSize : uint32
	{
		StatusBar = 0, // 16x16
		SlimToolbar = 1, // 20x20
	};

	DECLARE_DELEGATE(FOnToggleTraceRequested);

	SLATE_BEGIN_ARGS(SToggleTraceButton) 
		: _ButtonSize(EButtonSize::StatusBar)
	{ }
	
	/* An event called when the toggle trace button is clicked. */
	SLATE_EVENT(FOnToggleTraceRequested, OnToggleTraceRequested)
	
	/* Specifies if trace is running at the moment of call. */
	SLATE_ATTRIBUTE(bool, IsTraceRunning)
	
	/* Specifies the size of the button from a few presets. */
	SLATE_ARGUMENT(EButtonSize, ButtonSize)

	SLATE_END_ARGS()

public:
	TRACETOOLS_API SToggleTraceButton();
	TRACETOOLS_API virtual ~SToggleTraceButton();

	TRACETOOLS_API void Construct( const FArguments& InArgs);

private:
	FSlateColor GetRecordingButtonColor() const;
	FSlateColor GetRecordingButtonOutlineColor() const;
	FText GetRecordingButtonTooltipText() const;

	EVisibility GetStartTraceIconVisibility() const;
	EVisibility GetStopTraceIconVisibility() const;

	void ToggleTrace_OnClicked() const;

	const FSlateBrush* GetToggleTraceCenterBrush() const;
	const FSlateBrush* GetToggleTraceOutlineBrush() const;
	const FSlateBrush* GetToggleTraceStopBrush() const;

private:
	FOnToggleTraceRequested OnToggleTraceRequested;

	bool bIsTraceRecordButtonHovered = false;
	mutable double ConnectionStartTime = 0.0f;

	TSlateAttribute<bool> IsTraceRunningAttribute;
	EButtonSize ButtonSize;
};

} // namespace UE::TraceTools