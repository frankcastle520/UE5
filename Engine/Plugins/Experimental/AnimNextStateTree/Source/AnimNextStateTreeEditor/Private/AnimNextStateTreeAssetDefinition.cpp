// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextStateTreeAssetDefinition.h"
#include "IWorkspaceEditorModule.h"
#include "Workspace/AnimNextWorkspaceFactory.h"
#include "Modules/ModuleManager.h"

EAssetCommandResult UAssetDefinition_AnimNextStateTree::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	using namespace UE::AnimNext::Editor;
	using namespace UE::Workspace;

	for (UAnimNextStateTree* Asset : OpenArgs.LoadObjects<UAnimNextStateTree>())
	{
		IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::Get().LoadModuleChecked<IWorkspaceEditorModule>("WorkspaceEditor");
		WorkspaceEditorModule.OpenWorkspaceForObject(Asset, EOpenWorkspaceMethod::Default, UAnimNextWorkspaceFactory::StaticClass());
	}

	return EAssetCommandResult::Handled;
}
