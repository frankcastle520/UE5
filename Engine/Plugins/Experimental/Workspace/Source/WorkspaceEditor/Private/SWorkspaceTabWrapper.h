// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Workspace.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Navigation/SBreadcrumbTrail.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"
#include "WorkspaceEditor.h"
#include "WorkspaceEditorModule.h"

class SWorkspaceTabWrapper : public SCompoundWidget
{
public:
	SWorkspaceTabWrapper() = default;

	SLATE_BEGIN_ARGS( SWorkspaceTabWrapper ){}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, TSharedPtr<class FTabInfo> InTabInfo, TSharedPtr<UE::Workspace::FWorkspaceEditor> InWorkspaceEditor, UObject* InDocumentID);
	TSharedRef<SWidget> GetContent() const { return Content.ToSharedRef(); }
	TWeakObjectPtr<UObject> GetDocumentObject() const { return WeakDocumentObject; }
	
protected:
	void RebuildBreadcrumbTrail() const;
	const FSlateBrush* GetTabIcon() const;
	EVisibility IsWorkspaceNameVisible() const;
	FText GetWorkspaceName() const;

	void ExecuteSave() const;
	bool CanExecuteSave() const;
	bool IsSaveButtonVisible() const;

	TSharedPtr<SWidget> Content = nullptr;
	TWeakPtr<UE::Workspace::FWorkspaceEditor> WeakWorkspaceEditor = nullptr;
	TWeakObjectPtr<UObject> WeakDocumentObject = nullptr;

	TSharedPtr<SScrollBox> BreadcrumbTrailScrollBox = nullptr;
	TSharedPtr<SBreadcrumbTrail<TSharedPtr<UE::Workspace::FWorkspaceBreadcrumb>>> BreadcrumbTrail = nullptr;	
};
