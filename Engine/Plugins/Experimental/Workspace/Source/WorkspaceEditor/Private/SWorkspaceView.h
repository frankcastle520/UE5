// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContentBrowserDelegates.h"
#include "Widgets/SCompoundWidget.h"

class UWorkspace;
class UWorkSpaceAssetUserData;

namespace UE::Workspace
{
class SWorkspaceOutliner;
class IWorkspaceEditor;

class SWorkspaceView : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SWorkspaceView) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWorkspace* InWorkspace, TSharedRef<UE::Workspace::IWorkspaceEditor> InWorkspaceEditor);

	void SelectObject(UObject* InObject);
	
private:
	UWorkspace* Workspace = nullptr;
	TSharedPtr<SWorkspaceOutliner> SceneWorkspaceOutliner;
};

};