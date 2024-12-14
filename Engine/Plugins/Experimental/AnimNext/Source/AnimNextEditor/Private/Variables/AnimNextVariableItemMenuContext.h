// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AnimNextVariableItemMenuContext.generated.h"

class FUICommandList;
class UAnimNextRigVMAssetEditorData;
class UAnimNextRigVMAssetEntry;

namespace UE::Workspace
{
	class IWorkspaceEditor;
}

namespace UE::AnimNext::Editor
{
	class SVariablesOutliner;
}

UCLASS()
class UAnimNextVariableItemMenuContext : public UObject
{
	GENERATED_BODY()

public:
	// Currently selected asset's editor data
	UPROPERTY()
	TArray<TWeakObjectPtr<UAnimNextRigVMAssetEditorData>> WeakEditorDatas;

	// Currently selected entries
	UPROPERTY()
	TArray<TWeakObjectPtr<UAnimNextRigVMAssetEntry>> WeakEntries;

	TWeakPtr<UE::AnimNext::Editor::SVariablesOutliner> WeakOutliner;

	TWeakPtr<UE::Workspace::IWorkspaceEditor> WeakWorkspaceEditor;

	TWeakPtr<FUICommandList> WeakCommandList;
};
