// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/RigVMEdGraph.h"
#include "AnimNextEdGraph.generated.h"

class UAnimNextRigVMAssetEditorData;

namespace UE::AnimNext::UncookedOnly
{
	struct FUtils;
}

/**
  * Wraps UEdGraph which represents the node graph
  */
UCLASS(MinimalAPI)
class UAnimNextEdGraph : public URigVMEdGraph
{
	GENERATED_BODY()

	friend class UAnimNextModule_EditorData;
	friend class UAnimNextRigVMAssetEditorData;

	// UObject interface
	virtual void PostLoad() override;

	// URigVMEdGraph interface
	virtual FRigVMClient* GetRigVMClient() const override;
	
	void Initialize(UAnimNextRigVMAssetEditorData* InEditorData);
};