// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "InteractiveToolManager.h"
#include "Widgets/SWidget.h"

#include "ScriptableToolContextObjects.generated.h"

/**
 * Base class for context objects used in the Scriptable Tools Framework.
 */
UCLASS()
class SCRIPTABLETOOLSFRAMEWORK_API UScriptableToolContextObject : public UObject
{
	GENERATED_BODY()
public:
	/**
	 * Called by the mode when shutting context objects down, allowing them to do any cleanup.
	 * Initialization, on the other hand is usually done by some class-specific Initialize() method.
	 */
	virtual void Shutdown() {}

	/**
	 * Called whenever a tool is ended, for instance to let a context object remove listeners associated
	 * with that tool (it shouldn't have to do so, but may choose to for robustness).
	 */
	virtual void OnToolEnded(UInteractiveTool* DeadTool) {}
};


UCLASS()
class SCRIPTABLETOOLSFRAMEWORK_API UScriptableToolViewportWidgetAPI : public UScriptableToolContextObject
{
	GENERATED_BODY()

public:

	void Initialize(TUniqueFunction<void(TSharedRef<SWidget> InOverlaidWidget)> ReplaceOverlaidWidgetFuncIn,
	               	TUniqueFunction<void(TSharedRef<SWidget> InOverlaidWidget)> ClearOverlaidWidgetFuncIn)
	{
		ReplaceOverlaidWidgetFunc = MoveTemp(ReplaceOverlaidWidgetFuncIn);
		ClearOverlaidWidgetFunc = MoveTemp(ClearOverlaidWidgetFuncIn);
	}

	void SetOverlayWidget(TSharedRef<SWidget> InOverlaidWidget)
	{
		if (CurrentOverlaidWidget.IsValid())
		{
			ClearOverlayWidget();
		}

		CurrentOverlaidWidget = InOverlaidWidget.ToWeakPtr();

		ReplaceOverlaidWidgetFunc(InOverlaidWidget);
	}
	void ClearOverlayWidget()
	{
		if (CurrentOverlaidWidget.IsValid())
		{
			ClearOverlaidWidgetFunc(CurrentOverlaidWidget.Pin().ToSharedRef());
			CurrentOverlaidWidget.Reset();
		}
	}

	virtual void Shutdown() override 
	{
		ClearOverlayWidget();
	}


	virtual void OnToolEnded(UInteractiveTool* DeadTool) override
	{
		ClearOverlayWidget();
	}

private:

	TUniqueFunction<void(TSharedRef<SWidget> InOverlaidWidget)> ReplaceOverlaidWidgetFunc;
	TUniqueFunction<void(TSharedRef<SWidget> InOverlaidWidget)> ClearOverlaidWidgetFunc;

	TWeakPtr<SWidget> CurrentOverlaidWidget;
};