// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "SViewportToolBar.h"

class FPixWinPluginModule;
class FExtensionBase;
class FExtensibilityManager;
class FExtender;
class FToolBarBuilder;

class FPixWinPluginEditorExtension
{
public:
	FPixWinPluginEditorExtension(FPixWinPluginModule* ThePlugin);
	~FPixWinPluginEditorExtension();

private:
	void Initialize(FPixWinPluginModule* ThePlugin);
	void AddToolbarExtension(FToolBarBuilder& ToolbarBuilder, FPixWinPluginModule* ThePlugin);

	TSharedPtr<const FExtensionBase> ToolbarExtension;
	TSharedPtr<FExtensibilityManager> ExtensionManager;
	TSharedPtr<FExtender> ToolbarExtender;
};

#endif //WITH_EDITOR
