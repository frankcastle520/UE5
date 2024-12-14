// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SourceControlViewportUtils.h"

class FLevelEditorViewportClient;
class UToolMenu;
class SWidget;

// Adds an options menu to the Viewport's SHOW pill.
class FSourceControlViewportMenu : public TSharedFromThis<FSourceControlViewportMenu, ESPMode::ThreadSafe>
{
public:
	FSourceControlViewportMenu();
	~FSourceControlViewportMenu();

public:
	void Init();
	void SetEnabled(bool bInEnabled);

private:
	void InsertViewportMenu();
	void PopulateViewportMenu(UToolMenu* InMenu);
	void RemoveViewportMenu();

private:
	void ShowAll(FLevelEditorViewportClient* ViewportClient);
	void HideAll(FLevelEditorViewportClient* ViewportClient);

	void ToggleHighlight(FLevelEditorViewportClient* ViewportClient, ESourceControlStatus Status);
	bool IsHighlighted(FLevelEditorViewportClient* ViewportClient, ESourceControlStatus Status) const;

	void SetOpacityValue(FLevelEditorViewportClient* ViewportClient, uint8 NewValue);
	uint8 GetOpacityValue(FLevelEditorViewportClient* ViewportClient) const;

private:
	void RecordToggleEvent(const FString& Param, bool bEnabled) const;

private:
	TSharedPtr<SWidget> OpacityWidget;
};