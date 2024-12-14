// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IStateTreeEditorHost.h"

class FStateTreeEditor;

class FStandaloneStateTreeEditorHost : public IStateTreeEditorHost
{	
public:
	void Init(const TWeakPtr<FStateTreeEditor>& InWeakStateTreeEditor);
	
	// IStateTreeEditorHost overrides
	virtual UStateTree* GetStateTree() const override;
	virtual FSimpleMulticastDelegate& OnStateTreeChanged() override;
	virtual TSharedPtr<IDetailsView> GetAssetDetailsView() override;
	virtual TSharedPtr<IDetailsView> GetDetailsView() override;

	virtual FName GetCompilerLogName() const override;
	virtual FName GetCompilerTabName() const override;

protected:
	TWeakPtr<FStateTreeEditor> WeakStateTreeEditor;
	FSimpleMulticastDelegate OnStateTreeChangedDelegate;
};