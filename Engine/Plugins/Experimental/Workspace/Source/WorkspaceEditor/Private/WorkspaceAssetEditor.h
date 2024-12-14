// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Tools/UAssetEditor.h"
#include "Workspace.h"
#include "WorkspaceAssetEditor.generated.h"

class FBaseAssetToolkit;

UCLASS(Transient)
class UWorkspaceAssetEditor : public UAssetEditor
{
	GENERATED_BODY()
public:
	void SetObjectToEdit(UWorkspace* InWorkspace);
	UWorkspace* GetObjectToEdit();
protected:
	virtual void GetObjectsToEdit(TArray<UObject*>& OutObjectsToEdit) override;
	virtual TSharedPtr<FBaseAssetToolkit> CreateToolkit() override;

private:
	TObjectPtr<UWorkspace> ObjectToEdit;
};

