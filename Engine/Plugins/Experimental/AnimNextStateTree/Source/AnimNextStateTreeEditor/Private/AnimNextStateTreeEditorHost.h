// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IStateTreeEditorHost.h"

namespace UE::Workspace
{
	class IWorkspaceEditor;
}

class FAnimNextStateTreeEditorHost : public IStateTreeEditorHost
{
public:
	void Init(const TWeakPtr<UE::Workspace::IWorkspaceEditor>& InWeakWorkspaceEditor);

	// IStateTreeEditorHost overrides
	virtual UStateTree* GetStateTree() const override;
	virtual FName GetCompilerLogName() const override;
	virtual FName GetCompilerTabName() const override;
	
	virtual FSimpleMulticastDelegate& OnStateTreeChanged() override;
	virtual TSharedPtr<IDetailsView> GetAssetDetailsView() override;
	virtual TSharedPtr<IDetailsView> GetDetailsView() override;

protected:
	void OnWorkspaceFocussedAssetChanged(TObjectPtr<UObject> InObject) const;

	TWeakPtr<UE::Workspace::IWorkspaceEditor> WeakWorkspaceEditor;	
	FSimpleMulticastDelegate OnStateTreeChangedDelegate;
};