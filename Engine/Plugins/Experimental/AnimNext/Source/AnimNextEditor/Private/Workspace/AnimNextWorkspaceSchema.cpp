// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimNextWorkspaceSchema.h"

#include "AnimNextEditorModule.h"
#include "Graph/AnimNextAnimationGraph.h"
#include "Module/AnimNextModule.h"

#define LOCTEXT_NAMESPACE "AnimNextWorkspaceSchema"

FText UAnimNextWorkspaceSchema::GetDisplayName() const
{
	return LOCTEXT("DisplayName", "AnimNext Workspace");
}

TConstArrayView<FTopLevelAssetPath> UAnimNextWorkspaceSchema::GetSupportedAssetClassPaths() const
{
	const UE::AnimNext::Editor::FAnimNextEditorModule& Module = FModuleManager::Get().LoadModuleChecked<UE::AnimNext::Editor::FAnimNextEditorModule>("AnimNextEditor");

	return Module.SupportedAssetClasses;
}

#undef LOCTEXT_NAMESPACE