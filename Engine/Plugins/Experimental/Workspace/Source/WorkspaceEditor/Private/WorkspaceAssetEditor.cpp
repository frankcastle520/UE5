// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorkspaceAssetEditor.h"
#include "WorkspaceEditor.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorkspaceAssetEditor)

void UWorkspaceAssetEditor::GetObjectsToEdit(TArray<UObject*>& OutObjectsToEdit)
{
	OutObjectsToEdit.Add(ObjectToEdit);
}

void UWorkspaceAssetEditor::SetObjectToEdit(UWorkspace* InWorkspace)
{
	ObjectToEdit = InWorkspace;
}

UWorkspace* UWorkspaceAssetEditor::GetObjectToEdit()
{
	return ObjectToEdit;
}

TSharedPtr<FBaseAssetToolkit> UWorkspaceAssetEditor::CreateToolkit()
{
	return MakeShared<UE::Workspace::FWorkspaceEditor>(this);
}

