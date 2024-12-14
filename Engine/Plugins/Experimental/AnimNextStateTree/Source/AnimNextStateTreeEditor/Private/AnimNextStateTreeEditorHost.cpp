// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextStateTreeEditorHost.h"

#include "AnimNextStateTree.h"
#include "IAnimNextEditorModule.h"
#include "StateTree.h"

#include "IWorkspaceEditor.h"

void FAnimNextStateTreeEditorHost::Init(const TWeakPtr<UE::Workspace::IWorkspaceEditor>& InWeakWorkspaceEditor)
{
	WeakWorkspaceEditor = InWeakWorkspaceEditor;
	
	TSharedPtr<UE::Workspace::IWorkspaceEditor> SharedEditor = InWeakWorkspaceEditor.Pin();
	check(SharedEditor.IsValid());
	SharedEditor->OnFocussedAssetChanged().AddSP(this, &FAnimNextStateTreeEditorHost::OnWorkspaceFocussedAssetChanged);
}

UStateTree* FAnimNextStateTreeEditorHost::GetStateTree() const
{
	if (TSharedPtr<UE::Workspace::IWorkspaceEditor> SharedWorkspaceEditor = WeakWorkspaceEditor.Pin())
	{
		const TObjectPtr<UAnimNextStateTree> AnimNextStateTreePtr = SharedWorkspaceEditor->GetFocussedAsset<UAnimNextStateTree>();
		return AnimNextStateTreePtr ? AnimNextStateTreePtr->StateTree : nullptr;
	}	

	return nullptr;
}

FName FAnimNextStateTreeEditorHost::GetCompilerLogName() const
{
	return UE::AnimNext::Editor::LogListingName;
}

FName FAnimNextStateTreeEditorHost::GetCompilerTabName() const
{
	return UE::AnimNext::Editor::CompilerResultsTabName;
}

void FAnimNextStateTreeEditorHost::OnWorkspaceFocussedAssetChanged(TObjectPtr<UObject> InObject) const
{
	OnStateTreeChangedDelegate.Broadcast();
}

FSimpleMulticastDelegate& FAnimNextStateTreeEditorHost::OnStateTreeChanged()
{
	return OnStateTreeChangedDelegate;
}

TSharedPtr<IDetailsView> FAnimNextStateTreeEditorHost::GetAssetDetailsView()
{
	if (TSharedPtr<UE::Workspace::IWorkspaceEditor> SharedEditor = WeakWorkspaceEditor.Pin())
	{
		return SharedEditor->GetDetailsView();
	}
	
	return nullptr;
}

TSharedPtr<IDetailsView> FAnimNextStateTreeEditorHost::GetDetailsView()
{
	if (TSharedPtr<UE::Workspace::IWorkspaceEditor> SharedEditor = WeakWorkspaceEditor.Pin())
	{
		return SharedEditor->GetDetailsView();
	}
	
	return nullptr;
}
