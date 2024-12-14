// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextDataInterfaceAssetDefinition.h"
#include "ContentBrowserMenuContexts.h"
#include "IWorkspaceEditorModule.h"
#include "Workspace/AnimNextWorkspaceFactory.h"

#define LOCTEXT_NAMESPACE "AnimNextAssetDefinitions"

EAssetCommandResult UAssetDefinition_AnimNextDataInterface::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
	using namespace UE::Workspace;

	for (UAnimNextDataInterface* Asset : OpenArgs.LoadObjects<UAnimNextDataInterface>())
	{
		IWorkspaceEditorModule& WorkspaceEditorModule = FModuleManager::Get().LoadModuleChecked<IWorkspaceEditorModule>("WorkspaceEditor");
		WorkspaceEditorModule.OpenWorkspaceForObject(Asset, EOpenWorkspaceMethod::Default, UAnimNextWorkspaceFactory::StaticClass());
	}

	return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE